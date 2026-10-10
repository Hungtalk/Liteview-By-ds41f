// ============================================================================
//  i18n.cpp : 多语言实现（内置英文 + 外置语言包 lang/<code>.csv）
//
//  语言包格式见 i18n.h 顶部注释；解析器为按行 RFC4180 风格：
//  UTF-8（可带 BOM）、CRLF/LF、# 注释、双引号字段、"" 转义、
//  \n / \t / \\ 转义；三列为标准写法（key,english,translation），
//  两列（key,translation）也可接受。
// ============================================================================
#include "i18n.h"
#include "i18n_embedded.h"
#include "util.h"

#include <cstdarg>
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// ---------------------------------------------------------------------------
// 内置英文表 / key 表（顺序与 Sid 一致）
// ---------------------------------------------------------------------------
#define LV_STR_EN(id, en) en,
static const wchar_t* kEnglish[] = { LITEVIEW_STRINGS(LV_STR_EN) };
#undef LV_STR_EN

#define LV_STR_KEY(id, en) #id,
static const char* kKeys[] = { LITEVIEW_STRINGS(LV_STR_KEY) };
#undef LV_STR_KEY

static const int kCount = (int)(sizeof(kEnglish) / sizeof(kEnglish[0]));
static_assert((int)Sid::k_count == (int)(sizeof(kEnglish) / sizeof(kEnglish[0])),
              "string table size mismatch");

namespace {

std::vector<std::wstring> g_over;       // 语言包覆盖文本
std::vector<char>         g_overSet;    // 是否已覆盖
std::wstring              g_code = L"en";
std::wstring              g_name = L"English";

// 空白裁剪 / 判空（放在最前：下面的内嵌包元数据扫描与解析器都要用）
std::string trim_ascii(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

bool all_space_ascii(const std::string& s) {
    for (char c : s)
        if (c != ' ' && c != '\t') return false;
    return true;
}

// ---- 内嵌语言包（构建期编译进 exe，见 i18n_embedded.h） ----
struct PackBlob {
    std::wstring code;
    std::wstring name;                  // 由 CSV 的 "# name:" 解析出来
    std::vector<u8> bytes;
};
std::vector<PackBlob> g_embedded;
bool                  g_embeddedReady = false;

// "# name: xxx" 注释中提取显示名。
// 返回值为“数据行起点”相对 p 的偏移：跳过 BOM 与全部注释行，使内嵌包与
// 磁盘包走完全相同的解析路径。
size_t embedded_scan_meta(const u8* p, size_t n, std::wstring& nameOut) {
    size_t off = 0;
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) off = 3;
    std::string text((const char*)p + off, n - off);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t j = text.find('\n', pos);
        std::string line = (j == std::string::npos) ? text.substr(pos) : text.substr(pos, j - pos);
        pos = (j == std::string::npos) ? text.size() : j + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != '#') break;                 // 数据行开始
        std::string c = line.substr(1);
        size_t q = c.find_first_not_of(" \t");
        if (q != std::string::npos && c.compare(q, 4, "name") == 0) {
            size_t r = c.find_first_of(":=", q + 4);
            if (r != std::string::npos) nameOut = utf8_to_utf16(trim_ascii(c.substr(r + 1)));
        }
    }
    return off + pos;
}

// 把内嵌包登记进 g_embedded（只需一次）
void ensure_embedded() {
    if (g_embeddedReady) return;
    g_embeddedReady = true;
    int n = 0;
    const LvEmbeddedPack* p = lv_embedded_packs(&n);
    if (!p) return;
    g_embedded.reserve((size_t)n);
    for (int i = 0; i < n; i++) {
        if (!p[i].code || !p[i].data || p[i].size == 0) continue;
        PackBlob b;
        b.code = p[i].code;
        b.name = b.code;
        std::wstring nm;
        embedded_scan_meta(p[i].data, p[i].size, nm);
        if (!nm.empty()) b.name = nm;
        b.bytes.assign(p[i].data, p[i].data + p[i].size);
        g_embedded.push_back(std::move(b));
    }
}

void ensure_state() {
    if ((int)g_over.size() == kCount) return;
    g_over.assign((size_t)kCount, std::wstring());
    g_overSet.assign((size_t)kCount, 0);
}

const std::unordered_map<std::string, int>& keymap() {
    static std::unordered_map<std::string, int>* m = nullptr;
    if (!m) {
        m = new std::unordered_map<std::string, int>();
        for (int i = 0; i < kCount; i++) m->emplace(kKeys[i], i);
    }
    return *m;
}

std::wstring& norm_ref(std::wstring& s) {    for (auto& c : s) {
        if (c == L'_') c = L'-';
        else if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    }
    return s;
}

std::wstring norm_code(const std::wstring& s) {
    std::wstring t = s;
    return norm_ref(t);
}

