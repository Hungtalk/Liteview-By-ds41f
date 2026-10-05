// ============================================================================
//  codec_gif.cpp : 自研 GIF 解码器（首帧）
//  支持: GIF87a/89a、全局/局部调色板、透明色、隔行、动画计数
//  说明: 本查看器显示首帧；meta.frames 返回总帧数
// ============================================================================
#include "codec.h"

namespace {

struct LsbBits {
    const u8* d = nullptr;
    size_t n = 0;
    size_t pos = 0;
    u32 buf = 0;
    int cnt = 0;
    bool get(u32& out, int nbits) {
        while (cnt < nbits) {
            if (pos >= n) return false;
            buf |= u32(d[pos++]) << cnt;
            cnt += 8;
        }
        out = buf & ((1u << nbits) - 1u);
        buf >>= nbits;
        cnt -= nbits;
        return true;
    }
};

// GIF LZW 解码（LSB 优先）
bool lzw_decode(const u8* data, size_t n, int minCode, std::vector<u8>& out, size_t expectPixels) {
    if (minCode < 2 || minCode > 8) return false;
    const int clear = 1 << minCode;
    const int endCode = clear + 1;
    std::vector<u16> prefix(4096);
    std::vector<u8>  suffix(4096);
    for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (u8)i; }

    int next = endCode + 1;
    int curSize = minCode + 1;
    int prev = -1;

    LsbBits br{ data, n };
    std::vector<u8> stack;
    const size_t limit = expectPixels + 4096;

    while (out.size() < limit) {
        u32 code;
        if (!br.get(code, curSize)) break;
        if ((int)code == clear) {
            next = endCode + 1;
            curSize = minCode + 1;
            prev = -1;
            continue;
        }
        if ((int)code == endCode) break;

        int inCode = (int)code;
        int curFirst = -1;
        if (prev < 0) {
            if (inCode >= clear) return false;
            curFirst = suffix[inCode];
            out.push_back((u8)curFirst);
        } else {
            stack.clear();
            if (inCode < next) {
                int c = inCode, guard = 0;
                while (c >= clear) {
                    if (guard++ > 4096 || c == 0xFFFF) return false;
                    stack.push_back(suffix[c]);
                    c = prefix[c];
                }
                curFirst = suffix[c];
                stack.push_back((u8)curFirst);
                for (size_t i = stack.size(); i-- > 0;) out.push_back(stack[i]);
            } else if (inCode == next) {
                // KwKwK: 当前串 = 上一串 + 上一串首字符
                int c = prev, guard = 0;
                while (c >= clear) {
                    if (guard++ > 4096 || c == 0xFFFF) return false;
                    stack.push_back(suffix[c]);
                    c = prefix[c];
                }
                curFirst = suffix[c];
                stack.push_back((u8)curFirst);
                for (size_t i = stack.size(); i-- > 0;) out.push_back(stack[i]);
                out.push_back((u8)curFirst);
            } else {
                return false;
            }
            if (next < 4096) {
                prefix[next] = (u16)prev;
                suffix[next] = (u8)curFirst;
                next++;
                if (next == (1 << curSize) && curSize < 12) curSize++;
            }
        }
        prev = inCode;
    }
    return out.size() >= expectPixels;
}

// 收集子块数据
bool gather_subblocks(const u8* p, size_t n, size_t& pos, std::vector<u8>& out) {
    for (;;) {
        if (pos >= n) return false;
        u8 sz = p[pos++];
        if (sz == 0) break;
        if (pos + sz > n) return false;
        out.insert(out.end(), p + pos, p + pos + sz);
        pos += sz;
    }
    return true;
}

u32 palette_entry(const u8* tbl, int index) {
    const u8* q = tbl + (size_t)index * 3;
    return pack_argb(255, q[0], q[1], q[2]);
}

} // namespace

