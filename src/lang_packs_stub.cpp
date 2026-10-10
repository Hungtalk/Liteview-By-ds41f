// ============================================================================
//  lang_packs_stub.cpp : 无内嵌语言包时的占位实现
//
//  当构建环境没有 Python（无法运行 tools/embed_lang.py，也就没有
//  src/lang_packs_generated.cpp）时，构建脚本改为编译本文件并传入
//  -DLITEVIEW_EMBEDDED_PACKS=0。此时程序仍可构建，只是语言由
//  内置英文 + 外置 lang\*.csv 提供。
// ============================================================================
#include "i18n_embedded.h"

const LvEmbeddedPack* lv_embedded_packs(int* count) {
    if (count) *count = 0;
    return nullptr;
}
