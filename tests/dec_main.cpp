// 测试用: 解码图片并输出信息 / 原始 RGBA 像素（本机 Linux 编译，对照 PIL 验证解码器）
#include "codec.h"
#include "util.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: dec_main <file> [dump.raw] [--oriented]\n"); return 2; }
    bool oriented = (argc >= 4 && !strcmp(argv[3], "--oriented"));
    std::wstring path = utf8_to_utf16(argv[1]);
    Image img;
    ImageMeta meta;
    std::wstring err;
    if (!decode_image_file(path, img, meta, err)) {
        printf("FAIL %s : %s\n", argv[1], utf16_to_utf8(err).c_str());
        return 1;
    }
    if (oriented && meta.exif.valid && meta.exif.orientation >= 2 && meta.exif.orientation <= 8)
        apply_exif_orientation(img, meta.exif.orientation);
    printf("OK %s %dx%d format=%s frames=%d\n", argv[1], img.w, img.h,
           utf16_to_utf8(meta.formatName).c_str(), meta.frames);
    if (meta.exif.valid) {
        printf("  exif: orient=%d make=%s model=%s date=%s exp=%s f=%s iso=%s focal=%s\n",
               meta.exif.orientation,
               utf16_to_utf8(meta.exif.make).c_str(), utf16_to_utf8(meta.exif.model).c_str(),
               utf16_to_utf8(meta.exif.dateTime).c_str(), utf16_to_utf8(meta.exif.exposure).c_str(),
               utf16_to_utf8(meta.exif.fnumber).c_str(), utf16_to_utf8(meta.exif.iso).c_str(),
               utf16_to_utf8(meta.exif.focal).c_str());
    }
    if (argc >= 3) {
        FILE* f = fopen(argv[2], "wb");
        if (!f) { printf("cannot write %s\n", argv[2]); return 3; }
        // 输出 RGBA 字节序（与 PIL 的 RGBA 对照一致）
        std::vector<u8> outbuf;
        outbuf.reserve(img.px.size() * 4);
        for (u32 c : img.px) {
            outbuf.push_back((u8)px_r(c));
            outbuf.push_back((u8)px_g(c));
            outbuf.push_back((u8)px_b(c));
            outbuf.push_back((u8)px_a(c));
        }
        fwrite(outbuf.data(), 1, outbuf.size(), f);
        fclose(f);
    }
    return 0;
}
