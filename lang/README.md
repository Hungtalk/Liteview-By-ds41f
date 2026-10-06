# LiteView 语言包 / Language packs

LiteView 界面默认显示内置英文；其它语言通过本目录的 CSV 语言包提供。
默认按系统语言自动选择，也可在应用内切换（`Tab` → 设置 → 语言），
或命令行指定：`LiteView.exe --lang=zh-CN`。

## 格式

- 文件名即语言代码：`zh-CN.csv`、`zh-TW.csv`、`ja.csv`、`de.csv` …
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

## 使用

- 把 `<语言代码>.csv` 放到 `LiteView.exe` 同目录的 `lang\` 文件夹
  （或 `%LOCALAPPDATA%\LiteView\lang`）；
- 程序启动时按系统 UI 语言自动匹配（例如系统为简体中文 → `zh-CN.csv`），
  找不到对应语言包时回退内置英文。

## 制作新语言

1. 复制 [template.csv](template.csv) 并重命名为目标语言代码（如 `fr.csv`）；
2. 只填第三列 `translation`，不确定的条目留空即可（将显示英文）；
3. 用 UTF-8 保存；
4. 可用 `tests/i18n_tool check <你的文件>.csv` 检查格式与覆盖率
   （`i18n_tool` 由 `tests/run_all.sh` 编译生成，或参考该脚本手动编译）。

---

# Language packs (English)

The LiteView UI ships with embedded English; other languages are provided as
CSV packs in this folder. The default language follows the system UI language;
it can also be chosen in the app (`Tab` → Settings → Language) or forced with
`LiteView.exe --lang=zh-CN`.

- File name = language code (`zh-CN.csv`, `zh-TW.csv`, `ja.csv`, `de.csv`, …);
- UTF-8; one record per line; three columns `key,english,translation`:
  - `key` – entry id (do not change);
  - `english` – built-in English text (reference only, not read by the app);
  - `translation` – the translation; **leave empty to keep English**.
- `#` starts a comment; `# name: Your language` sets the display name shown in
  the settings panel.
- Wrap a field in double quotes if it contains commas, quotes or edge spaces;
  write a quote inside a field as `""`.
- `\n` = newline, `\t` = tab, `\\` = backslash; keep `%d` / `%s` / `%%` placeholders.
- Put packs into the `lang\` folder next to `LiteView.exe`
  (or `%LOCALAPPDATA%\LiteView\lang`).

Bundled packs: `zh-CN.csv` (Simplified Chinese) and `template.csv` (starter file).
