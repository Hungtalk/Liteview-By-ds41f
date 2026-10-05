#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""运行 dec_main 解码样例图片，与 expect 对照。

判定:
  - PNG/BMP 无损: 必须逐像素完全一致（16bit 允许 ±2 舍入）
  - JPEG: 与 libjpeg 实现存在 ±舍入/上采样差异, 允许少量像素有较大差值,
          但整体不允许出现结构性错误（大面积超差）
"""
import os, subprocess, sys
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.path.join(HERE, 'samples')
DEC = os.path.join(HERE, 'dec_main')

def diff_stats(dump_path, w, h, exp_img):
    raw = open(dump_path, 'rb').read()
    if len(raw) != w * h * 4:
        return None
    a = Image.frombytes('RGBA', (w, h), raw)
    b = exp_img.convert('RGBA')
    if a.size != b.size:
        return None
    d = ImageChops.difference(a, b)
    ch = d.split()
    m = ImageChops.lighter(ImageChops.lighter(ch[0], ch[1]), ImageChops.lighter(ch[2], ch[3]))
    ex = m.getextrema()[1]
    hist = m.histogram()
    bad48 = sum(hist[49:])
    total = a.size[0] * a.size[1]
    return ex, bad48 / total

def main():
    pats = sys.argv[1:] or None
    files = []
    for fn in sorted(os.listdir(SAMPLES)):
        if fn.endswith('.expect.png'):
            continue
        if fn.startswith('e_orient'):
            continue        # EXIF 旋转专用, 由 make_exif_jpeg.py 单独校验
        if pats and not any(fn.startswith(p) for p in pats):
            continue
        files.append(fn)

    fail = 0
    for fn in files:
        path = os.path.join(SAMPLES, fn)
        exp_path = os.path.splitext(path)[0] + '.expect.png'
        if not os.path.exists(exp_path):
            continue
        dump = os.path.join(SAMPLES, '0dump.raw')
        r = subprocess.run([DEC, path, dump], capture_output=True, text=True)
        out = r.stdout.strip()
        if r.returncode != 0:
            print(f'DECODE-FAIL {fn}: {out} {r.stderr.strip()}')
            fail += 1
            continue
        parts = out.split()
        try:
            w, h = map(int, parts[2].split('x'))
        except Exception:
            print(f'ODD-OUTPUT {fn}: {out}')
            fail += 1
            continue
        exp = Image.open(exp_path)
        st = diff_stats(dump, w, h, exp)
        if st is None:
            print(f'SIZE-FAIL {fn} (decoded {w}x{h}, expect {exp.size})')
            fail += 1
            continue
        ex, frac = st
        fmt = ''
        for t in parts:
            if t.startswith('format='):
                fmt = t.split('=', 1)[1]
        isjpg = (fmt == 'JPEG') or fn.lower().endswith(('.jpg', '.jpeg'))
        if isjpg:
            ok = (ex <= 130) and (frac <= 0.015)
            tag = 'ok ' if ok else 'JPEG-FAIL'
            print(f'{tag} {fn} (maxdiff={ex}, frac>48={frac:.4%})')
        else:
            tol = 2 if '16' in fn else 0
            ok = ex <= tol
            tag = 'ok ' if ok else 'PIX-FAIL'
            print(f'{tag} {fn} (maxdiff={ex})')
        if not ok:
            fail += 1
    print('----')
    print('FAILED:', fail)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main())
