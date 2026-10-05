// ============================================================================
//  LiteView - 轻量图片查看器
//  common.h : 基础类型与内联小工具（不依赖 Windows 头文件，便于本机单元测试）
// ============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <limits>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i16 = int16_t;
using i32 = int32_t;

// ---------------------------------------------------------------------------
// 像素格式：内存中每个像素 4 字节，字节序 B,G,R,A；按整数解释为 0xAARRGGBB
// ---------------------------------------------------------------------------
static inline u32 pack_argb(int a, int r, int g, int b) {
    return (u32(a & 255) << 24) | (u32(r & 255) << 16) | (u32(g & 255) << 8) | u32(b & 255);
}
static inline int px_a(u32 c) { return int(c >> 24); }
static inline int px_r(u32 c) { return int((c >> 16) & 255); }
static inline int px_g(u32 c) { return int((c >> 8) & 255); }
static inline int px_b(u32 c) { return int(c & 255); }

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline u8  clamp8(int v) { return (u8)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// 小端读取（不依赖平台字节序的写法）
static inline u32 rd16le(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8); }
static inline u32 rd32le(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
static inline u32 rd16be(const u8* p) { return (u32(p[0]) << 8) | u32(p[1]); }
static inline u32 rd32be(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
static inline i32 rdi32le(const u8* p) { return (i32)rd32le(p); }

// 图像尺寸安全上限（约 64M 像素 = 256MB 像素缓冲）
static const u64 kMaxImagePixels = 64ull * 1000ull * 1000ull;
static const int kMaxImageSide     = 65535;

// ---------------------------------------------------------------------------
// 简单的 UTF-8 <-> UTF-16 转换（不依赖系统组件）
// ---------------------------------------------------------------------------
std::wstring utf8_to_utf16(const std::string& s);
std::string  utf16_to_utf8(const std::wstring& s);
