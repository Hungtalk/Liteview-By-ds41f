# LiteView 语言包 / Language packs

LiteView 界面默认显示内置英文；其它语言由本目录的 CSV 语言包提供。

**语言包在构建期被编译进 `LiteView.exe`**（见下文「内嵌机制」），所以发布时无需
附带 `lang\` 目录。默认按系统语言自动选择，也可在应用内切换
（`Tab` → 设置 → 语言），或命令行指定：`LiteView.exe --lang=ja-JP`。

## 格式

- 文件名即语言代码：`zh-CN.csv`、`ja-JP.csv`、`zh-TW.csv`、`de.csv` …
- UTF-8 编码（可带 BOM）；每行一条记录，三列：

  ```
  key,english,translation
  ```

  - `key`：条目 ID（不要修改）；
  - `english`：内置英文原文（仅作对照/翻译参考，程序不读取）；
  - `translation`：译文。**留空 = 使用内置英文**。
- 以 `#` 开头的行是注释；`# name: 你的语言名` 会显示在设置面板的语言行。
- 字段含逗号/引号/首尾空格时用双引号包裹；字段内的引号写成 `""`。
- 文本中的 `\n` 表示换行、`\t` 表示制表符、`\\` 表示反斜杠。
- 请保留 `%d`、`%s`、`%%` 等格式化占位符。

## 内嵌机制（单文件发布）

```
lang/*.csv  ──tools/embed_lang.py──▶  src/lang_packs_generated.cpp  ──编译──▶  LiteView.exe
```

- 生成的是**原始 UTF-8 字节数组**，不做字符集转换，因此中日文等文本不会受编译器
  执行字符集影响（这也是不把语言包写成 `.rc` 资源的原因：`rc.exe` 的代码页处理会踩坑）。
- `src/lang_packs_generated.cpp` 已随仓库提交，**克隆后可直接构建**；改过 `lang/*.csv`
  后需要重新生成：

  ```bat
  python tools\embed_lang.py            :: 重新生成
  python tools\embed_lang.py --check    :: 只校验（缺 key / 未知 key / 占位符不一致 → 非零退出）
  ```

- 没有任何语言包时（没有 Python 或 `lang\` 为空），构建会改用
  [`src/lang_packs_stub.cpp`](../src/lang_packs_stub.cpp) 占位实现，程序仍可运行
  —— 此时只有内置英文 + 外置语言包。

### 语言包优先级（高 → 低）

1. `LiteView.exe` 同目录 `lang\<代码>.csv`
2. `%LOCALAPPDATA%\LiteView\lang\<代码>.csv`
3. **内嵌语言包**（本目录的 CSV 在构建时编入 exe）
4. 内置英文（`src/i18n.h` 的 `LITEVIEW_STRINGS` 表）

因此把同名 CSV 放到 exe 旁边即可**不重新编译**替换内嵌译文。

### 语言代码匹配

按 精确 → 主语言码 的顺序匹配，且**双向**回退：

| 系统/请求 | 命中 | 说明 |
|---|---|---|
| `zh-CN` | `zh-CN` | 精确 |
| `zh-CN` | `zh` | 请求带地区码时退到主语言码 |
| `ja` | `ja-JP` | 请求只有主语言码时向上匹配带地区码的包（Windows 上 `LOCALE_SISO639LANGNAME` 只返回 `ja`） |
| `xx-YY` | 内置英文 | 无匹配时回退 |

## 使用

- 什么都不用做：内置语言（简体中文、日语）随 exe 一起工作；
- 需要覆盖/新增时，把 `<语言代码>.csv` 放到 `LiteView.exe` 同目录的 `lang\`
  （或 `%LOCALAPPDATA%\LiteView\lang`）。

## 制作新语言

1. 复制 [template.csv](template.csv) 并重命名为目标语言代码（如 `fr.csv`）；
2. 只填第三列 `translation`，不确定的条目留空即可（将显示英文）；
3. 用 UTF-8 保存；
4. 用 `tests/i18n_tool check <你的文件>.csv --require-all` 检查格式与覆盖率
   （`i18n_tool` 由 `tests/run_all.sh` 编译生成，或参考该脚本手动编译）；
5. 想并入内置语言集合：`python tools\embed_lang.py` 后重新编译。

## 现有语言包

| 文件 | 语言 | 覆盖率 |
|---|---|---|
| [zh-CN.csv](zh-CN.csv) | 中文（简体） | 137/137 |
| [ja-JP.csv](ja-JP.csv) | 日本語 | 137/137 |
| [template.csv](template.csv) | 翻译模板（不作为语言列出） | — |

---

# Language packs (English)

The LiteView UI ships with embedded English; other languages come from CSV packs
in this folder, which are **compiled into the executable at build time** — no
`lang\` folder has to be shipped. The default language follows the system UI
language; it can also be chosen in the app (`Tab` → Settings → Language) or
forced with `LiteView.exe --lang=ja-JP`.

- File name = language code (`zh-CN.csv`, `ja-JP.csv`, `zh-TW.csv`, `de.csv`, …);
- UTF-8; one record per line; three columns `key,english,translation`:
  - `key` – entry id (do not change);
  - `english` – built-in English text (reference only, not read by the app);
  - `translation` – the translation; **leave empty to keep English**.
- `#` starts a comment; `# name: Your language` sets the display name shown in
  the settings panel.
- Wrap a field in double quotes if it contains commas, quotes or edge spaces;
  write a quote inside a field as `""`.
- `\n` = newline, `\t` = tab, `\\` = backslash; keep `%d` / `%s` / `%%` placeholders.

## How embedding works

`tools/embed_lang.py` turns every `lang/*.csv` into a raw UTF-8 byte array in
`src/lang_packs_generated.cpp` (committed to the repository), which is compiled
into the exe. Regenerate it after editing a pack:

```bat
python tools\embed_lang.py            :: regenerate
python tools\embed_lang.py --check    :: validate only (non-zero exit on problems)
```

Priority (highest first): `lang\` next to the exe → `%LOCALAPPDATA%\LiteView\lang`
→ embedded packs → built-in English. So dropping a same-named CSV next to the exe
overrides the embedded translation without a rebuild.

Language codes fall back in both directions: a request for `ja` matches
`ja-JP` (Japanese Windows reports the bare ISO code `ja`), and `zh-CN` matches a
`zh` pack.

Bundled packs: `zh-CN.csv` (Simplified Chinese), `ja-JP.csv` (Japanese) and
`template.csv` (starter file for new translations).
