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
    echo [错误] 未找到 MSBuild / Visual Studio C++ 工具。
    echo        可直接用 Visual Studio 打开 LiteView.sln 构建（推荐）。
    exit /b 1
)

echo === 构建 Release x64 ===
%MSBUILD% LiteView.sln /nologo /m /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
    echo [失败] 请查看上方编译输出。
    exit /b 1
)

echo.
if exist "x64\Release\LiteView.exe" (
    echo 生成: %~dp0x64\Release\LiteView.exe
) else if exist "bin\Release\LiteView.exe" (
    echo 生成: %~dp0bin\Release\LiteView.exe
) else (
    echo 构建完成，输出位于工程目录下 x64\Release\
)
endlocal
