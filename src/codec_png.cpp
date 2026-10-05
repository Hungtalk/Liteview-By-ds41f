// ============================================================================
//  codec_png.cpp : 自研 PNG 解码器（配自研 inflate）
//  支持: 灰度 1/2/4/8/16、真彩 8/16、调色板 1/2/4/8、灰度+Alpha、RGBA、
//        tRNS 透明、Adam7 隔行扫描
// ============================================================================
#include "codec.h"
#include "inflate.h"

namespace {

// ---- CRC32 ----
u32 g_crc_table[256];
bool g_crc_init = false;

void crc_build() {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
    g_crc_init = true;
}

u32 crc32_of(const u8* p, size_t n) {
    if (!g_crc_init) crc_build();
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = g_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- 块遍历 ----
struct PngChunk {
    char type[5] = { 0 };
    const u8* data = nullptr;
    u32 len = 0;
};

struct PngParser {
    const u8* p;
    size_t    n;
    size_t    pos = 8;
    const u8* crcStart = nullptr;
    size_t    crcLen = 0;
    u32       crcStored = 0;

    PngParser(const u8* d, size_t s) : p(d), n(s) {}

    bool next(PngChunk& c) {
        if (pos + 12 > n) return false;
        u32 len = rd32be(p + pos);
        if (len > 0x7FFFFFFFu || pos + 12 + (size_t)len > n) { pos = n; return false; }
        memcpy(c.type, p + pos + 4, 4);
        c.type[4] = 0;
        c.data = p + pos + 8;
        c.len = len;
        crcStart = p + pos + 4;          // type + data
        crcLen = 4 + (size_t)len;
        crcStored = rd32be(p + pos + 8 + len);
        pos += 12 + (size_t)len;
        return true;
    }
    bool crc_ok() const { return crc32_of(crcStart, crcLen) == crcStored; }
};

// ---- 解码状态 ----
struct PngState {
    u32 w = 0, h = 0;
    int depth = 0, color = 0, interlace = 0;
    std::vector<u8>  idat;
    std::vector<u32> pal;             // 0xAARRGGBB
    std::vector<u8>  trnsRaw;
    bool hasTrns = false;
    u32 trnsGray = 0, trnsR = 0, trnsG = 0, trnsB = 0;
};

bool depth_ok(int color, int depth) {
    switch (color) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 2: return depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 4: return depth == 8 || depth == 16;
    case 6: return depth == 8 || depth == 16;
    default: return false;
    }
}

inline u8 to8_16(u32 v) { return (u8)((v * 255u + 32895u) >> 16); }

int paeth(int a, int b, int c) {
    int pa = std::abs(a + b - c - a), pb = std::abs(a + b - c - b), pc = std::abs(a + b - c - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

bool unfilter_row(u8* cur, const u8* prev, size_t n, int unit, int filter) {
    switch (filter) {
    case 0: break;
    case 1:
        for (size_t i = (size_t)unit; i < n; i++) cur[i] = (u8)(cur[i] + cur[i - unit]);
        break;
    case 2:
        if (prev) for (size_t i = 0; i < n; i++) cur[i] = (u8)(cur[i] + prev[i]);
        break;
    case 3:
        if (prev) {
            for (size_t i = 0; i < (size_t)unit && i < n; i++) cur[i] = (u8)(cur[i] + (prev[i] >> 1));
            for (size_t i = (size_t)unit; i < n; i++)
                cur[i] = (u8)(cur[i] + (((int)cur[i - unit] + prev[i]) >> 1));
        } else {
            for (size_t i = (size_t)unit; i < n; i++) cur[i] = (u8)(cur[i] + cur[i - unit]);
        }
        break;
    case 4:
        if (prev) {
            for (size_t i = 0; i < (size_t)unit && i < n; i++) cur[i] = (u8)(cur[i] + prev[i]);
            for (size_t i = (size_t)unit; i < n; i++)
                cur[i] = (u8)(cur[i] + paeth(cur[i - unit], prev[i], prev[i - unit]));
        } else {
            for (size_t i = (size_t)unit; i < n; i++) cur[i] = (u8)(cur[i] + cur[i - unit]);
        }
        break;
    default:
        return false;
    }
    return true;
}

// 一行解码为 ARGB
struct PngDec {
    const PngState& st;

    void line(const u8* row, int width, u32* outp) const {
        switch (st.color) {
        case 0: // 灰度
            switch (st.depth) {
            case 1: case 2: case 4: {
                int maxv = (1 << st.depth) - 1;
                for (int x = 0; x < width; x++) {
                    int sh = (8 - st.depth * (x + 1)) & 7;
                    u32 v = (row[(x * st.depth) >> 3] >> sh) & (u32)maxv;
                    u8 g = (u8)(v * 255u / (u32)maxv);
                    bool t = st.hasTrns && v == st.trnsGray;
                    outp[x] = pack_argb(t ? 0 : 255, g, g, g);
                }
                break; }
            case 8:
                for (int x = 0; x < width; x++) {
                    u8 g = row[x];
                    bool t = st.hasTrns && (u32)g == st.trnsGray;
                    outp[x] = pack_argb(t ? 0 : 255, g, g, g);
                }
                break;
            default: // 16
                for (int x = 0; x < width; x++) {
                    u32 v = rd16be(row + x * 2);
                    u8 g = to8_16(v);
                    bool t = st.hasTrns && v == st.trnsGray;
                    outp[x] = pack_argb(t ? 0 : 255, g, g, g);
                }
                break;
            }
            break;
        case 2: // RGB
            if (st.depth == 8) {
                for (int x = 0; x < width; x++) {
                    u8 r = row[x * 3], g = row[x * 3 + 1], b = row[x * 3 + 2];
                    bool t = st.hasTrns && r == (u8)st.trnsR && g == (u8)st.trnsG && b == (u8)st.trnsB;
                    outp[x] = pack_argb(t ? 0 : 255, r, g, b);
                }
            } else {
                for (int x = 0; x < width; x++) {
                    u32 r = rd16be(row + x * 6), g = rd16be(row + x * 6 + 2), b = rd16be(row + x * 6 + 4);
                    bool t = st.hasTrns && r == st.trnsR && g == st.trnsG && b == st.trnsB;
                    outp[x] = pack_argb(t ? 0 : 255, to8_16(r), to8_16(g), to8_16(b));
                }
            }
            break;
        case 3: // 调色板
            switch (st.depth) {
            case 1: case 2: case 4:
                for (int x = 0; x < width; x++) {
                    int sh = (8 - st.depth * (x + 1)) & 7;
                    u32 idx = (row[(x * st.depth) >> 3] >> sh) & ((1u << st.depth) - 1);
                    outp[x] = st.pal[idx & 255];
                }
                break;
            default: // 8
                for (int x = 0; x < width; x++) outp[x] = st.pal[row[x]];
                break;
            }
            break;
        case 4: // 灰度 + Alpha
            if (st.depth == 8) {
                for (int x = 0; x < width; x++)
                    outp[x] = pack_argb(row[x * 2 + 1], row[x * 2], row[x * 2], row[x * 2]);
            } else {
                for (int x = 0; x < width; x++) {
                    u8 g = to8_16(rd16be(row + x * 4));
                    u8 a = to8_16(rd16be(row + x * 4 + 2));
                    outp[x] = pack_argb(a, g, g, g);
                }
            }
            break;
        case 6: // RGBA
            if (st.depth == 8) {
                for (int x = 0; x < width; x++)
                    outp[x] = pack_argb(row[x * 4 + 3], row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
            } else {
                for (int x = 0; x < width; x++) {
                    u8 r = to8_16(rd16be(row + x * 8));
                    u8 g = to8_16(rd16be(row + x * 8 + 2));
                    u8 b = to8_16(rd16be(row + x * 8 + 4));
                    u8 a = to8_16(rd16be(row + x * 8 + 6));
                    outp[x] = pack_argb(a, r, g, b);
                }
            }
            break;
        default:
            for (int x = 0; x < width; x++) outp[x] = 0xFF000000u;
            break;
        }
    }
};

const int kIx0[7] = { 0, 4, 0, 2, 0, 1, 0 };
const int kIy0[7] = { 0, 0, 4, 0, 2, 0, 1 };
const int kIxs[7] = { 8, 8, 4, 4, 2, 2, 1 };
const int kIys[7] = { 8, 8, 8, 4, 4, 2, 2 };

} // namespace

bool png_decode(const u8* p, size_t n, Image& img, std::wstring& err) {
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 8 || memcmp(p, sig, 8) != 0) { err = L"不是 PNG 文件"; return false; }

    PngState st;
    st.pal.assign(256, 0xFF000000u);
    bool gotIhdr = false;

    PngParser ps(p, n);
    PngChunk c;
    while (ps.next(c)) {
        if (!memcmp(c.type, "IHDR", 4)) {
            if (c.len < 13) { err = L"PNG IHDR 损坏"; return false; }
            if (!ps.crc_ok()) { err = L"PNG 数据校验失败(CRC)"; return false; }
            st.w = rd32be(c.data);
            st.h = rd32be(c.data + 4);
            st.depth = c.data[8];
            st.color = c.data[9];
            int comp = c.data[10], filt = c.data[11];
            st.interlace = c.data[12];
            gotIhdr = true;
            if (comp != 0 || filt != 0 || st.interlace > 1) { err = L"PNG 不支持的特性"; return false; }
            if (!depth_ok(st.color, st.depth)) { err = L"PNG 位深/颜色类型不合法"; return false; }
            if (st.w == 0 || st.h == 0 || st.w > kMaxImageSide || st.h > kMaxImageSide ||
                (u64)st.w * st.h > kMaxImagePixels) { err = L"PNG 尺寸无效或过大"; return false; }
        } else if (!memcmp(c.type, "PLTE", 4)) {
            if (!ps.crc_ok()) { err = L"PNG 数据校验失败(CRC)"; return false; }
            int count = (int)std::min<u32>(c.len / 3, 256);
            for (int i = 0; i < count; i++)
                st.pal[i] = pack_argb(255, c.data[i * 3], c.data[i * 3 + 1], c.data[i * 3 + 2]);
        } else if (!memcmp(c.type, "tRNS", 4)) {
            if (!ps.crc_ok()) { err = L"PNG 数据校验失败(CRC)"; return false; }
            st.hasTrns = true;
            st.trnsRaw.assign(c.data, c.data + c.len);
        } else if (!memcmp(c.type, "IDAT", 4)) {
            if (!ps.crc_ok()) { err = L"PNG 数据校验失败(CRC)"; return false; }
            st.idat.insert(st.idat.end(), c.data, c.data + c.len);
        } else if (!memcmp(c.type, "IEND", 4)) {
            break;
        }
        // 其它块忽略
    }

    if (!gotIhdr || st.idat.empty()) { err = L"PNG 数据不完整"; return false; }
    if (st.color == 3) {
        // 调色板透明表
        if (st.hasTrns) {
            for (size_t i = 0; i < st.trnsRaw.size() && i < 256; i++)
                st.pal[i] = (st.pal[i] & 0x00FFFFFFu) | ((u32)st.trnsRaw[i] << 24);
        }
    } else if (st.hasTrns) {
        if (st.color == 0 && st.trnsRaw.size() >= 2) st.trnsGray = rd16be(st.trnsRaw.data());
        else if (st.color == 2 && st.trnsRaw.size() >= 6) {
            st.trnsR = rd16be(st.trnsRaw.data());
            st.trnsG = rd16be(st.trnsRaw.data() + 2);
            st.trnsB = rd16be(st.trnsRaw.data() + 4);
        } else {
            st.hasTrns = false;
        }
    }

    const int channels = (st.color == 0) ? 1 : (st.color == 2 ? 3 : (st.color == 3 ? 1 : (st.color == 4 ? 2 : 4)));
    const int bitsPerPixel = channels * st.depth;
    const int filterUnit = std::max(1, (bitsPerPixel + 7) / 8);

    std::vector<u8> raw;
    size_t hint = (size_t)st.h * (((size_t)st.w * bitsPerPixel + 7) / 8 + 1);
    std::string zerr;
    if (!zlib_inflate(st.idat.data(), st.idat.size(), raw, &zerr, hint)) {
        err = L"PNG 解压失败: " + utf8_to_utf16(zerr);
        return false;
    }

    img.reset((int)st.w, (int)st.h);
    PngDec dec{ st };

    const size_t maxRow = (((size_t)st.w * bitsPerPixel + 7) / 8) + 1;
    std::vector<u8>  cur(maxRow), prev(maxRow, 0);
    std::vector<u32> lineBuf(st.w);

    if (st.interlace == 0) {
        const size_t rowBytes = ((size_t)st.w * bitsPerPixel + 7) / 8;
        if (raw.size() < (rowBytes + 1) * (size_t)st.h) { err = L"PNG 像素数据不完整"; return false; }
        for (u32 y = 0; y < st.h; y++) {
            const u8* src = raw.data() + (size_t)y * (rowBytes + 1);
            memcpy(cur.data(), src + 1, rowBytes);
            if (!unfilter_row(cur.data(), y ? prev.data() : nullptr, rowBytes, filterUnit, src[0])) {
                err = L"PNG 行滤波类型非法"; return false;
            }
            dec.line(cur.data(), (int)st.w, lineBuf.data());
            memcpy(img.row((int)y), lineBuf.data(), (size_t)st.w * 4);
            prev.swap(cur);
        }
    } else {
        size_t off = 0;
        for (int pass = 0; pass < 7; pass++) {
            const int pw = ((int)st.w - kIx0[pass] + kIxs[pass] - 1) / kIxs[pass];
            const int ph = ((int)st.h - kIy0[pass] + kIys[pass] - 1) / kIys[pass];
            if (pw <= 0 || ph <= 0) continue;
            const size_t rowBytes = ((size_t)pw * bitsPerPixel + 7) / 8;
            if (off + (rowBytes + 1) * (size_t)ph > raw.size()) { err = L"PNG 像素数据不完整"; return false; }
            for (int y = 0; y < ph; y++) {
                const u8* src = raw.data() + off;
                off += rowBytes + 1;
                memcpy(cur.data(), src + 1, rowBytes);
                if (!unfilter_row(cur.data(), y ? prev.data() : nullptr, rowBytes, filterUnit, src[0])) {
                    err = L"PNG 行滤波类型非法"; return false;
                }
                dec.line(cur.data(), pw, lineBuf.data());
                const int cy = kIy0[pass] + y * kIys[pass];
                u32* dst = img.row(cy);
                for (int x = 0; x < pw; x++)
                    dst[kIx0[pass] + x * kIxs[pass]] = lineBuf[x];
                prev.swap(cur);
            }
        }
    }
    return true;
}
