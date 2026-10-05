# LiteView — 轻量图片查看器（Windows / C++ / 纯 Win32）

> [English](README.md) · 🌐 简体中文

一个**自备解码协议**的轻量图片查看器：图片解码（BMP / PNG / JPEG / GIF）全部为自研实现，
界面为纯 Win32 + GDI 自绘（自绘工具栏、状态栏、文件浏览面板、设置面板、信息浮层），
**不调用** Windows 照片应用、Windows Photo Viewer、WIC / GDI+、.NET、Qt 等任何可能被精简掉的组件
（包括窗口内一切绘制均为自绘，不用系统通用对话框）。

Release 单文件约 **300KB**（静态链接运行库，无外部 DLL），可在被裁剪过的 Windows（LTSC / Server / 双精简版）上运行。

---

## 一、需求对照

| # | 需求 | 实现方式 |
|---|------|----------|
| 1 | 自备查看协议、不调用系统组件 | 自研 BMP/PNG/JPEG/GIF 解码器（内置自研 DEFLATE/zlib）；自绘 UI，无系统图片 API；单实例用自备 `WM_COPYDATA` 协议把“打开图片”请求转发给已运行窗口；文件关联使用自备 ProgID `LiteView.Image`（HKCU，免管理员） |
| 2 | 体积小、轻量 | 无第三方库、无 MFC/ATL、无资源字体；Release 约 300KB（MinGW 交叉编译 strip 后实测 268KB），启动即用 |
| 3 | 设置关联 | 设置面板内一键“注册 / 取消”；命令行 `--register` / `--unregister`；写入“打开方式”列表并把默认值指向本程序（可撤销，自动备份原值） |
| 4 | 基本查看按钮 | 工具栏：打开、上一张、下一张、缩小、放大、适应窗口、1:1、左旋、右旋、幻灯片、全屏、设置（矢量图标，鼠标悬停有提示） |
| 5 | 兼容触屏 | `WM_GESTURE`：单指拖动平移 / 滑动切换、双指捏合缩放（以手势中心为锚点）、惯性甩动切换图片；按钮与列表均按 DPI 放大，适配手指点按 |
| 6 | 自定义设置 | 背景色、打开时缩放、放大插值（平滑/锐利）、EXIF 自动旋转、幻灯片间隔/循环、记忆文件夹、启动最大化、置顶、循环浏览、重置；保存到 INI |
| 7 | 启动默认最大化 | 默认 `SW_SHOWMAXIMIZED`（设置中可关），并保留上次窗口行为 |
| 8 | 滑动 / 按钮切换图片 | 按钮、方向键、鼠标拖拽滑动、触屏滑动 / 甩动，全部支持；可选择是否循环 |
| 9 | 选择图片 | 自绘“打开图片”面板：设备列表 / 目录 / 图片列表、键盘与鼠标导航（不依赖系统文件对话框） |
| 10 | 1024×768 ～ 3840×2160 | 布局流式自适应（工具栏可自动换行）；4K 图片配 mip 链 + 盒式重采样，缩放/平移流畅；测试集包含 3840×2160 JPEG/PNG 用例 |
| 11 | 高 DPI | 清单声明 PerMonitorV2（旧系统回退 PerMonitor/System）+ 运行时 API 兜底；`WM_DPICHANGED` 动态重建字体与布局 |

---

## 二、操作一览

### 快捷键
| 按键 | 功能 |
|------|------|
| `← / →`、`PgUp / PgDn` | 上一张 / 下一张 |
| `Home / End` | 第一张 / 最后一张 |
| `+ / -`、鼠标滚轮 | 以光标为中心缩放 |
| `0` 或 `F` | 适应窗口 |
| `1` | 原始大小 1:1 |
| 双击图片 | 在“适应窗口 / 1:1”间切换 |
| `R` / `Shift+R` | 顺时针 / 逆时针旋转 |
| `空格` 或 `S` | 幻灯片开 / 关 |
| `I` | 信息浮层（尺寸、格式、EXIF…） |
| `O` | 打开图片面板 |
| `B` | 切换背景色 |
| `Tab` | 设置面板 |
| `F11` | 全屏 |
| `Esc` | 关闭面板 / 浮层 / 退出全屏 / 停止幻灯片 |

### 鼠标
- 滚轮缩放（指向光标处）；100% 以内拖动为平移；缩小状态下左右拖动为“滑动切图”。
- 中键拖动 = 平移；右键 = 关闭面板 / 停止幻灯片。

### 触屏
- 单指拖动：图片放大时平移；适应窗口时左右滑动切换上一张 / 下一张（支持甩动惯性）。
- 双指捏合：缩放（锚定手势中心）。

---

## 三、构建

### 方式 A：Visual Studio（推荐）
1. 用 **Visual Studio 2026 Community**（或 2022）打开 `LiteView.sln`。
2. 若 IDE 提示“重定向项目工具集”，直接确认（工程未写死 PlatformToolset）。
3. 选择 `Release | x64` 生成即可，产物在 `x64\Release\LiteView.exe`（也可选 `x86` 生成 32 位版）。

> 工程要点：静态运行库（`/MT`）、`/utf-8`、`/W4`、SubSystem Windows；已适配 VS2022/VS2026（v143/v145 重定向即可）。
> 高 DPI 清单通过「清单工具 → 附加清单文件」合并 `res\LiteView.manifest`（含 PerMonitorV2）；
> `LiteView.rc` 只放图标与版本信息（仅 MinGW 构建时由 `windres -DLITEVIEW_EMBED_RC_MANIFEST` 在 rc 中嵌入清单），
> 从根源上避免“rc 清单 + VS 自动清单”两份重复导致的 CVT1100/LNK1123。

