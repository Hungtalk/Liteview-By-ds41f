// ============================================================================
//  render.cpp
// ============================================================================
#include "render.h"
#include "util.h"

// ---------------------------------------------------------------------------
// ImageDoc
// ---------------------------------------------------------------------------
void ImageDoc::clear() {
    base.release();
    mips.clear();
    meta = ImageMeta();
    path.clear();
    fileSize = 0;
    hasAlpha = false;
}

bool ImageDoc::load(const std::wstring& p, bool applyExif, std::wstring& err) {
    std::vector<u8> data;
    if (!read_file_bytes(p, data, err)) return false;
    fileSize = data.size();
    if (!decode_image(data, base, meta, err)) return false;
    if (applyExif && meta.exif.valid && meta.exif.orientation >= 2 && meta.exif.orientation <= 8)
        image_apply_exif_orientation(base, meta.exif.orientation);
    hasAlpha = image_has_alpha(base);
    path = p;
    build_mips();
    return true;
}

void ImageDoc::rotate(int dir) {
    if (base.empty()) return;
    Image t;
    if (dir > 0) image_rot90_cw(base, t);
    else         image_rot90_ccw(base, t);
    base = std::move(t);
    build_mips();
}

void ImageDoc::build_mips() {
    mips.clear();
    for (;;) {
        const Image& prev = mips.empty() ? base : mips.back();
        if (prev.w <= 1 && prev.h <= 1) break;
        Image half;
        image_downsample_half(prev, half);
        if (half.w >= prev.w && half.h >= prev.h) break;
        mips.push_back(std::move(half));
        if (mips.size() > 24) break;
    }
}

const Image* ImageDoc::mip_for(double scale, int& levelUsed) const {
    levelUsed = 0;
    if (scale >= 0.5 || mips.empty()) return &base;
    int level = 0;
    double s = scale;
    while (s < 0.5 && level < (int)mips.size()) { s *= 2.0; level++; }
    levelUsed = level;
    if (level <= 0) return &base;
    if (level > (int)mips.size()) level = (int)mips.size();
    return &mips[level - 1];
}

