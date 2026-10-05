// ============================================================================
//  inflate.h : 自研 DEFLATE/zlib 解压（PNG 解码使用，不依赖 zlib/系统组件）
// ============================================================================
#pragma once

#include "common.h"

// 解压 DEFLATE 原始流（不含 zlib 头/尾）
bool inflate_raw(const u8* data, size_t size, std::vector<u8>& out, size_t reserveHint = 0);

// 解压 zlib 封装流（含 2 字节头 + adler32 校验）
bool zlib_inflate(const u8* data, size_t size, std::vector<u8>& out,
                  std::string* err = nullptr, size_t reserveHint = 0);
