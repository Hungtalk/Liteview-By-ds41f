#!/usr/bin/env bash
# ============================================================
#  LiteView - Linux/WSL 下用 MinGW-w64 交叉编译
#  依赖: g++-mingw-w64-x86-64 (x86_64-w64-mingw32-g++)
# ============================================================
set -e
cd "$(dirname "$0")"
mkdir -p build

echo "=== [1/4] embed language packs: lang/*.csv -> src/lang_packs_generated.cpp ==="
if command -v python3 >/dev/null 2>&1; then
  python3 tools/embed_lang.py
elif command -v python >/dev/null 2>&1; then
  python tools/embed_lang.py
else
  echo "   (no python: using the committed src/lang_packs_generated.cpp)"
fi

echo "=== [2/4] compile resources ==="
x86_64-w64-mingw32-windres -I src -DLITEVIEW_EMBED_RC_MANIFEST LiteView.rc -O coff -o build/LiteView_res.o

echo "=== [3/4] compile & link ==="
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -municode -mwindows \
  -DNOMINMAX -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Isrc \
  src/image.cpp src/util.cpp src/inflate.cpp src/codec.cpp src/codec_bmp.cpp \
  src/codec_png.cpp src/codec_gif.cpp src/codec_jpeg.cpp src/render.cpp \
  src/settings.cpp src/assoc.cpp src/i18n.cpp src/lang_packs_generated.cpp \
  src/viewer.cpp src/toolbar.cpp src/panels.cpp src/main.cpp build/LiteView_res.o \
  -o build/LiteView.exe -lgdi32 -luser32 -lshell32 -ladvapi32

echo "=== [4/4] optional external packs (override the embedded ones) ==="
mkdir -p build/lang
cp -f lang/*.csv build/lang/

ls -la build/LiteView.exe
echo "OK: build/LiteView.exe  (language packs are embedded; build/lang/ overrides them)"