// 在已登记的内嵌语言包中查找；未命中返回 nullptr
const PackBlob* find_embedded(const std::wstring& code) {
    ensure_embedded();
    std::wstring want = norm_code(code);
    if (want.empty()) return nullptr;
    for (const auto& b : g_embedded)
        if (norm_code(b.code) == want) return &b;
    return nullptr;
}

// \n / \t / \\ 转义（其余反斜杠原样保留，不影响 Windows 路径等文本）
std::string unescape_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') { out += '\n'; i++; continue; }
            if (n == 't') { out += '\t'; i++; continue; }
            if (n == '\\') { out += '\\'; i++; continue; }
        }
        out += c;
    }
    return out;
}

// 按行解析一条 CSV 记录（双引号字段，"" 表示引号字符；跨行字段不支持，
// 换行请使用 \n 转义写法）
void parse_record(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQuote) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
                else inQuote = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            inQuote = true;
        } else if (c == ',') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(cur);
}

// 语言包目录（按优先级）：exe\lang → 用户目录\lang
std::vector<std::wstring> lang_dirs() {
    std::vector<std::wstring> v;
    v.push_back(i18n_lang_dir());
    v.push_back(join_path(user_data_dir(), L"lang"));
    return v;
}

// 解析语言包文本（已去掉 BOM）并写入覆盖表。
// 磁盘文件与内嵌内存走的是同一个函数，保证行为一致。
bool parse_pack_text(const std::string& text, std::wstring* nameOut,
                     std::vector<std::string>* unknownKeys) {
    bool sawData = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t j = text.find('\n', pos);
        std::string line = (j == std::string::npos) ? text.substr(pos) : text.substr(pos, j - pos);
        pos = (j == std::string::npos) ? text.size() : j + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line[0] == '#') {                        // 注释 / 元数据
            if (nameOut) {
                std::string c = line.substr(1);
                size_t p = c.find_first_not_of(" \t");
                if (p != std::string::npos && c.compare(p, 4, "name") == 0) {
                    size_t q = c.find_first_of(":=", p + 4);
                    if (q != std::string::npos)
                        *nameOut = utf8_to_utf16(trim_ascii(c.substr(q + 1)));
                }
            }
            continue;
        }

        std::vector<std::string> f;
        parse_record(line, f);
        std::string key = trim_ascii(f.empty() ? std::string() : f[0]);
        if (key.empty()) continue;
        if (!sawData) {
            sawData = true;
            if (key == "key" || key == "id") continue;   // 表头行
        }

        std::string val;
        if (f.size() >= 3) {
            val = f[2];
            for (size_t k = 3; k < f.size(); k++) { val += ','; val += f[k]; }
        } else if (f.size() == 2) {
            val = f[1];                                  // 两列简写: key,translation
        }
        val = unescape_text(val);
        if (all_space_ascii(val)) continue;              // 留空 = 保留英文

        auto it = keymap().find(key);
        if (it == keymap().end()) {
            if (unknownKeys) unknownKeys->push_back(key);
        } else {
            g_over[(size_t)it->second] = utf8_to_utf16(val);
            g_overSet[(size_t)it->second] = 1;
        }
    }
    return true;
}

// 在可用语言包中查找，匹配顺序（从最精确到最宽松）：
//   1. 精确匹配            zh-CN → zh-CN
//   2. 请求带地区码时退到主语言码  zh-CN → zh
//   3. 请求只有主语言码时向上匹配带地区码的包  ja → ja-JP
//      （Windows 上 GetLocaleInfoW(LOCALE_SISO639LANGNAME) 只给 "ja"，
//        而语言包习惯按 "ja-JP" 命名，没有这一步日语等语言不会被自动选中）
//   4. 同主语言码的多个包（zh-CN / zh-TW）按代码自然排序取第一个
bool find_pack(const std::wstring& raw, LangPack& out) {
    std::wstring want = norm_code(raw);
    if (want.empty()) return false;
    std::vector<LangPack> packs = i18n_available_packs();
    for (const auto& p : packs) {
        if (norm_code(p.code) == want) { out = p; return true; }
    }
    size_t dash = want.find(L'-');
    if (dash != std::wstring::npos && dash > 0) {
        std::wstring prim = want.substr(0, dash);
        for (const auto& p : packs) {
            if (norm_code(p.code) == prim) { out = p; return true; }
        }
    } else {
        for (const auto& p : packs) {
            std::wstring c = norm_code(p.code);
            if (c.size() > want.size() && c.compare(0, want.size(), want) == 0 &&
                c[want.size()] == L'-') {
                out = p;
                return true;
            }
        }
    }
    return false;
}