// ---------------------------------------------------------------------------
// 采样
// ---------------------------------------------------------------------------
static inline u32 blend_over(u32 bg, u32 fg) {
    u32 a = fg >> 24;
    if (a >= 255) return fg;
    if (a == 0) return bg;
    u32 inv = 255 - a;
    u32 r = ((((fg >> 16) & 255u) * a) + (((bg >> 16) & 255u) * inv) + 127u) / 255u;
    u32 g = ((((fg >> 8) & 255u) * a) + (((bg >> 8) & 255u) * inv) + 127u) / 255u;
    u32 b = (((fg & 255u) * a) + ((bg & 255u) * inv) + 127u) / 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline u32 sample_nearest(const Image& im, double u, double v) {
    int x = clampi((int)std::floor(u + 0.5), 0, im.w - 1);
    int y = clampi((int)std::floor(v + 0.5), 0, im.h - 1);
    return im.row(y)[x];
}

static inline u32 sample_bilinear(const Image& im, double u, double v) {
    double fu = std::floor(u), fv = std::floor(v);
    double tx = u - fu, ty = v - fv;
    int x0 = clampi((int)fu, 0, im.w - 1);
    int y0 = clampi((int)fv, 0, im.h - 1);
    int x1 = clampi((int)fu + 1, 0, im.w - 1);
    int y1 = clampi((int)fv + 1, 0, im.h - 1);
    const u32* r0 = im.row(y0);
    const u32* r1 = im.row(y1);
    u32 c00 = r0[x0], c10 = r0[x1], c01 = r1[x0], c11 = r1[x1];
    double w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
    double a = px_a(c00) * w00 + px_a(c10) * w10 + px_a(c01) * w01 + px_a(c11) * w11;
    double r = px_r(c00) * w00 + px_r(c10) * w10 + px_r(c01) * w01 + px_r(c11) * w11;
    double g = px_g(c00) * w00 + px_g(c10) * w10 + px_g(c01) * w01 + px_g(c11) * w11;
    double b = px_b(c00) * w00 + px_b(c10) * w10 + px_b(c01) * w01 + px_b(c11) * w11;
    return pack_argb((int)(a + 0.5), (int)(r + 0.5), (int)(g + 0.5), (int)(b + 0.5));
}

// 分数盒滤波（用于缩小，区间最多覆盖 3x3 个源像素）
static u32 sample_box(const Image& im, double u0, double u1, double v0, double v1) {
    double sa = 0, sr = 0, sg = 0, sb = 0, wsum = 0;
    int iy1 = (int)std::ceil(v1), ix1 = (int)std::ceil(u1);
    for (int iy = (int)std::floor(v0); iy < iy1; iy++) {
        double wy = std::min(v1, iy + 1.0) - std::max(v0, (double)iy);
        if (wy <= 0) continue;
        int yy = clampi(iy, 0, im.h - 1);
        const u32* row = im.row(yy);
        for (int ix = (int)std::floor(u0); ix < ix1; ix++) {
            double wx = std::min(u1, ix + 1.0) - std::max(u0, (double)ix);
            if (wx <= 0) continue;
            int xx = clampi(ix, 0, im.w - 1);
            u32 c = row[xx];
            double w = wx * wy;
            sa += px_a(c) * w; sr += px_r(c) * w;
            sg += px_g(c) * w; sb += px_b(c) * w;
            wsum += w;
        }
    }
    if (wsum <= 0) return 0;
    return pack_argb((int)(sa / wsum + 0.5), (int)(sr / wsum + 0.5),
                     (int)(sg / wsum + 0.5), (int)(sb / wsum + 0.5));
}

// ---------------------------------------------------------------------------
// 主渲染
// ---------------------------------------------------------------------------
ViewTransform compute_view_transform(const ViewDraw& vd, const Image& img) {
    (void)img;
    ViewTransform t;
    t.scale = vd.scale;
    t.originX = vd.dstW * 0.5 - vd.cx * vd.scale + vd.shiftX;
    t.originY = vd.dstH * 0.5 - vd.cy * vd.scale + vd.shiftY;
    return t;
}

void render_image(u32* dst, const ViewDraw& vd, const ImageDoc& doc) {
    const int stride = vd.dstStride ? vd.dstStride : vd.dstW;
    // 背景
    if (vd.checker) {
        const int cs = 10;
        for (int y = 0; y < vd.dstH; y++) {
            u32* row = dst + (size_t)y * stride;
            int cy = y / cs;
            for (int x = 0; x < vd.dstW; x++) {
                int cx = x / cs;
                row[x] = ((cx + cy) & 1) ? 0xFF3A3A3A : 0xFF555555;
            }
        }
    } else {
        for (int y = 0; y < vd.dstH; y++) {
            u32* row = dst + (size_t)y * stride;
            for (int x = 0; x < vd.dstW; x++) row[x] = vd.bg;
        }
    }

    const Image& img = doc.base;
    if (img.empty() || vd.scale <= 0) return;

    ViewTransform t = compute_view_transform(vd, img);
    const double sc = vd.scale;
    int x0 = std::max(0, (int)std::floor(t.originX));
    int x1 = std::min(vd.dstW, (int)std::ceil(t.originX + img.w * sc));
    int y0 = std::max(0, (int)std::floor(t.originY));
    int y1 = std::min(vd.dstH, (int)std::ceil(t.originY + img.h * sc));
    if (x1 <= x0 || y1 <= y0) return;

    if (sc >= 1.0) {
        const bool smooth = vd.smooth && sc > 1.01;
        for (int y = y0; y < y1; y++) {
            u32* drow = dst + (size_t)y * stride;
            double v = ((y + 0.5) - t.originY) / sc - 0.5;
            for (int x = x0; x < x1; x++) {
                double u = ((x + 0.5) - t.originX) / sc - 0.5;
                u32 c = smooth ? sample_bilinear(img, u, v) : sample_nearest(img, u, v);
                if ((c >> 24) == 0 && !vd.checker) continue;
                drow[x] = blend_over(drow[x], c);
            }
        }
    } else {
        int lv = 0;
        const Image* mp = doc.mip_for(sc, lv);
        const double es = sc * (double)(1 << lv);       // 有效缩放 ∈ [0.5, 1)
        const double inv = 1.0 / es;
        for (int y = y0; y < y1; y++) {
            u32* drow = dst + (size_t)y * stride;
            double v0 = ((double)y - t.originY) * inv;
            double v1 = ((double)(y + 1) - t.originY) * inv;
            for (int x = x0; x < x1; x++) {
                double u0 = ((double)x - t.originX) * inv;
                double u1 = ((double)(x + 1) - t.originX) * inv;
                u32 c = sample_box(*mp, u0, u1, v0, v1);
                if ((c >> 24) == 0 && !vd.checker) continue;
                drow[x] = blend_over(drow[x], c);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 简单盒式缩放
// ---------------------------------------------------------------------------
void image_scale_box(const Image& src, Image& dst, int w, int h) {
    if (src.empty() || w <= 0 || h <= 0) { dst.release(); return; }
    dst.reset(w, h);
    for (int y = 0; y < h; y++) {
        double v0 = (double)y * src.h / h;
        double v1 = (double)(y + 1) * src.h / h;
        u32* drow = dst.row(y);
        for (int x = 0; x < w; x++) {
            double u0 = (double)x * src.w / w;
            double u1 = (double)(x + 1) * src.w / w;
            drow[x] = sample_box(src, u0, u1, v0, v1);
        }
    }
}
