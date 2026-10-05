#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 djpeg -nosmooth 的输出作为参考，校验 JPEG 解码器（同口径：最近邻上采样）。
判定: 逐像素最大差 ≤ 8（IDCT 实现差异），无大面积超差。"""
import os, subprocess, sys
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.path.join(HERE, 'samples')
DEC = os.path.join(HERE, 'dec_main')

def main():
    pats = sys.argv[1:] or None
    files = [f for f in sorted(os.listdir(SAMPLES))
             if f.lower().endswith(('.jpg', '.jpeg')) and not f.endswith('.expect.png')]
    # 扩展名为 jpg 但内容其实是别的格式的，djpeg 会失败, 自动跳过
    fail = 0
    for fn in files:
        if pats and not any(fn.startswith(p) for p in pats):
            continue
        path = os.path.join(SAMPLES, fn)
        ref_ppm = os.path.join(SAMPLES, '0ref.ppm')
        r = subprocess.run(['djpeg', '-nosmooth', '-ppm', path], capture_output=True)
        if r.returncode != 0 or not r.stdout:
            print(f'skip {fn} (djpeg rc={r.returncode})')
            continue
        open(ref_ppm, 'wb').write(r.stdout)
        dump = os.path.join(SAMPLES, '0dump.raw')
        r2 = subprocess.run([DEC, path, dump], capture_output=True, text=True)
        if r2.returncode != 0:
            print(f'DECODE-FAIL {fn}: {r2.stdout.strip()}')
            fail += 1
            continue
        parts = r2.stdout.split()
        w, h = map(int, parts[2].split('x'))
        raw = open(dump, 'rb').read()
        got = Image.frombytes('RGBA', (w, h), raw)
        ref = Image.open(ref_ppm).convert('RGBA')
        if ref.size != got.size:
            print(f'SIZE-FAIL {fn}: ours {got.size} ref {ref.size}')
            fail += 1
            continue
        d = ImageChops.difference(got, ref)
        ch = d.split()
        m = ImageChops.lighter(ImageChops.lighter(ch[0], ch[1]), ImageChops.lighter(ch[2], ch[3]))
        ex = m.getextrema()[1]
        hist = m.histogram()
        n8 = sum(hist[9:])
        total = w * h
        ok = ex <= 8 and n8 == 0
        print(('ok  ' if ok else 'FAIL') + f' {fn} (maxdiff={ex}, >8: {n8}/{total})')
        if not ok:
            fail += 1
    print('----')
    print('FAILED:', fail)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main())
