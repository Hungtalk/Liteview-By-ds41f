#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 res/app.ico（多尺寸）。依赖 Pillow。"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'res')
os.makedirs(OUT, exist_ok=True)
SZ = 256

img = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))

# 背景：圆角 + 蓝色渐变
grad = Image.new('RGBA', (SZ, SZ))
gd = ImageDraw.Draw(grad)
for y in range(SZ):
    t = y / (SZ - 1)
    gd.line([(0, y), (SZ, y)], fill=(int(26 + t * 26), int(104 + t * 62), int(176 + t * 58), 255))
mask = Image.new('L', (SZ, SZ), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, SZ - 1, SZ - 1], radius=48, fill=255)
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)
# 白色相框
d.rounded_rectangle([36, 46, SZ - 36, SZ - 42], radius=14, fill=(250, 250, 250, 255))
# 画面
d.rounded_rectangle([48, 58, SZ - 48, SZ - 54], radius=8, fill=(44, 122, 192, 255))
# 太阳
d.ellipse([158, 76, 196, 114], fill=(255, 214, 90, 255))
# 山
d.polygon([(48, SZ - 54), (110, 118), (170, SZ - 54)], fill=(70, 170, 126, 255))
d.polygon([(118, SZ - 54), (176, 148), (SZ - 48, SZ - 54)], fill=(46, 136, 102, 255))

ico = os.path.join(OUT, 'app.ico')
img.save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print('written', ico, os.path.getsize(ico), 'bytes')
