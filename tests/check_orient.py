#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""对照 orient_test 输出与 PIL 的 transpose 语义。"""
import glob, os, re, sys
from PIL import Image

src = Image.new('RGBA', (5, 3))
for y in range(3):
    for x in range(5):
        src.putpixel((x, y), (x * 40, y * 80, (x + y) * 20, 255))

ops = {
    1: None,
    2: Image.FLIP_LEFT_RIGHT,
    3: Image.ROTATE_180,
    4: Image.FLIP_TOP_BOTTOM,
    5: Image.TRANSPOSE,
    6: Image.ROTATE_270,
    7: Image.TRANSVERSE,
    8: Image.ROTATE_90,
}

fail = 0
for o, op in ops.items():
    exp = src if op is None else src.transpose(op)
    files = glob.glob('of/of_%d_*.raw' % o)
    if not files:
        print('orient %d: 缺少输出文件' % o)
        fail += 1
        continue
    m = re.search(r'_(\d+)x(\d+)\.raw$', files[0])
    w, h = int(m.group(1)), int(m.group(2))
    if (w, h) != exp.size:
        print('orient %d: 尺寸不符 %dx%d vs %s' % (o, w, h, exp.size))
        fail += 1
        continue
    got = Image.frombytes('RGBA', (w, h), open(files[0], 'rb').read())
    ok = list(got.getdata()) == list(exp.getdata())
    print('orient %d: %s' % (o, 'ok' if ok else 'FAIL'))
    if not ok:
        fail += 1

print('FAILED:', fail)
sys.exit(1 if fail else 0)
