# LiteView — Lightweight Image Viewer (Windows / C++ / Pure Win32)

> 🌐 English · [简体中文](README.zh-CN.md)

A lightweight image viewer with a **self-contained viewing stack**: all image decoding (BMP / PNG / JPEG / GIF) is implemented from scratch, and the entire UI is custom-drawn with pure Win32 + GDI (owner-drawn toolbar, status bar, file browser panel, settings panel and info overlay). It **does not call** the Windows Photos app, Windows Photo Viewer, WIC / GDI+, .NET, Qt or any other component that may be stripped from trimmed Windows editions — everything inside the window is self-drawn, and even file selection uses a built-in panel instead of the system common dialog.

A Release build is a single executable of about **300 KB** (static runtime, no external DLLs) and runs on stripped-down Windows editions (LTSC / Server / debloated images).

---

## 1. Requirements Coverage

| # | Requirement | Implementation |
|---|-------------|----------------|
| 1 | Own viewing protocol, no system components | Hand-written BMP/PNG/JPEG/GIF decoders (including a from-scratch DEFLATE/zlib); fully self-drawn UI with no system imaging APIs; single-instance `WM_COPYDATA` protocol forwards "open image" requests to the running window; file association uses its own ProgID `LiteView.Image` (HKCU, no admin required) |
| 2 | Small and lightweight | No third-party libraries, no MFC/ATL, no bundled fonts; Release ≈ 300 KB (268 KB stripped in a MinGW cross-build), ready immediately on launch |
| 3 | File association | One-click "Register / Unregister" in the settings panel; `--register` / `--unregister` command-line flags; adds the app to the "Open with" list and points the default value at it (reversible, original values backed up automatically) |
| 4 | Basic viewing buttons | Toolbar: Open, Previous, Next, Zoom out, Zoom in, Fit to window, 1:1, Rotate left, Rotate right, Slideshow, Fullscreen, Settings (vector icons with hover tooltips) |
| 5 | Touch-friendly | `WM_GESTURE`: one-finger drag to pan / swipe to switch images, two-finger pinch to zoom (anchored at the gesture center), inertial flick to switch images; buttons and list rows scale with DPI for finger input |
| 6 | Custom settings | Background color, zoom mode on open, zoom interpolation (smooth / sharp), auto-rotate by EXIF, slideshow interval / loop, remember last folder, start maximized, always-on-top, loop browsing, reset; persisted to an INI file |
| 7 | Starts maximized | `SW_SHOWMAXIMIZED` by default (can be disabled in settings) |
| 8 | Swipe / button navigation | Buttons, arrow keys, mouse drag-swipe and touch swipe / flick are all supported; optional wrap-around at the ends |
| 9 | Pick an image | Self-drawn "Open image" panel: drive list / folders / image files with keyboard and mouse navigation (no system file dialog) |
| 10 | 1024×768 up to 3840×2160 | Fully adaptive layout (toolbar wraps automatically); 4K images use a mip chain + box resampling for smooth zooming and panning; the test corpus includes 3840×2160 JPEG/PNG cases |
| 11 | High-DPI aware | PerMonitorV2 manifest declaration (with PerMonitor / System fallbacks on older systems) plus a runtime API fallback; `WM_DPICHANGED` rebuilds fonts and layout dynamically |

---

## 2. Controls

### Keyboard shortcuts
| Key | Action |
|-----|--------|
| `← / →`, `PgUp / PgDn` | Previous / next image |
| `Home / End` | First / last image |
| `+ / -`, mouse wheel | Zoom around the cursor |
| `0` or `F` | Fit to window |
| `1` | Actual size (1:1) |
| Double-click image | Toggle between fit and 1:1 |
| `R` / `Shift+R` | Rotate clockwise / counter-clockwise |
| `Space` or `S` | Toggle slideshow |
| `I` | Info overlay (size, format, EXIF…) |
| `O` | Open image panel |
| `B` | Cycle background color |
| `Tab` | Settings panel |
| `F11` | Fullscreen |
| `Esc` | Close panel / overlay / leave fullscreen / stop slideshow |

