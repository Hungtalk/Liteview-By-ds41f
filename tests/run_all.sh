#!/usr/bin/env bash
# 解码器全量对照测试（需要 g++ / python3 / Pillow；djpeg 可选）
set -e
cd "$(dirname "$0")"
SRC=../src

echo "== 1. 生成测试图片 =="
python3 gen_images.py > /dev/null

echo "== 2. 构建解码测试程序 =="
g++ -std=c++17 -O2 -I"$SRC" \
  "$SRC/util.cpp" "$SRC/i18n.cpp" "$SRC/image.cpp" "$SRC/inflate.cpp" "$SRC/codec.cpp" \
  "$SRC/codec_bmp.cpp" "$SRC/codec_png.cpp" "$SRC/codec_gif.cpp" "$SRC/codec_jpeg.cpp" \
  dec_main.cpp -o dec_main

echo "== 3. 与 Pillow 逐像素对照 =="
python3 compare.py

echo "== 4. djpeg -nosmooth 同口径严格对照 =="
if command -v djpeg > /dev/null; then
    python3 compare_djpeg.py
else
    echo "   (未安装 djpeg, 跳过；apt install libjpeg-turbo-progs)"
fi

echo "== 5. EXIF 解析与方向应用 =="
python3 make_exif_jpeg.py

echo "== 6. 8 种 EXIF 方向变换对照 =="
g++ -std=c++17 -O2 -I"$SRC" \
  "$SRC/util.cpp" "$SRC/i18n.cpp" "$SRC/image.cpp" "$SRC/inflate.cpp" "$SRC/codec.cpp" \
  "$SRC/codec_bmp.cpp" "$SRC/codec_png.cpp" "$SRC/codec_gif.cpp" "$SRC/codec_jpeg.cpp" \
  orient_test.cpp -o orient_test
mkdir -p of
./orient_test of > /dev/null
python3 check_orient.py

echo "== 7. 语言包完整性检查（i18n）=="
g++ -std=c++17 -O2 -I"$SRC" \
  "$SRC/util.cpp" "$SRC/i18n.cpp" i18n_tool.cpp -o i18n_tool
./i18n_tool check ../lang/zh-CN.csv --require-all

echo
echo "==== 全部测试通过 ===="
