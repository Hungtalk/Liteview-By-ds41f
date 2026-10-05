// ============================================================================
//  util.h : 路径 / 文件 / 简单 INI（可移植，Windows 特化部分在 util.cpp 内）
// ============================================================================
#pragma once

#include "common.h"

// ---- 字符串 / 路径 ----
std::wstring to_lower(std::wstring s);
bool         ends_with_ci(const std::wstring& s, const std::wstring& suffix);
std::wstring file_name_of(const std::wstring& path);
std::wstring dir_of(const std::wstring& path);
std::wstring ext_of(const std::wstring& path);              // 小写、含点，如 L".jpg"
std::wstring join_path(const std::wstring& dir, const std::wstring& name);
std::wstring path_leaf_middle_ellipsis(const std::wstring& path, size_t max_chars);
bool         path_is_dir(const std::wstring& p);
bool         path_exists(const std::wstring& p);
int          natural_compare(const std::wstring& a, const std::wstring& b); // 自然排序
u64          file_size_of(const std::wstring& p);

struct DirEntry {
    std::wstring name;
    bool isDir = false;
};
// 列出目录内容（已排序：目录优先，其余自然排序）；返回 false 表示目录打不开
bool list_dir(const std::wstring& dir, std::vector<DirEntry>& out);
// Windows: {"C:\\","D:\\",...}；其它平台: {"/"}
std::vector<std::wstring> list_drives();

// ---- 程序 / 数据目录 ----
std::wstring exe_path();        // 程序完整路径
std::wstring exe_dir();         // 程序所在目录
std::wstring user_data_dir();   // %LOCALAPPDATA%\LiteView 或 ~/.config/LiteView

std::wstring format_file_size(u64 bytes);

// ---- 文件读写 ----
bool read_file_bytes(const std::wstring& path, std::vector<u8>& out, std::wstring& err);
bool write_file_bytes(const std::wstring& path, const void* data, size_t size);

// ---- 简易 INI（UTF-8，无外部依赖） ----
class IniFile {
public:
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    std::wstring get_str(const std::wstring& sec, const std::wstring& key, const std::wstring& def) const;
    int          get_int(const std::wstring& sec, const std::wstring& key, int def) const;
    void set_str(const std::wstring& sec, const std::wstring& key, const std::wstring& val);
    void set_int(const std::wstring& sec, const std::wstring& key, int val);

private:
    // 组合键： section + '\x1f' + key
    std::vector<std::pair<std::wstring, std::wstring>> items_;
    static std::wstring make_key(const std::wstring& sec, const std::wstring& key);
};
