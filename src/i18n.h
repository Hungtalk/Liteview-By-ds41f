// ============================================================================
//  i18n.h : 多语言文本（内置英文 + 外置语言包）
//
//  设计：
//   * 程序内置英文文案（LITEVIEW_STRINGS 表）——缺包/缺条目时的回退文本；
//   * 中文等其它语言放在 exe 同目录 lang\<code>.csv（或
//     %LOCALAPPDATA%\LiteView\lang），启动时按系统语言自动选择；
//   * 支持运行时切换（设置面板 → 语言），设置保存到 INI 的
//     [general] language（auto = 跟随系统）。
//
//  语言包格式（UTF-8，可带 BOM；三列）：
//      key,english,translation
//   * 第一行可为表头（key,english,translation）；# 开头为注释行；
//   * "# name: 中文（简体）" 可声明语言显示名（设置面板中显示）；
//   * translation 留空 = 使用内置英文；字段可用双引号包裹，"" 表示引号字符；
//   * 文本中 \n = 换行、\t = 制表符、\\ = 反斜杠；
//   * 格式化占位符（%d / %s / %% 等）必须与英文原文保持一致。
//
//  新增文案步骤：在 LITEVIEW_STRINGS 中加一行 X(id, "English")，
//  代码里用 tr(Sid::id) / trf(Sid::id, ...)，并把对应条目补进各语言包。
// ============================================================================
#pragma once

