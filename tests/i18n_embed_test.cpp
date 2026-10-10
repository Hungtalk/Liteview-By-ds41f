// ============================================================================
//  i18n_embed_test.cpp : 内嵌语言包回归测试（Windows 本机运行，不出窗口）
//
//  验证：
//   1. 语言包已编译进 exe（无需任何 lang\ 目录即可用）；
//   2. 可切换语言列表包含 zh-CN / ja-JP，显示名（# name:）解析正确；
//   3. 自动选择：Windows 日语环境返回 "ja"，必须命中 ja-JP（地区码回退）；
//   4. 切换后文案确为对应语言（非 ASCII 的 UTF-8/UTF-16 解码正确）；
//   5. 未知语言回退内置英文；
//   6. 优先级：exe 旁的 lang\<code>.csv 覆盖内嵌包，未翻译条目回退英文。
//
//  用例 1/2/6 依赖“exe 同目录没有 lang\ 目录”这一前提，而本测试通常从仓库根
//  目录运行（那里有 lang\），因此子进程用例会把 exe 复制到临时目录再跑：
//       i18n_embed_test                 → 主用例（对当前环境自适应）
//       i18n_embed_test child embedded  → 无 lang\ ：必须使用内嵌包
//       i18n_embed_test child override  → 有 lang\ ：必须覆盖内嵌包
//
//  用法: i18n_embed_test
//  退出码 0 = 全部通过
// ============================================================================
#include "i18n.h"
#include "util.h"

#include <cstdio>
#include <cstring>
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

// ---------------------------------------------------------------------------
// 子进程用例：把 exe 放到临时目录（可选放一个 lang\ 覆盖包）后运行自身
// ---------------------------------------------------------------------------
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

// mode: "embedded"（不放 lang\）或 "override"（放一个只翻译 app_title 的包）
static int child_case(const std::string& mode) {
    printf("   child[%s]: exe dir = %s\n", mode.c_str(), narrow(exe_dir()).c_str());

    std::vector<LangPack> packs = i18n_available_packs();
    check(has_pack(packs, L"ja-JP"), "child: ja-JP available");
    check(has_pack(packs, L"zh-CN"), "child: zh-CN available");

    const LangPack* ja = find_pack_info(packs, L"ja-JP");
    const LangPack* zh = find_pack_info(packs, L"zh-CN");

    if (mode == "embedded") {
        check(ja && i18n_is_embedded_path(ja->path), "child: ja-JP comes from the embedded source");
        check(zh && i18n_is_embedded_path(zh->path), "child: zh-CN comes from the embedded source");
        // "# name: 日本語" 必须被解析出来且 UTF-8 → UTF-16 解码正确
        check(ja && ja->name == L"\u65e5\u672c\u8a9e",
              "child: ja-JP display name parsed as 日本語 from the pack header");
        check(zh && zh->name == L"\u4e2d\u6587\uff08\u7b80\u4f53\uff09",
              "child: zh-CN display name parsed as 中文（简体）");
        i18n_init(L"ja-JP");
        const std::wstring title = tr(Sid::app_title);
        printf("   child: app_title(ja) = %s\n", narrow(title).c_str());
        check(title == L"LiteView \u753b\u50cf\u30d3\u30e5\u30fc\u30a2",
              "child: embedded Japanese text is served from inside the exe");
        check(i18n_override_count() == i18n_string_count(),
              "child: embedded pack covers all entries");
    } else if (mode == "override") {
        check(zh && !i18n_is_embedded_path(zh->path),
              "child: zh-CN now comes from disk (overrides embedded)");
        i18n_init(L"zh-CN");
        const std::wstring title = tr(Sid::app_title);
        printf("   child: app_title = %s\n", narrow(title).c_str());
        check(title == L"OVERRIDE-TITLE", "child: external pack text overrides the embedded one");
        check(tr(Sid::settings_title) == std::wstring(i18n_english((int)Sid::settings_title)),
              "child: untranslated entry falls back to built-in English");
        check(i18n_override_count() == 1, "child: exactly one entry overridden");
    } else {
        check(false, "child: unknown mode");
    }
    return g_fail;
}

