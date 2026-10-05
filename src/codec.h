// ============================================================================
//  codec.h : 图片解码器统一接口
//  全部为自研解码（BMP / PNG / JPEG / GIF），不调用 WIC/GDI+/系统编解码组件
// ============================================================================
#pragma once

#include "common.h"
#include "image.h"

struct ExifInfo {
    bool valid = false;
    int  orientation = 1;            // 1..8
    std::wstring make, model, dateTime;
    std::wstring exposure;           // 例如 "1/250 s"
    std::wstring fnumber;            // 例如 "f/2.8"
    std::wstring iso;                // 例如 "ISO 200"
    std::wstring focal;              // 例如 "35 mm"
};

struct ImageMeta {
    std::wstring formatName;         // L"PNG"、L"JPEG" 等
    ExifInfo     exif;
    int          frames = 1;         // GIF 帧数（仅显示用；本查看器显示首帧）
};

// 从内存数据解码（按文件魔数自动识别格式）
bool decode_image(const std::vector<u8>& data, Image& img, ImageMeta& meta, std::wstring& err);

// 便捷封装：读文件 + 解码（不做 EXIF 旋转）
bool decode_image_file(const std::wstring& path, Image& img, ImageMeta& meta, std::wstring& err);

// 按 EXIF 方向(1..8)把图像旋转/镜像到正确方向
void apply_exif_orientation(Image& img, int orientation);

// ---- 各格式解码器（内部实现，返回 false 时 err 有说明） ----
bool bmp_decode (const u8* p, size_t n, Image& img, std::wstring& err);
bool png_decode (const u8* p, size_t n, Image& img, std::wstring& err);
bool jpeg_decode(const u8* p, size_t n, Image& img, ExifInfo& exif, std::wstring& err);
bool gif_decode (const u8* p, size_t n, Image& img, int* frameCount, std::wstring& err);

// EXIF (APP1/TIFF) 解析，供 jpeg 使用；也单独导出便于测试
bool parse_exif_tiff(const u8* p, size_t n, ExifInfo& out);

// 该扩展名（ext_of() 结果，小写含点）是否为本查看器支持的图片格式
bool is_supported_image_ext(const std::wstring& ext);
