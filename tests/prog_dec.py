#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按 libjpeg 算法完整解码渐进式 JPEG（仅解码熵数据，不重建图像），
用于定位扫描边界与解码位置。"""
import sys

def u16(d,o): return (d[o]<<8)|d[o+1]

class BR:
    def __init__(self, data, start):
        self.d = data; self.p = start; self.buf = 0; self.cnt = 0; self.hit = False
    def get(self, k):
        while self.cnt < k:
            b = self.d[self.p]; self.p += 1
            if b == 0xFF:
                b2 = self.d[self.p]
                if b2 == 0: self.p += 1
                else:
                    self.hit = True; self.marker = b2; return 0
            self.buf = ((self.buf << 8) | b) & 0xFFFFFF
            self.cnt += 8
        v = (self.buf >> (self.cnt - k)) & ((1 << k) - 1)
        self.cnt -= k
        return v
    def bitpos(self):
        return (self.p, self.cnt)

class Huff:
    def __init__(self, counts, syms):
        self.counts = counts; self.syms = syms
        # 规范表: codes[(len,code)] = sym
        self.codes = {}
        code = 0; k = 0
        for l in range(1,17):
            for _ in range(counts[l-1]):
                self.codes[(l, code)] = syms[k]; k += 1; code += 1
            code <<= 1
    def dec(self, br):
        code = 0
        for l in range(1,17):
            code = (code<<1) | br.get(1)
            if br.hit: return None
            if (l, code) in self.codes:
                return self.codes[(l, code)]
        return None

data = open(sys.argv[1] if len(sys.argv)>1 else 'samples/j_prog420.jpg','rb').read()
n = len(data)

# ---- 解析 ----
qt = {}; comps = []; W = H = 0; hmax = vmax = 1
dht = {}; scans = []; restart = 0
pos = 2
while pos+1 < n:
    while data[pos] != 0xFF: pos += 1
    while data[pos] == 0xFF: pos += 1
    m = data[pos]; pos += 1
    if m == 0xD8 or 0xD0 <= m <= 0xD7 or m == 0x01: continue
    if m == 0xD9: break
    L = u16(data,pos)
    if m == 0xDB:
        q = pos+2; end = pos+L
        while q < end:
            pq = data[q]>>4; tq = data[q]&15; q+=1
            tbl = []
            for i in range(64):
                if pq: tbl.append(u16(data,q)); q+=2
                else: tbl.append(data[q]); q+=1
            qt[tq] = tbl
    elif m == 0xC2:
        H = u16(data,pos+3); W = u16(data,pos+5); nc = data[pos+7]
        q = pos+8
        for i in range(nc):
            cid, hv, tq = data[q], data[q+1], data[q+2]; q+=3
            comps.append({'id':cid,'hs':hv>>4,'vs':hv&15,'tq':tq})
            hmax = max(hmax, hv>>4); vmax = max(vmax, hv&15)
    elif m == 0xC4:
        q = pos+2; end = pos+L
        while q+17 <= end:
            tc = data[q]>>4; th = data[q]&15; q+=1
            counts = list(data[q:q+16]); q+=16
            total = sum(counts); syms = list(data[q:q+total]); q+=total
            dht[(tc,th)] = Huff(counts, syms)
    elif m == 0xDD:
        restart = u16(data,pos+2)
        pos += L; continue
    elif m == 0xDA:
        ns = data[pos+2]; q = pos+3; sc = []
        for i in range(ns):
            cid, tdta = data[q], data[q+1]; q += 2
            sc.append((cid, tdta>>4, tdta&15))
        ss, se, ahal = data[q], data[q+1], data[q+2]
        estart = q+3
        e = estart
        while e+1 < n:
            if data[e] == 0xFF:
                if data[e+1] == 0 or 0xD0 <= data[e+1] <= 0xD7: e += 2; continue
                break
            e += 1
        scans.append((sc, ss, se, ahal>>4, ahal&15, estart, e))
        pos = e
        continue
    pos += L

# block 网格（系数数组按 MCU 边界补齐；非交错扫描按未补齐尺寸遍历）
mcuX = (W + 8*hmax - 1)//(8*hmax)
mcuY = (H + 8*vmax - 1)//(8*vmax)
for c in comps:
    cw = (W*c['hs'] + hmax - 1)//hmax
    ch = (H*c['vs'] + vmax - 1)//vmax
    c['bw'] = (cw+7)//8; c['bh'] = (ch+7)//8
    c['bwP'] = mcuX * c['hs']; c['bhP'] = mcuY * c['vs']
    c['coef'] = [[0]*64 for _ in range(c['bwP']*c['bhP'])]
    c['pred'] = 0
ZZ = [0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63]

ext = lambda v,s: v - ((1<<s)-1) if v < (1<<(s-1)) else v

def decode_scan(sc, ss, se, ah, al, estart):
    br = BR(data, estart)
    eobrun = 0
    for c in comps: c['pred'] = 0
    listc = []
    for (cid, td, ta) in sc:
        c = next(x for x in comps if x['id']==cid)
        c['td'] = td; c['ta'] = ta; listc.append(c)
    def block(c, bx, by): return c['coef'][by*c['bwP']+bx]
    def refine(cf, k, p1):
        if cf[k]:
            if br.get(1) and not (cf[k] & p1):
                cf[k] += p1 if cf[k] > 0 else -p1
        return True
    def dec_block(c, blk):
        nonlocal eobrun
        if ss == 0:
            if ah == 0:
                t = dht[(0,c['td'])].dec(br)
                if t is None: return False
                diff = ext(br.get(t), t) if t else 0
                c['pred'] += diff
                blk[0] = c['pred'] << al
            else:
                bit = br.get(1)
                blk[0] |= bit << al
        if se > 0:
            tbl = dht[(1,c['ta'])]
            p1 = 1 << al
            k = max(ss,1)
            if ah == 0:
                if eobrun > 0: eobrun -= 1; return True
                while k <= se:
                    rs = tbl.dec(br)
                    if rs is None: return False
                    r = rs>>4; s = rs&15
                    if s == 0:
                        if r < 15:
                            eobrun = (1<<r)-1
                            if r: eobrun += br.get(r)
                            return True
                        k += 16
                        continue
                    k += r
                    if k > se: return False
                    blk[ZZ[k]] = ext(br.get(s), s) << al
                    k += 1
                return True
            else:
                # 借 libjpeg 逻辑
                def refine_step(kk):
                    cf = blk[ZZ[kk]]
                    if cf:
                        if br.get(1) and not (cf & p1):
                            blk[ZZ[kk]] = cf + (p1 if cf > 0 else -p1)
                if eobrun == 0:
                    while k <= se:
                        rs = tbl.dec(br)
                        if rs is None: return False
                        r = rs>>4; s = rs&15
                        if s:
                            s = p1 if br.get(1) else -p1
                        else:
                            if r != 15:
                                eobrun = (1<<r)
                                if r: eobrun += br.get(r)
                                break
                        # advance
                        while True:
                            if blk[ZZ[k]] != 0:
                                refine_step(k)
                            else:
                                r -= 1
                                if r < 0: break
                            k += 1
                            if k > se: break
                        if s != 0 and k <= se:
                            blk[ZZ[k]] = s
                        k += 1
                if eobrun > 0:
                    # 补修正位: 从当前 k 到 se （k 保持当前值）
                    while k <= se:
                        refine_step(k); k += 1
                    eobrun -= 1
                return True
        return True

    # 逐 MCU
    import os
    vb = os.environ.get('VERBOSE', '')
    if len(listc) == 1:
        c = listc[0]
        n = 0
        for by in range(c['bh']):
            for bx in range(c['bw']):
                if vb == str(cur_scan[0]):
                    print(f'  blk{n} ({bx},{by}) pos={br.p} cnt={br.cnt}')
                n += 1
                if not dec_block(c, block(c,bx,by)): return False, br, c, bx, by
    else:
        for my in range(mcuY):
            for mx in range(mcuX):
                for c in listc:
                    for v in range(c['vs']):
                        for u in range(c['hs']):
                            if not dec_block(c, block(c, mx*c['hs']+u, my*c['vs']+v)):
                                return False, br, c, mx*c['hs']+u, my*c['vs']+v
    return True, br, None, -1, -1

ok_all = True
cur_scan = [0]
for i,(sc,ss,se,ah,al,estart,e) in enumerate(scans):
    cur_scan[0] = i
    ok, br, c, bx, by = decode_scan(sc, ss, se, ah, al, estart)
    endpos = br.p
    status = 'ok' if (ok and endpos==e) else ('END-POS-OFF' if ok else 'DECODE-FAIL')
    if status != 'ok': ok_all = False
    print(f'scan{i}: comps={[x[0] for x in sc]} ss={ss} se={se} ah={ah} al={al} '
          f'ent=[{estart},{e}) our_end={endpos} {status}' + (f' fail@blk{c["id"]}({bx},{by})' if not ok else ''))
print('ALL OK' if ok_all else 'HAS FAILURE')
