#!/usr/bin/env bash
# ============================================================================
#  LiteView 全量对照测试
#
#  需要: g++ / python3 / Pillow；djpeg 可选（有则做 JPEG 严格对照）
#  可移植: 不依赖 Windows 头文件（仅解码器 + i18n，无 UI）
#
#  注意: codec_*.cpp 通过 tr()/trf() 输出本地化错误信息，因此链接测试程序时
#        必须同时带上 src/i18n.cpp 与一个语言包实现
#        （src/lang_packs_generated.cpp，缺失时用 src/lang_packs_stub.cpp）。
# ============================================================================
set -e
cd "$(dirname "$0")"
SRC=../src
CXX="g++ -std=c++17 -O2 -I$SRC"

# 语言包实现：优先内嵌生成物，缺失时用占位实现
PACK_SRC="$SRC/lang_packs_generated.cpp"
if [ ! -f "$PACK_SRC" ]; then
    PACK_SRC="$SRC/lang_packs_stub.cpp"
fi

# 解码器 + 本地化（不含 UI）
CORE="$SRC/util.cpp $SRC/image.cpp $SRC/inflate.cpp $SRC/i18n.cpp $PACK_SRC \
      $SRC/codec.cpp $SRC/codec_bmp.cpp $SRC/codec_png.cpp $SRC/codec_gif.cpp $SRC/codec_jpeg.cpp"

echo "== 1. 生成测试图片 =="
python3 gen_images.py > /dev/null

echo "== 2. 构建解码测试程序 =="
$CXX $CORE dec_main.cpp -o dec_main

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
$CXX $CORE orient_test.cpp -o orient_test
mkdir -p of
./orient_test of > /dev/null
python3 check_orient.py

echo "== 7. 语言包完整性检查（i18n）=="
$CXX $SRC/util.cpp $SRC/i18n.cpp $PACK_SRC i18n_tool.cpp -o i18n_tool
for pack in "$SRC"/../lang/*.csv; do
    case "$(basename "$pack")" in
        template.csv) continue ;;
    esac
    ./i18n_tool check "$pack" --require-all
done

echo "== 8. 无内嵌包时的回退路径（stub）=="
$CXX $SRC/util.cpp $SRC/i18n.cpp $SRC/lang_packs_stub.cpp i18n_tool.cpp -o i18n_tool_stub
./i18n_tool_stub list > /dev/null
echo "   stub 构建通过（仅内置英文 + 外置语言包）"

echo
echo "==== 全部测试通过 ===="
