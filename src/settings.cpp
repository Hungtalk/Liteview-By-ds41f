// ============================================================================
//  settings.cpp
// ============================================================================
#include "settings.h"
#include "util.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

std::wstring Settings::best_load_path() {
    std::wstring exeIni = join_path(exe_dir(), L"LiteView.ini");
    if (path_exists(exeIni)) return exeIni;
    std::wstring userIni = join_path(user_data_dir(), L"LiteView.ini");
    if (path_exists(userIni)) return userIni;
    return exeIni;   // 默认 exe 目录（不存在也能读，返回空设置）
}

std::wstring Settings::best_save_path() {
    // 优先程序目录；若上次已存在则沿用；否则尝试写一个探针文件
    std::wstring exeIni = join_path(exe_dir(), L"LiteView.ini");
    if (path_exists(exeIni)) return exeIni;
    std::wstring probe = join_path(exe_dir(), L".lv_write_test");
    if (write_file_bytes(probe, "x", 1)) {
#ifdef _WIN32
        DeleteFileW(probe.c_str());
#else
        remove(utf16_to_utf8(probe).c_str());
#endif
        return exeIni;
    }
    return join_path(user_data_dir(), L"LiteView.ini");
}

void Settings::load() {
    path = best_load_path();
    IniFile ini;
    if (!ini.load(path)) return;
    bg             = clampi(ini.get_int(L"view", L"bg", bg), 0, 3);
    fitOnOpen      = clampi(ini.get_int(L"view", L"fitOnOpen", fitOnOpen), 0, 2);
    smoothZoom     = clampi(ini.get_int(L"view", L"smoothZoom", smoothZoom), 0, 1);
    exifRotate     = clampi(ini.get_int(L"view", L"exifRotate", exifRotate), 0, 1);
    {
        std::wstring z = ini.get_str(L"view", L"lastZoom", L"0");
        lastZoom = wcstod(z.c_str(), nullptr);
        if (lastZoom < 0.02 || lastZoom > 64.0) lastZoom = 0.0;
    }
    slideInterval  = ini.get_int(L"slide", L"interval", slideInterval);
    if (slideInterval < 1 || slideInterval > 600) slideInterval = 5;
    slideLoop      = clampi(ini.get_int(L"slide", L"loop", slideLoop), 0, 1);
    rememberDir    = clampi(ini.get_int(L"general", L"rememberDir", rememberDir), 0, 1);
    lastDir        = ini.get_str(L"general", L"lastDir", lastDir);
    startMaximized = clampi(ini.get_int(L"general", L"maximized", startMaximized), 0, 1);
    topmost        = clampi(ini.get_int(L"general", L"topmost", topmost), 0, 1);
    loopFiles      = clampi(ini.get_int(L"general", L"loopFiles", loopFiles), 0, 1);
}

bool Settings::save() const {
    std::wstring p = best_save_path();
    IniFile ini;
    ini.set_int(L"view", L"bg", bg);
    ini.set_int(L"view", L"fitOnOpen", fitOnOpen);
    ini.set_int(L"view", L"smoothZoom", smoothZoom);
    ini.set_int(L"view", L"exifRotate", exifRotate);
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%.4f", lastZoom);
        ini.set_str(L"view", L"lastZoom", buf);
    }
    ini.set_int(L"slide", L"interval", slideInterval);
    ini.set_int(L"slide", L"loop", slideLoop);
    ini.set_int(L"general", L"rememberDir", rememberDir);
    ini.set_str(L"general", L"lastDir", lastDir);
    ini.set_int(L"general", L"maximized", startMaximized);
    ini.set_int(L"general", L"topmost", topmost);
    ini.set_int(L"general", L"loopFiles", loopFiles);
    const_cast<Settings*>(this)->path = p;
    return ini.save(p);
}
