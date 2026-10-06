// ============================================================================
//  settings.h : 用户自定义设置（INI 存储，程序目录优先，只读时落用户目录）
// ============================================================================
#pragma once

#include "common.h"
#include <string>

struct Settings {
    // 视图
    int bg = 0;                 // 0 黑 1 深灰 2 浅灰 3 白
    int fitOnOpen = 0;          // 0 适应窗口 1 原始大小 2 记住上次
    int smoothZoom = 1;         // 1 平滑(双线性) 0 锐利(最近邻)
    int exifRotate = 1;         // 自动应用 EXIF 方向
    double lastZoom = 0.0;      // 上次缩放（fitOnOpen==2 时使用）

    // 幻灯片
    int slideInterval = 5;      // 秒
    int slideLoop = 1;

    // 常规
    int rememberDir = 1;
    std::wstring lastDir;
    int startMaximized = 1;
    int topmost = 0;
    int loopFiles = 1;          // 浏览到末尾后循环
    std::wstring language = L"auto";   // auto=跟随系统 或语言包代码（如 zh-CN）

    // ---- 存取 ----
    void load();
    bool save() const;
    std::wstring path;          // 实际使用的 INI 路径

private:
    static std::wstring best_load_path();
    static std::wstring best_save_path();
};
