@echo off
rem ============================================================
rem  LiteView - 使用 MinGW-w64 构建（可选备用方案）
rem ============================================================
setlocal
cd /d "%~dp0"

where x86_64-w64-mingw32-g++ >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 x86_64-w64-mingw32-g++（MinGW-w64 工具链）
    exit /b 1
)

if not exist build mkdir build

echo === 编译资源 ===
x86_64-w64-mingw32-windres -I src -DLITEVIEW_EMBED_RC_MANIFEST LiteView.rc -O coff -o build\LiteView_res.o
if errorlevel 1 exit /b 1

echo === 编译链接 ===
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -municode -mwindows ^
  -DNOMINMAX -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Isrc ^
  src\image.cpp src\util.cpp src\inflate.cpp src\codec.cpp src\codec_bmp.cpp ^
  src\codec_png.cpp src\codec_gif.cpp src\codec_jpeg.cpp src\render.cpp ^
  src\settings.cpp src\assoc.cpp src\viewer.cpp src\toolbar.cpp src\panels.cpp ^
  src\main.cpp build\LiteView_res.o ^
  -o build\LiteView.exe -lgdi32 -luser32 -lshell32 -ladvapi32
if errorlevel 1 exit /b 1

echo.
echo 生成: %~dp0build\LiteView.exe
endlocal
