// ============================================================================
//  codec_jpeg.cpp : 自研 JPEG 解码器
//  支持: 基线(SOF0/SOF1) + 渐进式(SOF2)、灰度/YCbCr、4:4:4/4:2:2/4:2:0/4:1:1、
//        restart marker、EXIF(方向/相机/曝光信息)
//  不支持: 12bit、CMYK/YCCK 4 分量（会给出明确错误）
// ============================================================================
#include "codec.h"
#include "i18n.h"

#ifdef LITEVIEW_JPEG_DEBUG
#  include <cstdio>
#  define JDBG(...) fprintf(stderr, __VA_ARGS__)
#else
#  define JDBG(...) ((void)0)
#endif

namespace {

// 标准 zigzag: k(扫描顺序) -> 自然顺序
const u8 kZZ[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

inline int extend_bits(int v, int s) {
    if (s == 0) return 0;
    return (v < (1 << (s - 1))) ? (v - ((1 << s) - 1)) : v;
}

// ---------------------------------------------------------------------------
// 熵解码位读取（MSB first, 0xFF00 去填充, 识别 marker 边界）
// ---------------------------------------------------------------------------
struct BitReader {
    const u8* p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    u32 buf = 0;
    int cnt = 0;
    bool hitMarker = false;   // 遇到 marker（RSTn/EOI/...），marker 码字节尚未消耗
    u8   marker = 0;
    bool err = false;         // 数据意外结束

    void reset(const u8* d, size_t sz, size_t start) {
        p = d; n = sz; pos = start;
        buf = 0; cnt = 0; hitMarker = false; marker = 0; err = false;
    }

    u32 get(int k) {
        while (cnt < k) {
            if (hitMarker || pos >= n) { err = true; return 0; }
            u8 b = p[pos++];
            if (b == 0xFF) {
                if (pos >= n) { err = true; return 0; }
                u8 b2 = p[pos];
                if (b2 == 0x00) { pos++; b = 0xFF; }
                else { hitMarker = true; marker = b2; return 0; }
            }
            buf = (buf << 8) | b;
            cnt += 8;
        }
        u32 v = (buf >> (cnt - k)) & ((1u << k) - 1u);
        cnt -= k;
        return v;
    }

    // 取下一个 marker（消耗掉）；-1 表示无
    int fetch_marker() {
        if (hitMarker) { hitMarker = false; pos++; return marker; }
        while (pos < n) {
            u8 b = p[pos++];
            if (b == 0xFF) {
                while (pos < n && p[pos] == 0xFF) pos++;
                if (pos >= n) break;
                u8 m = p[pos++];
                if (m == 0x00) continue;
                return m;
            }
        }
        return -1;
    }

    void clear_bits() { buf = 0; cnt = 0; }
};

// ---------------------------------------------------------------------------
// Huffman 表（规范码）
// ---------------------------------------------------------------------------
struct Huff {
    int count[17] = { 0 };
    int firstcode[17] = { 0 };
    int firstindex[17] = { 0 };
    u16 symbol[256] = { 0 };
    int total = 0;
    bool valid = false;

    bool build(const u8* counts16, const u8* syms) {
        total = 0;
        for (int l = 1; l <= 16; l++) { count[l] = counts16[l - 1]; total += count[l]; }
        if (total <= 0 || total > 256) return false;
        int left = 1;
        for (int l = 1; l <= 16; l++) {
            left = (left << 1) - count[l];
            if (left < 0) return false;
        }
        int code = 0, idx = 0;
        for (int l = 1; l <= 16; l++) {
            firstcode[l] = code;
            firstindex[l] = idx;
            code += count[l];
            idx += count[l];
            code <<= 1;
        }
        for (int i = 0; i < total; i++) symbol[i] = (u16)syms[i];
        valid = true;
        return true;
    }

    int decode(BitReader& br) const {
        int code = 0;
        for (int len = 1; len <= 16; len++) {
            code = (code << 1) | (int)br.get(1);
            if (br.err || br.hitMarker) return -1;
            unsigned diff = (unsigned)code - (unsigned)firstcode[len];
            if (diff < (unsigned)count[len]) return symbol[firstindex[len] + (int)diff];
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// 浮点可分 IDCT（预计算余弦表）
// ---------------------------------------------------------------------------
struct IdctTables {
    float c[8];
    float cosv[8][8];
    IdctTables() {
        const double PI = 3.14159265358979323846;
        for (int u = 0; u < 8; u++) {
            c[u] = (u == 0) ? (float)(1.0 / std::sqrt(2.0)) : 1.0f;
            for (int x = 0; x < 8; x++)
                cosv[u][x] = (float)std::cos((2 * x + 1) * u * PI / 16.0);
        }
    }
};
const IdctTables& idct_tables() { static IdctTables t; return t; }

void idct_block(const i16* coef, const u16* quant, u8* plane, int stride, int bx, int by) {
    const IdctTables& T = idct_tables();
    float F[64];
    bool dcOnly = true;
    for (int i = 0; i < 64; i++) {
        if (coef[i]) {
            F[i] = (float)((int)coef[i] * (int)quant[i]);
            if (i) dcOnly = false;
        } else {
            F[i] = 0.0f;
        }
    }
    u8* dst = plane + (size_t)(by * 8) * stride + bx * 8;

    if (dcOnly) {
        int dc = (int)std::lround(F[0] * 0.125f) + 128;
        dc = clampi(dc, 0, 255);
        for (int y = 0; y < 8; y++) memset(dst + y * stride, dc, 8);
        return;
    }

    float col[8][8];
    for (int u = 0; u < 8; u++) {
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += T.c[v] * F[v * 8 + u] * T.cosv[v][y];
            col[u][y] = s;
        }
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += T.c[u] * col[u][y] * T.cosv[u][x];
            dst[y * stride + x] = (u8)clampi((int)std::lround(s * 0.25f) + 128, 0, 255);
        }
    }
}

// ---------------------------------------------------------------------------
// 解码器主体
// ---------------------------------------------------------------------------
struct Comp {
    int id = 0, hs = 1, vs = 1, tq = 0;
    int dcId = 0, acId = 0;
    int bw = 0, bh = 0;          // 8x8 块数（未补齐，非交错扫描遍历用）
    int bwP = 0, bhP = 0;        // 按 MCU 边界补齐后的块网格（交错扫描访问用）
    int dcPred = 0;
    std::vector<i16> coef;       // bwP*bhP*64 原始系数
    std::vector<u8>  plane;      // IDCT 后的平面
};

struct Jpeg {
    const u8* p = nullptr;
    size_t n = 0;
    size_t pos = 0;

    bool progressive = false;
    bool gotFrame = false;
    int  w = 0, h = 0, ncomp = 0;
    int  hmax = 1, vmax = 1;
    int  mcusX = 0, mcusY = 0;
    int  restartInterval = 0;

    Comp comp[4];
    Huff dcTbl[4], acTbl[4];
    u16  quant[4][64];
    ExifInfo exif;

    BitReader br;

    // ---------------- 通用工具 ----------------
    int next_marker() {
        while (pos + 1 <= n) {
            if (p[pos] != 0xFF) { pos++; continue; }
            size_t q = pos + 1;
            while (q < n && p[q] == 0xFF) q++;
            if (q >= n) break;
            u8 m = p[q];
            pos = q + 1;
            if (m == 0x00) continue;
            return m;
        }
        return -1;
    }

    bool segment_bounds(size_t& content, size_t& end) {
        if (pos + 2 > n) return false;
        u16 L = (u16)rd16be(p + pos);
        if (L < 2 || pos + L > n) return false;
        content = pos + 2;
        end = pos + L;
        return true;
    }

    // ---------------- 段解析 ----------------
    bool parse_dqt() {
        size_t content, end;
        if (!segment_bounds(content, end)) return false;
        size_t q = content;
        while (q < end) {
            u8 pq = (u8)(p[q] >> 4), tq = (u8)(p[q] & 15);
            q++;
            if (tq > 3) return false;
            for (int k = 0; k < 64; k++) {
                u16 v;
                if (pq) {
                    if (q + 2 > end) return false;
                    v = (u16)rd16be(p + q); q += 2;
                } else {
                    if (q + 1 > end) return false;
                    v = p[q++];
                }
                quant[tq][kZZ[k]] = v;
            }
        }
        pos = end;
        return true;
    }

    bool parse_dht() {
        size_t content, end;
        if (!segment_bounds(content, end)) return false;
        size_t q = content;
        while (q + 17 <= end) {
            u8 tc = (u8)(p[q] >> 4), th = (u8)(p[q] & 15);
            q++;
            int total = 0;
            u8 counts[16];
            for (int i = 0; i < 16; i++) { counts[i] = p[q + i]; total += counts[i]; }
            q += 16;
            if (th > 3 || total > 256 || q + (size_t)total > end) return false;
            Huff& ht = (tc == 0) ? dcTbl[th] : acTbl[th];
            if (!ht.build(counts, p + q)) return false;
            JDBG("DHT tc=%d th=%d total=%d counts=[", tc, th, total);
            for (int i = 0; i < 16; i++) JDBG("%d%s", counts[i], i < 15 ? "," : "");
            JDBG("] syms=[");
            for (int i = 0; i < total && i < 8; i++) JDBG("%02X%s", p[q + i], i < 7 ? "," : "");
            JDBG("]\n");
            q += total;
        }
        pos = end;
        return true;
    }

    bool parse_sof(int type) {
        size_t content, end;
        if (!segment_bounds(content, end)) return false;
        if (content + 6 > end) return false;
        int prec = p[content];
        h = (int)rd16be(p + content + 1);
        w = (int)rd16be(p + content + 3);
        ncomp = p[content + 5];
        if (prec != 8) return false;                    // 12bit 不支持
        if (w <= 0 || h <= 0 || w > kMaxImageSide || h > kMaxImageSide ||
            (u64)w * h > kMaxImagePixels) return false;
        if (ncomp < 1 || ncomp > 4) return false;
        if (type == 3 || type == 7 || type == 11) return false;   // 无损模式不支持
        progressive = (type == 2);

        size_t q = content + 6;
        if (q + (size_t)ncomp * 3 > end) return false;
        hmax = vmax = 1;
        for (int i = 0; i < ncomp; i++) {
            Comp& c = comp[i];
            c.id = p[q]; c.hs = (u8)(p[q + 1] >> 4); c.vs = (u8)(p[q + 1] & 15); c.tq = p[q + 2];
            q += 3;
            if (c.hs < 1 || c.hs > 4 || c.vs < 1 || c.vs > 4 || c.tq > 3) return false;
            hmax = std::max(hmax, (int)c.hs);
            vmax = std::max(vmax, (int)c.vs);
        }
        // 分配系数缓冲
        mcusX = (w + 8 * hmax - 1) / (8 * hmax);
        mcusY = (h + 8 * vmax - 1) / (8 * vmax);
        for (int i = 0; i < ncomp; i++) {
            Comp& c = comp[i];
            int compW = (w * c.hs + hmax - 1) / hmax;
            int compH = (h * c.vs + vmax - 1) / vmax;
            c.bw = (compW + 7) / 8;
            c.bh = (compH + 7) / 8;
            // 交错扫描会访问到每个 MCU 内的全部 h*v 个块（含边缘补齐块），
            // 与 libjpeg 一致：系数数组按 MCU 边界补齐分配，避免越界。
            c.bwP = mcusX * c.hs;
            c.bhP = mcusY * c.vs;
            c.coef.assign((size_t)c.bwP * c.bhP * 64, 0);
            c.dcPred = 0;
        }
        pos = end;
        gotFrame = true;
        return true;
    }

    // ---------------- 块解码 ----------------
    i16* block_ptr(Comp& c, int bx, int by) {
        return &c.coef[((size_t)by * c.bwP + bx) * 64];
    }

    bool dec_dc_first(Comp& c, i16* blk, int al) {
        int t = dcTbl[c.dcId].decode(br);
        if (t < 0) return false;
        if (t > 15) return false;
        int diff = extend_bits((int)br.get(t), t);
        if (br.err || br.hitMarker) return false;
        c.dcPred += diff;
        blk[0] = (i16)(c.dcPred << al);
        return true;
    }

    bool dec_dc_refine(i16* blk, int al) {
        int bit = (int)br.get(1);
        if (br.err || br.hitMarker) return false;
        blk[0] = (i16)(blk[0] | (bit << al));
        return true;
    }

    bool dec_ac_first(Comp& c, i16* blk, int ss, int se, int al, i32& eobrun) {
        if (eobrun > 0) { eobrun--; return true; }
        const Huff& act = acTbl[c.acId];
        for (int k = std::max(ss, 1); k <= se; k++) {
            int rs = act.decode(br);
            JDBG("   ac rs=%02X k=%d\n", rs, k);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r < 15) {
                    eobrun = (1 << r) - 1;
                    if (r) eobrun += (i32)br.get(r);
                    if (br.err || br.hitMarker) return false;
                    break;
                }
                k += 15;
                continue;
            }
            k += r;
            if (k > se) return false;
            int v = (int)br.get(s);
            if (br.err || br.hitMarker) return false;
            blk[kZZ[k]] = (i16)(extend_bits(v, s) << al);
        }
        return true;
    }

    bool dec_ac_refine(Comp& c, i16* blk, int ss, int se, int al, i32& eobrun) {
        const Huff& act = acTbl[c.acId];
        const int p1 = 1 << al;
        int k = ss;

        if (eobrun > 0) {
            for (; k <= se; k++) {
                i16& cf = blk[kZZ[k]];
                if (cf != 0) {
                    int b = (int)br.get(1);
                    if (br.err || br.hitMarker) return false;
                    if (b && !(cf & p1)) cf = (i16)(cf + (cf > 0 ? p1 : -p1));
                }
            }
            eobrun--;
            return true;
        }

        auto refine_at = [&](int kk) -> bool {
            i16& cf = blk[kZZ[kk]];
            if (cf != 0) {
                int b = (int)br.get(1);
                if (br.err || br.hitMarker) return false;
                if (b && !(cf & p1)) cf = (i16)(cf + (cf > 0 ? p1 : -p1));
            }
            return true;
        };

        while (k <= se) {
            int rs = act.decode(br);
            if (rs < 0) { JDBG("refine: huff fail k=%d eob=%d\n", k, (int)eobrun); return false; }
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r < 15) {
                    eobrun = (1 << r) - 1;
                    if (r) eobrun += (i32)br.get(r);
                    if (br.err || br.hitMarker) return false;
                    for (; k <= se; k++)
                        if (blk[kZZ[k]] != 0 && !refine_at(k)) return false;
                    break;
                }
                // ZRL: 跳过 16 个零历史系数（途中非零系数读修正位）
                int zeros = 16;
                while (zeros > 0 && k <= se) {
                    if (blk[kZZ[k]] != 0) {
                        if (!refine_at(k)) return false;
                    } else {
                        zeros--;
                    }
                    k++;
                }
                continue;
            }
            if (s != 1) { JDBG("refine: s=%d rs=%02X k=%d\n", s, rs, k); return false; }
            // 新系数的符号位
            int sign = (int)br.get(1);
            if (br.err || br.hitMarker) return false;
            // 跳过 r 个零历史系数
            while (k <= se) {
                if (blk[kZZ[k]] != 0) {
                    if (!refine_at(k)) return false;
                } else {
                    if (r == 0) break;
                    r--;
                }
                k++;
            }
            if (k > se) { JDBG("refine: place beyond band k=%d se=%d\n", k, se); return false; }
            blk[kZZ[k]] = (i16)(sign ? p1 : -p1);
            k++;
        }
        return true;
    }

    bool decode_block_for_scan(Comp& c, i16* blk, int ss, int se, int ah, int al, i32& eobrun) {
        if (ss == 0) {
            if (ah == 0) { if (!dec_dc_first(c, blk, al)) { JDBG("  dc_first fail\n"); return false; } }
            else         { if (!dec_dc_refine(blk, al)) { JDBG("  dc_refine fail\n"); return false; } }
        }
        if (se > 0) {
            if (ah == 0) { if (!dec_ac_first(c, blk, ss, se, al, eobrun)) { JDBG("  ac_first fail\n"); return false; } }
            else         { if (!dec_ac_refine(c, blk, ss, se, al, eobrun)) { JDBG("  ac_refine fail\n"); return false; } }
        }
        return true;
    }

    // 处理 restart 标记（返回 false = 扫描应停止）
    bool take_restart() {
        if (br.hitMarker) {
            if (br.marker >= 0xD0 && br.marker <= 0xD7) {
                br.hitMarker = false;
                br.pos++;              // 跳过 marker 码字节
                br.clear_bits();
                return true;
            }
            return false;
        }
        int m = br.fetch_marker();
        if (m >= 0xD0 && m <= 0xD7) { br.clear_bits(); return true; }
        if (m >= 0) { br.hitMarker = true; br.marker = (u8)m; }
        return false;
    }

    // ---------------- 扫描解码 ----------------
    bool decode_scan() {
        if (pos + 4 > n) return false;
        u16 L = (u16)rd16be(p + pos);
        if (L < 6 || pos + L > n) return false;
        size_t q = pos + 2;
        int ns = p[q];
        if (ns < 1 || ns > 4) return false;
        if (q + 1 + (size_t)ns * 2 + 3 > pos + L) return false;

        int scanIdx[4];
        for (int i = 0; i < ns; i++) {
            int id = p[q + 1 + i * 2];
            int td = p[q + 2 + i * 2] >> 4;
            int ta = p[q + 2 + i * 2] & 15;
            int ci = -1;
            for (int k = 0; k < ncomp; k++) if (comp[k].id == id) { ci = k; break; }
            if (ci < 0 || td > 3 || ta > 3) return false;
            comp[ci].dcId = td;
            comp[ci].acId = ta;
            scanIdx[i] = ci;
        }
        q += 1 + (size_t)ns * 2;
        int ss = p[q], se = p[q + 1], ahal = p[q + 2];
        q += 3;
        int ah = ahal >> 4, al = ahal & 15;
        if (ns > 1 && (ss != 0 || se != 63)) { /* 交错扫描应为全带 */ }
        if (ss > se || se > 63) return false;
        if (ns == 1 && se == 0 && ah != 0 && al == 0) { /* 允许 */ }

        br.reset(p, n, q);
        for (int k = 0; k < ncomp; k++) comp[k].dcPred = 0;
        JDBG("scan ns=%d ss=%d se=%d ah=%d al=%d comp0(id=%d dcT=%d acT=%d %dx%d blk) pos=%zu\n",
             ns, ss, se, ah, al, comp[scanIdx[0]].id, comp[scanIdx[0]].dcId,
             comp[scanIdx[0]].acId, comp[scanIdx[0]].bw, comp[scanIdx[0]].bh, q);

        i32 eobrun = 0;
        int units = restartInterval;      // 使第一个单元前不触发
        int unitCount = 0;
        bool ok = true;
        bool stop = false;

        auto before_unit = [&]() -> bool {   // 返回 false = 停止扫描
            if (restartInterval > 0 && unitCount == restartInterval) {
                if (!take_restart()) return false;
                eobrun = 0;
                for (int k = 0; k < ncomp; k++) comp[k].dcPred = 0;
                unitCount = 0;
            }
            return true;
        };

        if (ns == 1) {
            Comp& c = comp[scanIdx[0]];
            int blkNo = 0;
            for (int by = 0; by < c.bh && ok && !stop; by++) {
                for (int bx = 0; bx < c.bw && ok && !stop; bx++) {
                    if (blkNo < 4) JDBG("  blk #%d (%d,%d)\n", blkNo, bx, by);
                    if (!before_unit()) { stop = true; break; }
                    i16* blk = block_ptr(c, bx, by);
                    ok = decode_block_for_scan(c, blk, ss, se, ah, al, eobrun);
                    unitCount++;
                    blkNo++;
                }
            }
        } else {
            for (int my = 0; my < mcusY && ok && !stop; my++) {
                for (int mx = 0; mx < mcusX && ok && !stop; mx++) {
                    if (!before_unit()) { stop = true; break; }
                    for (int i = 0; i < ns && ok; i++) {
                        Comp& c = comp[scanIdx[i]];
                        for (int v = 0; v < c.vs && ok; v++) {
                            for (int u = 0; u < c.hs && ok; u++) {
                                i16* blk = block_ptr(c, mx * c.hs + u, my * c.vs + v);
                                ok = decode_block_for_scan(c, blk, ss, se, ah, al, eobrun);
                            }
                        }
                    }
                    unitCount++;
                }
            }
        }
        pos = br.pos;                    // 记录字节位置，后续由 next_marker 继续
        (void)units;
        JDBG("scan end ok=%d err=%d hit=%d pos=%zu\n", (int)ok, (int)br.err, (int)br.hitMarker, pos);
        return ok && !br.err;
    }

    // ---------------- IDCT + 组装 ----------------
    bool build_image(Image& img, std::wstring& err) {
        if (!gotFrame) { err = tr(Sid::err_jpeg_no_frame); return false; }

        // 每个分量 IDCT 到平面（按补齐网格，写满整个平面）
        for (int i = 0; i < ncomp; i++) {
            Comp& c = comp[i];
            const int stride = c.bwP * 8;
            c.plane.assign((size_t)stride * c.bhP * 8, 128);
            for (int by = 0; by < c.bhP; by++)
                for (int bx = 0; bx < c.bwP; bx++)
                    idct_block(block_ptr(c, bx, by), quant[c.tq], c.plane.data(), stride, bx, by);
        }

        if (ncomp == 4) { err = tr(Sid::err_jpeg_cmyk); return false; }
        if (ncomp == 2) { err = tr(Sid::err_jpeg_components); return false; }

        img.reset(w, h);
        if (ncomp == 1) {
            const Comp& y = comp[0];
            const int stride = y.bwP * 8;
            for (int yy = 0; yy < h; yy++) {
                const u8* sr = y.plane.data() + (size_t)(yy * y.vs / vmax) * stride;
                u32* dr = img.row(yy);
                for (int xx = 0; xx < w; xx++) {
                    int v = sr[xx * y.hs / hmax];
                    dr[xx] = pack_argb(255, v, v, v);
                }
            }
            return true;
        }

        // 组件是否为 RGB（'R','G','B'）
        bool isRGB = (ncomp == 3 &&
                      comp[0].id == 'R' && comp[1].id == 'G' && comp[2].id == 'B');
        const int yr = isRGB ? 0 : 1, yb = isRGB ? 2 : 2;

        const Comp& cy = comp[0];
        const Comp& c1 = comp[yr];
        const Comp& c2 = comp[yb];
        const int sY = cy.bwP * 8, s1 = c1.bwP * 8, s2 = c2.bwP * 8;

        for (int yy = 0; yy < h; yy++) {
            const u8* rowY = cy.plane.data() + (size_t)(yy * cy.vs / vmax) * sY;
            const u8* row1 = c1.plane.data() + (size_t)(yy * c1.vs / vmax) * s1;
            const u8* row2 = c2.plane.data() + (size_t)(yy * c2.vs / vmax) * s2;
            u32* dr = img.row(yy);
            for (int xx = 0; xx < w; xx++) {
                int Y = rowY[xx * cy.hs / hmax];
                int A = row1[xx * c1.hs / hmax];
                int B = row2[xx * c2.hs / hmax];
                int r, g, b;
                if (isRGB) {
                    r = Y; g = A; b = B;
                } else {
                    int cb = A - 128, cr = B - 128;
                    r = Y + ((91881 * cr + 32768) >> 16);
                    g = Y - ((22554 * cb + 46802 * cr + 32768) >> 16);
                    b = Y + ((116130 * cb + 32768) >> 16);
                }
                dr[xx] = pack_argb(255, clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
            }
        }
        return true;
    }
};

} // namespace

