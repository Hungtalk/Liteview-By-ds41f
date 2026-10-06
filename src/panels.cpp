// ============================================================================
//  panels.cpp : 文件浏览面板 + 设置面板
// ============================================================================
#include "viewer.h"
#include "assoc.h"
#include "codec.h"
#include "i18n.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

// ---------------------------------------------------------------------------
// 公共
// ---------------------------------------------------------------------------
std::wstring slide_interval_text(int sec) {
    wchar_t buf[64];
    swprintf(buf, 64, tr(Sid::settings_seconds), sec);
    return buf;
}

static const int kSlideSteps[] = { 2, 3, 5, 8, 10, 15, 30 };
static const int kSlideStepCount = (int)(sizeof(kSlideSteps) / sizeof(kSlideSteps[0]));

static const int kSettingsRowCount = 14;   // 与 build_settings_rows() 的行数保持一致

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

// ---------------------------------------------------------------------------
// 文件面板
// ---------------------------------------------------------------------------
static void fp_ensure_visible(Viewer& v) {
    int rows = (v.fpListBottom - v.fpListTop) / (v.fpRowH ? v.fpRowH : 1);
    if (rows < 1) rows = 1;
    if (v.fpSel < v.fpTop) v.fpTop = v.fpSel;
    if (v.fpSel >= v.fpTop + rows) v.fpTop = v.fpSel - rows + 1;
    if (v.fpTop < 0) v.fpTop = 0;
}

void filepanel_load(Viewer& v, const std::wstring& dir) {
    v.fpDir = dir;
    v.fpSel = 0;
    v.fpTop = 0;
    v.fpItems.clear();
    if (dir.empty()) {
        for (const auto& d : v.drives)
            v.fpItems.push_back(DirEntry{ d, true });
    } else {
        std::vector<DirEntry> es;
        if (!list_dir(dir, es)) {
            viewer_show_toast(v, tr(Sid::toast_cannot_open_dir));
            return;
        }
        for (const auto& e : es) {
            if (e.isDir) {
                if (!e.name.empty() && e.name[0] == L'$') continue;   // 跳过 $RECYCLE.BIN 等
                v.fpItems.push_back(e);
            } else if (is_supported_image_ext(ext_of(e.name))) {
                v.fpItems.push_back(e);
            }
        }
    }
    v.mode = UiMode::FilePanel;
    viewer_refresh(v);
}

static void filepanel_up(Viewer& v) {
    if (v.fpDir.empty()) return;
    std::wstring up;
    if (v.fpDir.size() <= 3 && v.fpDir.size() >= 2 && v.fpDir[1] == L':') {
        up = L"";                                        // 盘根 → 设备列表
    } else {
        up = dir_of(v.fpDir);
        if (up.size() == 2 && up[1] == L':') up += L'\\';
    }
    filepanel_load(v, up);
}

void filepanel_activate(Viewer& v) {
    if (v.fpSel < 0 || v.fpSel >= (int)v.fpItems.size()) return;
    const DirEntry& e = v.fpItems[v.fpSel];
    if (e.isDir) {
        std::wstring dir = v.fpDir.empty() ? e.name : join_path(v.fpDir, e.name);
        filepanel_load(v, dir);
    } else {
        std::wstring full = join_path(v.fpDir, e.name);
        viewer_open_file(v, full, false);
    }
}

void filepanel_hit(Viewer& v, POINT pt, bool dbl) {
    if (!PtInRect(&v.fpRect, pt)) {
        v.mode = UiMode::View;
        viewer_refresh(v);
        return;
    }
    if (pt.y >= v.fpListTop && pt.y < v.fpListBottom && v.fpRowH > 0) {
        int idx = v.fpTop + (pt.y - v.fpListTop) / v.fpRowH;
        if (idx >= 0 && idx < (int)v.fpItems.size()) {
            if (dbl) { v.fpSel = idx; filepanel_activate(v); return; }
            if (v.fpSel == idx) { filepanel_activate(v); return; }
            v.fpSel = idx;
            viewer_refresh(v);
        }
    } else if (pt.y < v.fpListTop) {
        filepanel_up(v);                                  // 点击标题栏 → 上一层
    }
}

