# 开发环境与构建 / Development & build

构建产物与用户文档见 [README.md](../README.md) / [README.zh-CN.md](../README.zh-CN.md)。
git 访问 GitHub 的代理配置见 [git-proxy.md](git-proxy.md)。

---

## 一、工具链位置（本机实测）

| 工具 | 位置 / 说明 |
|---|---|
| git | **不在 PATH**。用 VS 自带的：`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe` |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 编译环境 | `...\VC\Auxiliary\Build\vcvars64.bat` |
| Python | `python`（用于 `tools/embed_lang.py`、`tests/*.py`） |
| MinGW-w64 | 本机**未安装**；`build_mingw.*` 仅在有交叉工具链时可用 |

## 二、命令行构建

```bat
:: 推荐：脚本会先重新生成内嵌语言包，再交给 MSBuild
build_msvc.bat

:: 手动
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
msbuild LiteView.sln /p:Configuration=Release /p:Platform=x64
```

### 两个容易踩的坑

**1）`/utf-8` 必须开。**
源码含中文注释、`i18n.h` 里有 `\"` 转义、语言包含中日文。缺少 `/utf-8` 时
`cl.exe` 会按系统 ANSI 代码页（本机 936）解析，报 `C4819`，甚至
`C2295: 转义的"": 在宏定义中非法`、`C2001: 字符串字面量中的换行符`。
工程已通过 `<AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>` 设置；
手动写编译命令时不要漏掉。

**2）`PlatformToolset` 必须显式声明。**
`LiteView.vcxproj` 中四处的 `<PlatformToolset>v145</PlatformToolset>`（VS2026）
**不要删**：不声明时 MSBuild 会回退到 **v100（VS2010）** 并以
`MSB8020: 无法找到 Visual Studio 2010 的生成工具` 失败。

- VS2022：打开工程按提示「重定向项目」→ 变成 v143 即可（IDE 会自动改这四行）；
- CLI + 旧工具集：`msbuild ... /p:PlatformToolset=v143` 覆盖（CI 就是这么做的）。

## 三、语言包与内嵌机制

语言包在**构建期**被编译进 exe，因此发布只有一个文件。改过 `lang/*.csv` 后：

```bat
python tools\embed_lang.py            :: 重新生成 src\lang_packs_generated.cpp
python tools\embed_lang.py --check    :: 只校验（缺 key / 未知 key / 占位符不一致 → 非零退出）
```

生成物 `src/lang_packs_generated.cpp` 随仓库提交，因此**克隆后无需 Python 也能直接构建**；
若生成文件缺失，工程会自动改用 `src/lang_packs_stub.cpp`（仅内置英文）。
机制与优先级详见 [lang/README.md](../lang/README.md) 与
[src/i18n_embedded.h](../src/i18n_embedded.h)。

## 四、测试

| 测试 | 平台 | 运行方式 |
|---|---|---|
| 解码器对照（Pillow / djpeg） | Linux/WSL | `tests/run_all.sh`（需 `g++`、`python3`、Pillow；`djpeg` 可选） |
| 语言包完整性 | 跨平台 | `tests/i18n_tool check lang/ja-JP.csv --require-all` |
| 内嵌语言包回归 | **Windows** | 见下（`i18n_tool` 覆盖不到内嵌路径与优先级，需要真实 exe） |

内嵌语言包测试在 Windows 下手工编译（本机 PATH 里没有 g++/cl，用 vcvars64）：

```bat
call "C:\...\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0601 ^
  /I src src\i18n.cpp src\lang_packs_generated.cpp src\util.cpp src\image.cpp src\inflate.cpp ^
  src\codec.cpp src\codec_bmp.cpp src\codec_png.cpp src\codec_gif.cpp src\codec_jpeg.cpp ^
  tests\i18n_embed_test.cpp /Fe:i18n_embed_test.exe
i18n_embed_test.exe
```

覆盖：内嵌包可用性 / `# name:` 解析 / `ja` → `ja-JP` 地区码回退 / 占位符格式化 /
未知语言回退英文 / **外置 `lang\` 覆盖内嵌包**（复制自身到临时目录后以子进程验证）。

## 五、CI

`.github/workflows/ci.yml`：Linux job 跑解码器 + 语言包校验 + 内嵌语言包测试，
Windows job 跑 MSVC Release 构建。工作流通过 `/p:PlatformToolset=` 按 runner
实际安装的工具集覆盖，避免被写死的版本卡住。推送工作流文件需要 token 具备
`workflow` 作用域（见 [git-proxy.md](git-proxy.md) 第五节）。
