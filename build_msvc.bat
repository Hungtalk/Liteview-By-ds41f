@echo off
rem ============================================================
rem  LiteView - 使用 MSVC (Visual Studio) 命令行构建 Release x64
rem  如果已有 VS 开发者命令提示符环境，直接双击本脚本即可；
rem  否则脚本会通过 vswhere 定位并初始化 VS 环境。
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "MSBUILD="
where msbuild >nul 2>nul
if not errorlevel 1 set "MSBUILD=msbuild"

if not defined MSBUILD (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
        if defined VSPATH (
            call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
            set "MSBUILD=msbuild"
        )
    )
)

if not defined MSBUILD (
    echo [ERROR] MSBuild / Visual Studio C++ tools not found.
    echo         You can open LiteView.sln in Visual Studio and build (recommended).
    exit /b 1
)

echo === [1/2] embed language packs: lang\*.csv -> src\lang_packs_generated.cpp ===
python tools\embed_lang.py
if errorlevel 1 (
    echo [WARN] language pack generation failed ^(no Python or no lang\*.csv^).
    echo        Using the committed src\lang_packs_generated.cpp instead.
)

echo.
echo === [2/2] building Release x64 ===
%MSBUILD% LiteView.sln /nologo /m /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
    echo [FAILED] check the build output above.
    exit /b 1
)

echo.
if exist "x64\Release\LiteView.exe" (
    echo Output: %~dp0x64\Release\LiteView.exe
    echo Language packs are embedded in the exe; lang\next to it is optional ^(overrides^).
) else if exist "bin\Release\LiteView.exe" (
    echo Output: %~dp0bin\Release\LiteView.exe
) else (
    echo Build finished; output under x64\Release\
)
endlocal
