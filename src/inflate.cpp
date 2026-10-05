// ============================================================================
//  inflate.cpp : DEFLATE (RFC1951) + zlib (RFC1950) 解压实现
// ============================================================================
#include "inflate.h"

namespace {

struct BitReader {
    const u8* data;
    size_t    size;
    size_t    pos = 0;
    u32       buf = 0;
    int       cnt = 0;
    bool      err = false;

    BitReader(const u8* d, size_t n) : data(d), size(n) {}

    void need(int k) {
        while (cnt < k) {
            if (pos >= size) { err = true; return; }
            buf |= u32(data[pos++]) << cnt;
            cnt += 8;
        }
    }
    u32 bits(int k) {
        if (k == 0) return 0;
        need(k);
        u32 v = buf & ((1u << k) - 1u);
        buf >>= k;
        cnt -= k;
        return v;
    }
    void align_byte() {
        int drop = cnt & 7;
        buf >>= drop;
        cnt -= drop;
    }
};

// 规范 Huffman 表
struct Huff {
    u16 count[16];      // count[len] : 长度为 len 的码字数（len 1..15）
    u16 symbol[320];
    int firstcode[16];
    int firstindex[16];

    bool build(const u8* lengths, int n) {
        for (int i = 0; i < 16; i++) count[i] = 0;
        for (int i = 0; i < n; i++) {
            if (lengths[i] > 15) return false;
            count[lengths[i]]++;
        }
        int left = 1;
        for (int len = 1; len <= 15; len++) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;             // 过完备
        }
        if (left > 0 && count[0] == 0) {
            // 允许不完备（部分编码器如此），解码时遇到空槽返回失败即可
        }
        if (count[0] == n) return false;            // 空表
        int code = 0, idx = 0;
        for (int len = 1; len <= 15; len++) {
            firstcode[len] = code;
            firstindex[len] = idx;
            code += count[len];
            idx += count[len];
            code <<= 1;
        }
        // 符号表按“码长升序、同长按原始顺序”排列；注意长度为 0 的符号不占位
        int pos[16];
        for (int l = 1; l <= 15; l++) pos[l] = firstindex[l];
        for (int i = 0; i < n; i++) {
            int l = lengths[i];
            if (l) symbol[pos[l]++] = (u16)i;
        }
        return true;
    }

    int decode(BitReader& br) const {
        int code = 0;
        for (int len = 1; len <= 15; len++) {
            code = (code << 1) | (int)br.bits(1);
            if (br.err) return -1;
            unsigned diff = (unsigned)code - (unsigned)firstcode[len];
            if (diff < (unsigned)count[len])
                return symbol[firstindex[len] + (int)diff];
        }
        return -1;
    }
};

const u16 kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const u8  kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const u16 kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const u8  kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

const Huff& fixed_lit() {
    static Huff h; static bool init = false;
    if (!init) {
        u8 len[288];
        for (int i = 0; i < 144; i++) len[i] = 8;
        for (int i = 144; i < 256; i++) len[i] = 9;
        for (int i = 256; i < 280; i++) len[i] = 7;
        for (int i = 280; i < 288; i++) len[i] = 8;
        h.build(len, 288);
        init = true;
    }
    return h;
}
const Huff& fixed_dist() {
    static Huff h; static bool init = false;
    if (!init) {
        u8 len[32];
        for (int i = 0; i < 32; i++) len[i] = 5;
        h.build(len, 32);
        init = true;
    }
    return h;
}

bool stored_block(BitReader& br, std::vector<u8>& out) {
    br.align_byte();
    u32 len = br.bits(16);
    u32 nlen = br.bits(16);
    if (br.err) return false;
    if ((len ^ 0xFFFFu) != nlen) return false;
    out.reserve(out.size() + len);
    for (u32 i = 0; i < len; i++) {
        out.push_back((u8)br.bits(8));
        if (br.err) return false;
    }
    return true;
}

