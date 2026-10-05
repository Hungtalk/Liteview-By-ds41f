// ============================================================================
//  util.cpp : 路径 / 文件 / INI 实现
// ============================================================================
#include "util.h"

#include <cwctype>
#include <filesystem>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#  include <fstream>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16
// ---------------------------------------------------------------------------
std::wstring utf8_to_utf16(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned c = (unsigned char)s[i++];
        unsigned cp = 0;
        int more = 0;
        if (c < 0x80)      { cp = c;        more = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; more = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; more = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; more = 3; }
        else { cp = 0xFFFD; more = 0; }
        for (int k = 0; k < more; k++) {
            if (i >= n) { cp = 0xFFFD; break; }
            unsigned cc = (unsigned char)s[i];
            if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            i++;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp <= 0xFFFF) out.push_back((wchar_t)cp);
        else {
            cp -= 0x10000;
            out.push_back((wchar_t)(0xD800 + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

std::string utf16_to_utf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 2);
    size_t i = 0, n = s.size();
    while (i < n) {
        u32 cp = (u32)s[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < n) {
            u32 lo = (u32)s[i];
            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 字符串 / 路径
// ---------------------------------------------------------------------------
std::wstring to_lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

bool ends_with_ci(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    return to_lower(s.substr(s.size() - suffix.size())) == to_lower(suffix);
}

std::wstring file_name_of(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring dir_of(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return std::wstring();
    std::wstring d = path.substr(0, p);
    // 保留盘符根目录（"C:" -> "C:\\"）
    if (d.size() == 2 && d[1] == L':') d += L'\\';
    return d;
}

std::wstring ext_of(const std::wstring& path) {
    std::wstring name = file_name_of(path);
    size_t p = name.find_last_of(L'.');
    if (p == std::wstring::npos || p == 0) return std::wstring();
    return to_lower(name.substr(p));
}

std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L'\\' + name;
}

std::wstring path_leaf_middle_ellipsis(const std::wstring& path, size_t max_chars) {
    if (path.size() <= max_chars || max_chars < 8) return path;
    size_t head = max_chars / 3;
    size_t tail = max_chars - head - 1;
    return path.substr(0, head) + L"…" + path.substr(path.size() - tail);
}

bool path_is_dir(const std::wstring& p) {
    std::error_code ec;
    return fs::is_directory(fs::path(p), ec);
}
bool path_exists(const std::wstring& p) {
    std::error_code ec;
    return fs::exists(fs::path(p), ec);
}

u64 file_size_of(const std::wstring& p) {
    std::error_code ec;
    auto sz = fs::file_size(fs::path(p), ec);
    return ec ? 0ull : (u64)sz;
}

bool list_dir(const std::wstring& dir, std::vector<DirEntry>& out) {
    out.clear();
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), fs::directory_options::skip_permission_denied, ec);
    if (ec) return false;
    for (const auto& e : it) {
        DirEntry de;
        de.name = e.path().filename().wstring();
        std::error_code ec2;
        de.isDir = e.is_directory(ec2);
        out.push_back(std::move(de));
    }
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return natural_compare(a.name, b.name) < 0;
    });
    return true;
}

int natural_compare(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        wchar_t ca = a[i], cb = b[j];
        if (iswdigit(ca) && iswdigit(cb)) {
            while (i < a.size() && a[i] == L'0') i++;
            while (j < b.size() && b[j] == L'0') j++;
            size_t is = i, js = j;
            while (is < a.size() && iswdigit(a[is])) is++;
            while (js < b.size() && iswdigit(b[js])) js++;
            size_t la = is - i, lb = js - j;
            if (la != lb) return la < lb ? -1 : 1;
            for (size_t k = 0; k < la; k++)
                if (a[i + k] != b[j + k]) return a[i + k] < b[j + k] ? -1 : 1;
            i = is; j = js;
            continue;
        }
        wchar_t la = (wchar_t)towlower(ca), lb = (wchar_t)towlower(cb);
        if (la != lb) return la < lb ? -1 : 1;
        i++; j++;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::vector<std::wstring> list_drives() {
    std::vector<std::wstring> v;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            wchar_t buf[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
            v.push_back(buf);
        }
    }
#else
    v.push_back(L"/");
#endif
    return v;
}

std::wstring exe_path() {
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n > 0 && n < 4096) return std::wstring(buf, n);
    return L"LiteView.exe";
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; return utf8_to_utf16(buf); }
    return L"LiteView";
#endif
}

std::wstring exe_dir() {
    std::wstring d = dir_of(exe_path());
    return d.empty() ? L"." : d;
}

std::wstring user_data_dir() {
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, 4096);
    if (n > 0 && n < 4096) return std::wstring(buf, n) + L"\\LiteView";
    n = GetEnvironmentVariableW(L"USERPROFILE", buf, 4096);
    if (n > 0) return std::wstring(buf, n) + L"\\AppData\\Local\\LiteView";
    return exe_dir();
#else
    const char* home = getenv("HOME");
    return utf8_to_utf16(home ? home : "/tmp") + L"/.config/LiteView";
#endif
}

