// ============================================================================
//  main.cpp : 程序入口（DPI、命令行、单实例“自备协议”、拖放）
// ============================================================================
#include "viewer.h"
#include "i18n.h"
#include "assoc.h"

#include <shellapi.h>
#include <vector>
#include <string>

static const wchar_t* kClassName = L"LiteViewMainWindow";

// 单实例 IPC: WM_COPYDATA 传递以 '\0' 分隔的文件路径列表
static const DWORD kCopyDataMagic = 0x4C56'0001;

static void open_from_copydata(Viewer& v, PCOPYDATASTRUCT cds) {
    if (!cds || !cds->lpData || cds->cbData < sizeof(wchar_t)) return;
    const wchar_t* p = (const wchar_t*)cds->lpData;
    const wchar_t* end = (const wchar_t*)((const char*)cds->lpData + cds->cbData);
    if (p < end && *p) viewer_open_path(v, p);      // 打开第一个路径
}

static LRESULT CALLBACK LiteViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Viewer* v = (Viewer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        v = (Viewer*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!v) return DefWindowProcW(hwnd, msg, wp, lp);

    if (msg == WM_CREATE) {
        viewer_init(*v, hwnd);
        // 触摸手势：单指平移 + 双指缩放
#if defined(GID_ZOOM)
        GESTURECONFIG gc[3] = {};
        gc[0].dwID = GID_PAN;
        gc[0].dwWant = 0;          // 全部平移分量（含惯性甩动）
        gc[0].dwBlock = 0;
        gc[1].dwID = GID_ZOOM;
        gc[1].dwWant = 0;
        gc[1].dwBlock = 0;
        gc[2].dwID = GID_PRESSANDTAP;
        gc[2].dwWant = 0;
        gc[2].dwBlock = GC_PRESSANDTAP;
        SetGestureConfig(hwnd, 0, 3, gc, sizeof(GESTURECONFIG));
        RegisterTouchWindow(hwnd, 0);
#endif
        DragAcceptFiles(hwnd, TRUE);
        SetFocus(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        viewer_destroy(*v);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_COPYDATA) {
        PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT)lp;
        if (cds && cds->dwData == kCopyDataMagic) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            open_from_copydata(*v, cds);
        }
        return TRUE;
    }
    if (msg == WM_DROPFILES) {
        HDROP hd = (HDROP)wp;
        UINT count = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            std::vector<wchar_t> buf(32768);
            if (DragQueryFileW(hd, 0, buf.data(), (UINT)buf.size()) > 0)
                viewer_open_path(*v, buf.data());
            else if (count > 1) {
                // 拖进来的是一组文件：取第一个可识别的
                for (UINT i = 0; i < count; i++) {
                    if (DragQueryFileW(hd, i, buf.data(), (UINT)buf.size()) > 0) {
                        viewer_open_path(*v, buf.data());
                        break;
                    }
                }
            }
        }
        DragFinish(hd);
        return 0;
    }

    LRESULT out = 0;
    if (viewer_on_message(*v, msg, wp, lp, out)) return out;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::wstring make_absolute(const std::wstring& p) {
    if (p.empty()) return p;
    if (p.size() >= 2 && p[1] == L':') return p;                 // "C:\..."
    if (p[0] == L'\\' || p[0] == L'/') return p;                 // 根相对
    wchar_t buf[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, buf, nullptr);
    if (n > 0 && n < 4096) return std::wstring(buf, n);
    return p;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    (void)nCmdShow;

    // 高 DPI：优先 PerMonitorV2（清单之外的运行时兜底）
    {
        typedef BOOL(WINAPI * PFN_SetCtx)(void*);
        HMODULE user = GetModuleHandleW(L"user32.dll");
        if (user) {
            PFN_SetCtx pfn = (PFN_SetCtx)(void*)GetProcAddress(user, "SetProcessDpiAwarenessContext");
            if (pfn) pfn((void*)-4);              // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    // 命令行
    std::vector<std::wstring> paths;
    bool wantFull = false, doReg = false, doUnreg = false;
    int slideSec = 0;
    std::wstring langOverride;               // --lang=xx 指定语言（空 = 自动）
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                std::wstring a = argv[i];
                if (a == L"--register") doReg = true;
                else if (a == L"--unregister") doUnreg = true;
                else if (a == L"--fullscreen") wantFull = true;
                else if (a.compare(0, 7, L"--lang=") == 0) langOverride = a.substr(7);
                else if (a == L"--slide" && i + 1 < argc) slideSec = _wtoi(argv[++i]);
                else if (!a.empty() && a[0] == L'-') continue;
                else paths.push_back(make_absolute(a));
            }
            LocalFree(argv);
        }
    }

    // 语言：--lang= 优先，其次设置（默认 auto = 跟随系统语言）
    {
        Settings boot;
        boot.load();
        i18n_init(langOverride.empty() ? boot.language : langOverride);
    }

    if (doReg || doUnreg) {
        std::wstring err;
        bool ok = false;
        if (doReg) {
            ok = assoc::register_assoc(exe_path(), err) && assoc::set_default_assoc(exe_path(), err);
            assoc::notify_shell();
        } else {
            ok = assoc::unregister_assoc(err);
            assoc::notify_shell();
        }
        const wchar_t* msgOk = doReg ? tr(Sid::cli_assoc_registered) : tr(Sid::cli_assoc_removed);
        MessageBoxW(nullptr, ok ? msgOk : (err.empty() ? tr(Sid::cli_operation_failed) : err.c_str()),
                    L"LiteView", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return ok ? 0 : 1;
    }

    // 单实例：把路径发给已运行的窗口
    {
        HWND prev = FindWindowW(kClassName, nullptr);
        if (!prev) {
            // 再尝试一次（可能刚好在退出）
            Sleep(10);
            prev = FindWindowW(kClassName, nullptr);
        }
        if (prev) {
            if (paths.empty()) {
                ShowWindow(prev, SW_RESTORE);
                SetForegroundWindow(prev);
                return 0;
            }
            std::vector<wchar_t> buf;
            for (const auto& p : paths) {
                buf.insert(buf.end(), p.begin(), p.end());
                buf.push_back(0);
            }
            buf.push_back(0);
            COPYDATASTRUCT cds{};
            cds.dwData = kCopyDataMagic;
            cds.cbData = (DWORD)(buf.size() * sizeof(wchar_t));
            cds.lpData = buf.data();
            SendMessageW(prev, WM_COPYDATA, 0, (LPARAM)&cds);
            SetForegroundWindow(prev);
            return 0;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LiteViewWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;

    Viewer* v = new Viewer();

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w0 = std::min(sw * 4 / 5, 1360);
    int h0 = std::min(sh * 4 / 5, 860);
    HWND hwnd = CreateWindowExW(0, kClassName, tr(Sid::app_title),
                                WS_OVERLAPPEDWINDOW,
                                (sw - w0) / 2, (sh - h0) / 2, w0, h0,
                                nullptr, nullptr, hInst, v);
    if (!hwnd) { delete v; return 1; }

    if (!paths.empty()) viewer_open_path(*v, paths[0]);
    if (slideSec > 0) {
        v->set.slideInterval = slideSec;
        viewer_set_slideshow(*v, true);
    }

    if (wantFull) viewer_toggle_fullscreen(*v);
    else if (v->set.startMaximized) ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    else ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    delete v;
    return 0;
}