// ============================================================================
// EXIF (TIFF) 解析
// ============================================================================
namespace {

struct Tiff {
    const u8* p;
    size_t n;
    bool le;

    u16 get16(size_t o) const { if (o + 2 > n) return 0; return le ? (u16)rd16le(p + o) : (u16)rd16be(p + o); }
    u32 get32(size_t o) const { if (o + 4 > n) return 0; return le ? rd32le(p + o) : rd32be(p + o); }
    size_t type_size(int t) const {
        switch (t) {
        case 1: case 2: case 7: return 1;
        case 3: return 2;
        case 4: case 9: return 4;
        case 5: case 10: return 8;
        default: return 0;
        }
    }
    bool entry_value_off(size_t ent, size_t count, int type, size_t& off, size_t& need) const {
        size_t sz = type_size(type);
        if (!sz) return false;
        need = sz * count;
        if (need <= 4) { off = ent + 8; return off + need <= n; }
        off = get32(ent + 8);
        return off + need <= n;
    }
    std::wstring read_ascii(size_t ent, size_t count) const {
        size_t off, need;
        if (!entry_value_off(ent, count, 2, off, need)) return std::wstring();
        std::wstring s;
        for (size_t i = 0; i < count; i++) {
            char ch = (char)p[off + i];
            if (ch == 0) break;
            s.push_back((wchar_t)(unsigned char)ch);
        }
        while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
        return s;
    }
    u32 read_short(size_t ent, size_t count, size_t idx = 0) const {
        size_t off, need;
        if (!entry_value_off(ent, count, 3, off, need)) return 0;
        if (idx * 2 + 2 > need) return 0;
        return get16(off + idx * 2);
    }
    u32 read_long(size_t ent, size_t count, size_t idx = 0) const {
        size_t off, need;
        if (!entry_value_off(ent, count, 4, off, need)) return 0;
        if (idx * 4 + 4 > need) return 0;
        return get32(off + idx * 4);
    }
    bool read_rational(size_t ent, size_t count, u32& num, u32& den, size_t idx = 0) const {
        size_t off, need;
        if (!entry_value_off(ent, count, 5, off, need)) return false;
        if (idx * 8 + 8 > need) return false;
        num = get32(off + idx * 8);
        den = get32(off + idx * 8 + 4);
        return true;
    }
};

std::wstring fmt_double(double v, const wchar_t* fmt) {
    wchar_t buf[64];
    swprintf(buf, 64, fmt, v);
    return buf;
}

void read_ifd(const Tiff& t, size_t ifdOff, ExifInfo& out, bool isExifIfd, int depth) {
    if (depth > 3 || ifdOff + 2 > t.n) return;
    size_t count = t.get16(ifdOff);
    size_t ent = ifdOff + 2;
    if (ent + count * 12 > t.n) count = (t.n >= ent) ? (t.n - ent) / 12 : 0;

    for (size_t i = 0; i < count; i++) {
        size_t e = ent + i * 12;
        int tag = t.get16(e);
        int type = t.get16(e + 2);
        size_t cnt = t.get32(e + 4);
        if (!isExifIfd && depth == 0) {
            switch (tag) {
            case 0x010F: out.make = t.read_ascii(e, cnt); break;
            case 0x0110: out.model = t.read_ascii(e, cnt); break;
            case 0x0132: out.dateTime = t.read_ascii(e, cnt); break;
            case 0x0112: {
                u32 o = t.read_short(e, cnt);
                if (o >= 1 && o <= 8) out.orientation = (int)o;
                break; }
            case 0x8769: {
                u32 sub = t.read_long(e, cnt);
                if (sub > 0 && sub < t.n) read_ifd(t, sub, out, true, depth + 1);
                break; }
            default: break;
            }
        } else if (isExifIfd) {
            switch (tag) {
            case 0x829A: {  // ExposureTime
                u32 num, den;
                if (t.read_rational(e, cnt, num, den) && den > 0) {
                    double s = (double)num / den;
                    if (num == 1) { wchar_t b[48]; swprintf(b, 48, L"1/%u s", den); out.exposure = b; }
                    else if (s >= 1.0) out.exposure = fmt_double(s, L"%.1f s");
                    else { wchar_t b[48]; swprintf(b, 48, L"1/%.0f s", 1.0 / s); out.exposure = b; }
                }
                break; }
            case 0x829D: {  // FNumber
                u32 num, den;
                if (t.read_rational(e, cnt, num, den) && den > 0)
                    out.fnumber = fmt_double((double)num / den, L"f/%.1f");
                break; }
            case 0x8827: {  // ISO
                u32 iso = (type == 3) ? t.read_short(e, cnt) : t.read_long(e, cnt);
                if (iso > 0) { wchar_t b[32]; swprintf(b, 32, L"ISO %u", iso); out.iso = b; }
                break; }
            case 0x920A: {  // FocalLength
                u32 num, den;
                if (t.read_rational(e, cnt, num, den) && den > 0)
                    out.focal = fmt_double((double)num / den, L"%.0f mm");
                break; }
            default: break;
            }
        }
    }
}

} // namespace