#ifdef _WIN32
// 在临时目录里跑一个子用例；prepareOverride=true 时先放入覆盖用语言包
static void remove_tree(const std::wstring& dir) {
    std::wstring spec = dir + L"\\*";
    if (spec.size() >= MAX_PATH) return;
    wchar_t pattern[MAX_PATH] = {};
    for (size_t i = 0; i < spec.size(); i++) pattern[i] = spec[i];
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = join_path(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(child);
            else DeleteFileW(child.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

static void run_child(const char* mode, bool prepareOverride) {
    wchar_t tmp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmp)) { check(false, "GetTempPathW"); return; }
    std::wstring dir = join_path(tmp, std::wstring(L"liteview_i18n_") +
                                      std::wstring(mode, mode + strlen(mode)));
    std::wstring langDir = join_path(dir, L"lang");
    remove_tree(dir);                                   // 清掉上次运行残留
    CreateDirectoryW(dir.c_str(), nullptr);
    if (prepareOverride) CreateDirectoryW(langDir.c_str(), nullptr);

    std::wstring exeCopy = join_path(dir, L"i18n_embed_test.exe");
    if (!copy_self(exeCopy)) { check(false, "copy test exe to temp dir"); return; }
    if (prepareOverride) {
        // 只翻译 app_title，其余留空 → 同时验证“覆盖 + 回退英文”
        write_text_file(join_path(langDir, L"zh-CN.csv"),
                        "# name: Override\nkey,english,translation\n"
                        "app_title,LiteView Image Viewer,OVERRIDE-TITLE\n");
    }

    std::wstring cmd = L"\"" + exeCopy + L"\" child " + std::wstring(mode, mode + strlen(mode));
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, dir.c_str(),
                        &si, &pi)) {
        check(false, "spawn child process");
        return;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    char what[128];
    snprintf(what, sizeof(what), "child run '%s' passed", mode);
    check(code == 0, what);
    remove_tree(dir);
}
#endif

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "child") return child_case(argv[2]);

    printf("== i18n embedded pack test ==\n");

    const int n = i18n_string_count();
    printf("string table: %d entries\n", n);
    check(n == 137, "string table has 137 entries");

    // 1) 可切换语言列表包含两个语言包（来源取决于当前 exe 目录是否有 lang\）
    std::vector<LangPack> packs = i18n_available_packs();
    printf("available packs: %d\n", (int)packs.size());
    for (const auto& p : packs)
        printf("   - %-8s name=%-16s path=%s\n", narrow(p.code).c_str(), narrow(p.name).c_str(),
               narrow(p.path).c_str());
    check(has_pack(packs, L"zh-CN"), "pack zh-CN is available");
    check(has_pack(packs, L"ja-JP"), "pack ja-JP is available");

    const LangPack* ja = find_pack_info(packs, L"ja-JP");
    const LangPack* zh = find_pack_info(packs, L"zh-CN");
    const bool fromDisk = (zh && !i18n_is_embedded_path(zh->path));
    printf("   source: %s\n", fromDisk ? "lang\\ next to the exe (expected in-repo)"
                                       : "embedded");

    // 2) 强制选择日语：文案确实变了，且非 ASCII 正确
    i18n_init(L"ja-JP");
    printf("current: code=%s name=%s\n", narrow(i18n_current_code()).c_str(),
           narrow(i18n_current_name()).c_str());
    check(to_lower(i18n_current_code()) == L"ja-jp", "current code is ja-JP");
    const std::wstring jaSettings = tr(Sid::settings_title);
    printf("settings_title(ja) = %s\n", narrow(jaSettings).c_str());
    check(jaSettings != std::wstring(i18n_english((int)Sid::settings_title)),
          "Japanese text differs from built-in English");
    check(jaSettings.find(L"\u8a2d\u5b9a") != std::wstring::npos,
          "settings_title contains the Japanese word 設定 (UTF-8/UTF-16 decoding OK)");
    check(i18n_override_count() == n, "ja-JP covers all 137 entries");
    if (ja && fromDisk) check(!i18n_is_embedded_path(ja->path), "disk packs win over embedded ones");

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
    check(zhTitle == L"LiteView \u56fe\u7247\u67e5\u770b\u5668",
          "zh-CN app_title is 图片查看器");
    const std::wstring sec = trf(Sid::settings_seconds, 7);
    printf("settings_seconds(7) = %s\n", narrow(sec).c_str());
    check(sec == L"7 \u79d2", "trf formats the %d placeholder in Japanese/Chinese style");

    // 5) 回退：未知语言 → 内置英文
    i18n_init(L"xx-YY");
    printf("init(xx-YY) -> code=%s\n", narrow(i18n_current_code()).c_str());
    check(tr(Sid::settings_title) == std::wstring(i18n_english((int)Sid::settings_title)),
          "unknown language falls back to built-in English");
    check(i18n_override_count() == 0, "no overrides applied after fallback");

#ifdef _WIN32
    // 6) 来源与优先级：在临时目录里分别验证“只用内嵌包”和“外置包覆盖内嵌包”
    run_child("embedded", false);
    run_child("override", true);
#endif

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
