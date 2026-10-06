// ============================================================================
//  viewer.cpp : 查看器核心（布局 / 绘制 / 输入 / 手势 / 导航）
// ============================================================================
#include "viewer.h"
#include "codec.h"
#include "i18n.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------
static const u32 kToolbarBg   = 0xFF2D2D30;
static const u32 kToolbarLine = 0xFF191919;
static const u32 kBtnHot      = 0xFF3E3E42;
static const u32 kBtnDown     = 0xFF0A5CA8;
static const u32 kIconColor   = 0xFFDCDCDC;
static const u32 kIconAccent  = 0xFF4FC3F7;
static const u32 kStatusBg    = 0xFF252526;
static const u32 kStatusText  = 0xFFC9C9C9;
static const u32 kStatusDim   = 0xFF8A8A8A;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void fill_rect32(u32* base, int stride, const RECT& clip, const RECT& r, u32 color) {
    int x0 = std::max<int>(r.left, clip.left), x1 = std::min<int>(r.right, clip.right);
    int y0 = std::max<int>(r.top, clip.top), y1 = std::min<int>(r.bottom, clip.bottom);
    for (int y = y0; y < y1; y++) {
        u32* row = base + (size_t)y * stride;
        for (int x = x0; x < x1; x++) row[x] = color;
    }
}

static void darken_rect(u32* base, int stride, const RECT& r, int percent) {
    int keep = 100 - percent;
    for (int y = r.top; y < r.bottom; y++) {
        u32* row = base + (size_t)y * stride;
        for (int x = r.left; x < r.right; x++) {
            u32 c = row[x];
            u32 a = c >> 24;
            if (!a) continue;
            u32 rr = (((c >> 16) & 255u) * (u32)keep) / 100u;
            u32 gg = (((c >> 8) & 255u) * (u32)keep) / 100u;
            u32 bb = ((c & 255u) * (u32)keep) / 100u;
            row[x] = (a << 24) | (rr << 16) | (gg << 8) | bb;
        }
    }
}

static u32 view_bg_color(int idx) {
    switch (idx) {
    case 1: return 0xFF1E1E1E;
    case 2: return 0xFF4B4B4B;
    case 3: return 0xFFF5F5F5;
    default: return 0xFF000000;
    }
}

static double view_cx(const Viewer& v) { return (v.rcView.left + v.rcView.right) * 0.5; }
static double view_cy(const Viewer& v) { return (v.rcView.top + v.rcView.bottom) * 0.5; }

static POINT view_center_client(const Viewer& v) {
    POINT p;
    p.x = (LONG)(v.rcView.left + (v.rcView.right - v.rcView.left) / 2);
    p.y = (LONG)(v.rcView.top + (v.rcView.bottom - v.rcView.top) / 2);
    return p;
}

static UINT query_dpi(HWND hwnd) {
    typedef UINT(WINAPI * PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)(void*)0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE user = GetModuleHandleW(L"user32.dll");
        if (user) pfn = (PFN_GetDpiForWindow)(void*)GetProcAddress(user, "GetDpiForWindow");
    }
    if (pfn) {
        UINT d = pfn(hwnd);
        if (d) return d;
    }
    HDC dc = GetDC(hwnd);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return d ? d : 96;
}