### Mouse
- The wheel zooms toward the cursor; drag to pan while zoomed in; at fit scale, dragging left/right swipes to the previous / next image.
- Middle-button drag pans; right-click closes panels / stops the slideshow.

### Touch
- One finger: drag to pan while zoomed in; swipe left / right to switch images when the image fits (with flick inertia).
- Two fingers: pinch to zoom, anchored at the gesture center.

---

## 3. Building

### Option A — Visual Studio (recommended)
1. Open `LiteView.sln` with **Visual Studio 2026 Community** (or 2022).
2. If the IDE offers to retarget the toolset, just confirm (the project does not hard-code a PlatformToolset).
3. Build `Release | x64`; the output is `x64\Release\LiteView.exe` (pick `x86` for a 32-bit build).

> Project highlights: static runtime (`/MT`), `/utf-8`, `/W4`, SubSystem Windows; verified with VS2022 / VS2026 (v143 / v145 — accept the retarget prompt).
> The high-DPI manifest is merged through **Manifest Tool → Additional Manifest Files** (`res\LiteView.manifest`, PerMonitorV2 included);
> `LiteView.rc` only carries the icon and version info (only MinGW builds embed a manifest in the .rc, via `windres -DLITEVIEW_EMBED_RC_MANIFEST`),
> which eliminates duplicate-manifest CVT1100 / LNK1123 errors at the source.

### Option B — Command line (no IDE)
```bat
:: In a "VS Developer Command Prompt":
build_msvc.bat
```

### Option C — MinGW-w64 (alternative cross-build)
```bat
:: On Windows (MinGW-w64 on PATH):
build_mingw.bat

:: Or cross-compile on Linux / WSL:
./build_mingw.sh
```

> All build paths copy `lang\*.csv` next to the built `LiteView.exe` (MSVC: project post-build event; MinGW scripts: copy step), so language packs work out of the box.

---

## 4. File Association (Settings panel → File association)

- Clicking "not registered · click to register" will:
  - create `HKCU\Software\Classes\LiteView.Image` (with icon and `shell\open\command`);
  - add **OpenWithProgids** entries for `.jpg .jpeg .jpe .jfif .png .gif .bmp .dib`, plus `Applications\LiteView.exe` (so the system picker lists it);
  - back up the current extension defaults into `HKCU\Software\LiteView\Backup`, then point the HKCU defaults at LiteView (clicking "registered · click to unregister" fully rolls back).
- Or from the command line: `LiteView.exe --register` / `LiteView.exe --unregister`.
- Note: on Windows 10/11 the default app is guarded by the system UserChoice mechanism and cannot be changed silently by an application; if LiteView does not become the default right away, confirm it via **Right-click → Open with → Choose another app → LiteView → Always use**.

## 5. Settings Storage

`LiteView.ini` next to the executable is preferred (portable); if that folder is not writable it falls back to `%LOCALAPPDATA%\LiteView\LiteView.ini`. The effective path is shown at the bottom of the settings panel.

---

## 6. Languages (i18n)