#include "common.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 字符串表（X 宏）。id == 语言包 CSV 中的 key（一一对应）。
// ---------------------------------------------------------------------------
#define LITEVIEW_STRINGS(X) \
    /* ---- 通用 ---- */ \
    X(app_title,                       L"LiteView Image Viewer") \
    /* ---- 工具栏按钮提示 ---- */ \
    X(tip_open,                        L"Open image (O)") \
    X(tip_prev,                        L"Previous image (←)") \
    X(tip_next,                        L"Next image (→)") \
    X(tip_zoom_out,                    L"Zoom out (- / wheel)") \
    X(tip_zoom_in,                     L"Zoom in (+ / wheel)") \
    X(tip_fit,                         L"Fit to window (0)") \
    X(tip_actual,                      L"Actual size 1:1 (1)") \
    X(tip_rot_ccw,                     L"Rotate counter-clockwise (Shift+R)") \
    X(tip_rot_cw,                      L"Rotate clockwise (R)") \
    X(tip_slide,                       L"Slideshow (Space)") \
    X(tip_fullscreen,                  L"Fullscreen (F11)") \
    X(tip_settings,                    L"Settings (Tab)") \
    /* ---- 状态栏 / 空窗口提示 ---- */ \
    X(status_loading,                  L"Loading…") \
    X(status_no_image,                 L"No image open — press O to choose an image, or drag one into the window") \
    X(status_frames,                   L"   %d frames (GIF)") \
    X(status_slideshow,                L"   Slideshow playing") \
    X(empty_hint_line1,                L"Drag an image here, or press O to open one") \
    X(empty_hint_line2,                L"Supports JPG / PNG / GIF / BMP — built-in decoders, no system components") \
    /* ---- 信息浮层 ---- */ \
    X(info_path,                       L"Path:  ") \
    X(info_size,                       L"Size:  %d × %d        Format:  %s        File:  %s") \
    X(info_zoom,                       L"Zoom:  %d%%        Viewport:  %d × %d") \
    X(info_gif_frames,                 L"GIF frames:  %d (showing first frame)") \
    X(info_camera,                     L"Camera:  ") \
    X(info_date,                       L"Date taken:  ") \
    X(info_shutter,                    L"Shutter ") \
    X(info_aperture,                   L"Aperture ") \
    X(info_focal,                      L"Focal length ") \
    X(info_exif_orientation,           L"EXIF orientation:  %d (auto-corrected)") \
    X(info_close_hint,                 L"Press I or click anywhere to close") \
    /* ---- 文件面板 ---- */ \
    X(panel_this_pc,                   L"This PC") \
    X(panel_open_title,                L"Open image") \
    X(panel_back_hint,                 L"This PC (click title to go back)") \
    X(panel_no_files,                  L"(No image files)") \
    X(panel_key_hint,                  L"Double-click/Enter open · Backspace up · Esc close") \
    X(toast_cannot_open_dir,           L"Cannot open folder") \
    /* ---- 提示条（toast） ---- */ \
    X(toast_only_one,                  L"Only one image in this folder") \
    X(toast_last,                      L"Already at the last image") \
    X(toast_first,                     L"Already at the first image") \
    X(toast_no_file,                   L"File does not exist") \
    X(toast_cannot_open,               L"Cannot open: %s") \
    X(toast_assoc_off,                 L"File association removed") \
    X(toast_assoc_on,                  L"Registered: LiteView is available in \"Open with\"") \
    X(toast_assoc_fail,                L"Registration failed") \
    X(toast_defaults_restored,         L"Defaults restored") \
    /* ---- 设置面板 ---- */ \
    X(settings_title,                  L"Settings") \
    X(settings_bg,                     L"Background color") \
    X(settings_bg_black,               L"Black") \
    X(settings_bg_dark,                L"Dark gray") \
    X(settings_bg_light,               L"Light gray") \
    X(settings_bg_white,               L"White") \
    X(settings_fit_on_open,            L"Zoom when opening an image") \
    X(settings_fit,                    L"Fit to window") \
    X(settings_actual,                 L"Actual size") \
    X(settings_fit_last,               L"Remember last") \
    X(settings_interp,                 L"Zoom interpolation (above 100%)") \
    X(settings_interp_smooth,          L"Smooth (bilinear)") \
    X(settings_interp_sharp,           L"Sharp (nearest)") \
    X(settings_exif_rotate,            L"Auto-correct by EXIF orientation") \
    X(settings_slide_interval,         L"Slideshow interval") \
    X(settings_slide_loop,             L"Slideshow loop") \
    X(settings_remember_dir,           L"Remember last folder") \
    X(settings_maximized,              L"Start maximized") \
    X(settings_topmost,                L"Always on top") \
    X(settings_loop_files,             L"Loop after last image") \
    X(settings_language,               L"Language") \
    X(settings_language_auto,          L"Auto (follow system)") \
    X(settings_language_en,            L"English") \
    X(settings_assoc,                  L"File association (add to \"Open with\")") \
    X(settings_assoc_on,               L"Registered · click to remove") \
    X(settings_assoc_off,              L"Not registered · click to register") \
    X(settings_reset,                  L"Reset to defaults") \
    X(settings_close,                  L"Close settings") \
    X(settings_footer,                 L"Click a row to change · Settings saved to %s") \
    X(settings_on,                     L"On") \
    X(settings_off,                    L"Off") \
    X(settings_seconds,                L"%d s") \
    /* ---- 命令行 / 消息框 ---- */ \
    X(cli_assoc_registered,            L"File association registered.\n\nTip: On Windows 10/11, to set it as the default app, confirm via\n\"Open with → Choose another app → LiteView → Always use\".") \
    X(cli_assoc_removed,               L"File association removed.") \
    X(cli_operation_failed,            L"Operation failed") \
    /* ---- 文件关联错误 ---- */ \
    X(assoc_friendly_name,             L"LiteView Image") \
    X(err_assoc_progid,                L"Failed to write ProgID") \
    X(err_assoc_default,               L"Failed to set the default association (possibly restricted by policy)") \
    X(err_assoc_windows_only,          L"Windows only") \
    /* ---- 解码器错误（通用 / BMP / GIF / JPEG / PNG / zlib） ---- */ \
    X(err_file_too_small,              L"File too small to be a valid image") \
    X(err_unsupported_format,          L"Unsupported image format (supported: BMP / PNG / JPEG / GIF)") \
    X(err_bmp_header,                  L"Corrupt BMP file header") \
    X(err_bmp_version,                 L"Unsupported BMP version") \
    X(err_bmp_masks,                   L"Missing BMP color masks") \
    X(err_bmp_size,                    L"Invalid or oversized BMP dimensions") \
    X(err_bmp_bpp,                     L"Unsupported BMP bit depth") \
    X(err_bmp_compression,             L"Unsupported BMP compression") \
    X(err_bmp_palette,                 L"BMP palette out of bounds") \
    X(err_bmp_rle_bpp,                 L"Invalid BMP RLE bit depth") \
    X(err_bmp_no_pixels,               L"Missing BMP pixel data") \
    X(err_bmp_pixels_incomplete,       L"Incomplete BMP pixel data") \
    X(err_gif_not_gif,                 L"Not a GIF file") \
    X(err_gif_size,                    L"Invalid or oversized GIF dimensions") \
    X(err_gif_palette,                 L"GIF palette out of bounds") \
    X(err_gif_corrupt,                 L"Corrupt GIF data") \
    X(err_gif_pixels_incomplete,       L"Incomplete GIF pixel data") \
    X(err_gif_block,                   L"Corrupt GIF image block") \
    X(err_gif_incomplete,              L"Incomplete GIF data") \
    X(err_jpeg_no_frame,               L"JPEG is missing its frame header") \
    X(err_jpeg_cmyk,                   L"CMYK/YCCK color JPEG is not supported") \
    X(err_jpeg_components,             L"Unexpected JPEG component count") \
    X(err_jpeg_not_jpeg,               L"Not a JPEG file") \
    X(err_jpeg_structure,              L"Corrupt JPEG structure") \
    X(err_jpeg_scan,                   L"Corrupt JPEG scan data") \
    X(err_jpeg_dqt,                    L"Corrupt JPEG quantization table") \
    X(err_jpeg_dht,                    L"Corrupt JPEG Huffman table") \
    X(err_jpeg_sof,                    L"Unsupported or corrupt JPEG frame header") \
    X(err_jpeg_dri,                    L"Corrupt JPEG DRI segment") \
    X(err_jpeg_app1,                   L"Corrupt JPEG APP1 segment") \
    X(err_jpeg_feature,                L"Unsupported JPEG feature") \
    X(err_jpeg_incomplete,             L"Incomplete JPEG data") \
    X(err_png_not_png,                 L"Not a PNG file") \
    X(err_png_ihdr,                    L"Corrupt PNG IHDR") \
    X(err_png_crc,                     L"PNG data check failed (CRC)") \
    X(err_png_feature,                 L"Unsupported PNG feature") \
    X(err_png_depth,                   L"Invalid PNG bit depth / color type") \
    X(err_png_size,                    L"Invalid or oversized PNG dimensions") \
    X(err_png_incomplete,              L"Incomplete PNG data") \
    X(err_png_uncompress,              L"PNG decompression failed: %s") \
    X(err_png_pixels_incomplete,       L"Incomplete PNG pixel data") \
    X(err_png_filter,                  L"Invalid PNG row filter type") \
    X(err_zlib_short,                  L"zlib data too short") \
    X(err_zlib_not_deflate,            L"Not deflate-compressed data") \
    X(err_zlib_window,                 L"Invalid zlib window size") \
    X(err_zlib_header,                 L"zlib header check failed") \
    X(err_zlib_dictionary,             L"Preset dictionary not supported") \
    X(err_zlib_corrupt,                L"Corrupt DEFLATE data") \
    X(err_zlib_adler,                  L"adler32 checksum failed") \
    /* ---- 文件读写错误 ---- */ \
    X(err_file_open,                   L"Cannot open file") \
    X(err_file_size,                   L"Cannot determine file size") \
    X(err_file_too_big,                L"File too large (over 2 GB)") \
    X(err_file_read,                   L"Failed to read file")

