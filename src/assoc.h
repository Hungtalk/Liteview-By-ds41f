// ============================================================================
//  assoc.h : 文件关联（写 HKCU\Software\Classes，免管理员权限）
// ============================================================================
#pragma once

#include "common.h"
#include <string>
#include <vector>

namespace assoc {

// 支持关联的扩展名（小写含点）
std::vector<std::wstring> supported_exts();

// 是否已在本用户下注册（“打开方式”列表 + ProgID 存在）
bool is_registered(const std::wstring& exePath);

// 注册：ProgID + OpenWithProgids + Applications 项（加入“打开方式”，不抢占默认）
bool register_assoc(const std::wstring& exePath, std::wstring& err);

// 在 register 基础上，把 HKCU 下的扩展名默认值指向本程序（备份原值，可撤销）
bool set_default_assoc(const std::wstring& exePath, std::wstring& err);

// 撤销注册并恢复备份的默认值
bool unregister_assoc(std::wstring& err);

// 通知资源管理器刷新图标/关联
void notify_shell();

} // namespace assoc
