// ============================================================================
//  assoc.cpp : 注册表关联实现（HKCU，免管理员）
// ============================================================================
#include "assoc.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

namespace assoc {

static const wchar_t* kProgId = L"LiteView.Image";
static const wchar_t* kProgFriendly = L"LiteView 图片";
static const wchar_t* kAppName = L"LiteView";

std::vector<std::wstring> supported_exts() {
    return { L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp", L".dib" };
}

static bool reg_set_str(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& val) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG rc = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)val.c_str(),
                             (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

static bool reg_get_str(HKEY root, const std::wstring& sub, const wchar_t* name, std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[2048];
    DWORD sz = sizeof(buf), type = 0;
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, (BYTE*)buf, &sz);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    out.assign(buf, sz / sizeof(wchar_t));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return true;
}

static bool reg_del_value(HKEY root, const std::wstring& sub, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return true;
    LONG rc = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

static bool reg_del_tree(HKEY root, const std::wstring& sub) {
    LONG rc = RegDeleteTreeW(root, sub.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

static std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

static std::wstring progid_key() { return std::wstring(L"Software\\Classes\\") + kProgId; }

bool is_registered(const std::wstring& exePath) {
    (void)exePath;
    std::wstring cmd;
    return reg_get_str(HKEY_CURRENT_USER, progid_key() + L"\\shell\\open\\command", nullptr, cmd) ||
           reg_get_str(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LiteView.exe\\shell\\open\\command", nullptr, cmd);
}

bool register_assoc(const std::wstring& exePath, std::wstring& err) {
    std::wstring base = progid_key();
    if (!reg_set_str(HKEY_CURRENT_USER, base, nullptr, kProgFriendly)) { err = L"写入 ProgID 失败"; return false; }
    reg_set_str(HKEY_CURRENT_USER, base + L"\\DefaultIcon", nullptr, quote(exePath) + L",0");
    reg_set_str(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", nullptr, quote(exePath) + L" \"%1\"");
    reg_set_str(HKEY_CURRENT_USER, base + L"\\shell\\open", L"FriendlyAppName", kAppName);

    // “打开方式”候选项 + 应用清单
    for (const auto& ext : supported_exts())
        reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext + L"\\OpenWithProgids", kProgId, L"");

    std::wstring appKey = L"Software\\Classes\\Applications\\LiteView.exe";
    reg_set_str(HKEY_CURRENT_USER, appKey + L"\\shell\\open\\command", nullptr, quote(exePath) + L" \"%1\"");
    reg_set_str(HKEY_CURRENT_USER, appKey, L"FriendlyAppName", kAppName);
    // SupportedTypes（供“打开方式”选择器列出）
    for (const auto& ext : supported_exts())
        reg_set_str(HKEY_CURRENT_USER, appKey + L"\\SupportedTypes", ext.c_str(), L"");
    return true;
}

bool set_default_assoc(const std::wstring& exePath, std::wstring& err) {
    if (!register_assoc(exePath, err)) return false;
    for (const auto& ext : supported_exts()) {
        std::wstring extKey = L"Software\\Classes\\" + ext;
        // 备份原默认值（用户备份区，只备份一次）
        std::wstring backupKey = L"Software\\LiteView\\Backup";
        std::wstring oldVal;
        bool hadOld = reg_get_str(HKEY_CURRENT_USER, extKey, nullptr, oldVal);
        std::wstring savedVal;
        bool alreadySaved = reg_get_str(HKEY_CURRENT_USER, backupKey, ext.c_str(), savedVal);
        if (!alreadySaved) {
            reg_set_str(HKEY_CURRENT_USER, backupKey, ext.c_str(), hadOld ? oldVal : std::wstring(L"@none"));
        }
        reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext + L"\\OpenWithList\\LiteView.exe", L"", L"");
        if (!reg_set_str(HKEY_CURRENT_USER, extKey, nullptr, kProgId)) {
            err = L"设置默认关联失败（可能被系统策略限制）";
            return false;
        }
    }
    return true;
}

bool unregister_assoc(std::wstring& err) {
    (void)err;
    for (const auto& ext : supported_exts()) {
        reg_del_value(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext + L"\\OpenWithProgids", kProgId);
        reg_del_tree(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext + L"\\OpenWithList\\LiteView.exe");
        // 恢复默认值（如果备份里不是 @none）
        std::wstring saved;
        if (reg_get_str(HKEY_CURRENT_USER, L"Software\\LiteView\\Backup", ext.c_str(), saved)) {
            if (saved != L"@none") {
                reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext, nullptr, saved);
            } else {
                reg_del_value(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext, nullptr);
            }
        }
    }
    reg_del_tree(HKEY_CURRENT_USER, progid_key());
    reg_del_tree(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LiteView.exe");
    return true;
}

void notify_shell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace assoc

#else  // 非 Windows 平台（仅用于本机测试编译，不可用）

namespace assoc {
std::vector<std::wstring> supported_exts() { return {}; }
bool is_registered(const std::wstring&) { return false; }
bool register_assoc(const std::wstring&, std::wstring& err) { err = L"仅 Windows 支持"; return false; }
bool set_default_assoc(const std::wstring&, std::wstring& err) { err = L"仅 Windows 支持"; return false; }
bool unregister_assoc(std::wstring&) { return false; }
void notify_shell() {}
} // namespace assoc

#endif
