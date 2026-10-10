// ============================================================================
//  i18n_embed_test.cpp : 内嵌语言包回归测试（Windows 本机运行，不出窗口）
//
//  验证：
//   1. 语言包已编译进 exe（不新增文件、不做任何磁盘准备即可用）；
//   2. 可切换语言列表包含内嵌语言（zh-CN / ja-JP），显示名解析正确；
//   3. 自动选择：Windows 日语环境返回 "ja"，必须命中 ja-JP（地区码回退）；
//   4. 切换后文案确为对应语言（非 ASCII 的 UTF-8/UTF-16 解码正确）；
//   5. 未知语言回退内置英文；
//   6. 外置 lang\<code>.csv 覆盖内嵌包；外置包未翻译的条目回退英文。
//
//  用法: i18n_embed_test
//  退出码 0 = 全部通过
// ============================================================================
#include "i18n.h"
#include "util.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static std::string narrow(const std::wstring& w) { return utf16_to_utf8(w); }

static bool has_pack(const std::vector<LangPack>& v, const wchar_t* code) {
    for (const auto& p : v)
        if (to_lower(p.code) == to_lower(std::wstring(code))) return true;
    return false;
}

static const LangPack* find_pack_info(const std::vector<LangPack>& v, const wchar_t* code) {
    for (const auto& p : v)
        if (to_lower(p.code) == to_lower(std::wstring(code))) return &p;
    return nullptr;
}

// 用例 6：把 exe 复制到临时目录并放一个外置语言包，验证优先级。
// 用子进程跑自己（参数 "child"），因为语言包目录取决于 exe_dir()。
static void write_text_file(const std::wstring& path, const std::string& text) {
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr);
    CloseHandle(h);
#else
    std::vector<u8> b(text.begin(), text.end());
    write_file_bytes(path, b.data(), b.size());
#endif
}

static bool copy_self(const std::wstring& dst) {
#ifdef _WIN32
    wchar_t self[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) return false;
    return CopyFileW(self, dst.c_str(), FALSE) != 0;
#else
    (void)dst;
    return false;
#endif
}

