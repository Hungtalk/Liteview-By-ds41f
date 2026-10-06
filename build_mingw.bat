@echo off
rem ============================================================
rem  LiteView - 使用 MinGW-w64 构建（可选备用方案）
rem ============================================================
setlocal
cd /d "%~dp0"

where x86_64-w64-mingw32-g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] x86_64-w64-mingw32-g++ not found - install the MinGW-w64 toolchain
    exit /b 1
)

if not exist build mkdir build

echo === [1/3] compile resources ===
x86_64-w64-mingw32-windres -I src -DLITEVIEW_EMBED_RC_MANIFEST LiteView.rc -O coff -o build\LiteView_res.o
if errorlevel 1 exit /b 1

echo === [2/3] compile and link ===
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -municode -mwindows ^
  -DNOMINMAX -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Isrc ^
  src\image.cpp src\util.cpp src\inflate.cpp src\codec.cpp src\codec_bmp.cpp ^
  src\codec_png.cpp src\codec_gif.cpp src\codec_jpeg.cpp src\render.cpp ^
  src\settings.cpp src\assoc.cpp src\i18n.cpp src\viewer.cpp src\toolbar.cpp ^
  src\panels.cpp src\main.cpp build\LiteView_res.o ^
  -o build\LiteView.exe -lgdi32 -luser32 -lshell32 -ladvapi32
if errorlevel 1 exit /b 1

echo === [3/3] copy language packs ===
if not exist build\lang mkdir build\lang
xcopy /Y /I /Q "lang\*.csv" "build\lang" >nul

echo.
echo Output: %~dp0build\LiteView.exe
endlocal
