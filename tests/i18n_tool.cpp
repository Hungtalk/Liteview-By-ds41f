// ============================================================================
//  i18n_tool.cpp : 语言包工具（本机 Linux/Windows 编译均可，不依赖 Windows 头）
//
//  用法:
//    i18n_tool dump                    导出翻译模板 CSV（key,english,translation）
//    i18n_tool list                    列出全部 key
//    i18n_tool check <pack.csv> [--require-all]
//        校验语言包：报告覆盖率 / 未知 key / 缺失条目；
//        --require-all 时只要有缺失条目即以非零退出（用于 CI/自测）
// ============================================================================
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string to_utf8(const std::wstring& w) { return utf16_to_utf8(w); }

static void write_field(FILE* f, const std::string& s) {
    // 与加载器的转义规则对应：\ → \\、换行 → \n、制表 → \t，
    // 保证每条记录占一行（加载器按行解析）
    std::string t;
    t.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\')      t += "\\\\";
        else if (c == '\n') t += "\\n";
        else if (c == '\t') t += "\\t";
        else if (c == '\r') continue;
        else                t += c;
    }
    bool needQuote = false;
    for (char c : t)
        if (c == ',' || c == '"') { needQuote = true; break; }
    if (!t.empty() && (t.front() == ' ' || t.back() == ' ')) needQuote = true;
    if (!needQuote) { fputs(t.c_str(), f); return; }
    fputc('"', f);
    for (char c : t) { if (c == '"') fputc('"', f); fputc(c, f); }
    fputc('"', f);
}

static int cmd_dump() {
    printf("key,english,translation\n");
    const int n = i18n_string_count();
    for (int i = 0; i < n; i++) {
        write_field(stdout, i18n_key(i));
        fputc(',', stdout);
        write_field(stdout, to_utf8(i18n_english(i)));
        fputc(',', stdout);
        fputc('\n', stdout);
    }
    return 0;
}

static int cmd_list() {
    const int n = i18n_string_count();
    for (int i = 0; i < n; i++) printf("%s\n", i18n_key(i));
    return 0;
}

static int cmd_check(const char* path, bool requireAll) {
    std::wstring name;
    std::vector<std::string> unknown;
    if (!i18n_load_file(utf8_to_utf16(path), &name, &unknown)) {
        fprintf(stderr, "cannot read language pack: %s\n", path);
        return 2;
    }
    const int n = i18n_string_count();
    const int got = i18n_override_count();
    printf("== i18n check: %s\n", path);
    if (!name.empty()) printf("   name        : %s\n", to_utf8(name).c_str());
    printf("   translated  : %d / %d (%.1f%%)\n", got, n, n ? got * 100.0 / n : 0.0);
    printf("   unknown keys: %d\n", (int)unknown.size());
    for (const auto& k : unknown) printf("     - %s\n", k.c_str());

    int missing = 0;
    std::vector<const char*> miss;
    for (int i = 0; i < n; i++) {
        if (!i18n_is_overridden(i)) {
            missing++;
            if (miss.size() < 50) miss.push_back(i18n_key(i));
        }
    }
    printf("   missing     : %d\n", missing);
    for (const char* k : miss) printf("     - %s\n", k);

    if (!unknown.empty()) return 1;
    if (requireAll && missing > 0) return 1;
    printf("OK\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "dump") == 0) return cmd_dump();
    if (argc >= 2 && strcmp(argv[1], "list") == 0) return cmd_list();
    if (argc >= 3 && strcmp(argv[1], "check") == 0) {
        bool req = (argc >= 4 && strcmp(argv[3], "--require-all") == 0);
        return cmd_check(argv[2], req);
    }
    fprintf(stderr, "usage: i18n_tool dump | list | check <pack.csv> [--require-all]\n");
    return 2;
}
