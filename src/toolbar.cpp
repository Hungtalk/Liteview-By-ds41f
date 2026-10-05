// ============================================================================
//  toolbar.cpp : 自绘工具栏（矢量图标，无系统控件依赖）
// ============================================================================
#include "viewer.h"
#include <cmath>

const wchar_t* toolbar_tip(int id) {
    switch (id) {
    case BTN_OPEN:     return L"打开图片（O）";
    case BTN_PREV:     return L"上一张（←）";
    case BTN_NEXT:     return L"下一张（→）";
    case BTN_ZOOM_OUT: return L"缩小（- / 滚轮）";
    case BTN_ZOOM_IN:  return L"放大（+ / 滚轮）";
    case BTN_FIT:      return L"适应窗口（0）";
    case BTN_ONE:      return L"原始大小 1:1（1）";
    case BTN_ROT_L:    return L"逆时针旋转（Shift+R）";
    case BTN_ROT_R:    return L"顺时针旋转（R）";
    case BTN_SLIDE:    return L"幻灯片放映（空格）";
    case BTN_FULL:     return L"全屏（F11）";
    case BTN_SETTINGS: return L"设置（Tab）";
    default:           return L"";
    }
}

void toolbar_layout(Viewer& v, int width) {
    v.btnRects.clear();
    int pad = dpi_px(v, 6);
    int bh = dpi_px(v, 30);
    int gap = dpi_px(v, 4);
    int y = dpi_px(v, 5);
    int x = pad;
    for (int id = 0; id < BTN_COUNT; id++) {
        int w = (id == BTN_OPEN) ? dpi_px(v, 42) : dpi_px(v, 36);
        if (x + w > width - pad && x > pad) {
            x = pad;
            y += bh + gap;
        }
        RECT r{ x, y, x + w, y + bh };
        v.btnRects.push_back(std::make_pair(r, id));
        x += w + gap;
    }
    v.toolbarH = y + bh + dpi_px(v, 5);
}

int viewer_hit_toolbar(Viewer& v, POINT pt) {
    if (v.toolbarH <= 0) return -1;
    for (const auto& b : v.btnRects)
        if (PtInRect(&b.first, pt)) return b.second;
    return -1;
}