static HFONT make_font(UINT dpi, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, (int)dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void rebuild_fonts(Viewer& v) {
    if (v.fontUI) DeleteObject(v.fontUI);
    if (v.fontBold) DeleteObject(v.fontBold);
    if (v.fontBig) DeleteObject(v.fontBig);
    v.fontUI = make_font(v.dpi, 9, FW_NORMAL);
    v.fontBold = make_font(v.dpi, 9, FW_SEMIBOLD);
    v.fontBig = make_font(v.dpi, 11, FW_NORMAL);
}

static void ensure_backbuffer(Viewer& v, int W, int H) {
    if (v.dib && v.dibW == W && v.dibH == H) return;
    if (v.dib) {
        if (v.memDC && v.dibOld) SelectObject(v.memDC, v.dibOld);
        DeleteObject(v.dib);
        v.dib = nullptr;
        v.dibOld = nullptr;
        v.dibBits = nullptr;
    }
    HDC screen = GetDC(v.hwnd);
    if (!v.memDC) v.memDC = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;             // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    v.dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(v.hwnd, screen);
    if (!v.dib) return;
    v.dibBits = (u32*)bits;
    v.dibW = W;
    v.dibH = H;
    v.dibOld = SelectObject(v.memDC, v.dib);
}

static double fit_scale(const Viewer& v) {
    if (v.doc.base.empty()) return 1.0;
    int m = dpi_px(v, 10);
    double w = (v.rcView.right - v.rcView.left) - m * 2.0;
    double h = (v.rcView.bottom - v.rcView.top) - m * 2.0;
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    double s = std::min(w / v.doc.base.w, h / v.doc.base.h);
    return clampd(s, 1.0 / 64.0, 32.0);
}

static void clamp_view(Viewer& v) {
    const Image& im = v.doc.base;
    if (im.empty()) return;
    double vw = (double)(v.rcView.right - v.rcView.left);
    double vh = (double)(v.rcView.bottom - v.rcView.top);
    double iw = im.w * v.scale;
    double ih = im.h * v.scale;
    if (iw <= vw) v.cx = im.w / 2.0;
    else {
        double half = vw / (2.0 * v.scale);
        v.cx = clampd(v.cx, half, im.w - half);
    }
    if (ih <= vh) v.cy = im.h / 2.0;
    else {
        double half = vh / (2.0 * v.scale);
        v.cy = clampd(v.cy, half, im.h - half);
    }
}

// ---------------------------------------------------------------------------
// 初始化 / 布局
// ---------------------------------------------------------------------------
void viewer_init(Viewer& v, HWND hwnd) {
    v.hwnd = hwnd;
    v.dpi = query_dpi(hwnd);
    v.set.load();
    rebuild_fonts(v);
    v.drives = list_drives();
    v.mode = UiMode::View;
    viewer_layout(v);
}

void viewer_destroy(Viewer& v) {
    KillTimer(v.hwnd, TID_SLIDE);
    KillTimer(v.hwnd, TID_TOAST);
    viewer_save_settings(v);
    if (v.dib) {
        if (v.memDC && v.dibOld) SelectObject(v.memDC, v.dibOld);
        DeleteObject(v.dib);
        v.dib = nullptr;
    }
    if (v.memDC) { DeleteDC(v.memDC); v.memDC = nullptr; }
    if (v.fontUI) { DeleteObject(v.fontUI); v.fontUI = nullptr; }
    if (v.fontBold) { DeleteObject(v.fontBold); v.fontBold = nullptr; }
    if (v.fontBig) { DeleteObject(v.fontBig); v.fontBig = nullptr; }
}

void viewer_dpi_changed(Viewer& v, UINT newDpi) {
    if (newDpi == 0) newDpi = 96;
    v.dpi = newDpi;
    rebuild_fonts(v);
    viewer_layout(v);
    viewer_refresh(v);
}

void viewer_layout(Viewer& v) {
    RECT rc;
    GetClientRect(v.hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (v.fullscreen) {
        v.btnRects.clear();
        v.toolbarH = 0;
        v.statusH = 0;
        v.rcToolbar = { 0, 0, W, 0 };
        v.rcStatus = { 0, H, W, H };
        v.rcView = { 0, 0, W, H };
    } else {
        toolbar_layout(v, W);
        v.rcToolbar = { 0, 0, W, v.toolbarH };
        v.statusH = dpi_px(v, 22);
        v.rcStatus = { 0, H - v.statusH, W, H };
        v.rcView = { 0, v.toolbarH, W, H - v.statusH };
    }
    v.viewDirty = true;
}

void viewer_refresh(Viewer& v) {
    v.viewDirty = true;
    InvalidateRect(v.hwnd, nullptr, FALSE);
}

void viewer_refresh_rect(Viewer& v, const RECT* region) {
    InvalidateRect(v.hwnd, region, FALSE);
}

// ---------------------------------------------------------------------------
// 打开 / 导航
// ---------------------------------------------------------------------------
static void load_dir_files(Viewer& v, const std::wstring& dir) {
    v.files.clear();
    v.filesDir = dir;
    std::vector<DirEntry> es;
    if (!list_dir(dir, es)) return;
    for (const auto& e : es)
        if (!e.isDir && is_supported_image_ext(ext_of(e.name)))
            v.files.push_back(e.name);
}

static void refresh_now(Viewer& v) {
    v.viewDirty = true;
    InvalidateRect(v.hwnd, nullptr, FALSE);
    UpdateWindow(v.hwnd);
}

static bool try_load(Viewer& v, int idx) {
    if (idx < 0 || idx >= (int)v.files.size()) return false;
    std::wstring full = join_path(v.filesDir, v.files[idx]);

    v.loading = true;
    refresh_now(v);

    ImageDoc nd;
    std::wstring err;
    bool ok = nd.load(full, v.set.exifRotate != 0, err);
    v.loading = false;

    if (!ok) {
        viewer_show_toast(v, err.empty() ? trf(Sid::toast_cannot_open, v.files[idx].c_str()) : err);
        v.viewDirty = true;
        InvalidateRect(v.hwnd, nullptr, FALSE);
        return false;
    }

    v.doc = std::move(nd);
    v.fileIndex = idx;
    v.shiftX = v.shiftY = 0;

    int f = v.set.fitOnOpen;
    if (f == 0) {
        v.scale = fit_scale(v);
        v.cx = v.doc.base.w / 2.0;
        v.cy = v.doc.base.h / 2.0;
    } else if (f == 1) {
        v.scale = 1.0;
        v.cx = v.doc.base.w / 2.0;
        v.cy = v.doc.base.h / 2.0;
        clamp_view(v);
    } else {
        if (v.set.lastZoom > 0.02) {
            v.scale = clampd(v.set.lastZoom, 1.0 / 64.0, 32.0);
            v.cx = v.doc.base.w / 2.0;
            v.cy = v.doc.base.h / 2.0;
            clamp_view(v);
        } else {
            v.scale = fit_scale(v);
            v.cx = v.doc.base.w / 2.0;
            v.cy = v.doc.base.h / 2.0;
        }
    }

    viewer_refresh(v);
    std::wstring title = file_name_of(full) + L" - LiteView";
    SetWindowTextW(v.hwnd, title.c_str());
    if (v.set.rememberDir) v.set.lastDir = v.filesDir;
    return true;
}

void viewer_open_file(Viewer& v, const std::wstring& path, bool keepPanel) {
    if (!path_exists(path)) { viewer_show_toast(v, tr(Sid::toast_no_file)); return; }
    std::wstring dir = dir_of(path);
    if (to_lower(dir) != to_lower(v.filesDir) || v.files.empty())
        load_dir_files(v, dir);
    std::wstring name = file_name_of(path);
    int idx = -1;
    for (int i = 0; i < (int)v.files.size(); i++)
        if (v.files[i] == name) { idx = i; break; }
    if (idx < 0) {
        v.files.clear();
        v.files.push_back(name);
        v.filesDir = dir;
        idx = 0;
    }
    if (!keepPanel) v.mode = UiMode::View;
    try_load(v, idx);
    viewer_refresh(v);
}

void viewer_open_path(Viewer& v, const std::wstring& path) {
    if (path_is_dir(path)) { filepanel_load(v, path); return; }
    viewer_open_file(v, path, false);
}

void viewer_next(Viewer& v, int delta, bool userAction) {
    if (v.files.empty()) return;
    if (userAction && v.slideshow) viewer_set_slideshow(v, false);
    int n = (int)v.files.size();
    if (n <= 1) {
        if (userAction) viewer_show_toast(v, tr(Sid::toast_only_one));
        return;
    }
    int i = v.fileIndex;
    for (int t = 0; t < n; t++) {
        i += delta;
        if (i < 0 || i >= n) {
            if (!v.set.loopFiles) {
                viewer_show_toast(v, delta > 0 ? tr(Sid::toast_last) : tr(Sid::toast_first));
                return;
            }
            i = (i % n + n) % n;
        }
        if (i == v.fileIndex) continue;
        if (try_load(v, i)) return;
    }
}

// ---------------------------------------------------------------------------
// 缩放 / 旋转 / 全屏 / 幻灯片
// ---------------------------------------------------------------------------
void viewer_fit(Viewer& v) {
    if (v.doc.base.empty()) return;
    v.scale = fit_scale(v);
    v.cx = v.doc.base.w / 2.0;
    v.cy = v.doc.base.h / 2.0;
    v.shiftX = v.shiftY = 0;
    viewer_refresh(v);
}

void viewer_actual(Viewer& v) {
    if (v.doc.base.empty()) return;
    v.scale = 1.0;
    clamp_view(v);
    viewer_refresh(v);
}

void viewer_zoom_at(Viewer& v, double factor, POINT pt) {
    if (v.doc.base.empty()) return;
    double old = v.scale;
    double ns = clampd(old * factor, 1.0 / 64.0, 32.0);
    if (std::abs(ns - old) < 1e-9) return;
    double vx = (double)pt.x - view_cx(v);
    double vy = (double)pt.y - view_cy(v);
    double ix = v.cx + vx / old;
    double iy = v.cy + vy / old;
    v.scale = ns;
    v.cx = ix - vx / ns;
    v.cy = iy - vy / ns;
    clamp_view(v);
    viewer_refresh(v);
}

void viewer_rotate(Viewer& v, int dir) {
    if (v.doc.base.empty()) return;
    v.doc.rotate(dir);
    clamp_view(v);
    viewer_refresh(v);
}

void viewer_toggle_fullscreen(Viewer& v) {
    if (!v.fullscreen) {
        v.fullPrevStyle = (DWORD)GetWindowLongW(v.hwnd, GWL_STYLE);
        v.fullPrevMaximized = IsZoomed(v.hwnd) != 0;
        GetWindowRect(v.hwnd, &v.rcFullPrev);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(v.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(v.hwnd, GWL_STYLE, (v.fullPrevStyle & ~(DWORD)WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(v.hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        v.fullscreen = true;
    } else {
        SetWindowLongW(v.hwnd, GWL_STYLE, v.fullPrevStyle);
        SetWindowPos(v.hwnd, nullptr,
                     v.rcFullPrev.left, v.rcFullPrev.top,
                     v.rcFullPrev.right - v.rcFullPrev.left,
                     v.rcFullPrev.bottom - v.rcFullPrev.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        if (v.fullPrevMaximized) ShowWindow(v.hwnd, SW_MAXIMIZE);
        v.fullscreen = false;
    }
    viewer_layout(v);
    viewer_refresh(v);
}

void viewer_set_slideshow(Viewer& v, bool on) {
    if (on == v.slideshow) return;
    v.slideshow = on;
    if (on) SetTimer(v.hwnd, TID_SLIDE, (UINT)std::max(1, v.set.slideInterval) * 1000u, nullptr);
    else KillTimer(v.hwnd, TID_SLIDE);
    viewer_refresh(v);
}

void viewer_show_toast(Viewer& v, const std::wstring& msg) {
    v.toast = msg;
    v.toastShown = true;
    SetTimer(v.hwnd, TID_TOAST, 2600, nullptr);
    viewer_refresh(v);
}

void viewer_save_settings(Viewer& v) {
    if (v.scale > 0.0) v.set.lastZoom = v.scale;
    if (v.set.rememberDir && !v.filesDir.empty()) v.set.lastDir = v.filesDir;
    v.set.save();
}

int viewer_scale_pct(Viewer& v) {
    return (int)std::lround(v.scale * 100.0);
}

// ---------------------------------------------------------------------------
// 状态栏文本
// ---------------------------------------------------------------------------
static std::wstring trim_w(std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    return s;
}

static std::wstring status_left(Viewer& v) {
    if (!v.hint.empty()) return v.hint;
    if (v.loading) return tr(Sid::status_loading);
    if (v.doc.base.empty()) return tr(Sid::status_no_image);
    wchar_t buf[700];
    std::wstring name = file_name_of(v.doc.path);
    int n = (int)v.files.size();
    if (n <= 0) n = 1;
    swprintf(buf, 700, L"%s   [%d/%d]   %d×%d   %s   %s",
             name.c_str(), v.fileIndex + 1, n,
             v.doc.base.w, v.doc.base.h,
             v.doc.meta.formatName.c_str(),
             format_file_size(v.doc.fileSize).c_str());
    std::wstring s = buf;
    if (v.doc.meta.frames > 1) {
        swprintf(buf, 700, tr(Sid::status_frames), v.doc.meta.frames);
        s += buf;
    }
    return s;
}

static std::wstring status_right(Viewer& v) {
    if (v.doc.base.empty()) return std::wstring();
    wchar_t buf[128];
    swprintf(buf, 128, L"%d%%", viewer_scale_pct(v));
    std::wstring s = buf;
    if (v.slideshow) s += tr(Sid::status_slideshow);
    return s;
}

std::wstring viewer_status_text(Viewer& v) {
    std::wstring l = status_left(v), r = status_right(v);
    return r.empty() ? l : (l + L"   " + r);
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
static void paint_status(Viewer& v, HDC dc) {
    RECT r = v.rcStatus;
    if (r.bottom <= r.top) return;
    SelectObject(dc, v.fontUI);
    RECT t = r;
    t.left += dpi_px(v, 10);
    t.right -= dpi_px(v, 10);
    std::wstring left = status_left(v), right = status_right(v);
    if (!left.empty()) {
        SetTextColor(dc, argb_to_colorref(v.hint.empty() ? kStatusText : 0xFF7FD4FF));
        RECT tl = t;
        tl.right -= dpi_px(v, 130);
        DrawTextW(dc, left.c_str(), -1, &tl,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (!right.empty()) {
        SetTextColor(dc, argb_to_colorref(kStatusDim));
        RECT tr = t;
        DrawTextW(dc, right.c_str(), -1, &tr,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void paint_empty_hint(Viewer& v, HDC dc) {
    RECT r = v.rcView;
    SelectObject(dc, v.fontBig);
    SetTextColor(dc, RGB(150, 150, 150));
    RECT r1 = r;
    r1.top = (r.top + r.bottom) / 2 - dpi_px(v, 20);
    DrawTextW(dc, tr(Sid::empty_hint_line1), -1, &r1,
              DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, v.fontUI);
    SetTextColor(dc, RGB(110, 110, 110));
    RECT r2 = r;
    r2.top = r1.top + dpi_px(v, 30);
    DrawTextW(dc, tr(Sid::empty_hint_line2), -1, &r2,
              DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static std::vector<std::wstring> build_info_lines(Viewer& v) {
    std::vector<std::wstring> L;
    if (v.doc.base.empty()) return L;
    wchar_t buf[1024];
    L.push_back(file_name_of(v.doc.path));
    L.push_back(tr(Sid::info_path) + v.doc.path);
    swprintf(buf, 1024, tr(Sid::info_size),
             v.doc.base.w, v.doc.base.h, v.doc.meta.formatName.c_str(),
             format_file_size(v.doc.fileSize).c_str());
    L.push_back(buf);
    {
        int vw = v.rcView.right - v.rcView.left;
        int vh = v.rcView.bottom - v.rcView.top;
        swprintf(buf, 1024, tr(Sid::info_zoom),
                 viewer_scale_pct(v), vw, vh);
        L.push_back(buf);
    }
    if (v.doc.meta.frames > 1) {
        swprintf(buf, 1024, tr(Sid::info_gif_frames), v.doc.meta.frames);
        L.push_back(buf);
    }
    const ExifInfo& ex = v.doc.meta.exif;
    if (ex.valid) {
        if (!ex.make.empty() || !ex.model.empty()) {
            L.push_back(tr(Sid::info_camera) + trim_w(ex.make) + L" " + trim_w(ex.model));
        }
        if (!ex.dateTime.empty()) L.push_back(tr(Sid::info_date) + ex.dateTime);
        std::wstring exp;
        if (!ex.exposure.empty()) exp += tr(Sid::info_shutter) + ex.exposure + L"    ";
        if (!ex.fnumber.empty())  exp += tr(Sid::info_aperture) + ex.fnumber + L"    ";
        if (!ex.iso.empty())      exp += ex.iso + L"    ";
        if (!ex.focal.empty())    exp += tr(Sid::info_focal) + ex.focal;
        if (!exp.empty()) L.push_back(exp);
        if (ex.orientation >= 2 && ex.orientation <= 8) {
            swprintf(buf, 1024, tr(Sid::info_exif_orientation), ex.orientation);
            L.push_back(buf);
        }
    }
    return L;
}

static void paint_info(Viewer& v, HDC dc) {
    darken_rect(v.dibBits, v.dibW, v.rcView, 62);
    RECT clip{ 0, 0, v.dibW, v.dibH };
    RECT box = v.rcView;
    InflateRect(&box, -dpi_px(v, 16), -dpi_px(v, 16));
    fill_rect32(v.dibBits, v.dibW, clip, box, 0xC8181818);

    auto lines = build_info_lines(v);
    int x = box.left + dpi_px(v, 20);
    int y = box.top + dpi_px(v, 16);
    int lineH = dpi_px(v, 26);
    SelectObject(dc, v.fontBig);
    for (size_t i = 0; i < lines.size(); i++) {
        SetTextColor(dc, i == 0 ? RGB(255, 255, 255) : RGB(216, 216, 216));
        RECT r{ x, y, box.right - dpi_px(v, 12), y + lineH };
        DrawTextW(dc, lines[i].c_str(), -1, &r,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);
        y += lineH;
    }
    SelectObject(dc, v.fontUI);
    SetTextColor(dc, RGB(150, 150, 150));
    RECT r = box;
    r.bottom -= dpi_px(v, 8);
    DrawTextW(dc, tr(Sid::info_close_hint), -1, &r,
              DT_BOTTOM | DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    (void)lines;
}

static void paint_loading(Viewer& v, HDC dc) {
    RECT clip{ 0, 0, v.dibW, v.dibH };
    RECT r = v.rcView;
    int bw = dpi_px(v, 200), bh = dpi_px(v, 56);
    RECT box{ (r.left + r.right) / 2 - bw / 2, (r.top + r.bottom) / 2 - bh / 2,
              (r.left + r.right) / 2 + bw / 2, (r.top + r.bottom) / 2 + bh / 2 };
    fill_rect32(v.dibBits, v.dibW, clip, box, 0xE0282828);
    SelectObject(dc, v.fontUI);
    SetTextColor(dc, RGB(230, 230, 230));
    DrawTextW(dc, tr(Sid::status_loading), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void paint_toast(Viewer& v, HDC dc) {
    if (v.toast.empty()) return;
    SelectObject(dc, v.fontUI);
    SIZE sz{};
    GetTextExtentPoint32W(dc, v.toast.c_str(), (int)v.toast.size(), &sz);
    RECT clip{ 0, 0, v.dibW, v.dibH };
    int pad = dpi_px(v, 14);
    int bw = sz.cx + pad * 2;
    int bh = sz.cy + pad;
    int maxw = (v.rcView.right - v.rcView.left) - dpi_px(v, 40);
    if (bw > maxw) bw = maxw;
    int cx = (v.rcView.left + v.rcView.right) / 2;
    int bx = cx - bw / 2;
    int by = v.rcView.bottom - bh - dpi_px(v, 36);
    RECT box{ bx, by, bx + bw, by + bh };
    fill_rect32(v.dibBits, v.dibW, clip, box, 0xEC202020);
    RECT acc{ bx, by, bx + dpi_px(v, 3), by + bh };
    fill_rect32(v.dibBits, v.dibW, clip, acc, kIconAccent);
    SetTextColor(dc, RGB(235, 235, 235));
    DrawTextW(dc, v.toast.c_str(), -1, &box,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void viewer_paint(Viewer& v) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(v.hwnd, &ps);
    RECT rc;
    GetClientRect(v.hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) { EndPaint(v.hwnd, &ps); return; }

    ensure_backbuffer(v, W, H);
    if (!v.dibBits) { EndPaint(v.hwnd, &ps); return; }
    u32* base = v.dibBits;
    RECT clip{ 0, 0, W, H };

    // 1) 图像区域
    int vw = v.rcView.right - v.rcView.left;
    int vh = v.rcView.bottom - v.rcView.top;
    if (vw > 0 && vh > 0) {
        ViewDraw vd;
        vd.scale = v.scale;
        vd.cx = v.cx;
        vd.cy = v.cy;
        vd.dstW = vw;
        vd.dstH = vh;
        vd.dstStride = W;
        vd.bg = view_bg_color(v.set.bg);
        vd.checker = v.doc.hasAlpha;
        vd.smooth = v.set.smoothZoom != 0;
        vd.shiftX = v.shiftX;
        vd.shiftY = v.shiftY;
        render_image(base + (size_t)v.rcView.top * W + v.rcView.left, vd, v.doc);
    }

    // 2) 面板像素层
    if (v.mode != UiMode::View) panels_paint_back(v);

    // 3) 工具栏 / 状态栏底色
    if (v.toolbarH > 0) {
        fill_rect32(base, W, clip, v.rcToolbar, kToolbarBg);
        RECT line{ 0, v.rcToolbar.bottom - 1, W, v.rcToolbar.bottom };
        fill_rect32(base, W, clip, line, kToolbarLine);
    }
    if (!v.fullscreen) {
        fill_rect32(base, W, clip, v.rcStatus, kStatusBg);
        RECT line{ 0, v.rcStatus.top, W, v.rcStatus.top + 1 };
        fill_rect32(base, W, clip, line, kToolbarLine);
    }

    // 4) GDI 文本 / 图标层
    HDC dc = v.memDC;
    SetBkMode(dc, TRANSPARENT);
    if (v.toolbarH > 0) toolbar_paint(v, dc);
    if (!v.fullscreen) paint_status(v, dc);
    if (v.mode != UiMode::View) panels_paint_text(v, dc);
    if (v.doc.base.empty() && !v.loading && v.mode == UiMode::View) paint_empty_hint(v, dc);
    if (v.showInfo) paint_info(v, dc);
    if (v.loading) paint_loading(v, dc);
    if (v.toastShown) paint_toast(v, dc);

    BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    EndPaint(v.hwnd, &ps);
    v.viewDirty = false;
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------
static void track_leave(HWND hwnd) {
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);
}

static std::wstring panel_start_dir(Viewer& v) {
    if (!v.set.lastDir.empty() && path_is_dir(v.set.lastDir)) return v.set.lastDir;
    if (!v.filesDir.empty() && path_is_dir(v.filesDir)) return v.filesDir;
    return exe_dir();
}

static void do_button(Viewer& v, int id) {
    switch (id) {
    case BTN_OPEN: filepanel_load(v, panel_start_dir(v)); break;
    case BTN_PREV: viewer_next(v, -1, true); break;
    case BTN_NEXT: viewer_next(v, +1, true); break;
    case BTN_ZOOM_OUT: { POINT c = view_center_client(v); viewer_zoom_at(v, 1.0 / 1.25, c); break; }
    case BTN_ZOOM_IN:  { POINT c = view_center_client(v); viewer_zoom_at(v, 1.25, c); break; }
    case BTN_FIT: viewer_fit(v); break;
    case BTN_ONE: viewer_actual(v); break;
    case BTN_ROT_L: viewer_rotate(v, -1); break;
    case BTN_ROT_R: viewer_rotate(v, +1); break;
    case BTN_SLIDE: viewer_set_slideshow(v, !v.slideshow); break;
    case BTN_FULL: viewer_toggle_fullscreen(v); break;
    case BTN_SETTINGS: v.mode = UiMode::Settings; viewer_refresh(v); break;
    default: break;
    }
}

static void on_mouse_move(Viewer& v, POINT pt) {
    track_leave(v.hwnd);
    v.mouse = pt;
    if (v.dragging) {
        if (v.dragPan) {
            double dx = pt.x - v.dragStartPt.x;
            double dy = pt.y - v.dragStartPt.y;
            v.cx = v.dragStartCx - dx / v.scale;
            v.cy = v.dragStartCy - dy / v.scale;
            clamp_view(v);
            if (std::abs(dx) + std::abs(dy) > dpi_px(v, 5)) v.dragMoved = true;
            viewer_refresh(v);
        } else {
            double dx = pt.x - v.dragStartPt.x;
            double dy = pt.y - v.dragStartPt.y;
            v.dragRawX = dx;
            double w = (double)(v.rcView.right - v.rcView.left);
            v.shiftX = clampd(dx * 0.55, -w * 0.42, w * 0.42);
            v.shiftY = 0;
            if (std::abs(dx) + std::abs(dy) > dpi_px(v, 5)) v.dragMoved = true;
            viewer_refresh(v);
        }
        return;
    }
    int hot = viewer_hit_toolbar(v, pt);
    if (hot != v.hotBtn) {
        v.hotBtn = hot;
        v.hint = hot >= 0 ? toolbar_tip(hot) : std::wstring();
        InvalidateRect(v.hwnd, &v.rcStatus, FALSE);
        InvalidateRect(v.hwnd, &v.rcToolbar, FALSE);
    }
}

static void on_lbutton_down(Viewer& v, POINT pt) {
    SetFocus(v.hwnd);
    if (v.toastShown) {
        KillTimer(v.hwnd, TID_TOAST);
        v.toastShown = false;
        viewer_refresh(v);
    }
    if (v.showInfo) { v.showInfo = false; viewer_refresh(v); return; }

    // 工具栏优先（面板打开时点击工具栏会先关闭面板）
    int btn = viewer_hit_toolbar(v, pt);
    if (btn >= 0) {
        if (v.mode != UiMode::View) { v.mode = UiMode::View; viewer_refresh(v); return; }
        v.downBtn = btn;
        InvalidateRect(v.hwnd, &v.rcToolbar, FALSE);
        return;
    }
    if (v.mode == UiMode::FilePanel) { filepanel_hit(v, pt, false); return; }
    if (v.mode == UiMode::Settings) { settingspanel_hit(v, pt); return; }
    if (PtInRect(&v.rcView, pt)) {
        v.dragging = true;
        v.dragMoved = false;
        v.dragStartPt = pt;
        v.dragStartCx = v.cx;
        v.dragStartCy = v.cy;
        v.dragRawX = 0;
        v.dragPan = v.scale > fit_scale(v) * 1.02;
        v.shiftX = v.shiftY = 0;
        SetCapture(v.hwnd);
    }
}

static void on_lbutton_up(Viewer& v, POINT pt) {
    if (v.downBtn >= 0) {
        int btn = v.downBtn;
        v.downBtn = -1;
        if (viewer_hit_toolbar(v, pt) == btn) do_button(v, btn);
        InvalidateRect(v.hwnd, &v.rcToolbar, FALSE);
        return;
    }
    if (v.dragging) {
        v.dragging = false;
        ReleaseCapture();
        if (v.dragPan) {
            clamp_view(v);
        } else {
            double dx = v.dragRawX;
            double threshold = (v.rcView.right - v.rcView.left) * 0.16;
            v.shiftX = v.shiftY = 0;
            if (v.dragMoved && std::abs(dx) > threshold)
                viewer_next(v, dx < 0 ? 1 : -1, true);
        }
        viewer_refresh(v);
    }
}

static void on_dblclick(Viewer& v, POINT pt) {
    if (v.mode != UiMode::View || !PtInRect(&v.rcView, pt)) return;
    double fs = fit_scale(v);
    if (v.scale > fs * 1.01) viewer_fit(v);
    else { v.scale = 1.0; clamp_view(v); viewer_refresh(v); }
}

static bool on_key_down(Viewer& v, WPARAM key) {
    if (v.mode == UiMode::FilePanel) { filepanel_key(v, key); return true; }
    if (v.mode == UiMode::Settings) { settingspanel_key(v, key); return true; }

    switch (key) {
    case VK_ESCAPE:
        if (v.showInfo) { v.showInfo = false; viewer_refresh(v); }
        else if (v.fullscreen) viewer_toggle_fullscreen(v);
        else if (v.slideshow) viewer_set_slideshow(v, false);
        return true;
    case VK_LEFT: case VK_PRIOR: viewer_next(v, -1, true); return true;
    case VK_RIGHT: case VK_NEXT: viewer_next(v, +1, true); return true;
    case VK_HOME:
        if (!v.files.empty()) try_load(v, 0);
        return true;
    case VK_END:
        if (!v.files.empty()) try_load(v, (int)v.files.size() - 1);
        return true;
    case VK_ADD: case VK_OEM_PLUS: { POINT c = view_center_client(v); viewer_zoom_at(v, 1.25, c); return true; }
    case VK_SUBTRACT: case VK_OEM_MINUS: { POINT c = view_center_client(v); viewer_zoom_at(v, 1.0 / 1.25, c); return true; }
    case '0': case 'F': viewer_fit(v); return true;
    case '1': viewer_actual(v); return true;
    case 'R': viewer_rotate(v, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : +1); return true;
    case 'I': v.showInfo = !v.showInfo; viewer_refresh(v); return true;
    case 'B': v.set.bg = (v.set.bg + 1) % 4; viewer_refresh(v); return true;
    case 'O': filepanel_load(v, panel_start_dir(v)); return true;
    case 'S': case VK_SPACE: viewer_set_slideshow(v, !v.slideshow); return true;
    case VK_F11: viewer_toggle_fullscreen(v); return true;
    case VK_TAB:
        v.mode = (v.mode == UiMode::Settings) ? UiMode::View : UiMode::Settings;
        viewer_refresh(v);
        return true;
    default: break;
    }
    return false;
}

static void handle_gesture(Viewer& v, const GESTUREINFO& gi) {
    POINT gpt{ gi.ptsLocation.x, gi.ptsLocation.y };   // POINTS(短整型) → POINT
    if (v.mode != UiMode::View) {
        v.gestActive = false;
        v.gestZooming = false;
        v.shiftX = v.shiftY = 0;
        return;
    }
    switch (gi.dwID) {
    case GID_BEGIN:
        v.gestActive = true;
        v.gestZooming = false;
        v.gestRawX = v.gestRawY = 0;
        v.gestFlick = false;
        v.gestFlickDir = 0;
        v.gestStartDist = 0;
        v.gestLast = gpt;
        v.gestStart = gpt;
        break;

    case GID_ZOOM: {
        double dist = (double)gi.ullArguments;
        POINT pt = gpt;
        ScreenToClient(v.hwnd, &pt);
        if (v.gestStartDist <= 0 && dist > 1) {
            v.gestStartDist = dist;
            v.gestStartScale = v.scale;
            v.gestAnchorImgX = v.cx + (pt.x - view_cx(v)) / v.scale;
            v.gestAnchorImgY = v.cy + (pt.y - view_cy(v)) / v.scale;
            v.gestZooming = true;
        } else if (v.gestStartDist > 1) {
            double ratio = dist / v.gestStartDist;
            double ns = clampd(v.gestStartScale * ratio, 1.0 / 64.0, 32.0);
            v.scale = ns;
            v.cx = v.gestAnchorImgX - (pt.x - view_cx(v)) / ns;
            v.cy = v.gestAnchorImgY - (pt.y - view_cy(v)) / ns;
            clamp_view(v);
            v.gestZooming = true;
            viewer_refresh(v);
        }
        break;
    }

    case GID_PAN: {
        if (gi.dwFlags & GF_INERTIA) {
            v.gestFlick = true;
            short vx = (short)LOWORD(gi.ullArguments);
            v.gestFlickDir = vx >= 0 ? 1 : -1;      // 正=向右甩 → 上一张
            break;
        }
        double dx = gpt.x - v.gestLast.x;
        double dy = gpt.y - v.gestLast.y;
        v.gestLast = gpt;
        if (v.gestZooming) break;
        double fs = fit_scale(v);
        if (v.scale > fs * 1.02) {
            v.cx -= dx / v.scale;
            v.cy -= dy / v.scale;
            clamp_view(v);
        } else {
            v.gestRawX += dx;
            v.gestRawY += dy;
            double w = (double)(v.rcView.right - v.rcView.left);
            v.shiftX = clampd(v.gestRawX * 0.55, -w * 0.42, w * 0.42);
            v.shiftY = 0;
        }
        viewer_refresh(v);
        break;
    }

    case GID_END: {
        if (v.gestActive && !v.gestZooming) {
            double w = (double)(v.rcView.right - v.rcView.left);
            double thr = w * 0.16;
            bool horiz = std::abs(v.gestRawX) > std::abs(v.gestRawY) * 1.1;
            if (horiz && (std::abs(v.gestRawX) > thr || v.gestFlick)) {
                int dir = 0;
                if (std::abs(v.gestRawX) > thr) dir = v.gestRawX < 0 ? 1 : -1;
                else dir = (v.gestFlickDir > 0) ? -1 : 1;
                viewer_next(v, dir, true);
            }
        }
        v.gestActive = false;
        v.gestZooming = false;
        v.gestStartDist = 0;
        v.shiftX = v.shiftY = 0;
        viewer_refresh(v);
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 消息
// ---------------------------------------------------------------------------
bool viewer_on_message(Viewer& v, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    out = 0;
    switch (msg) {
    case WM_SIZE:
        viewer_layout(v);
        viewer_refresh(v);
        return true;

    case WM_DPICHANGED: {
        UINT nd = LOWORD(wp);
        viewer_dpi_changed(v, nd);
        RECT* pr = (RECT*)lp;
        if (pr) {
            SetWindowPos(v.hwnd, nullptr, pr->left, pr->top,
                         pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return true;
    }

    case WM_PAINT:
        viewer_paint(v);
        return true;

    case WM_ERASEBKGND:
        out = 1;
        return true;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        if (mmi) {
            mmi->ptMinTrackSize.x = dpi_px(v, 460);
            mmi->ptMinTrackSize.y = dpi_px(v, 320);
        }
        return true;
    }

    case WM_TIMER:
        if (wp == TID_SLIDE) {
            if (v.slideshow) viewer_next(v, +1, false);
        } else if (wp == TID_TOAST) {
            KillTimer(v.hwnd, TID_TOAST);
            v.toastShown = false;
            viewer_refresh(v);
        }
        return true;

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        on_mouse_move(v, pt);
        return true;
    }

    case WM_MOUSELEAVE:
        if (v.hotBtn != -1) {
            v.hotBtn = -1;
            v.hint.clear();
            InvalidateRect(v.hwnd, &v.rcStatus, FALSE);
            InvalidateRect(v.hwnd, &v.rcToolbar, FALSE);
        }
        return true;

    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        on_lbutton_down(v, pt);
        return true;
    }

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        on_lbutton_up(v, pt);
        return true;
    }

    case WM_LBUTTONDBLCLK: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        on_dblclick(v, pt);
        return true;
    }

    case WM_MBUTTONDOWN:
        if (v.mode == UiMode::View && PtInRect(&v.rcView, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) })) {
            v.dragging = true;
            v.dragPan = true;
            v.dragMoved = false;
            v.dragStartPt = POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            v.dragStartCx = v.cx;
            v.dragStartCy = v.cy;
            SetCapture(v.hwnd);
        }
        return true;

    case WM_MBUTTONUP:
        if (v.dragging) {
            v.dragging = false;
            ReleaseCapture();
            clamp_view(v);
            viewer_refresh(v);
        }
        return true;

    case WM_RBUTTONUP:
        if (v.mode != UiMode::View) { v.mode = UiMode::View; viewer_refresh(v); }
        else if (v.slideshow) viewer_set_slideshow(v, false);
        return true;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (v.mode == UiMode::FilePanel) {
            int lines = (delta / WHEEL_DELTA) * 3;
            int n = (int)v.fpItems.size();
            int rows = (v.fpListBottom - v.fpListTop) / (v.fpRowH ? v.fpRowH : 1);
            if (rows < 1) rows = 1;
            int maxTop = std::max(0, n - rows);
            v.fpTop = clampi(v.fpTop - lines, 0, maxTop);
            viewer_refresh(v);
            return true;
        }
        if (v.mode != UiMode::View) return true;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(v.hwnd, &pt);
        double f = std::pow(1.25, delta / 120.0);
        viewer_zoom_at(v, f, pt);
        return true;
    }

    case WM_KEYDOWN:
        if (on_key_down(v, wp)) return true;
        if (wp == VK_APPS) return true;
        return false;

    case WM_GESTURE: {
        GESTUREINFO gi{};
        gi.cbSize = sizeof(gi);
        if (GetGestureInfo((HGESTUREINFO)lp, &gi)) {
            handle_gesture(v, gi);
            CloseGestureInfoHandle((HGESTUREINFO)lp);
        }
        return true;
    }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return true;
        }
        return false;

    case WM_CAPTURECHANGED:
        if (v.dragging) {
            v.dragging = false;
            v.shiftX = v.shiftY = 0;
            viewer_refresh(v);
        }
        return false;

    default:
        return false;
    }
}
