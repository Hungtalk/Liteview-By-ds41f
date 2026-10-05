// ============================================================================
//  codec.cpp : 格式识别与统一入口
// ============================================================================
#include "codec.h"
#include "util.h"

void apply_exif_orientation(Image& img, int orientation) {
    image_apply_exif_orientation(img, orientation);
}

bool decode_image(const std::vector<u8>& d, Image& img, ImageMeta& meta, std::wstring& err) {
    meta = ImageMeta();
    img.release();
    if (d.size() < 16) { err = L"文件太小，不是有效图片"; return false; }

    const u8* p = d.data();
    const size_t n = d.size();

    if (n >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') {
        meta.formatName = L"PNG";
        return png_decode(p, n, img, err);
    }
    if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) {
        meta.formatName = L"JPEG";
        return jpeg_decode(p, n, img, meta.exif, err);
    }
    if (p[0] == 'B' && p[1] == 'M') {
        meta.formatName = L"BMP";
        return bmp_decode(p, n, img, err);
    }
    if (p[0] == 'G' && p[1] == 'I' && p[2] == 'F' && p[3] == '8') {
        meta.formatName = L"GIF";
        return gif_decode(p, n, img, &meta.frames, err);
    }
    err = L"不支持的图片格式（支持 BMP / PNG / JPEG / GIF）";
    return false;
}

bool decode_image_file(const std::wstring& path, Image& img, ImageMeta& meta, std::wstring& err) {
    std::vector<u8> data;
    if (!read_file_bytes(path, data, err)) return false;
    return decode_image(data, img, meta, err);
}

bool is_supported_image_ext(const std::wstring& ext) {
    static const wchar_t* kExts[] = {
        L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp", L".dib",
    };
    for (auto e : kExts)
        if (ext == e) return true;
    return false;
}
