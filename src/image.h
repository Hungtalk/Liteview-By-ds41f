// ============================================================================
//  image.h : 内存图像（32 位 ARGB）+ 基本几何操作
// ============================================================================
#pragma once

#include "common.h"

struct Image {
    int w = 0;
    int h = 0;
    std::vector<u32> px;   // 0xAARRGGBB，行优先

    bool empty() const { return w <= 0 || h <= 0 || px.empty(); }
    size_t count() const { return px.size(); }

    void reset(int W, int H) {
        w = W; h = H;
        px.assign((size_t)W * H, 0u);
    }
    void release() {
        w = h = 0;
        std::vector<u32>().swap(px);
    }

    u32*       row(int y)       { return px.data() + (size_t)y * w; }
    const u32* row(int y) const { return px.data() + (size_t)y * w; }

    u32 get(int x, int y) const {
        if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return 0u;
        return px[(size_t)y * w + x];
    }
    void set(int x, int y, u32 c) {
        if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) px[(size_t)y * w + x] = c;
    }
};

// ---- 几何变换（结果写入 dst） ----
void image_rot90_cw (const Image& s, Image& d);
void image_rot90_ccw(const Image& s, Image& d);
void image_rot180   (const Image& s, Image& d);
void image_flip_h   (const Image& s, Image& d);
void image_flip_v   (const Image& s, Image& d);
void image_transpose (const Image& s, Image& d);
void image_transverse(const Image& s, Image& d);

// 按 EXIF 方向（1..8）旋转/镜像图像；1 表示不处理
void image_apply_exif_orientation(Image& img, int orientation);

// 是否存在半透明/透明像素（用于决定是否画棋盘格背景）
bool image_has_alpha(const Image& img);

// 2x2 盒式半采样（构建 mip 链用）
void image_downsample_half(const Image& s, Image& d);
