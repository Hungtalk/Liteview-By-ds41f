// ============================================================================
//  render.h : 图像文档（mip 链）与视图渲染
// ============================================================================
#pragma once

#include "common.h"
#include "image.h"
#include "codec.h"
#include <string>
#include <vector>

struct ImageDoc {
    Image base;
    ImageMeta meta;
    std::wstring path;
    u64  fileSize = 0;
    bool hasAlpha = false;
    std::vector<Image> mips;      // 逐级 1/2 缩小

    void clear();
    bool load(const std::wstring& p, bool applyExif, std::wstring& err);
    void rotate(int dir);         // dir=1 顺时针, -1 逆时针（仅内存）
    void build_mips();
    const Image* mip_for(double scale, int& levelUsed) const;
};

struct ViewDraw {
    double scale = 1.0;           // 显示缩放（屏幕像素 / 图像像素）
    double cx = 0, cy = 0;        // 视图中心对应的图像坐标
    int dstW = 0, dstH = 0;
    int dstStride = 0;            // 目标行跨距（0 = 同 dstW）
    u32  bg = 0xFF000000;
    bool checker = false;         // 透明图 → 棋盘格
    bool smooth = true;           // 放大时双线性
    double shiftX = 0, shiftY = 0;  // 拖动偏移（设备像素）
};

struct ViewTransform {
    double scale = 1;
    double originX = 0, originY = 0;   // 图像 (0,0) 对应的视图坐标
};

ViewTransform compute_view_transform(const ViewDraw& vd, const Image& img);

// 合成图像区域到底层缓冲（含背景/棋盘格/透明混合）
void render_image(u32* dst, const ViewDraw& vd, const ImageDoc& doc);

// 盒式缩放到指定大小
void image_scale_box(const Image& src, Image& dst, int w, int h);