// 字符串 ID（id 与语言包 key 同名）
enum class Sid {
#define LV_STR_ENUM(id, en) id,
    LITEVIEW_STRINGS(LV_STR_ENUM)
#undef LV_STR_ENUM
    k_count
};

// ---------------------------------------------------------------------------
// 取文本
// ---------------------------------------------------------------------------
// 翻译文本；未装语言包或该条目未翻译时返回内置英文。
// 返回指针在程序生命周期内有效，可直接用于 DrawTextW / MessageBoxW。
const wchar_t* tr(Sid id);

// 带格式化（占位符须与英文原文一致），如 trf(Sid::settings_seconds, sec)
std::wstring trf(Sid id, ...);

// ---------------------------------------------------------------------------
// 语言包管理
// ---------------------------------------------------------------------------
struct LangPack {
    std::wstring code;    // 语言代码（文件名去 .csv），如 "zh-CN"
    std::wstring name;    // 显示名（"# name:" 声明或等于 code）
    std::wstring path;    // 完整路径
};

// 启动 / 切换：preferred 为 "auto" 或空 = 按系统语言；否则为语言代码。
// 找不到对应语言包时回退内置英文（并尽力加载 en 语言包）。
bool i18n_select(const std::wstring& preferred);
void i18n_init(const std::wstring& preferred);

std::wstring i18n_system_code();                 // 系统语言代码（如 "zh-CN"，无则 "en"）
const std::wstring& i18n_current_code();         // 当前生效代码（内置英文为 "en"）
const std::wstring& i18n_current_name();         // 当前语言显示名
std::wstring i18n_lang_dir();                    // 主语言包目录（exe\lang）
std::vector<LangPack> i18n_available_packs();    // 扫描 exe\lang 与用户目录 lang，按代码排序

// ---------------------------------------------------------------------------
// 工具 / 测试接口（语言包校验、模板导出；供 tests/i18n_tool 使用）
// ---------------------------------------------------------------------------
int            i18n_string_count();              // 条目总数
const char*    i18n_key(int index);              // 条目 key（ASCII）
const wchar_t* i18n_english(int index);          // 内置英文
const wchar_t* i18n_text(int index);             // 当前生效文本
bool           i18n_is_overridden(int index);    // 是否被语言包覆盖
int            i18n_override_count();            // 被覆盖的条目数
void           i18n_reset_overrides();           // 清空已加载的覆盖（回到内置英文）
// 直接加载某个语言包文件（重置现有覆盖后生效）。
// nameOut: 包的显示名；unknownKeys: 收集表中不存在的 key（可选）。
bool i18n_load_file(const std::wstring& path,
                    std::wstring* nameOut = nullptr,
                    std::vector<std::string>* unknownKeys = nullptr);