void filepanel_key(Viewer& v, WPARAM key) {
    int n = (int)v.fpItems.size();
    switch (key) {
    case VK_ESCAPE:
        v.mode = UiMode::View;
        viewer_refresh(v);
        return;
    case VK_UP:    if (v.fpSel > 0) v.fpSel--; break;
    case VK_DOWN:  if (v.fpSel < n - 1) v.fpSel++; break;
    case VK_PRIOR: v.fpSel = std::max(0, v.fpSel - 10); break;
    case VK_NEXT:  v.fpSel = std::min(n - 1, v.fpSel + 10); break;
    case VK_HOME:  v.fpSel = 0; break;
    case VK_END:   v.fpSel = n ? n - 1 : 0; break;
    case VK_RETURN: filepanel_activate(v); return;
    case VK_BACK: case VK_LEFT: filepanel_up(v); return;
    case VK_RIGHT:
        if (v.fpSel >= 0 && v.fpSel < n && v.fpItems[v.fpSel].isDir) filepanel_activate(v);
        return;
    default:
        return;
    }
    if (n > 0) fp_ensure_visible(v);
    viewer_refresh(v);
}

// ---------------------------------------------------------------------------
// 设置面板
// ---------------------------------------------------------------------------
// 语言行显示：auto → “自动（跟随系统） · 当前语言名”；否则显示当前语言名
static std::wstring settings_language_text(const Viewer& v) {
    std::wstring cur = i18n_current_name();
    if (v.set.language.empty() || v.set.language == L"auto")
        return std::wstring(tr(Sid::settings_language_auto)) + L" · " + cur;
    return cur;
}

// 语言切换后同步窗口标题（未打开图片时使用本地化的应用名）
static void update_window_title(Viewer& v) {
    std::wstring title = v.doc.base.empty()
        ? std::wstring(tr(Sid::app_title))
        : (file_name_of(v.doc.path) + L" - LiteView");
    SetWindowTextW(v.hwnd, title.c_str());
}

static std::vector<std::pair<std::wstring, std::wstring>> build_settings_rows(Viewer& v) {
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    auto yn = [](bool b) { return b ? tr(Sid::settings_on) : tr(Sid::settings_off); };
    static const Sid kBgIds[4] = { Sid::settings_bg_black, Sid::settings_bg_dark,
                                   Sid::settings_bg_light, Sid::settings_bg_white };
    static const Sid kFitIds[3] = { Sid::settings_fit, Sid::settings_actual, Sid::settings_fit_last };
    rows.push_back({ tr(Sid::settings_bg), tr(kBgIds[clampi(v.set.bg, 0, 3)]) });
    rows.push_back({ tr(Sid::settings_fit_on_open), tr(kFitIds[clampi(v.set.fitOnOpen, 0, 2)]) });
    rows.push_back({ tr(Sid::settings_interp),
                     v.set.smoothZoom ? tr(Sid::settings_interp_smooth) : tr(Sid::settings_interp_sharp) });
    rows.push_back({ tr(Sid::settings_exif_rotate), yn(v.set.exifRotate != 0) });
    rows.push_back({ tr(Sid::settings_slide_interval), slide_interval_text(v.set.slideInterval) });
    rows.push_back({ tr(Sid::settings_slide_loop), yn(v.set.slideLoop != 0) });
    rows.push_back({ tr(Sid::settings_remember_dir), yn(v.set.rememberDir != 0) });
    rows.push_back({ tr(Sid::settings_maximized), yn(v.set.startMaximized != 0) });
    rows.push_back({ tr(Sid::settings_topmost), yn(v.set.topmost != 0) });
    rows.push_back({ tr(Sid::settings_loop_files), yn(v.set.loopFiles != 0) });
    rows.push_back({ tr(Sid::settings_language), settings_language_text(v) });
    rows.push_back({ tr(Sid::settings_assoc),
                     assoc::is_registered(exe_path()) ? tr(Sid::settings_assoc_on) : tr(Sid::settings_assoc_off) });
    rows.push_back({ tr(Sid::settings_reset), L"" });
    rows.push_back({ tr(Sid::settings_close), L"" });
    return rows;
}

