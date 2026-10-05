// ============================================================================
//  image.cpp : 几何变换 / EXIF 方向 / 半采样
// ============================================================================
#include "image.h"

void image_rot90_cw(const Image& s, Image& d) {
    d.reset(s.h, s.w);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        for (int x = 0; x < s.w; x++) {
            // (sx,sy) -> (dx,dy) = (H-1-sy, sx)
            d.row(x)[s.h - 1 - y] = sr[x];
        }
    }
}

void image_rot90_ccw(const Image& s, Image& d) {
    d.reset(s.h, s.w);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        for (int x = 0; x < s.w; x++) {
            // (sx,sy) -> (dx,dy) = (sy, W-1-sx)
            d.row(s.w - 1 - x)[y] = sr[x];
        }
    }
}

void image_rot180(const Image& s, Image& d) {
    d.reset(s.w, s.h);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        u32* dr = d.row(s.h - 1 - y);
        for (int x = 0; x < s.w; x++) dr[s.w - 1 - x] = sr[x];
    }
}

void image_flip_h(const Image& s, Image& d) {
    d.reset(s.w, s.h);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        u32* dr = d.row(y);
        for (int x = 0; x < s.w; x++) dr[x] = sr[s.w - 1 - x];
    }
}

void image_flip_v(const Image& s, Image& d) {
    d.reset(s.w, s.h);
    for (int y = 0; y < s.h; y++)
        memcpy(d.row(y), s.row(s.h - 1 - y), (size_t)s.w * 4);
}

void image_transpose(const Image& s, Image& d) {
    d.reset(s.h, s.w);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        for (int x = 0; x < s.w; x++) d.row(x)[y] = sr[x];
    }
}

void image_transverse(const Image& s, Image& d) {
    d.reset(s.h, s.w);
    for (int y = 0; y < s.h; y++) {
        const u32* sr = s.row(y);
        for (int x = 0; x < s.w; x++) {
            // (sx,sy) -> (dx,dy) = (H-1-sy, W-1-sx)
            d.row(s.w - 1 - x)[s.h - 1 - y] = sr[x];
        }
    }
}

void image_apply_exif_orientation(Image& img, int o) {
    if (o < 2 || o > 8 || img.empty()) return;
    Image t;
    switch (o) {
    case 2: image_flip_h(img, t);      break;
    case 3: image_rot180(img, t);      break;
    case 4: image_flip_v(img, t);      break;
    case 5: image_transpose(img, t);   break;
    case 6: image_rot90_cw(img, t);    break;
    case 7: image_transverse(img, t);  break;
    case 8: image_rot90_ccw(img, t);   break;
    default: return;
    }
    img = std::move(t);
}

bool image_has_alpha(const Image& img) {
    const size_t n = img.px.size();
    const u32* p = img.px.data();
    for (size_t i = 0; i < n; i++)
        if ((p[i] >> 24) != 255u) return true;
    return false;
}

void image_downsample_half(const Image& s, Image& d) {
    const int dw = (s.w + 1) / 2;
    const int dh = (s.h + 1) / 2;
    d.reset(dw, dh);
    for (int y = 0; y < dh; y++) {
        const int sy0 = y * 2;
        const int sy1 = std::min(sy0 + 1, s.h - 1);
        const u32* r0 = s.row(sy0);
        const u32* r1 = s.row(sy1);
        u32* dr = d.row(y);
        for (int x = 0; x < dw; x++) {
            const int sx0 = x * 2;
            const int sx1 = std::min(sx0 + 1, s.w - 1);
            const u32 c00 = r0[sx0], c10 = r0[sx1], c01 = r1[sx0], c11 = r1[sx1];
            const u32 a = (u32(px_a(c00)) + px_a(c10) + px_a(c01) + px_a(c11) + 2) / 4;
            const u32 r = (u32(px_r(c00)) + px_r(c10) + px_r(c01) + px_r(c11) + 2) / 4;
            const u32 g = (u32(px_g(c00)) + px_g(c10) + px_g(c01) + px_g(c11) + 2) / 4;
            const u32 b = (u32(px_b(c00)) + px_b(c10) + px_b(c01) + px_b(c11) + 2) / 4;
            dr[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}