bool coded_block(BitReader& br, std::vector<u8>& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int sym = lit.decode(br);
        if (br.err || sym < 0) return false;
        if (sym < 256) {
            out.push_back((u8)sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int length = kLenBase[sym] + (int)br.bits(kLenExtra[sym]);
            int dsym = dist.decode(br);
            if (br.err || dsym < 0 || dsym >= 30) return false;
            int distance = kDistBase[dsym] + (int)br.bits(kDistExtra[dsym]);
            if (distance <= 0 || (size_t)distance > out.size()) return false;
            size_t src = out.size() - (size_t)distance;
            out.reserve(out.size() + (size_t)length);
            for (int i = 0; i < length; i++) out.push_back(out[src + (size_t)i]);
        }
    }
}

bool dynamic_block(BitReader& br, std::vector<u8>& out) {
    u32 hlit = br.bits(5) + 257;
    u32 hdist = br.bits(5) + 1;
    u32 hclen = br.bits(4) + 4;
    if (br.err || hlit > 288 || hdist > 32) return false;

    static const u8 order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    u8 clLen[19] = { 0 };
    for (u32 i = 0; i < hclen; i++) clLen[order[i]] = (u8)br.bits(3);
    if (br.err) return false;

    Huff cl;
    if (!cl.build(clLen, 19)) return false;

    u8 lens[288 + 32] = { 0 };
    u32 total = hlit + hdist;
    u32 i = 0;
    while (i < total) {
        int sym = cl.decode(br);
        if (br.err || sym < 0) return false;
        if (sym < 16) {
            lens[i++] = (u8)sym;
        } else {
            int rep;
            u8 val = 0;
            if (sym == 16) {
                if (i == 0) return false;
                val = lens[i - 1];
                rep = 3 + (int)br.bits(2);
            } else if (sym == 17) {
                rep = 3 + (int)br.bits(3);
            } else {
                rep = 11 + (int)br.bits(7);
            }
            if (br.err || i + (u32)rep > total) return false;
            for (int k = 0; k < rep; k++) lens[i++] = val;
        }
    }

    Huff lit, dist;
    if (!lit.build(lens, (int)hlit)) return false;
    if (!dist.build(lens + hlit, (int)hdist)) return false;
    return coded_block(br, out, lit, dist);
}

} // namespace

bool inflate_raw(const u8* data, size_t size, std::vector<u8>& out, size_t reserveHint) {
    if (reserveHint) out.reserve(reserveHint);
    BitReader br(data, size);
    for (;;) {
        u32 bfinal = br.bits(1);
        u32 btype = br.bits(2);
        if (br.err) return false;
        bool ok;
        if (btype == 0) ok = stored_block(br, out);
        else if (btype == 1) ok = coded_block(br, out, fixed_lit(), fixed_dist());
        else if (btype == 2) ok = dynamic_block(br, out);
        else ok = false;
        if (!ok) return false;
        if (bfinal) break;
    }
    return true;
}

static u32 adler32_of(const u8* p, size_t n) {
    u32 a = 1, b = 0;
    const u32 MOD = 65521;
    size_t i = 0;
    while (i < n) {
        size_t chunk = std::min<size_t>(n - i, 5552);
        for (size_t k = 0; k < chunk; k++) {
            a += p[i + k];
            b += a;
        }
        a %= MOD;
        b %= MOD;
        i += chunk;
    }
    return (b << 16) | a;
}

bool zlib_inflate(const u8* data, size_t size, std::vector<u8>& out, std::string* err, size_t reserveHint) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (size < 6) return fail("zlib 数据过短");
    u8 cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8) return fail("不是 deflate 压缩");
    if (((cmf >> 4) & 0x0F) > 7) return fail("窗口过大");
    if ((((u32)cmf << 8) | flg) % 31u != 0) return fail("zlib 头校验失败");
    if (flg & 0x20) return fail("不支持预设字典");
    // 尾部 4 字节为 adler32，inflate 会在 deflate 流结束时自行停下
    if (!inflate_raw(data + 2, size - 2, out, reserveHint))
        return fail("DEFLATE 数据损坏");
    u32 stored = rd32be(data + size - 4);
    if (adler32_of(out.data(), out.size()) != stored) return fail("adler32 校验失败");
    return true;
}