// 从 LangPack 描述的来源加载（内嵌虚拟路径 → 内存；否则读文件）
bool load_pack_source(const LangPack& p, std::wstring& nameOut) {
    if (i18n_is_embedded_path(p.path)) {
        const PackBlob* b = find_embedded(p.code);
        if (!b) return false;
        if (i18n_load_memory(b->bytes.data(), b->bytes.size(), &nameOut, nullptr)) {
            if (nameOut.empty()) nameOut = b->name;
            return true;
        }
        return false;
    }
    return i18n_load_file(p.path, &nameOut, nullptr);
}

bool apply_choice(const std::wstring& raw) {
    LangPack p;
    if (find_pack(raw, p)) {
        std::wstring nm;
        if (load_pack_source(p, nm)) {
            g_code = p.code;
            g_name = nm.empty() ? p.code : nm;
            return true;
        }
    }
    // 找不到 / 读取失败 → 英文（有 en 语言包则用包，否则内置）
    LangPack en;
    if (find_pack(L"en", en)) {
        std::wstring nm;
        if (load_pack_source(en, nm)) {
            g_code = en.code;
            g_name = nm.empty() ? L"English" : nm;
            return norm_code(raw) == L"en";
        }
    }
    i18n_reset_overrides();
    g_code = L"en";
    g_name = L"English";
    return norm_code(raw) == L"en" || raw.empty();
}

} // namespace

// ---------------------------------------------------------------------------
// 取文本
// ---------------------------------------------------------------------------
const wchar_t* tr(Sid id) {
    ensure_state();
    int i = (int)id;
    if (i < 0 || i >= kCount) return L"";
    if (g_overSet[(size_t)i]) return g_over[(size_t)i].c_str();
    return kEnglish[i];
}

std::wstring trf(Sid id, ...) {
    const wchar_t* fmt = tr(id);
    std::wstring out;
    va_list ap;
    va_start(ap, id);
    for (size_t cap = 256; cap <= 16384; cap *= 2) {
        std::vector<wchar_t> buf(cap);
        va_list ap2;
        va_copy(ap2, ap);
        int n = vswprintf(buf.data(), buf.size(), fmt, ap2);
        va_end(ap2);
        if (n >= 0 && (size_t)n < buf.size()) {
            out.assign(buf.data(), (size_t)n);
            break;
        }
        if (n >= 0) {
            out.assign(buf.data(), (size_t)n);   // 某些实现返回所需长度
            break;
        }
    }
    va_end(ap);
    return out;
}

// ---------------------------------------------------------------------------
// 系统语言
// ---------------------------------------------------------------------------
std::wstring i18n_system_code() {
#ifdef _WIN32
    LANGID lid = GetUserDefaultUILanguage();     // 用户界面语言
    if (!lid) lid = GetSystemDefaultUILanguage(); // 兜底：系统安装语言
    if (PRIMARYLANGID(lid) == LANG_CHINESE) {
        switch (SUBLANGID(lid)) {
        case SUBLANG_CHINESE_TRADITIONAL: return L"zh-TW";
        case SUBLANG_CHINESE_HONGKONG:    return L"zh-HK";
        case SUBLANG_CHINESE_MACAU:       return L"zh-MO";
        default:                          return L"zh-CN";  // 简体 / 新加坡
        }
    }
    wchar_t iso[32] = {};
    if (GetLocaleInfoW(MAKELCID(lid, SORT_DEFAULT), LOCALE_SISO639LANGNAME, iso, 32) && iso[0])
        return norm_code(iso);
    wchar_t locname[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(locname, LOCALE_NAME_MAX_LENGTH) > 0)
        return locname;
    return L"en";
#else
    const char* env = getenv("LC_ALL");
    if (!env || !*env) env = getenv("LC_MESSAGES");
    if (!env || !*env) env = getenv("LANG");
    std::string s = (env && *env) ? env : "";
    if (s.empty() || s == "C" || s == "POSIX") return L"en";
    size_t p = s.find_first_of(".@");               // zh_CN.UTF-8 → zh_CN
    if (p != std::string::npos) s = s.substr(0, p);
    return norm_code(utf8_to_utf16(s));
#endif
}

// ---------------------------------------------------------------------------
// 语言包管理
// ---------------------------------------------------------------------------
bool i18n_select(const std::wstring& preferred) {
    std::wstring want = preferred;
    if (want.empty() || norm_code(want) == L"auto") want = i18n_system_code();
    if (want.empty()) want = L"en";
    return apply_choice(want);
}

void i18n_init(const std::wstring& preferred) {
    i18n_select(preferred);
}

const std::wstring& i18n_current_code() {
    ensure_state();
    return g_code;
}

const std::wstring& i18n_current_name() {
    ensure_state();
    return g_name;
}

std::wstring i18n_lang_dir() {
    return join_path(exe_dir(), L"lang");
}

