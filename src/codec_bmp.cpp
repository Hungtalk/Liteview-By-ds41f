// ============================================================================
//  codec_bmp.cpp : 自研 BMP 解码器
//  支持: 1/4/8/16/24/32 bpp、BI_RGB、BI_RLE8、BI_RLE4、BI_BITFIELDS(含 alpha)
// ============================================================================
#include "codec.h"

namespace {

struct MaskInfo { u32 mask; int shift; int bits; };

MaskInfo make_mask(u32 m) {
    MaskInfo mi{ m, 0, 0 };
    if (!m) return mi;
    while (!(m & 1u)) { m >>= 1; mi.shift++; }
    while (m & 1u) { m >>= 1; mi.bits++; }
    return mi;
}

u8 scale_channel(u32 v, const MaskInfo& mi) {
    if (mi.bits == 0) return 0;
    if (mi.bits == 8) return (u8)v;
    u32 maxv = (1u << mi.bits) - 1u;
    return (u8)((v * 255u + maxv / 2u) / maxv);
}

} // namespace

bool bmp_decode(const u8* p, size_t n, Image& img, std::wstring& err) {
    if (n < 26 || p[0] != 'B' || p[1] != 'M') { err = L"BMP 文件头损坏"; return false; }

    const u32 dataOff = rd32le(p + 10);
    const u32 dibSize = rd32le(p + 14);

    int  w = 0, h = 0, bpp = 0;
    u32  comp = 0, clrUsed = 0;
    u32  rM = 0, gM = 0, bM = 0, aM = 0;
    size_t palOff = 0, palCount = 0;
    int  palEntry = 4;
    bool topDown = false;

    if (dibSize == 12) {                       // OS/2 v1 头
        w = (int)rd16le(p + 18);
        h = (int)rd16le(p + 20);
        bpp = (int)rd16le(p + 24);
        palOff = 14 + 12;
        palEntry = 3;
        palCount = (bpp <= 8) ? ((size_t)1 << bpp) : 0;
    } else {
        if (dibSize < 40 || n < 14 + 40) { err = L"不支持的 BMP 版本"; return false; }
        w = rdi32le(p + 18);
        h = rdi32le(p + 22);
        bpp = (int)rd16le(p + 28);
        comp = rd32le(p + 30);
        clrUsed = rd32le(p + 46);
        if (h < 0) { topDown = true; h = -h; }
        if (comp == 3 || comp == 6) {          // BI_BITFIELDS / BI_ALPHABITFIELDS
            size_t mo = 14 + 40;
            if (n < mo + 12) { err = L"BMP 掩码缺失"; return false; }
            rM = rd32le(p + mo);
            gM = rd32le(p + mo + 4);
            bM = rd32le(p + mo + 8);
            if (dibSize >= 56 && n >= mo + 16) {
                aM = rd32le(p + mo + 12);      // V4/V5 头内自带 alpha 掩码
            } else if (dibSize < 52) {
                // BITMAPINFOHEADER 情形：通过像素数据偏移推断是否带第 4 个掩码
                size_t base = 14 + (size_t)dibSize;
                if (dataOff >= base + 16 && n >= mo + 16) aM = rd32le(p + mo + 12);
            }
        }
        palCount = (bpp <= 8) ? (clrUsed ? (size_t)clrUsed : ((size_t)1 << bpp)) : 0;
        palOff = 14 + dibSize;
    }

    if (w <= 0 || h <= 0 || w > kMaxImageSide || h > kMaxImageSide ||
        (u64)w * (u64)h > kMaxImagePixels) { err = L"BMP 尺寸无效或过大"; return false; }
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) {
        err = L"BMP 位深不支持"; return false;
    }
    if (!(comp == 0 || comp == 1 || comp == 2 || comp == 3 || comp == 6)) {
        err = L"BMP 压缩方式不支持"; return false;
    }

    // ---- 调色板 ----
    u32 pal[256];
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u;
    if (palCount) {
        if (palCount > 256) palCount = 256;
        if ((u64)palOff + palCount * (size_t)palEntry > n) { err = L"BMP 调色板越界"; return false; }
        for (size_t i = 0; i < palCount; i++) {
            const u8* q = p + palOff + i * palEntry;
            u8 b = q[0], g = q[1], r = q[2], a = (palEntry == 4) ? q[3] : 255;
            if (a == 0) a = 255;               // 调色板 alpha 为 0 的常见情况 → 视为不透明
            pal[i] = pack_argb(a, r, g, b);
        }
    }

    // ---- RLE 压缩 ----
    if (comp == 1 || comp == 2) {
        if (bpp != 8 && bpp != 4) { err = L"BMP RLE 位深错误"; return false; }
        if (dataOff >= n) { err = L"BMP 像素数据缺失"; return false; }
        img.reset(w, h);
        std::vector<u8> idx((size_t)w * h, 0);
        int x = 0, y = topDown ? 0 : h - 1;
        const int dy = topDown ? 1 : -1;
        size_t pos = dataOff;
        bool ended = false;
        while (!ended && pos + 2 <= n) {
            u8 cnt = p[pos], val = p[pos + 1];
            pos += 2;
            if (cnt > 0) {
                for (int k = 0; k < cnt; k++) {
                    u8 v = val;
                    if (comp == 2) v = (k & 1) ? (val & 0x0F) : (u8)(val >> 4);
                    if (x >= 0 && x < w && y >= 0 && y < h) idx[(size_t)y * w + x] = v;
                    x++;
                }
            } else if (val == 0) {
                x = 0; y += dy;
            } else if (val == 1) {
                ended = true;
            } else if (val == 2) {
                if (pos + 2 > n) break;
                x += (int)p[pos];
                y += (int)p[pos + 1] * dy;
                pos += 2;
            } else {
                if (comp == 1) {
                    if (pos + val > n) break;
                    for (int k = 0; k < val; k++) {
                        if (x >= 0 && x < w && y >= 0 && y < h) idx[(size_t)y * w + x] = p[pos + k];
                        x++;
                    }
                    pos += val + (val & 1);
                } else {
                    size_t bytes = ((size_t)val + 1) / 2;
                    if (pos + bytes > n) break;
                    for (int k = 0; k < val; k++) {
                        u8 byte = p[pos + k / 2];
                        u8 v = (k & 1) ? (byte & 0x0F) : (u8)(byte >> 4);
                        if (x >= 0 && x < w && y >= 0 && y < h) idx[(size_t)y * w + x] = v;
                        x++;
                    }
                    pos += bytes + (bytes & 1);
                }
            }
        }
        for (int yy = 0; yy < h; yy++) {
            u32* dr = img.row(yy);
            const u8* ir = idx.data() + (size_t)yy * w;
            const u8 mask = (comp == 1) ? 255 : 15;
            for (int xx = 0; xx < w; xx++) dr[xx] = pal[ir[xx] & mask];
        }
        return true;
    }

    // ---- 常规位图 ----
    const size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
    if ((u64)dataOff + stride * (size_t)h > n) { err = L"BMP 像素数据不完整"; return false; }

    img.reset(w, h);
    const MaskInfo rm = make_mask(rM), gm = make_mask(gM), bm = make_mask(bM), am = make_mask(aM);
    bool anyAlpha = false;

    for (int y = 0; y < h; y++) {
        const u8* src = p + dataOff + stride * (size_t)(topDown ? y : (h - 1 - y));
        u32* dst = img.row(y);
        switch (bpp) {
        case 1:
            for (int x = 0; x < w; x++) {
                u8 byte = src[x >> 3];
                dst[x] = pal[(byte >> (7 - (x & 7))) & 1];
            }
            break;
        case 4:
            for (int x = 0; x < w; x++) {
                u8 byte = src[x >> 1];
                dst[x] = pal[(x & 1) ? (byte & 15) : (byte >> 4)];
            }
            break;
        case 8:
            for (int x = 0; x < w; x++) dst[x] = pal[src[x]];
            break;
        case 16:
            for (int x = 0; x < w; x++) {
                u32 v = rd16le(src + x * 2);
                u8 r, g, b, a = 255;
                if (comp == 3 || comp == 6) {
                    r = scale_channel((v & rM) >> rm.shift, rm);
                    g = scale_channel((v & gM) >> gm.shift, gm);
                    b = scale_channel((v & bM) >> bm.shift, bm);
                    if (am.bits) a = scale_channel((v & aM) >> am.shift, am);
                } else {
                    r = (u8)((((v >> 10) & 31) * 255 + 15) / 31);
                    g = (u8)((((v >> 5) & 31) * 255 + 15) / 31);
                    b = (u8)(((v & 31) * 255 + 15) / 31);
                }
                dst[x] = pack_argb(a, r, g, b);
            }
            break;
        case 24:
            for (int x = 0; x < w; x++) {
                const u8* q = src + x * 3;
                dst[x] = pack_argb(255, q[2], q[1], q[0]);
            }
            break;
        case 32:
            for (int x = 0; x < w; x++) {
                u32 v = rd32le(src + x * 4);
                u8 r, g, b, a;
                if (comp == 3 || comp == 6) {
                    r = scale_channel((v & rM) >> rm.shift, rm);
                    g = scale_channel((v & gM) >> gm.shift, gm);
                    b = scale_channel((v & bM) >> bm.shift, bm);
                    a = am.bits ? scale_channel((v & aM) >> am.shift, am) : 255;
                } else {
                    b = (u8)v; g = (u8)(v >> 8); r = (u8)(v >> 16); a = (u8)(v >> 24);
                    if (a) anyAlpha = true;
                }
                dst[x] = pack_argb(a, r, g, b);
            }
            break;
        }
    }

    // 32bpp BI_RGB 且 alpha 全为 0 → 按不透明处理；BITFIELDS 无 alpha 掩码同理
    if (bpp == 32 && ((comp == 0 && !anyAlpha) || ((comp == 3 || comp == 6) && !am.bits)))
        for (auto& c : img.px) c |= 0xFF000000u;

    return true;
}