- The UI ships with **embedded English**; the default language follows the **system UI language** at startup.
- Translations live in standalone CSV packs under the `lang\` folder next to `LiteView.exe` (or `%LOCALAPPDATA%\LiteView\lang`). A pack is matched automatically (`zh-CN` → `lang\zh-CN.csv`); when nothing matches, the app falls back to English.
- Bundled: [`lang/zh-CN.csv`](lang/zh-CN.csv) (Simplified Chinese); [`lang/template.csv`](lang/template.csv) is the starter file for new translations — the three-column format (`key,english,translation`) is documented in [`lang/README.md`](lang/README.md).
- To switch manually: **Settings → Language** (cycles through *Auto (follow system)* / installed packs / *English*), or launch with `LiteView.exe --lang=zh-CN`. The choice is stored as `[general] language` in `LiteView.ini`.
- Keep the formatting placeholders (`%d`, `%s`, `%%`) untouched in translations; `\n` = newline, `\t` = tab, `\\` = backslash. Empty cells keep the English text.

---

## 7. Repository Layout

```
LiteView/
├─ LiteView.sln / LiteView.vcxproj      Visual Studio project
├─ LiteView.rc                          icon / manifest / version resources
├─ build_msvc.bat, build_mingw.bat, build_mingw.sh
├─ res/  app.ico, LiteView.manifest
├─ src/
│   ├─ common.h  image.*  util.*        base types / image buffer / paths & INI
│   ├─ i18n.*                           localized text: embedded English + lang\*.csv packs
│   ├─ inflate.*                        from-scratch DEFLATE + zlib
│   ├─ codec.h  codec.cpp               format dispatch
│   ├─ codec_bmp.cpp / png / jpeg / gif hand-written decoders
│   ├─ render.*                         image document (mip chain) + scaling / compositing
│   ├─ settings.*                       user settings
│   ├─ assoc.*                          registry file association
│   ├─ viewer.*                         viewer window (layout / painting / input / gestures)
│   ├─ toolbar.cpp                      owner-drawn toolbar + vector icons
│   ├─ panels.cpp                       file panel + settings panel
│   └─ main.cpp                         entry point / single instance / drag & drop / CLI
├─ lang/  zh-CN.csv, template.csv, README.md
│                                      language packs (CSV) + translation guide
├─ tools/make_icon.py                   icon generator (Pillow)
└─ tests/                               decoder tests (Pillow + djpeg) + i18n pack checker
```

## 8. Decoders & Limitations

- **PNG**: 1/2/4/8/16-bit depths, grayscale / truecolor / palette / grayscale+alpha / RGBA, tRNS transparency, Adam7 interlacing, CRC validation.
- **JPEG**: baseline + progressive, grayscale / YCbCr (4:4:4 / 4:2:2 / 4:2:0 / 4:1:1), restart markers, EXIF (orientation / camera / exposure…); validated pixel-by-pixel against `djpeg -nosmooth` (max diff ≤ 3).
- **GIF**: 87a/89a, global / local palettes, transparency, interlacing; **animated GIFs show the first frame** (the total frame count is shown in the status bar).
- **BMP**: 1/4/8/16/24/32-bit, BI_RGB, BI_RLE4 / RLE8, BI_BITFIELDS (with alpha masks), top-down bitmaps.
- Not supported: CMYK/YCCK JPEG, 12-bit JPEG, WebP / TIFF / RAW (a clear error message is shown instead of a crash).
- Image size limit ≈ 64 megapixels; files larger than 2 GB are refused.

## 9. Tests (optional)

`tests/` contains decoder conformance tests (run in a Linux / Python environment):
```bash
./tests/run_all.sh
```
- `gen_images.py` generates a coverage corpus (hand-built interlaced PNGs, RLE4/8, 16-bit, transparent GIFs, 4K cases, …);
- `compare.py` compares pixel-by-pixel against Pillow (lossless formats: zero difference);
- `compare_djpeg.py` performs strict same-pipeline validation against `djpeg -nosmooth` (JPEG);
- `make_exif_jpeg.py` / `orient_test` validate EXIF parsing and all 8 orientation transforms.

## 10. FAQ

- **Build error CVT1100 "duplicate resource. type:MANIFEST" + LNK1123?**
  Two manifests are being embedded (one from `1 24 ...` in the .rc, one generated by VS). This repository avoids it by design (the .rc no longer embeds a manifest by default; the custom manifest is merged via Manifest Tool → Additional Manifest Files). If you use your own project, do either:
  ① Project Properties → Manifest Tool → Input and Output → **Embed Manifest = No**;
  ② remove the `1 24 ...` line from the .rc and reference `res\LiteView.manifest` under Manifest Tool → Additional Manifest Files.
- **Explorer icon not refreshed after registering the association?** Unregister once or restart Explorer, then re-register.
- **Why are there no system dialogs?** So the viewer still works on systems where the Photos app or common dialogs were removed; file picking, settings and info are all self-drawn panels.
- **The GIF does not animate?** This viewer is intentionally lightweight; only the first frame is displayed, and the frame count is shown in the status bar.
- **How do I add another language?** Copy `lang\template.csv` to `lang\<code>.csv` (e.g. `de.csv`), translate the third column (keep the placeholders), and save as UTF-8; the app picks it up on launch (Settings → Language switches immediately).

## 11. License

MIT — see [LICENSE.txt](LICENSE.txt).
