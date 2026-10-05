// 验证 EXIF 方向变换（与 PIL 的 transpose 语义对照）
#include "codec.h"
#include <cstdio>

int main(int argc, char** argv) {
    const char* dir = (argc >= 2) ? argv[1] : ".";
    Image src;
    src.reset(5, 3);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            src.set(x, y, pack_argb(255, x * 40, y * 80, (x + y) * 20));
    for (int o = 1; o <= 8; o++) {
        Image im = src;
        apply_exif_orientation(im, o);
        char name[256];
        snprintf(name, sizeof(name), "%s/of_%d_%dx%d.raw", dir, o, im.w, im.h);
        FILE* f = fopen(name, "wb");
        if (!f) { printf("write fail %s\n", name); return 1; }
        std::vector<u8> out;
        out.reserve(im.px.size() * 4);
        for (u32 c : im.px) {
            out.push_back((u8)px_r(c));
            out.push_back((u8)px_g(c));
            out.push_back((u8)px_b(c));
            out.push_back((u8)px_a(c));
        }
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    }
    printf("orient dumps written\n");
    return 0;
}