// ---------------------------------------------------------------------------
// 图标（GDI 矢量绘制）
// ---------------------------------------------------------------------------
void draw_toolbar_icon(HDC dc, int id, const RECT& r, COLORREF color) {
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    int s = (std::min(w, h) - 14) / 2;
    if (s < 5) s = 5;
    if (s > 9) s = 9;

    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HBRUSH fillBr = CreateSolidBrush(color);

    auto tri = [&](POINT a, POINT b, POINT c) {
        SelectObject(dc, fillBr);
        POINT p[3] = { a, b, c };
        Polygon(dc, p, 3);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
    };

    switch (id) {
    case BTN_PREV:
        tri({ cx + s - 2, cy - s }, { cx + s - 2, cy + s }, { cx - s, cy });
        break;
    case BTN_NEXT:
        tri({ cx - s + 2, cy - s }, { cx - s + 2, cy + s }, { cx + s, cy });
        break;
    case BTN_ZOOM_OUT:
        Ellipse(dc, cx - s, cy - s, cx + s, cy + s);
        MoveToEx(dc, cx - s / 2, cy, nullptr);
        LineTo(dc, cx + s / 2, cy);
        break;
    case BTN_ZOOM_IN:
        Ellipse(dc, cx - s, cy - s, cx + s, cy + s);
        MoveToEx(dc, cx - s / 2, cy, nullptr);
        LineTo(dc, cx + s / 2, cy);
        MoveToEx(dc, cx, cy - s / 2, nullptr);
        LineTo(dc, cx, cy + s / 2);
        break;
    case BTN_FIT:
        Rectangle(dc, cx - s, cy - s, cx + s, cy + s);
        MoveToEx(dc, cx - s + 2, cy - s + 2, nullptr);
        LineTo(dc, cx + s - 2, cy + s - 2);
        MoveToEx(dc, cx + s - 2, cy - s + 2, nullptr);
        LineTo(dc, cx - s + 2, cy + s - 2);
        break;
    case BTN_ONE: {
        SetTextColor(dc, color);
        RECT rr = r;
        DrawTextW(dc, L"1:1", -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break; }
    case BTN_OPEN: {
        Rectangle(dc, cx - s, cy - s / 2 + 2, cx + s, cy + s);
        Rectangle(dc, cx - s, cy - s / 2, cx - s / 3, cy - s / 2 + 4);
        break; }
    case BTN_ROT_L: {
        int rr = s + 1;
        Arc(dc, cx - rr, cy - rr, cx + rr, cy + rr, cx - rr, cy, cx, cy + rr);
        tri({ cx - rr - 4, cy + 2 }, { cx - rr + 4, cy + 2 }, { cx - rr, cy + 8 });
        break; }
    case BTN_ROT_R: {
        int rr = s + 1;
        Arc(dc, cx - rr, cy - rr, cx + rr, cy + rr, cx + rr, cy, cx, cy - rr);
        tri({ cx + rr - 4, cy - 2 }, { cx + rr + 4, cy - 2 }, { cx + rr, cy - 8 });
        break; }
    case BTN_FULL: {
        int e = s;
        // 四个角
        MoveToEx(dc, cx - e, cy - e / 2, nullptr); LineTo(dc, cx - e, cy - e); LineTo(dc, cx - e / 2, cy - e);
        MoveToEx(dc, cx + e, cy - e / 2, nullptr); LineTo(dc, cx + e, cy - e); LineTo(dc, cx + e / 2, cy - e);
        MoveToEx(dc, cx - e, cy + e / 2, nullptr); LineTo(dc, cx - e, cy + e); LineTo(dc, cx - e / 2, cy + e);
        MoveToEx(dc, cx + e, cy + e / 2, nullptr); LineTo(dc, cx + e, cy + e); LineTo(dc, cx + e / 2, cy + e);
        break; }
    case BTN_SETTINGS: {
        Ellipse(dc, cx - s + 1, cy - s + 1, cx + s - 1, cy + s - 1);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4.0;
            int x1 = cx + (int)(std::cos(a) * (s - 1));
            int y1 = cy + (int)(std::sin(a) * (s - 1));
            int x2 = cx + (int)(std::cos(a) * (s + 3));
            int y2 = cy + (int)(std::sin(a) * (s + 3));
            MoveToEx(dc, x1, y1, nullptr);
            LineTo(dc, x2, y2);
        }
        Ellipse(dc, cx - 3, cy - 3, cx + 3, cy + 3);
        break; }
    case BTN_SLIDE:
        tri({ cx - 4, cy - s }, { cx - 4, cy + s }, { cx + s - 1, cy });
        break;
    default:
        break;
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DeleteObject(fillBr);
}

void toolbar_paint(Viewer& v, HDC dc) {
    SelectObject(dc, v.fontBold);
    for (const auto& b : v.btnRects) {
        const RECT& r = b.first;
        int id = b.second;
        bool active = (id == BTN_SLIDE && v.slideshow);
        u32 bg = 0;
        if (v.downBtn == id && v.hotBtn == id) bg = 0xFF0A5CA8;
        else if (v.downBtn == id) bg = 0xFF0A5CA8;
        else if (v.hotBtn == id) bg = 0xFF3E3E42;
        else if (active) bg = 0xFF12466E;
        if (bg) {
            HBRUSH br = CreateSolidBrush(argb_to_colorref(bg));
            FillRect(dc, &r, br);
            DeleteObject(br);
        }
        COLORREF ic = argb_to_colorref(active ? 0xFF7FD4FF : 0xFFDCDCDC);
        if (v.doc.base.empty() && id != BTN_OPEN && id != BTN_FULL && id != BTN_SETTINGS && id != BTN_SLIDE)
            ic = argb_to_colorref(0xFF767676);          // 无图时禁用部分按钮
        draw_toolbar_icon(dc, id, r, ic);
    }
}
