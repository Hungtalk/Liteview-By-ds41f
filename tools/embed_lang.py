#!/usr/bin/env python3
# ============================================================================
#  embed_lang.py - 把 lang/*.csv 语言包编译进 LiteView 单一可执行文件
#
#  生成 src/lang_packs_generated.cpp：
#    * 每个语言包 = 一个 const unsigned char[]（原始 UTF-8 字节，不做任何转换，
#      因此中日文等非 ASCII 文本不会受编译器执行字符集影响）；
#    * 一张 lv_embedded_packs[] 表 + 生成信息（包数、条数、语言代码）。
#
#  生成的 .cpp 可脱离本脚本独立编译（不需要 CMake / Python 才能构建）。
#  Python 不可用时构建脚本会跳过本步骤，此时程序仍可用内置英文，
#  以及 exe 同目录 lang\ 下的外置语言包。
#
#  用法:
#    python tools/embed_lang.py [--lang-dir lang] [--out src/lang_packs_generated.cpp]
#                               [--check]
#    --check : 只校验 lang/*.csv 是否与 src/i18n.h 的 key 表一致，不写文件
#              （缺 key / 未知 key / 占位符数量不一致 → 非零退出，供 CI 使用）
# ============================================================================
import argparse
import os
import re
import sys

# 语言包显示名含非 ASCII（日本語 / 中文（简体）…），而 Windows 控制台默认代码页
# 可能是 cp1252/cp936，直接 print 会抛 UnicodeEncodeError 并中断构建。
# 这里在脚本级把标准输出切到 UTF-8 且不可编码字符降级为转义，保证构建不因
# “打印日志” 失败。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding='utf-8', errors='backslashreplace')
    except (AttributeError, ValueError):
        pass

# LITEVIEW_STRINGS 中的 X(key, L"English") 行
X_RE = re.compile(r'^\s*X\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,', re.MULTILINE)
# 非 ASCII 且非空白的字符（用于 warning）
PLACEHOLDER_RE = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?[diuoxXfFeEgGcs])')

# 不作为内置语言包的文件
SKIP_STEMS = {'template'}