### 方式 B：命令行（无需打开 IDE）
```bat
:: 在“VS 开发者命令提示符”中：
build_msvc.bat
```

### 方式 C：MinGW-w64（备用，交叉编译）
```bat
:: Windows 上（需安装 MinGW-w64 于 PATH）：
build_mingw.bat

:: 或在 Linux / WSL 中交叉编译：
./build_mingw.sh
```

---

## 四、文件关联（设置面板 → 文件关联）

- 点击“未注册 · 点击注册”后：
  - 写入 `HKCU\Software\Classes\LiteView.Image`（含图标与 `shell\open\command`）；
  - 为 `.jpg .jpeg .jpe .jfif .png .gif .bmp .dib` 添加 **OpenWithProgids**（“打开方式”列表）与 `Applications\LiteView.exe`（供系统选择器显示）；
  - 把扩展名默认值备份到 `HKCU\Software\LiteView\Backup` 后，将 HKCU 默认指向 LiteView（点击“已注册 · 点击取消”可完整回滚）。
- 或使用命令行：`LiteView.exe --register` / `LiteView.exe --unregister`。
- 说明：Windows 10/11 的“默认应用”由系统 UserChoice 保护，无法被程序静默更改；
  注册后如未立即成为默认，请在 **右键 → 打开方式 → 选择其他应用 → LiteView → 始终使用** 中确认。

## 五、设置存储

优先写入程序目录 `LiteView.ini`（便携）；目录不可写时自动回退到
`%LOCALAPPDATA%\LiteView\LiteView.ini`。设置面板底部会显示实际路径。

---

## 六、目录结构

```
LiteView/
├─ LiteView.sln / LiteView.vcxproj      Visual Studio 工程
├─ LiteView.rc                          图标 / 清单 / 版本资源
├─ build_msvc.bat, build_mingw.bat, build_mingw.sh
├─ res/  app.ico, LiteView.manifest
├─ src/
│   ├─ common.h  image.*  util.*        基础类型 / 图像缓冲 / 路径与 INI
│   ├─ inflate.*                       自研 DEFLATE + zlib
│   ├─ codec.h  codec.cpp               格式分发
│   ├─ codec_bmp.cpp / png / jpeg / gif 自研解码器
│   ├─ render.*                         图像文档(mip) + 缩放/合成渲染
│   ├─ settings.*                       用户设置
│   ├─ assoc.*                          注册表关联
│   ├─ viewer.*                         查看器窗口（布局/绘制/输入/手势）
│   ├─ toolbar.cpp                      自绘工具栏 + 矢量图标
│   ├─ panels.cpp                       文件面板 + 设置面板
│   └─ main.cpp                         入口 / 单实例 / 拖放 / 命令行
├─ tools/make_icon.py                   图标生成脚本（Pillow）
└─ tests/                               解码器对照测试（PIL + djpeg）
```

## 七、解码器说明与限制

- **PNG**：1/2/4/8/16 位深、灰度/真彩/调色板/灰度+Alpha/RGBA、tRNS 透明、Adam7 隔行、CRC 校验。
- **JPEG**：基线 + 渐进式，灰度/YCbCr（4:4:4 / 4:2:2 / 4:2:0 / 4:1:1），restart 标记，EXIF（方向/相机/曝光等）；已用 `djpeg -nosmooth` 同口径逐像素校验，最大差 ≤3。
- **GIF**：87a/89a、全局/局部调色板、透明、隔行；**动画 GIF 显示首帧**（状态栏提示总帧数）。
- **BMP**：1/4/8/16/24/32 位、BI_RGB、BI_RLE4/RLE8、BI_BITFIELDS（含 alpha 掩码）、顶朝下。
- 不支持：CMYK/YCCK JPEG、12 位 JPEG、WebP/TIFF/RAW（会给出明确的错误提示，不崩溃）。
- 图片上限约 6400 万像素；超大文件（>2GB）拒绝加载。

## 八、测试（可选）

`tests/` 内含解码器对照测试（本机为 Linux/Python 环境）：
```bash
./tests/run_all.sh
```
- `gen_images.py` 生成覆盖性样例（含手工构造的隔行 PNG、RLE4/8、16 位、透明 GIF、4K 等）；
- `compare.py` 与 Pillow 逐像素对照（无损格式零差异）；
- `compare_djpeg.py` 用 `djpeg -nosmooth` 做同口径严格校验（JPEG）；
- `make_exif_jpeg.py` / `orient_test` 校验 EXIF 解析与 8 种方向变换。

## 九、FAQ

- **生成时报错 CVT1100 “资源重复。类型: MANIFEST” + LNK1123 怎么办？**
  说明工程里同时存在两份清单被嵌入：`rc` 里 `1 24 ...` 一份、VS 自动生成的又一份。
  本工程已规避（rc 默认不嵌清单，改用“清单工具→附加清单文件”）；
  若你使用的是自建工程，二选一即可：
  ① 项目属性 → 清单工具 → 输入和输出 → **嵌入清单 = 否**；
  ② 删掉 `.rc` 里的 `1 24 ...` 行，改为在“清单工具 → 附加清单文件”里填 `res\LiteView.manifest`。
- **关联后资源管理器图标没变？** 注销一次或重启资源管理器；`--unregister` 后重新注册。
- **为什么界面不含系统对话框？** 为了在“照片/通用对话框被移除”的系统上依旧可用，文件选择、设置、信息均为自绘面板。
- **GIF 不动？** 本查看器定位是轻量查看，动画只显示首帧；状态栏会显示总帧数。

## 十、许可证

MIT，详见 [LICENSE.txt](LICENSE.txt)。
