// ============================================================================
//  i18n_embedded.h : 内嵌语言包（把 lang/*.csv 编译进 exe）
//
//  背景：LiteView 追求“单文件、无外部依赖”，因此语言包不应强制要求
//        随 exe 附带 lang\ 目录。做法是用 tools/embed_lang.py 在构建前把
//        lang/*.csv 转成 C++ 字节数组（generated/lang_packs_generated.cpp），
//        编译进程序；运行时优先使用这些内嵌语言包，磁盘上的 lang\ 仍然可用
//        （且优先级更高，便于用户不改程序就替换译文）。
//
//  优先级（详见 i18n.cpp）：
//        exe\lang\*.csv  >  %LOCALAPPDATA%\LiteView\lang\*.csv  >  内嵌语言包
//        任何情况下都可用内置英文兜底。
//
//  若没有 generated 文件（例如克隆后未执行生成脚本），程序仍可编译：
//  此时 LITEVIEW_EMBEDDED_PACKS 为 0，只有内置英文 + 外置语言包。
// ============================================================================
#pragma once

#include "common.h"

// 是否编译内嵌语言包（0/1），默认 1，即默认存在 generated 文件。
// 构建脚本在“Python 缺失 / 未生成”时显式传 -DLITEVIEW_EMBEDDED_PACKS=0，
// 此时仍需一份占位实现：直接编译 src/lang_packs_stub.cpp。
#ifndef LITEVIEW_EMBEDDED_PACKS
#  define LITEVIEW_EMBEDDED_PACKS 1
#endif

// 一个内嵌语言包：UTF-8 原始字节（不做字符集转换，避免编译期执行字符集问题）
struct LvEmbeddedPack {
    const wchar_t*     code;    // 语言代码，如 L"ja-JP"
    const unsigned char* data;  // CSV 字节
    size_t             size;    // 字节数
};

// 由 lang_packs_generated.cpp（或 lang_packs_stub.cpp）提供；
// 无内嵌包时返回 nullptr / *count=0
const LvEmbeddedPack* lv_embedded_packs(int* count);

// 把 LvEmbeddedPack 数组注册进 i18n 运行时（i18n_init 会先调用它）
void i18n_register_embedded(const LvEmbeddedPack* packs, int count);
