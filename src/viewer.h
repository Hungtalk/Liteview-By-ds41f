// ============================================================================
//  viewer.h : 查看器窗口核心（共享状态与各模块接口）
// ============================================================================
#pragma once

#include "common.h"
#include "render.h"
#include "settings.h"
#include "util.h"

#ifndef _WIN32
#error "The LiteView UI is Windows-only"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>

// 定时器
enum { TID_SLIDE = 1, TID_TOAST = 2 };

// 工具栏按钮
enum BtnId {
    BTN_OPEN = 0, BTN_PREV, BTN_NEXT, BTN_ZOOM_OUT, BTN_ZOOM_IN, BTN_FIT, BTN_ONE,
    BTN_ROT_L, BTN_ROT_R, BTN_SLIDE, BTN_FULL, BTN_SETTINGS, BTN_COUNT
};

enum class UiMode { View, FilePanel, Settings };

struct Viewer {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    Settings set;

    // 布局（设备像素）
    RECT rcToolbar{};
    RECT rcView{};
    RECT rcStatus{};
    int  toolbarH = 0;
    int  statusH = 0;

    // 文档 / 目录
    ImageDoc doc;
    std::vector<std::wstring> files;     // 同目录图片文件名（自然排序）
    std::wstring filesDir;
    int  fileIndex = -1;

    bool loading = false;

    // 视图状态
    double scale = 1.0;
    double cx = 0, cy = 0;
    double shiftX = 0, shiftY = 0;       // 交互临时偏移
    bool   viewDirty = true;

    UiMode mode = UiMode::View;
    bool   showInfo = false;
    bool   fullscreen = false;
    RECT   rcFullPrev{};
    DWORD  fullPrevStyle = 0;
    bool   fullPrevMaximized = false;

    // 呈现缓冲
    HDC     memDC = nullptr;
    HBITMAP dib = nullptr;
    u32*    dibBits = nullptr;
    int     dibW = 0, dibH = 0;
    HGDIOBJ dibOld = nullptr;

    HFONT fontUI = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontBig = nullptr;

    // 输入状态
    POINT mouse{ -9999, -9999 };
    int  hotBtn = -1, downBtn = -1;
    bool dragging = false;
    bool dragPan = false;               // true=平移 false=滑动切换候选
    bool dragMoved = false;
    double dragStartCx = 0, dragStartCy = 0;
    POINT dragStartPt{};
    double dragRawX = 0;                // 滑动累计（像素）
    double dragVelX = 0;                // 最近速度估计

    // 触摸手势
    bool   gestActive = false;
    bool   gestZooming = false;
    double gestStartDist = 0, gestStartScale = 1;
    POINT  gestLast{};
    POINT  gestStart{};
    double gestRawX = 0, gestRawY = 0;
    bool   gestFlick = false;
    double gestFlickDir = 0;
    double gestAnchorImgX = 0, gestAnchorImgY = 0;

    // 幻灯片
    bool slideshow = false;

    // 提示条
    std::wstring toast;
    bool   toastShown = false;

    // 文件面板
    std::wstring fpDir;                  // 空 = “此电脑”（设备列表）
    std::vector<DirEntry> fpItems;
    std::vector<std::wstring> drives;
    int fpSel = 0, fpTop = 0;
    RECT fpRect{};
    int  fpRowH = 0;
    int  fpListTop = 0, fpListBottom = 0;

    // 设置面板
    RECT spRect{};
    std::vector<std::pair<RECT, int>> spRows;
    int spHover = -1;

    // 工具栏按钮矩形（每行）
    std::vector<std::pair<RECT, int>> btnRects;

    // 状态栏提示
    std::wstring hint;
};

// 通用小工具
inline COLORREF argb_to_colorref(u32 c) { return RGB(px_r(c), px_g(c), px_b(c)); }
inline int dpi_px(const Viewer& v, int logical) { return MulDiv(logical, (int)v.dpi, 96); }

// ------------------------------------------------------------------ viewer.cpp
void viewer_init(Viewer& v, HWND hwnd);
void viewer_destroy(Viewer& v);
void viewer_layout(Viewer& v);
void viewer_paint(Viewer& v);
void viewer_refresh(Viewer& v);
void viewer_refresh_rect(Viewer& v, const RECT* region);
void viewer_dpi_changed(Viewer& v, UINT newDpi);
void viewer_open_path(Viewer& v, const std::wstring& path);
void viewer_open_file(Viewer& v, const std::wstring& path, bool keepPanel = false);
void viewer_next(Viewer& v, int delta, bool userAction);
void viewer_zoom_at(Viewer& v, double factor, POINT pt);
void viewer_fit(Viewer& v);
void viewer_actual(Viewer& v);
void viewer_rotate(Viewer& v, int dir);
void viewer_toggle_fullscreen(Viewer& v);
void viewer_set_slideshow(Viewer& v, bool on);
void viewer_show_toast(Viewer& v, const std::wstring& msg);
void viewer_save_settings(Viewer& v);
std::wstring viewer_status_text(Viewer& v);
int  viewer_hit_toolbar(Viewer& v, POINT pt);
int  viewer_scale_pct(Viewer& v);
bool viewer_on_message(Viewer& v, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);

// ------------------------------------------------------------------ toolbar.cpp
const wchar_t* toolbar_tip(int id);
void toolbar_layout(Viewer& v, int width);
void toolbar_paint(Viewer& v, HDC dc);

// ------------------------------------------------------------------ panels.cpp
void filepanel_load(Viewer& v, const std::wstring& dir);
void filepanel_activate(Viewer& v);
void filepanel_key(Viewer& v, WPARAM key);
void filepanel_hit(Viewer& v, POINT pt, bool dbl);
void panels_paint_back(Viewer& v);          // 像素层：遮罩 + 面板底
void panels_paint_text(Viewer& v, HDC dc);  // GDI 文本层
void settingspanel_hit(Viewer& v, POINT pt);
void settingspanel_key(Viewer& v, WPARAM key);
std::wstring slide_interval_text(int sec);

// ------------------------------------------------------------------ 图标绘制
void draw_toolbar_icon(HDC dc, int id, const RECT& r, COLORREF color);