bool gif_decode(const u8* p, size_t n, Image& img, int* frameCount, std::wstring& err) {
    if (n < 13 || memcmp(p, "GIF8", 4) != 0) { err = L"不是 GIF 文件"; return false; }

    const int W = (int)rd16le(p + 6);
    const int H = (int)rd16le(p + 8);
    if (W <= 0 || H <= 0 || W > kMaxImageSide || H > kMaxImageSide ||
        (u64)W * H > kMaxImagePixels) { err = L"GIF 尺寸无效或过大"; return false; }
    const u8 lsdPacked = p[10];
    const bool hasGct = (lsdPacked & 0x80) != 0;
    const int gctSize = 2 << (lsdPacked & 7);

    size_t pos = 13;
    const u8* gct = nullptr;
    if (hasGct) {
        if (pos + (size_t)gctSize * 3 > n) { err = L"GIF 调色板越界"; return false; }
        gct = p + pos;
        pos += (size_t)gctSize * 3;
    }

    img.reset(W, H);
    bool firstFrameDone = false;
    int frames = 0;

    int transparentIndex = -1;
    bool hasTransparency = false;

    auto decode_image_block = [&]() -> bool {
        if (pos + 10 > n) return false;
        const int left = (int)rd16le(p + pos);
        const int top = (int)rd16le(p + pos + 2);
        const int fw = (int)rd16le(p + pos + 4);
        const int fh = (int)rd16le(p + pos + 6);
        const u8 packed = p[pos + 8];
        pos += 9;
        const bool hasLct = (packed & 0x80) != 0;
        const bool interlaced = (packed & 0x40) != 0;
        const int lctSize = 2 << (packed & 7);
        const u8* lct = nullptr;
        if (hasLct) {
            if (pos + (size_t)lctSize * 3 > n) return false;
            lct = p + pos;
            pos += (size_t)lctSize * 3;
        }
        if (pos >= n) return false;
        const int minCode = p[pos++];
        std::vector<u8> data;
        if (!gather_subblocks(p, n, pos, data)) return false;

        frames++;
        if (firstFrameDone) return true;      // 只解码首帧，其余仅计数
        if (fw <= 0 || fh <= 0) return false;

        std::vector<u8> indices;
        if (!lzw_decode(data.data(), data.size(), minCode, indices, (size_t)fw * fh)) {
            err = L"GIF 数据损坏";
            return false;
        }
        if (indices.size() < (size_t)fw * fh) {
            err = L"GIF 像素数据不完整";
            return false;
        }

        const u8* pal = lct ? lct : gct;
        const int palSize = lct ? lctSize : gctSize;

        // 行序（隔行）
        std::vector<int> rows;
        if (interlaced) {
            static const int start[4] = { 0, 4, 2, 1 };
            static const int step[4] = { 8, 8, 4, 2 };
            for (int pass = 0; pass < 4; pass++)
                for (int y = start[pass]; y < fh; y += step[pass]) rows.push_back(y);
        } else {
            for (int y = 0; y < fh; y++) rows.push_back(y);
        }

        size_t idx = 0;
        for (int ry = 0; ry < fh; ry++) {
            const int y = rows[ry];
            const int cy = top + y;
            if (cy < 0 || cy >= H) { idx += fw; continue; }
            u32* dst = img.row(cy);
            for (int x = 0; x < fw; x++) {
                u8 v = indices[idx++];
                const int cx = left + x;
                if (cx < 0 || cx >= W) continue;
                if (hasTransparency && v == (u8)transparentIndex) continue;  // 保留底色
                if (pal && v < palSize) dst[cx] = palette_entry(pal, v);
                else dst[cx] = pack_argb(255, 0, 0, 0);
            }
        }
        firstFrameDone = true;
        return true;
    };

    // 主循环
    bool sawImage = false;
    for (;;) {
        if (pos >= n) break;
        u8 block = p[pos++];
        if (block == 0x3B) break;                    // Trailer
        if (block == 0x2C) {                          // Image Descriptor
            if (!decode_image_block()) { err = err.empty() ? L"GIF 图像块损坏" : err; return false; }
            sawImage = true;
        } else if (block == 0x21) {                   // 扩展块
            if (pos >= n) break;
            u8 label = p[pos++];
            if (label == 0xF9) {                      // 图形控制扩展
                if (pos >= n) break;
                const u8 sz = p[pos];
                if (pos + 1 + sz > n) break;
                const u8 gcePacked = p[pos + 1];
                if (sz >= 4) {
                    hasTransparency = (gcePacked & 1) != 0;
                    transparentIndex = p[pos + 4];    // packed 之后: 2 字节延时 + 透明色索引
                }
                pos += 1 + (size_t)sz;                // 跳到块终结符
                if (pos < n && p[pos] == 0) pos++;
            } else {
                std::vector<u8> tmp;
                if (!gather_subblocks(p, n, pos, tmp)) break;
            }
        } else {
            break;                                    // 未知块，停止
        }
    }

    if (!sawImage || !firstFrameDone) { err = L"GIF 数据不完整"; return false; }
    if (frameCount) *frameCount = frames > 0 ? frames : 1;
    return true;
}