std::wstring format_file_size(u64 bytes) {
    wchar_t buf[64];
    if (bytes < 1024ull) swprintf(buf, 64, L"%llu B", (unsigned long long)bytes);
    else if (bytes < 1024ull * 1024) swprintf(buf, 64, L"%.1f KB", (double)bytes / 1024.0);
    else if (bytes < 1024ull * 1024 * 1024) swprintf(buf, 64, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else swprintf(buf, 64, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// ---------------------------------------------------------------------------
// 文件读写
// ---------------------------------------------------------------------------
#ifdef _WIN32
bool read_file_bytes(const std::wstring& path, std::vector<u8>& out, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"无法打开文件"; return false; }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
        CloseHandle(h); err = L"无法获取文件大小"; return false;
    }
    if (sz.QuadPart > 0x7FFFFFFFll) {
        CloseHandle(h); err = L"文件太大（超过 2GB）"; return false;
    }
    out.resize((size_t)sz.QuadPart);
    size_t total = 0;
    while (total < out.size()) {
        DWORD got = 0;
        DWORD want = (DWORD)std::min<size_t>(out.size() - total, 1u << 24);
        if (!ReadFile(h, out.data() + total, want, &got, nullptr) || got == 0) {
            CloseHandle(h); err = L"读取文件失败"; return false;
        }
        total += got;
    }
    CloseHandle(h);
    return true;
}

bool write_file_bytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const u8* p = (const u8*)data;
    size_t total = 0;
    bool ok = true;
    while (total < size) {
        DWORD wrote = 0;
        DWORD want = (DWORD)std::min<size_t>(size - total, 1u << 24);
        if (!WriteFile(h, p + total, want, &wrote, nullptr) || wrote == 0) { ok = false; break; }
        total += wrote;
    }
    CloseHandle(h);
    return ok;
}
#else
bool read_file_bytes(const std::wstring& path, std::vector<u8>& out, std::wstring& err) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) { err = L"无法打开文件"; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) { err = L"无法获取文件大小"; return false; }
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0 && !f.read((char*)out.data(), n)) { err = L"读取文件失败"; return false; }
    return true;
}

bool write_file_bytes(const std::wstring& path, const void* data, size_t size) {
    std::ofstream f(fs::path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size) f.write((const char*)data, (std::streamsize)size);
    return (bool)f;
}
#endif

// ---------------------------------------------------------------------------
// 简易 INI
// ---------------------------------------------------------------------------
static std::wstring ini_trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) b--;
    return s.substr(a, b - a);
}

std::wstring IniFile::make_key(const std::wstring& sec, const std::wstring& key) {
    std::wstring k = sec;
    k.push_back(L'\x1f');
    k += key;
    return k;
}

bool IniFile::load(const std::wstring& path) {
    items_.clear();
    std::vector<u8> bytes;
    std::wstring err;
    if (!read_file_bytes(path, bytes, err)) return false;
    size_t off = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) off = 3;
    std::string text((const char*)bytes.data() + off, bytes.size() - off);
    std::wstring w = utf8_to_utf16(text);

    std::wstring sec;
    size_t i = 0;
    while (i <= w.size()) {
        size_t j = w.find(L'\n', i);
        if (j == std::wstring::npos) { j = w.size(); }
        std::wstring line = w.substr(i, j - i);
        if (j >= w.size() && line.empty()) break;
        i = j + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = ini_trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            sec = ini_trim(line.substr(1, line.size() - 2));
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        set_str(sec, ini_trim(line.substr(0, eq)), ini_trim(line.substr(eq + 1)));
    }
    return true;
}

bool IniFile::save(const std::wstring& path) const {
    std::wstring text;
    std::wstring curSec;
    bool first = true;
    for (const auto& kv : items_) {
        size_t p = kv.first.find(L'\x1f');
        std::wstring sec = p == std::wstring::npos ? std::wstring() : kv.first.substr(0, p);
        std::wstring key = p == std::wstring::npos ? kv.first : kv.first.substr(p + 1);
        if (first || sec != curSec) {
            if (!first) text += L"\r\n";
            text += L"[" + sec + L"]\r\n";
            curSec = sec;
            first = false;
        }
        text += key + L"=" + kv.second + L"\r\n";
    }
    std::string utf8 = utf16_to_utf8(text);
    return write_file_bytes(path, utf8.data(), utf8.size());
}

std::wstring IniFile::get_str(const std::wstring& sec, const std::wstring& key, const std::wstring& def) const {
    std::wstring k = make_key(sec, key);
    for (const auto& kv : items_)
        if (kv.first == k) return kv.second;
    return def;
}

int IniFile::get_int(const std::wstring& sec, const std::wstring& key, int def) const {
    std::wstring v = get_str(sec, key, std::wstring());
    if (v.empty()) return def;
    return (int)wcstol(v.c_str(), nullptr, 10);
}

void IniFile::set_str(const std::wstring& sec, const std::wstring& key, const std::wstring& val) {
    std::wstring k = make_key(sec, key);
    for (auto& kv : items_) {
        if (kv.first == k) { kv.second = val; return; }
    }
    items_.push_back({ k, val });
}

void IniFile::set_int(const std::wstring& sec, const std::wstring& key, int val) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", val);
    set_str(sec, key, buf);
}