std::vector<LangPack> i18n_available_packs() {
    std::vector<LangPack> v;
    std::vector<std::wstring> seen;
    for (const auto& dir : lang_dirs()) {
        std::vector<DirEntry> es;
        if (!list_dir(dir, es)) continue;
        for (const auto& e : es) {
            if (e.isDir || !ends_with_ci(e.name, L".csv")) continue;
            std::wstring code = e.name.substr(0, e.name.size() - 4);
            if (to_lower(code) == L"template") continue;   // 翻译模板不作为语言列出
            bool dup = false;
            for (const auto& s : seen)
                if (norm_code(s) == norm_code(code)) { dup = true; break; }
            if (dup) continue;                       // exe 目录优先
            seen.push_back(code);
            LangPack p;
            p.code = code;
            p.name = code;
            p.path = join_path(dir, e.name);
            v.push_back(p);
        }
    }

    // 内嵌语言包排在磁盘包之后：磁盘包同名时覆盖内嵌包（先去重保留磁盘那份），
    // 这样用户把 zh-CN.csv 丢进 lang\ 即可替换内嵌译文，无需重新编译。
    ensure_embedded();
    for (const auto& b : g_embedded) {
        bool dup = false;
        for (const auto& s : seen)
            if (norm_code(s) == norm_code(b.code)) { dup = true; break; }
        if (dup) continue;
        seen.push_back(b.code);
        LangPack p;
        p.code = b.code;
        p.name = b.name;
        p.path = std::wstring(kEmbeddedPrefix) + b.code;
        v.push_back(p);
    }

    std::sort(v.begin(), v.end(), [](const LangPack& a, const LangPack& b) {
        return natural_compare(a.code, b.code) < 0;
    });
    return v;
}

// ---------------------------------------------------------------------------
// 语言包加载（磁盘文件 / 内存字节共用同一解析器）
// ---------------------------------------------------------------------------
bool i18n_load_file(const std::wstring& path, std::wstring* nameOut,
                    std::vector<std::string>* unknownKeys) {
    ensure_state();
    i18n_reset_overrides();
    if (nameOut) nameOut->clear();

    std::vector<u8> bytes;
    std::wstring err;
    if (!read_file_bytes(path, bytes, err)) return false;

    size_t off = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) off = 3;
    std::string text((const char*)bytes.data() + off, bytes.size() - off);
    return parse_pack_text(text, nameOut, unknownKeys);
}

// 同一份文本解析器，供内存来源（内嵌语言包）使用：
// 与磁盘加载共用，避免两套 CSV 解析出现行为差异。
bool i18n_load_memory(const void* data, size_t size, std::wstring* nameOut,
                      std::vector<std::string>* unknownKeys) {
    ensure_state();
    i18n_reset_overrides();
    if (nameOut) nameOut->clear();
    if (!data || size == 0) return false;

    const u8* p = (const u8*)data;
    std::wstring nm;
    size_t off = embedded_scan_meta(p, size, nm);     // 跳过 BOM + 注释，取显示名
    if (nameOut && !nm.empty()) *nameOut = nm;

    std::string text((const char*)p + off, size - off);
    return parse_pack_text(text, nullptr, unknownKeys);
}

// ---------------------------------------------------------------------------
// 内嵌语言包登记（由 main / 测试在 i18n 使用前调用；多次调用无副作用）
// ---------------------------------------------------------------------------
const wchar_t* const kEmbeddedPrefix = L"@embed:";

bool i18n_is_embedded_path(const std::wstring& path) {
    size_t n = wcslen(kEmbeddedPrefix);
    return path.size() > n && path.compare(0, n, kEmbeddedPrefix) == 0;
}

void i18n_register_embedded(const LvEmbeddedPack* packs, int count) {
    ensure_state();
    if (!packs || count <= 0) return;
    ensure_embedded();
    (void)packs;
    (void)count;
}

// ---------------------------------------------------------------------------
// 工具 / 测试接口
// ---------------------------------------------------------------------------
int i18n_string_count() { return kCount; }

const char* i18n_key(int index) {
    return (index >= 0 && index < kCount) ? kKeys[index] : "";
}

const wchar_t* i18n_english(int index) {
    return (index >= 0 && index < kCount) ? kEnglish[index] : L"";
}

const wchar_t* i18n_text(int index) {
    return (index >= 0 && index < kCount) ? tr((Sid)index) : L"";
}

bool i18n_is_overridden(int index) {
    ensure_state();
    return index >= 0 && index < kCount && g_overSet[(size_t)index] != 0;
}

int i18n_override_count() {
    ensure_state();
    int n = 0;
    for (int i = 0; i < kCount; i++)
        if (g_overSet[(size_t)i]) n++;
    return n;
}

void i18n_reset_overrides() {
    ensure_state();
    std::fill(g_over.begin(), g_over.end(), std::wstring());
    std::fill(g_overSet.begin(), g_overSet.end(), '\0');
}