def read_text(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xef\xbb\xbf'):
        raw = raw[3:]
    return raw


def parse_keys(i18n_h):
    with open(i18n_h, 'r', encoding='utf-8') as f:
        text = f.read()
    # 只取宏定义体（第一个 #define LITEVIEW_STRINGS 之后到 X(...) 结束）
    start = text.find('#define LITEVIEW_STRINGS')
    if start < 0:
        raise SystemExit('cannot find LITEVIEW_STRINGS in %s' % i18n_h)
    body = text[start:]
    keys = X_RE.findall(body)
    if not keys:
        raise SystemExit('no keys parsed from %s' % i18n_h)
    # 去重保序（宏里 id 唯一）
    seen, out = set(), []
    for k in keys:
        if k not in seen:
            seen.add(k)
            out.append(k)
    return out


def csv_unescape(s):
    """与 i18n.cpp 的 unescape_text 一致：仅处理 \\n \\t \\\\。"""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            if n == 'n':
                out.append('\n'); i += 2; continue
            if n == 't':
                out.append('\t'); i += 2; continue
            if n == '\\':
                out.append('\\'); i += 2; continue
        out.append(c); i += 1
    return ''.join(out)


def parse_record(line):
    """与 i18n.cpp 的 parse_record 一致：双引号字段、"" 转义。"""
    fields, cur, in_quote = [], [], False
    i = 0
    while i < len(line):
        c = line[i]
        if in_quote:
            if c == '"':
                if i + 1 < len(line) and line[i + 1] == '"':
                    cur.append('"'); i += 2; continue
                in_quote = False
            else:
                cur.append(c)
        elif c == '"':
            in_quote = True
        elif c == ',':
            fields.append(''.join(cur)); cur = []
        else:
            cur.append(c)
        i += 1
    fields.append(''.join(cur))
    return fields


def parse_pack(raw, path, expect_keys):
    """解析语言包 → (name, {key: translation}, unknown_keys, missing_keys, problems)"""
    text = raw.decode('utf-8', errors='replace')
    name = None
    trans = {}
    unknown = []
    problems = []
    saw_data = False
    for lineno, line in enumerate(text.split('\n'), 1):
        if line.endswith('\r'):
            line = line[:-1]
        if not line:
            continue
        if line.startswith('#'):
            m = re.match(r'#\s*name\s*[:=]\s*(.+?)\s*$', line)
            if m:
                name = m.group(1)
            continue
        f = parse_record(line)
        key = (f[0] if f else '').strip(' \t\r')
        if not key:
            continue
        if not saw_data:
            saw_data = True
            if key in ('key', 'id'):
                continue
        if len(f) >= 3:
            val = ','.join(f[2:])
        elif len(f) == 2:
            val = f[1]
        else:
            val = ''
        val = csv_unescape(val)
        if val.strip(' \t') == '':
            continue
        if key not in expect_keys:
            unknown.append((lineno, key))
            continue
        trans[key] = val
    missing = [k for k in expect_keys if k not in trans]
    return name, trans, unknown, missing, problems


def check_placeholders(key, english, translation, expect_index):
    """占位符数量/种类一致性检查。english 取自 i18n.h。"""
    a = sorted(PLACEHOLDER_RE.findall(english))
    b = sorted(PLACEHOLDER_RE.findall(translation))
    if a != b:
        return 'placeholder mismatch: english=%s translation=%s' % (a, b)
    return None


def collect(lang_dir, keys):
    packs = []
    for fn in sorted(os.listdir(lang_dir)):
        if not fn.lower().endswith('.csv'):
            continue
        stem = fn[:-4]
        if stem.lower() in SKIP_STEMS:
            continue
        path = os.path.join(lang_dir, fn)
        raw = read_text(path)
        packs.append((stem, path, raw))
    return packs


def generate(packs, keys, out_path, i18n_h):
    lines = []
    w = lines.append
    w('// ============================================================================')
    w('//  lang_packs_generated.cpp  -  GENERATED FILE, DO NOT EDIT')
    w('//')
    w('//  由 tools/embed_lang.py 从 lang/*.csv 生成：把语言包以原始 UTF-8 字节')
    w('//  内嵌进可执行文件，使 LiteView 无需附带 lang\\ 目录即可显示多语言。')
    w('//')
    w('//  重新生成:  python tools/embed_lang.py')
    w('// ============================================================================')
    w('#include "i18n_embedded.h"')
    w('')
    w('#if LITEVIEW_EMBEDDED_PACKS')
    w('')
    w('#include <cstddef>')
    w('')
    w('namespace {')
    w('')
    for stem, path, raw in packs:
        sym = 'kPack_' + re.sub(r'[^A-Za-z0-9_]', '_', stem)
        w('// %s  (%d bytes, source: %s)' % (stem, len(raw), os.path.basename(path)))
        w('const unsigned char %s[] = {' % sym)
        for i in range(0, len(raw), 16):
            chunk = raw[i:i + 16]
            w('    ' + ' '.join('0x%02X,' % b for b in chunk))
        w('};')
        w('')
    w('const LvEmbeddedPack kPacks[] = {')
    for stem, path, raw in packs:
        sym = 'kPack_' + re.sub(r'[^A-Za-z0-9_]', '_', stem)
        w('    { L"%s", %s, sizeof(%s) },' % (stem, sym, sym))
    w('};')
    w('')
    w('} // namespace')
    w('')
    w('const LvEmbeddedPack* lv_embedded_packs(int* count) {')
    w('    if (count) *count = (int)(sizeof(kPacks) / sizeof(kPacks[0]));')
    w('    return kPacks;')
    w('}')
    w('')
    w('#else  // !LITEVIEW_EMBEDDED_PACKS —— 需要编译 src/lang_packs_stub.cpp')
    w('')
    w('#error "LITEVIEW_EMBEDDED_PACKS=0 时应改为编译 src/lang_packs_stub.cpp"')
    w('')
    w('#endif')
    w('')
    w('// generator: tools/embed_lang.py | packs: %d | keys per pack: %d' % (len(packs), len(keys)))
    w('')

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    return sum(len(r) for _, _, r in packs)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('--lang-dir', default=os.path.join(root, 'lang'))
    ap.add_argument('--out', default=os.path.join(root, 'src', 'lang_packs_generated.cpp'))
    ap.add_argument('--i18n-h', default=os.path.join(root, 'src', 'i18n.h'))
    ap.add_argument('--check', action='store_true',
                    help='只校验语言包与 key 表的一致性，不生成文件')
    args = ap.parse_args()

    keys = parse_keys(args.i18n_h)
    index = {k: i for i, k in enumerate(keys)}
    if not os.path.isdir(args.lang_dir):
        print('embed_lang: no language directory %s — nothing embedded' % args.lang_dir)
        return 0

    packs = collect(args.lang_dir, keys)
    if not packs:
        print('embed_lang: no *.csv in %s — nothing embedded' % args.lang_dir)
        return 0

    # 英文原文（用于占位符校验）
    with open(args.i18n_h, 'r', encoding='utf-8') as f:
        htext = f.read()
    english = {}
    for m in re.finditer(r'^\s*X\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*L"((?:[^"\\]|\\.)*)"\s*\)', htext, re.MULTILINE):
        english[m.group(1)] = m.group(2)

    rc = 0
    print('embed_lang: %d keys in table, %d pack(s)' % (len(keys), len(packs)))
    for stem, path, raw in packs:
        name, trans, unknown, missing, _ = parse_pack(raw, path, index)
        ph_bad = []
        for k, v in trans.items():
            en = english.get(k, '')
            msg = check_placeholders(k, en, v, index.get(k, 0))
            if msg:
                ph_bad.append('%s: %s' % (k, msg))
        pct = 100.0 * len(trans) / len(keys) if keys else 0.0
        print('  %-8s name=%-16s translated %3d/%d (%5.1f%%)  unknown=%d missing=%d phbad=%d'
              % (stem, name or '-', len(trans), len(keys), pct, len(unknown), len(missing), len(ph_bad)))
        for ln, k in unknown[:20]:
            print('      unknown key (line %d): %s' % (ln, k)); rc = 1
        if missing[:20]:
            for k in missing[:20]:
                print('      missing key: %s' % k)
        for m in ph_bad[:20]:
            print('      %s' % m); rc = 1

    if args.check:
        return rc

    total = generate(packs, keys, args.out, args.i18n_h)
    print('embed_lang: wrote %s (%d bytes of pack data)' % (args.out, total))
    return rc


if __name__ == '__main__':
    sys.exit(main())