static void settings_action(Viewer& v, int id) {
    switch (id) {
    case 0: v.set.bg = (v.set.bg + 1) % 4; break;
    case 1: v.set.fitOnOpen = (v.set.fitOnOpen + 1) % 3; break;
    case 2: v.set.smoothZoom = v.set.smoothZoom ? 0 : 1; break;
    case 3: v.set.exifRotate = v.set.exifRotate ? 0 : 1; break;
    case 4: {
        int idx = 0;
        for (int i = 0; i < kSlideStepCount; i++) if (kSlideSteps[i] == v.set.slideInterval) idx = i;
        idx = (idx + 1) % kSlideStepCount;
        v.set.slideInterval = kSlideSteps[idx];
        if (v.slideshow) viewer_set_slideshow(v, false), viewer_set_slideshow(v, true);
        break; }
    case 5: v.set.slideLoop = v.set.slideLoop ? 0 : 1; break;
    case 6: v.set.rememberDir = v.set.rememberDir ? 0 : 1; break;
    case 7: v.set.startMaximized = v.set.startMaximized ? 0 : 1; break;
    case 8:
        v.set.topmost = v.set.topmost ? 0 : 1;
        SetWindowPos(v.hwnd, v.set.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        break;
    case 9: v.set.loopFiles = v.set.loopFiles ? 0 : 1; break;
    case 10: {
        // 语言：auto → 各语言包 → 英文，循环切换
        std::vector<std::wstring> cycle;
        cycle.push_back(L"auto");
        bool hasEn = false;
        for (const auto& p : i18n_available_packs()) {
            cycle.push_back(p.code);
            if (to_lower(p.code) == L"en") hasEn = true;
        }
        if (!hasEn) cycle.push_back(L"en");
        size_t cur = 0;
        for (size_t i = 0; i < cycle.size(); i++)
            if (to_lower(cycle[i]) == to_lower(v.set.language)) { cur = i; break; }
        v.set.language = cycle[(cur + 1) % cycle.size()];
        if (v.set.language == L"auto") i18n_init(L"auto");
        else i18n_select(v.set.language);
        update_window_title(v);
        break; }
    case 11: {
        std::wstring err;
        if (assoc::is_registered(exe_path())) {
            assoc::unregister_assoc(err);
            viewer_show_toast(v, tr(Sid::toast_assoc_off));
        } else {
            if (assoc::register_assoc(exe_path(), err) && assoc::set_default_assoc(exe_path(), err))
                viewer_show_toast(v, tr(Sid::toast_assoc_on));
            else
                viewer_show_toast(v, err.empty() ? tr(Sid::toast_assoc_fail) : err);
        }
        assoc::notify_shell();
        break; }
    case 12:
        v.set = Settings();
        i18n_init(L"auto");
        SetWindowPos(v.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        viewer_save_settings(v);
        update_window_title(v);
        viewer_show_toast(v, tr(Sid::toast_defaults_restored));
        break;
    case 13:
        v.mode = UiMode::View;
        break;
    default:
        break;
    }
    v.set.save();
    viewer_refresh(v);
}

void settingspanel_hit(Viewer& v, POINT pt) {
    if (!PtInRect(&v.spRect, pt)) {
        v.mode = UiMode::View;
        viewer_refresh(v);
        return;
    }
    for (const auto& row : v.spRows) {
        if (PtInRect(&row.first, pt)) {
            settings_action(v, row.second);
            return;
        }
    }
}

void settingspanel_key(Viewer& v, WPARAM key) {
    if (key == VK_ESCAPE) {
        v.mode = UiMode::View;
        viewer_refresh(v);
    }
}

// ---------------------------------------------------------------------------
// 绘制：像素层
// ---------------------------------------------------------------------------
void panels_paint_back(Viewer& v) {
    RECT clip{ 0, 0, v.dibW, v.dibH };
    int vw = v.rcView.right - v.rcView.left;
    int vh = v.rcView.bottom - v.rcView.top;

    if (v.mode == UiMode::FilePanel) {
        int pw = (int)(vw * 0.38);
        pw = std::max(pw, dpi_px(v, 320));
        pw = std::min(pw, dpi_px(v, 560));
        if (pw > vw - dpi_px(v, 60)) pw = std::max(dpi_px(v, 200), vw - dpi_px(v, 60));
        v.fpRect = { v.rcView.left, v.rcView.top, v.rcView.left + pw, v.rcView.bottom };
        darken_rect(v.dibBits, v.dibW, v.rcView, 42);
        fill_rect32(v.dibBits, v.dibW, clip, v.fpRect, 0xFA212121);
        v.fpRowH = dpi_px(v, 26);
        v.fpListTop = v.fpRect.top + dpi_px(v, 68);
        v.fpListBottom = v.fpRect.bottom - dpi_px(v, 36);
        fp_ensure_visible(v);
    } else if (v.mode == UiMode::Settings) {
        int pw = std::min(dpi_px(v, 560), vw - dpi_px(v, 36));
        int ph = std::min(dpi_px(v, 600), vh - dpi_px(v, 36));
        int x0 = v.rcView.left + (vw - pw) / 2;
        int y0 = v.rcView.top + (vh - ph) / 2;
        v.spRect = { x0, y0, x0 + pw, y0 + ph };
        darken_rect(v.dibBits, v.dibW, v.rcView, 55);
        fill_rect32(v.dibBits, v.dibW, clip, v.spRect, 0xFA212121);

        v.spRows.clear();
        int rh = dpi_px(v, 33);
        int y = v.spRect.top + dpi_px(v, 54);
        for (int i = 0; i < kSettingsRowCount; i++) {
            if (y + rh > v.spRect.bottom - dpi_px(v, 30)) break;
            RECT r{ v.spRect.left + dpi_px(v, 8), y, v.spRect.right - dpi_px(v, 8), y + rh };
            v.spRows.push_back(std::make_pair(r, i));
            y += rh;
        }
    }
}

// ---------------------------------------------------------------------------
// 绘制：GDI 层
// ---------------------------------------------------------------------------
static void draw_folder_glyph(HDC dc, int x, int y, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    MoveToEx(dc, x, y + 9, nullptr);
    LineTo(dc, x, y + 2);
    LineTo(dc, x + 4, y + 2);
    LineTo(dc, x + 5, y + 4);
    LineTo(dc, x + 10, y + 4);
    LineTo(dc, x + 10, y + 9);
    LineTo(dc, x, y + 9);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void draw_file_glyph(HDC dc, int x, int y, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y + 2, x + 9, y + 10);
    MoveToEx(dc, x + 2, y + 4, nullptr);
    LineTo(dc, x + 6, y + 4);
    MoveToEx(dc, x + 2, y + 6, nullptr);
    LineTo(dc, x + 6, y + 6);
    MoveToEx(dc, x + 2, y + 8, nullptr);
    LineTo(dc, x + 6, y + 8);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void paint_file_panel(Viewer& v, HDC dc) {
    const int pad = dpi_px(v, 12);
    // 标题
    SelectObject(dc, v.fontBold);
    SetTextColor(dc, RGB(240, 240, 240));
    RECT r{ v.fpRect.left + pad, v.fpRect.top + dpi_px(v, 8),
            v.fpRect.right - pad, v.fpRect.top + dpi_px(v, 32) };
    DrawTextW(dc, v.fpDir.empty() ? tr(Sid::panel_this_pc) : tr(Sid::panel_open_title), -1, &r,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    // 路径
    SelectObject(dc, v.fontUI);
    SetTextColor(dc, RGB(150, 170, 200));
    std::wstring path = v.fpDir.empty() ? std::wstring(tr(Sid::panel_back_hint)) : v.fpDir;
    RECT rp{ v.fpRect.left + pad, v.fpRect.top + dpi_px(v, 32),
             v.fpRect.right - pad, v.fpRect.top + dpi_px(v, 52) };
    DrawTextW(dc, path.c_str(), -1, &rp,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 列表
    int n = (int)v.fpItems.size();
    if (n == 0) {
        SetTextColor(dc, RGB(130, 130, 130));
        RECT re{ v.fpRect.left + pad, v.fpListTop, v.fpRect.right - pad, v.fpListTop + v.fpRowH };
        DrawTextW(dc, tr(Sid::panel_no_files), -1, &re, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }
    int visRows = (v.fpListBottom - v.fpListTop) / v.fpRowH;
    for (int i = v.fpTop; i < n && i < v.fpTop + visRows + 1; i++) {
        int y = v.fpListTop + (i - v.fpTop) * v.fpRowH;
        if (y + v.fpRowH > v.fpListBottom) break;
        RECT row{ v.fpRect.left + dpi_px(v, 4), y, v.fpRect.right - dpi_px(v, 4), y + v.fpRowH };
        if (i == v.fpSel) {
            HBRUSH br = CreateSolidBrush(RGB(0x09, 0x47, 0x71));
            FillRect(dc, &row, br);
            DeleteObject(br);
        }
        const DirEntry& e = v.fpItems[i];
        int ix = row.left + dpi_px(v, 8);
        int iy = y + (v.fpRowH - dpi_px(v, 14)) / 2;
        if (e.isDir) {
            draw_folder_glyph(dc, ix, iy, RGB(220, 180, 80));
            SetTextColor(dc, RGB(235, 235, 235));
        } else {
            draw_file_glyph(dc, ix, iy, RGB(120, 190, 240));
            SetTextColor(dc, i == v.fpSel ? RGB(255, 255, 255) : RGB(210, 210, 210));
        }
        RECT rt{ ix + dpi_px(v, 18), y, row.right - dpi_px(v, 6), y + v.fpRowH };
        DrawTextW(dc, e.name.c_str(), -1, &rt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // 底部提示
    SetTextColor(dc, RGB(140, 140, 140));
    RECT rh{ v.fpRect.left + pad, v.fpRect.bottom - dpi_px(v, 30),
             v.fpRect.right - pad, v.fpRect.bottom - dpi_px(v, 6) };
    DrawTextW(dc, tr(Sid::panel_key_hint), -1, &rh,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void paint_settings_panel(Viewer& v, HDC dc) {
    const int pad = dpi_px(v, 16);
    SelectObject(dc, v.fontBold);
    SetTextColor(dc, RGB(240, 240, 240));
    RECT rt{ v.spRect.left + pad, v.spRect.top + dpi_px(v, 10),
             v.spRect.right - pad, v.spRect.top + dpi_px(v, 44) };
    DrawTextW(dc, tr(Sid::settings_title), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    auto rows = build_settings_rows(v);
    for (size_t i = 0; i < v.spRows.size() && i < rows.size(); i++) {
        const RECT& r = v.spRows[i].first;
        int id = v.spRows[i].second;
        SelectObject(dc, v.fontUI);
        SetTextColor(dc, RGB(225, 225, 225));
        RECT rl = r;
        rl.left += dpi_px(v, 8);
        DrawTextW(dc, rows[i].first.c_str(), -1, &rl,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (!rows[i].second.empty()) {
            bool isAssoc = (id == 10);
            SetTextColor(dc, isAssoc ? RGB(255, 200, 90) : RGB(130, 200, 250));
            RECT rr = r;
            rr.right -= dpi_px(v, 8);
            DrawTextW(dc, rows[i].second.c_str(), -1, &rr,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SelectObject(dc, v.fontUI);
    SetTextColor(dc, RGB(140, 140, 140));
    RECT rf{ v.spRect.left + pad, v.spRect.bottom - dpi_px(v, 26),
             v.spRect.right - pad, v.spRect.bottom - dpi_px(v, 6) };
    std::wstring footer = trf(Sid::settings_footer, v.set.path.c_str());
    DrawTextW(dc, footer.c_str(), -1, &rf,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void panels_paint_text(Viewer& v, HDC dc) {
    if (v.mode == UiMode::FilePanel) paint_file_panel(v, dc);
    else if (v.mode == UiMode::Settings) paint_settings_panel(v, dc);
}
