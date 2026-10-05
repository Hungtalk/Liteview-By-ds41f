#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成测试图片（含手工构造的特殊 PNG/BMP），并写出 .expect.png 参考图。

参考图规则:
  - 普通文件: 用 PIL 重新打开并转 RGBA 作为参考
  - 手工文件: 按解码器应有的输出直接构造参考
"""
import os, struct, zlib, random
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'samples')
os.makedirs(OUT, exist_ok=True)

def save_expect_png(path, w, h, rgba_bytes):
    Image.frombytes('RGBA', (w, h), rgba_bytes).save(path)

def pattern_rgba(w, h, alpha=None, seed=1):
    rnd = random.Random(seed)
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r = (x * 7 + y * 3) & 255
            g = (x * 13 + y * 5 + rnd.randrange(0, 16)) & 255
            b = (x * 29 + y * 11 + rnd.randrange(0, 16)) & 255
            a = 255 if alpha is None else alpha(x, y)
            data += bytes([r, g, b, a])
    return bytes(data)

def expect_from_image(img):
    return img.convert('RGBA').tobytes()

def norm_transparent(b):
    # alpha=0 的像素 RGB 无意义, 统一置 0 (与解码器约定一致)
    b = bytearray(b)
    for i in range(0, len(b), 4):
        if b[i + 3] == 0:
            b[i] = b[i + 1] = b[i + 2] = 0
    return bytes(b)

# ============================================================ 普通 PNG (PIL)
def gen_pil_png():
    w, h = 96, 64
    rgba = pattern_rgba(w, h)
    img = Image.frombytes('RGBA', (w, h), rgba)
    img.convert('RGB').save(f'{OUT}/p_rgb8.png')
    save_expect_png(f'{OUT}/p_rgb8.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_rgb8.png')))
    img.save(f'{OUT}/p_rgba8.png')
    save_expect_png(f'{OUT}/p_rgba8.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_rgba8.png')))

    # 调色板
    pal = Image.frombytes('RGBA', (w, h), pattern_rgba(w, h, seed=2)).convert('P', palette=Image.ADAPTIVE, colors=64)
    pal.save(f'{OUT}/p_pal8.png')
    save_expect_png(f'{OUT}/p_pal8.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_pal8.png')))

    # 调色板 + 透明
    pal2 = pal.copy()
    pal2.save(f'{OUT}/p_pal8_trns.png', transparency=bytes(range(0, 256, 4))[:len(pal2.getpalette())//3])
    save_expect_png(f'{OUT}/p_pal8_trns.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_pal8_trns.png')))

    # 灰度 / 1-bit
    gray = Image.frombytes('RGBA', (w, h), pattern_rgba(w, h, seed=3)).convert('L')
    gray.save(f'{OUT}/p_gray8.png')
    save_expect_png(f'{OUT}/p_gray8.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_gray8.png')))

    mono = gray.convert('1')
    mono.save(f'{OUT}/p_gray1.png')
    save_expect_png(f'{OUT}/p_gray1.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/p_gray1.png')))

    # 16 位灰度（PIL 保存为 16-bit PNG）
    vals = []
    rnd = random.Random(4)
    for i in range(w * h):
        vals.append(rnd.randrange(0, 65536))
    g16 = Image.new('I;16', (w, h))
    g16.putdata(vals)
    g16.save(f'{OUT}/p_gray16.png')
    exp = bytearray()
    for v in vals:
        g = (v * 255 + 32895) >> 16
        exp += bytes([g, g, g, 255])
    save_expect_png(f'{OUT}/p_gray16.expect.png', w, h, bytes(exp))

# ============================================================ 手工 PNG
def png_chunk(typ, data):
    return struct.pack('>I', len(data)) + typ + data + struct.pack('>I', zlib.crc32(typ + data) & 0xffffffff)

def write_png(path, w, h, bitdepth, colortype, rows, plte=None, trns=None, interlace=0, split=40):
    ihdr = struct.pack('>IIBBBBB', w, h, bitdepth, colortype, 0, 0, interlace)
    raw = b''.join(rows)
    idat = zlib.compress(raw, 9)
    data = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', ihdr)
    if plte is not None:
        data += png_chunk(b'PLTE', plte)
    if trns is not None:
        data += png_chunk(b'tRNS', trns)
    data += png_chunk(b'tEXt', b'Comment\x00hello')      # 未知块应被忽略
    for i in range(0, len(idat), split):
        data += png_chunk(b'IDAT', idat[i:i+split])
    data += png_chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(data)

def gen_manual_png():
    # --- 16bit RGB，非隔行 ---
    w, h = 33, 21
    rnd = random.Random(5)
    rows, exp = [], bytearray()
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            r = rnd.randrange(0, 65536); g = rnd.randrange(0, 65536); b = rnd.randrange(0, 65536)
            row += struct.pack('>HHH', r, g, b)
            exp += bytes([(r*255+32895)>>16, (g*255+32895)>>16, (b*255+32895)>>16, 255])
        rows.append(bytes(row))
    write_png(f'{OUT}/p_rgb16.png', w, h, 16, 2, rows)
    save_expect_png(f'{OUT}/p_rgb16.expect.png', w, h, bytes(exp))

    # --- RGBA8 隔行 (Adam7) ---
    w, h = 35, 27
    rgba = pattern_rgba(w, h, alpha=lambda x, y: (x * 8 + y * 3) & 255, seed=6)
    ix0 = [0,4,0,2,0,1,0]; iy0 = [0,0,4,0,2,0,1]
    ixs = [8,8,4,4,2,2,1]; iys = [8,8,8,4,4,2,2]
    rows = []
    for p in range(7):
        pw = (w - ix0[p] + ixs[p] - 1) // ixs[p]
        ph = (h - iy0[p] + iys[p] - 1) // iys[p]
        if pw <= 0 or ph <= 0: continue
        for y in range(ph):
            row = bytearray([0])
            for x in range(pw):
                sx = ix0[p] + x * ixs[p]; sy = iy0[p] + y * iys[p]
                o = (sy * w + sx) * 4
                row += rgba[o:o+4]
            rows.append(bytes(row))
    write_png(f'{OUT}/p_rgba_interlaced.png', w, h, 8, 6, rows, interlace=1)
    save_expect_png(f'{OUT}/p_rgba_interlaced.expect.png', w, h, rgba)

    # --- 灰度 1bit 宽度非 8 倍数 ---
    w, h = 13, 9
    rnd = random.Random(7)
    rows, exp = [], bytearray()
    for y in range(h):
        bits = [rnd.randrange(0, 2) for _ in range(w)]
        row = bytearray([0])
        byte = 0
        for i, b in enumerate(bits):
            byte |= b << (7 - (i % 8))
            if i % 8 == 7:
                row.append(byte); byte = 0
        if w % 8:
            row.append(byte)
        rows.append(bytes(row))
        for b in bits:
            v = 255 if b else 0
            exp += bytes([v, v, v, 255])
    write_png(f'{OUT}/p_gray1_w13.png', w, h, 1, 0, rows)
    save_expect_png(f'{OUT}/p_gray1_w13.expect.png', w, h, bytes(exp))

    # --- RGB8 + tRNS 透明色 ---
    w, h = 17, 11
    rows, exp = [], bytearray()
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            r, g, b = x * 16 % 256, y * 20 % 256, 128
            if x == 3 and y == 4: r, g, b = 1, 2, 3          # 透明色
            row += bytes([r, g, b])
            a = 0 if (x == 3 and y == 4) else 255
            exp += bytes([r, g, b, a])
        rows.append(bytes(row))
    write_png(f'{OUT}/p_rgb8_trns.png', w, h, 8, 2, rows, trns=struct.pack('>HHH', 1, 2, 3))
    save_expect_png(f'{OUT}/p_rgb8_trns.expect.png', w, h, bytes(exp))

    # --- 2bit 调色板, 隔行 ---
    w, h = 19, 15
    plte = bytes([255,0,0, 0,255,0, 0,0,255, 255,255,255])
    rnd = random.Random(8)
    src = [[rnd.randrange(0, 4) for _ in range(w)] for _ in range(h)]
    exps = bytearray()
    for y in range(h):
        for x in range(w):
            c = [(255,0,0),(0,255,0),(0,0,255),(255,255,255)][src[y][x]]
            exps += bytes([c[0], c[1], c[2], 255])
    rows = []
    for p in range(7):
        pw = (w - ix0[p] + ixs[p] - 1) // ixs[p]
        ph = (h - iy0[p] + iys[p] - 1) // iys[p]
        if pw <= 0 or ph <= 0: continue
        for y in range(ph):
            bits = []
            for x in range(pw):
                sx = ix0[p] + x * ixs[p]; sy = iy0[p] + y * iys[p]
                bits.append(src[sy][sx])
            row = bytearray([0])
            byte = 0
            for i, b in enumerate(bits):
                byte |= b << (6 - 2 * (i % 4))
                if i % 4 == 3:
                    row.append(byte); byte = 0
            if pw % 4:
                row.append(byte)
            rows.append(bytes(row))
    write_png(f'{OUT}/p_pal2_interlaced.png', w, h, 2, 3, rows, plte=plte, interlace=1)
    save_expect_png(f'{OUT}/p_pal2_interlaced.expect.png', w, h, bytes(exps))

# ============================================================ BMP
def bmp_header(w, h, bpp, comp, datasize, palcount=0, topdown=False):
    off = 14 + 40 + palcount * 4
    fh = b'BM' + struct.pack('<IHHI', off + datasize, 0, 0, off)
    ih = struct.pack('<IiiHHIIiiII', 40, w, -h if topdown else h, 1, bpp, comp, datasize, 2835, 2835, palcount, 0)
    return fh + ih

def gen_manual_bmp():
    rnd = random.Random(9)

    # 16bpp 555
    w, h = 23, 13
    rows, exp = [], bytearray()
    for y in range(h - 1, -1, -1):   # bottom-up
        row = bytearray()
        for x in range(w):
            r = rnd.randrange(0, 32); g = rnd.randrange(0, 32); b = rnd.randrange(0, 32)
            row += struct.pack('<H', (r << 10) | (g << 5) | b)
        if len(row) % 4: row += b'\x00' * (4 - len(row) % 4)
        rows.append(bytes(row))
    for y in range(h):
        for x in range(w):
            pass
    # 期望值
    exp = bytearray()
    for y in range(h):
        for x in range(w):
            v = struct.unpack('<H', rows[h - 1 - y][x*2:x*2+2])[0]
            r8 = ((((v >> 10) & 31) * 255 + 15) // 31)
            g8 = ((((v >> 5) & 31) * 255 + 15) // 31)
            b8 = (((v & 31) * 255 + 15) // 31)
            exp += bytes([r8, g8, b8, 255])
    data = b''.join(rows)
    with open(f'{OUT}/b_16bpp.bmp', 'wb') as f:
        f.write(bmp_header(w, h, 16, 0, len(data)) + data)
    save_expect_png(f'{OUT}/b_16bpp.expect.png', w, h, bytes(exp))

    # 32bpp BI_RGB, alpha 全 0 -> 应视为不透明
    w, h = 21, 17
    rows, exp = [], bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b = (x*11)&255, (y*17)&255, (x*y)&255
            row += bytes([b, g, r, 0])
        rows.append(bytes(row))
    for y in range(h):
        for x in range(w):
            r, g, b = (x*11)&255, (y*17)&255, (x*y)&255
            exp += bytes([r, g, b, 255])
    data = b''.join(rows)
    with open(f'{OUT}/b_32bpp_a0.bmp', 'wb') as f:
        f.write(bmp_header(w, h, 32, 0, len(data)) + data)
    save_expect_png(f'{OUT}/b_32bpp_a0.expect.png', w, h, bytes(exp))
    # 注: rows 为自下而上存储，期望值已按上面单独计算

    # 32bpp 含真实 alpha
    w, h = 21, 17
    rows, exp = [], bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = (x*11)&255, (y*17)&255, (x*y)&255, 100 + (x+y) % 156
            row += bytes([b, g, r, 0])   # 注意: 这里故意写 0, 用掩码版本测试真 alpha
        rows.append(bytes(row))
    data = b''.join(rows)
    # 用 BITFIELDS 版本 (comp=3) 写真实 alpha
    # 手工填掩码: R=0x00FF0000 G=0x0000FF00 B=0x000000FF A=0xFF000000
    fh_off = 14 + 40 + 16
    payload = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = (x*11)&255, (y*17)&255, (x*y)&255, 100 + (x+y) % 156
            payload += struct.pack('<I', (a << 24) | (r << 16) | (g << 8) | b)
    fh = b'BM' + struct.pack('<IHHI', fh_off + len(payload), 0, 0, fh_off)
    ih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 3, len(payload), 2835, 2835, 0, 0)
    masks = struct.pack('<IIII', 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    with open(f'{OUT}/b_32bpp_ba.bmp', 'wb') as f:
        f.write(fh + ih + masks + bytes(payload))
    for y in range(h):
        for x in range(w):
            r, g, b, a = (x*11)&255, (y*17)&255, (x*y)&255, 100 + (x+y) % 156
            exp += bytes([r, g, b, a])
    save_expect_png(f'{OUT}/b_32bpp_ba.expect.png', w, h, bytes(exp))

    # RLE8
    w, h = 20, 12
    src = [[(x * 3 + y * 7) % 256 for x in range(w)] for y in range(h)]
    exp = bytearray()
    for y in range(h):
        for x in range(w):
            v = src[y][x]
            exp += bytes([v, v, v, 255])

    def rle8_row(values):
        out = bytearray()
        x = 0
        ww = len(values)
        while x < ww:
            v = values[x]
            run = 1
            while x + run < ww and values[x + run] == v and run < 255:
                run += 1
            if run >= 3:
                out += bytes([run, v]); x += run
                continue
            seg = []
            while x < ww and len(seg) < 255:
                v2 = values[x]
                cnt = 1
                while x + cnt < ww and values[x + cnt] == v2 and cnt < 255:
                    cnt += 1
                if cnt >= 3:
                    break
                seg.append(v2); x += 1
            if len(seg) >= 3:
                out += bytes([0, len(seg)]) + bytes(seg)
                if len(seg) & 1:
                    out.append(0)
            else:
                for b in seg:
                    out += bytes([1, b])
        return out

    payload = bytearray()
    for y in range(h - 1, -1, -1):
        payload += rle8_row(src[y])
        payload += b'\x00\x00'
    payload += b'\x00\x01'
    pal = b''.join(bytes([i, i, i, 0]) for i in range(256))
    fh_off = 14 + 40 + 1024
    fh = b'BM' + struct.pack('<IHHI', fh_off + len(payload), 0, 0, fh_off)
    ih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 8, 1, len(payload), 2835, 2835, 256, 0)
    with open(f'{OUT}/b_rle8.bmp', 'wb') as f:
        f.write(fh + ih + pal + bytes(payload))
    save_expect_png(f'{OUT}/b_rle8.expect.png', w, h, bytes(exp))

    # RLE4 (全部用绝对模式编码, 验证半字节解包)
    w, h = 9, 5
    rnd4 = random.Random(11)
    nib = [[rnd4.randrange(0, 16) for _ in range(w)] for _ in range(h)]
    exp = bytearray()
    for y in range(h):
        for x in range(w):
            v = nib[y][x] * 17   # 调色板灰阶: n -> n*17
            exp += bytes([v, v, v, 255])
    payload = bytearray()
    for y in range(h - 1, -1, -1):
        vals = nib[y]
        payload += bytes([0, w])
        packed = bytearray()
        for i in range(0, w, 2):
            hi = vals[i]
            lo = vals[i + 1] if i + 1 < w else 0
            packed.append((hi << 4) | lo)
        payload += packed
        if len(packed) & 1:
            payload.append(0)
        payload += b'\x00\x00'
    payload += b'\x00\x01'
    pal = b''.join(bytes([min(i * 17, 255), min(i * 17, 255), min(i * 17, 255), 0]) for i in range(256))
    fh_off = 14 + 40 + 1024
    fh = b'BM' + struct.pack('<IHHI', fh_off + len(payload), 0, 0, fh_off)
    ih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 4, 2, len(payload), 2835, 2835, 256, 0)
    with open(f'{OUT}/b_rle4.bmp', 'wb') as f:
        f.write(fh + ih + pal + bytes(payload))
    save_expect_png(f'{OUT}/b_rle4.expect.png', w, h, bytes(exp))

    # 24bpp 顶朝下 (负高度)
    w, h = 18, 10
    exp = bytearray(); payload = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r, g, b = (x*20)&255, (y*25)&255, (x+y)&255
            row += bytes([b, g, r])
            exp += bytes([r, g, b, 255])
        if len(row) % 4: row += b'\x00' * (4 - len(row) % 4)
        payload += row
    fh_off = 14 + 40
    fh = b'BM' + struct.pack('<IHHI', fh_off + len(payload), 0, 0, fh_off)
    ih = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 24, 0, len(payload), 2835, 2835, 0, 0)
    with open(f'{OUT}/b_24_topdown.bmp', 'wb') as f:
        f.write(fh + ih + bytes(payload))
    save_expect_png(f'{OUT}/b_24_topdown.expect.png', w, h, bytes(exp))

# ============================================================ PIL BMP
def gen_pil_bmp():
    w, h = 64, 48
    img = Image.frombytes('RGBA', (w, h), pattern_rgba(w, h, seed=10))
    for mode, name in [('RGB', 'b_rgb24'), ('RGBA', 'b_rgba32')]:
        p = f'{OUT}/{name}.bmp'
        img.convert(mode).save(p)
        save_expect_png(f'{OUT}/{name}.expect.png', w, h, expect_from_image(Image.open(p)))
    p = f'{OUT}/b_pal8.bmp'
    img.convert('P', palette=Image.ADAPTIVE, colors=64).save(p)
    save_expect_png(f'{OUT}/b_pal8.expect.png', w, h, expect_from_image(Image.open(p)))
    p = f'{OUT}/b_pal4.bmp'
    img.convert('P', palette=Image.ADAPTIVE, colors=16).save(p)
    save_expect_png(f'{OUT}/b_pal4.expect.png', w, h, expect_from_image(Image.open(p)))
    p = f'{OUT}/b_mono1.bmp'
    img.convert('1').save(p)
    save_expect_png(f'{OUT}/b_mono1.expect.png', w, h, expect_from_image(Image.open(p)))

# ============================================================ JPEG
def gen_jpeg():
    w, h = 160, 120
    img = Image.new('RGB', (w, h))
    pix = img.load()
    rnd = random.Random(21)
    for y in range(h):
        for x in range(w):
            r = (x * 255) // w
            g = (y * 255) // h
            b = ((x + y) * 255) // (w + h)
            n = rnd.randrange(-14, 15)
            pix[x, y] = (max(0, min(255, r + n)), max(0, min(255, g - n)), max(0, min(255, b + n)))

    cases = [
        ('j_rgb444_q92.jpg', dict(quality=92, subsampling=0)),
        ('j_rgb422_q88.jpg', dict(quality=88, subsampling=1)),
        ('j_rgb420_q85.jpg', dict(quality=85, subsampling=2)),
        ('j_rgb411_q85.jpg', dict(quality=85, subsampling=(4, 1))),
        ('j_q20.jpg', dict(quality=20, subsampling=2)),
        ('j_prog420.jpg', dict(quality=85, subsampling=2, progressive=True)),
        ('j_prog444_q90.jpg', dict(quality=90, subsampling=0, progressive=True)),
    ]
    for name, opts in cases:
        try:
            img.save(f'{OUT}/{name}', **opts)
        except Exception as e:
            print('skip', name, e)
            continue
        save_expect_png(f'{OUT}/{name[:-4]}.expect.png', w, h,
                        expect_from_image(Image.open(f'{OUT}/{name}')))
        print('jpeg:', name)

    gray = img.convert('L')
    gray.save(f'{OUT}/j_gray_q90.jpg', quality=90)
    save_expect_png(f'{OUT}/j_gray_q90.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/j_gray_q90.jpg')))
    gray.save(f'{OUT}/j_gray_prog.jpg', quality=90, progressive=True)
    save_expect_png(f'{OUT}/j_gray_prog.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/j_gray_prog.jpg')))
    print('jpeg: gray cases')

    # 小画布放大成 4K（近邻采样保持离散值，测试大图路径）
    tile = Image.new('RGB', (240, 135))
    pt = tile.load()
    for y in range(135):
        for x in range(240):
            pt[x, y] = ((x * 7 + y * 3) & 255, (x * 3 + y * 11) & 255, (x * 13 + y * 5) & 255)
    big = tile.resize((3840, 2160), Image.NEAREST)
    big.save(f'{OUT}/j_4k.jpg', quality=80, subsampling=2)
    save_expect_png(f'{OUT}/j_4k.expect.png', 3840, 2160, expect_from_image(Image.open(f'{OUT}/j_4k.jpg')))
    big.save(f'{OUT}/p_4k.png')
    save_expect_png(f'{OUT}/p_4k.expect.png', 3840, 2160, expect_from_image(Image.open(f'{OUT}/p_4k.png')))
    # 4K 调色板 PNG（测试大数据量 + 调色板）
    p4 = big.convert('P', palette=Image.ADAPTIVE, colors=200)
    p4.save(f'{OUT}/p_4k_pal.png')
    save_expect_png(f'{OUT}/p_4k_pal.expect.png', 3840, 2160, expect_from_image(Image.open(f'{OUT}/p_4k_pal.png')))
    print('4k cases done')

def gen_real():
    import glob, shutil
    srcs = sorted(glob.glob('/upload/*.[jJ][pP][gG]') + glob.glob('/upload/*.[jJ][pP][eE][gG]') +
                  glob.glob('/upload/*.[pP][nN][gG]'))
    n = 0
    for s in srcs:
        if n >= 8:
            break
        try:
            im = Image.open(s)
            im.load()
            if im.mode in ('CMYK', 'YCbCr'):
                continue
        except Exception:
            continue
        base = os.path.splitext(os.path.basename(s))[0][-10:]
        ext = os.path.splitext(s)[1].lower()
        name = f'r{n:02d}_{base}{ext}'
        dst = f'{OUT}/{name}'
        shutil.copyfile(s, dst)
        save_expect_png(f'{dst[:-len(ext)]}.expect.png', im.size[0], im.size[1], expect_from_image(im))
        print('real:', name, im.mode, im.size)
        n += 1

def gen_gif():
    w, h = 80, 60
    img = Image.frombytes('RGBA', (w, h), pattern_rgba(w, h, seed=31)).convert('P', palette=Image.ADAPTIVE, colors=64)
    img.save(f'{OUT}/g_static.gif')
    save_expect_png(f'{OUT}/g_static.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/g_static.gif')))
    print('gif: static')

    img2 = img.copy()
    img2.save(f'{OUT}/g_trns.gif', transparency=5)
    save_expect_png(f'{OUT}/g_trns.expect.png', w, h, norm_transparent(expect_from_image(Image.open(f'{OUT}/g_trns.gif'))))
    print('gif: transparency')

    img.save(f'{OUT}/g_interlace.gif', interlace=True)
    save_expect_png(f'{OUT}/g_interlace.expect.png', w, h, expect_from_image(Image.open(f'{OUT}/g_interlace.gif')))
    print('gif: interlaced')

    # 动画（首帧为参考）
    frames = []
    for k in range(3):
        f = Image.frombytes('RGBA', (w, h), pattern_rgba(w, h, seed=40 + k)).convert('P', palette=Image.ADAPTIVE, colors=32)
        frames.append(f)
    frames[0].save(f'{OUT}/g_anim.gif', save_all=True, append_images=frames[1:], duration=200, loop=0)
    save_expect_png(f'{OUT}/g_anim.expect.png', w, h, norm_transparent(expect_from_image(Image.open(f'{OUT}/g_anim.gif'))))
    print('gif: animated')


if __name__ == '__main__':
    gen_pil_png()
    gen_manual_png()
    gen_manual_bmp()
    gen_pil_bmp()
    gen_jpeg()
    gen_gif()
    gen_real()
    print('images generated in', OUT)