static int child_case() {
    printf("   child: exe dir = %s\n", narrow(exe_dir()).c_str());
    std::vector<LangPack> packs = i18n_available_packs();
    const LangPack* zh = find_pack_info(packs, L"zh-CN");
    check(zh != nullptr, "child: zh-CN visible in pack list");
    if (zh) check(!i18n_is_embedded_path(zh->path), "child: zh-CN now comes from disk (overrides embedded)");

    i18n_init(L"zh-CN");
    const std::wstring title = tr(Sid::app_title);
    printf("   child: app_title = %s\n", narrow(title).c_str());
    check(title == L"OVERRIDE-TITLE", "child: external pack text overrides the embedded one");

    const std::wstring footer = tr(Sid::settings_title);
    const std::wstring enFooter(i18n_english((int)Sid::settings_title));
    printf("   child: settings_title = %s\n", narrow(footer).c_str());
    check(footer == enFooter, "child: untranslated entry falls back to built-in English");
    check(i18n_override_count() == 1, "child: exactly one entry overridden");
    return g_fail;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "child") return child_case();

    printf("== i18n embedded pack test ==\n");

    const int n = i18n_string_count();
    printf("string table: %d entries\n", n);
    check(n == 137, "string table has 137 entries");

    // 1) 可切换语言列表来自内嵌包（没有 lang\ 目录也应出现）
    std::vector<LangPack> packs = i18n_available_packs();
    printf("available packs: %d\n", (int)packs.size());
    for (const auto& p : packs)
        printf("   - %-8s name=%-16s path=%s\n", narrow(p.code).c_str(), narrow(p.name).c_str(),
               narrow(p.path).c_str());
    check(has_pack(packs, L"zh-CN"), "embedded pack zh-CN is available");
    check(has_pack(packs, L"ja-JP"), "embedded pack ja-JP is available");

    const LangPack* ja = find_pack_info(packs, L"ja-JP");
    if (ja) {
        check(i18n_is_embedded_path(ja->path), "ja-JP comes from the embedded source");
        // "# name: 日本語" 必须被解析出来并正确解码（UTF-8 -> UTF-16）
        check(ja->name == L"\u65e5\u672c\u8a9e",
              "ja-JP display name parsed as 日本語 from the pack header");
    } else {
        check(false, "ja-JP entry present");
    }

    // 2) 强制选择日语：文案确实变了，且非 ASCII 正确
    i18n_init(L"ja-JP");
    printf("current: code=%s name=%s\n", narrow(i18n_current_code()).c_str(),
           narrow(i18n_current_name()).c_str());
    check(to_lower(i18n_current_code()) == L"ja-jp", "current code is ja-JP");
    const std::wstring jaTitle = tr(Sid::app_title);
    printf("app_title(ja) = %s\n", narrow(jaTitle).c_str());
    const std::wstring jaSettings = tr(Sid::settings_title);
    printf("settings_title(ja) = %s\n", narrow(jaSettings).c_str());
    check(jaSettings != std::wstring(i18n_english((int)Sid::settings_title)),
          "Japanese text differs from built-in English");
    check(jaSettings.find(L"\u8a2d\u5b9a") != std::wstring::npos,
          "settings_title contains the Japanese word 設定 (UTF-8/UTF-16 decoding OK)");
    check(i18n_override_count() == n, "ja-JP covers all 137 entries");

    // 3) 自动选择：Windows 日语环境返回 "ja"，必须命中 ja-JP
    i18n_init(L"ja");
    printf("init(ja) -> code=%s name=%s\n", narrow(i18n_current_code()).c_str(),
           narrow(i18n_current_name()).c_str());
    check(to_lower(i18n_current_code()) == L"ja-jp",
          "system code ja matches the ja-JP pack (region-tagged fallback)");

    // 4) 简体中文 + 占位符格式化
    i18n_init(L"zh-CN");
    const std::wstring zhTitle = tr(Sid::app_title);
    printf("app_title(zh-CN) = %s\n", narrow(zhTitle).c_str());
    check(zhTitle.find(L"\u56fe\u7247") != std::wstring::npos,
          "zh-CN app_title contains 图片");
    const std::wstring sec = trf(Sid::settings_seconds, 7);
    printf("settings_seconds(7) = %s\n", narrow(sec).c_str());
    check(sec.find(L"7") != std::wstring::npos, "trf formats the %d placeholder");

    // 5) 回退：未知语言 → 内置英文
    i18n_init(L"xx-YY");
    printf("init(xx-YY) -> code=%s\n", narrow(i18n_current_code()).c_str());
    check(tr(Sid::settings_title) == std::wstring(i18n_english((int)Sid::settings_title)),
          "unknown language falls back to built-in English");
    check(i18n_override_count() == 0, "no overrides applied after fallback");

    // 6) 外置语言包覆盖内嵌包（在临时目录里以子进程方式验证）
#ifdef _WIN32
    {
        wchar_t tmp[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, tmp)) {
            std::wstring dir = join_path(tmp, L"liteview_i18n_test");
            std::wstring langDir = join_path(dir, L"lang");
            std::vector<std::wstring> dummy;
            CreateDirectoryW(dir.c_str(), nullptr);
            CreateDirectoryW(langDir.c_str(), nullptr);
            std::wstring exeCopy = join_path(dir, L"i18n_embed_test.exe");
            if (copy_self(exeCopy)) {
                // 只翻译 app_title，其余留空 → 验证“覆盖 + 回退英文”
                write_text_file(join_path(langDir, L"zh-CN.csv"),
                                "# name: Override\nkey,english,translation\n"
                                "app_title,LiteView Image Viewer,OVERRIDE-TITLE\n");
                std::wstring cmd = L"\"" + exeCopy + L"\" child";
                std::vector<wchar_t> buf(cmd.begin(), cmd.end());
                buf.push_back(0);
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                   dir.c_str(), &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 60000);
                    DWORD code = 1;
                    GetExitCodeProcess(pi.hProcess, &code);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    check(code == 0, "external lang\\ pack overrides the embedded pack (child run)");
                } else {
                    check(false, "spawn child process for override check");
                }
            } else {
                check(false, "copy test exe to temp dir");
            }
        } else {
            check(false, "GetTempPathW");
        }
    }
#endif

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