bool parse_exif_tiff(const u8* p, size_t n, ExifInfo& out) {
    if (n < 8) return false;
    bool le;
    if (p[0] == 'I' && p[1] == 'I') le = true;
    else if (p[0] == 'M' && p[1] == 'M') le = false;
    else return false;
    Tiff t{ p, n, le };
    if (t.get16(2) != 42) return false;
    size_t ifd0 = t.get32(4);
    if (ifd0 < 8 || ifd0 + 2 > n) return false;
    out.valid = true;
    read_ifd(t, ifd0, out, false, 0);
    return true;
}

// ============================================================================
// JPEG 入口
// ============================================================================
bool jpeg_decode(const u8* p, size_t n, Image& img, ExifInfo& exif, std::wstring& err) {
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) { err = tr(Sid::err_jpeg_not_jpeg); return false; }

    Jpeg j;
    j.p = p;
    j.n = n;
    j.pos = 2;
    j.exif = ExifInfo();

    bool gotScan = false;
    int guard = 0;

    for (;;) {
        if (++guard > 100000) { err = tr(Sid::err_jpeg_structure); return false; }
        int m = j.next_marker();
        if (m < 0) break;
        if (m == 0xD9) break;                       // EOI
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;   // 独立的 RST/TEM

        if (m == 0xDA) {
            if (!j.decode_scan()) { err = tr(Sid::err_jpeg_scan); return false; }
            gotScan = true;
            continue;
        }
        switch (m) {
        case 0xDB: if (!j.parse_dqt()) { err = tr(Sid::err_jpeg_dqt); return false; } break;
        case 0xC4: if (!j.parse_dht()) { err = tr(Sid::err_jpeg_dht); return false; } break;
        case 0xC0: case 0xC1: case 0xC2:
            if (!j.parse_sof(m & 15)) { err = tr(Sid::err_jpeg_sof); return false; }
            break;
        case 0xDD: {
            size_t content, end;
            if (!j.segment_bounds(content, end) || content + 2 > end) { err = tr(Sid::err_jpeg_dri); return false; }
            j.restartInterval = (int)rd16be(p + content);
            j.pos = end;
            break; }
        case 0xE1: {  // APP1: EXIF
            size_t content, end;
            if (!j.segment_bounds(content, end)) { err = tr(Sid::err_jpeg_app1); return false; }
            if (end - content >= 6 && !memcmp(p + content, "Exif\0\0", 6)) {
                parse_exif_tiff(p + content + 6, end - content - 6, j.exif);
            }
            j.pos = end;
            break; }
        default: {
            // 其它段（APPn/COM 等）：跳过
            size_t content, end;
            if (m == 0xDD || (m >= 0xC0 && m <= 0xCF)) { err = tr(Sid::err_jpeg_feature); return false; }
            if (!j.segment_bounds(content, end)) break;   // 无长度字段的话直接结束
            j.pos = end;
            break; }
        }
    }

    if (!j.gotFrame || !gotScan) { err = tr(Sid::err_jpeg_incomplete); return false; }
    if (!j.build_image(img, err)) return false;
    exif = j.exif;
    return true;
}
