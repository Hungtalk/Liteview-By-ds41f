#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""构造带 EXIF(方向/相机信息)的 JPEG，验证 EXIF 解析与方向应用。"""
import os, struct, subprocess, sys
from PIL import Image, ImageOps, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'samples')
DEC = os.path.join(HERE, 'dec_main')

def exif_app1(orientation=6, make='LiteCam', model='LC-1'):
    # TIFF 小端结构: IFD0: Make(0x010F ASCII), Model(0x0110), Orientation(0x0112 SHORT)
    n_entries = 3
    data = bytearray()
    def str_entry(tag, s):
        b = s.encode() + b'\x00'
        off = 8 + 2 + 12 * n_entries + 4 + len(data)
        data.extend(b)
        if len(b) <= 4:
            val = b + b'\x00' * (4 - len(b))
            return struct.pack('<HHI', tag, 2, len(b)) + val
        return struct.pack('<HHII', tag, 2, len(b), off)
    ifd = bytearray(struct.pack('<H', n_entries))
    ifd += str_entry(0x010F, make)
    ifd += str_entry(0x0110, model)
    ifd += struct.pack('<HHIHH', 0x0112, 3, 1, orientation, 0)
    ifd += struct.pack('<I', 0)
    tiff_body = b'II*\x00' + struct.pack('<I', 8) + bytes(ifd) + bytes(data)
    payload = b'Exif\x00\x00' + tiff_body
    return b'\xFF\xE1' + struct.pack('>H', len(payload) + 2) + payload

def splice_app1(src_jpg, dst_jpg, app1):
    d = open(src_jpg, 'rb').read()
    assert d[:2] == b'\xFF\xD8'
    out = d[:2] + app1 + d[2:]
    open(dst_jpg, 'wb').write(out)

def main():
    base = os.path.join(OUT, 'j_rgb420_q85.jpg')
    if not os.path.exists(base):
        print('base jpeg missing, run gen_images.py first')
        return 1
    fail = 0
    for orient in (1, 6, 8):
        dst = os.path.join(OUT, f'e_orient{orient}.jpg')
        splice_app1(base, dst, exif_app1(orientation=orient))
        ImageOps.exif_transpose(Image.open(dst)).convert('RGBA').save(
            os.path.join(OUT, f'e_orient{orient}.expect.png'))
        r = subprocess.run([DEC, dst], capture_output=True, text=True)
        ok_info = ('orient=%d' % orient) in r.stdout and 'LiteCam' in r.stdout
        r2 = subprocess.run([DEC, dst, os.path.join(OUT, '0dump.raw'), '--oriented'],
                            capture_output=True, text=True)
        dims = r2.stdout.split()[2].split('x')
        w, h = int(dims[0]), int(dims[1])
        raw = open(os.path.join(OUT, '0dump.raw'), 'rb').read()
        got = Image.frombytes('RGBA', (w, h), raw)
        exp = Image.open(os.path.join(OUT, f'e_orient{orient}.expect.png')).convert('RGBA')
        ok_pix = (got.size == exp.size)
        ex = -1
        if ok_pix:
            d = ImageChops.difference(got, exp)
            ch = d.split()
            m = ImageChops.lighter(ImageChops.lighter(ch[0], ch[1]), ImageChops.lighter(ch[2], ch[3]))
            ex = m.getextrema()[1]
            ok_pix = ex <= 12
        print(f'orient{orient}: info={"ok" if ok_info else "FAIL"} pix={"ok" if ok_pix else "FAIL"} (maxdiff={ex})')
        if not (ok_info and ok_pix):
            fail += 1
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main())
