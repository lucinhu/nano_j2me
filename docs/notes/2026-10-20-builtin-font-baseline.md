# Builtin CJK weight and mixed-script baselines

The builtin Chinese face now uses Source Han Sans CN **Regular**, with one shared raster baseline per face. Latin line heights, MIDP baselines and character advances are unchanged.

## Root causes

The previous generator measured only the Han ideograph envelope, then individually shrank and vertically centred glyphs that did not fit that window. Fullwidth Latin descenders, Greek/Cyrillic letters, accented letters and box rules could consequently have inconsistent sizes and vertical positions.

SMALL and MEDIUM also share the same CJK bitmap, while their Latin ascents differ (10 versus 12). The renderer positioned that shared bitmap using the requested Latin ascent, shifting the same Chinese cell relative to Latin between sizes.

## Changes and cost

- Render the same 11/15 px em sizes using Regular instead of Light. Keep 12/16 px advances and all 7445 GB2312 code points.
- Store 12x16 and 16x20 cells with explicit provider baseline rows 12 and 16. Extra storage preserves accents and descenders without per-character shrinking or clipping. Generation fails if any ink falls outside the shared window.
- Append `nj_font.glyph_ascent`: zero preserves the existing provider contract; a positive value locates the baseline within provider cells. Resolve this metric with the glyph, including fallback providers. Fixed Latin bitmaps retain the descriptor ascent.
- Reset the copied provider ascent for Android's Java rasterizer; its bitmaps already use the face ascent. TTF descriptors are zero-initialized and retain their existing behaviour.
- Include the upstream SIL OFL 1.1 notice at `docs/licenses/source-han-sans-OFL.txt`. The generated header points to this notice. Upstream: https://github.com/adobe-fonts/source-han-sans/blob/release/LICENSE.txt

Bitmap storage grows from 416920 to 536040 bytes: **119120 bytes (116.3 KiB)** of constant data. There is no glyph cache or per-glyph allocation; descriptors gain one integer (including alignment). `NJ_ENABLE_FONT_CJK=0` continues to omit the tables. This deliberately spends ROM to preserve native-size strokes rather than squeezing taller letters into smaller individual rasters.

## Reproduction and validation

Generation used Pillow 12.2.0, FreeType 2.14.3 and `/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Regular.otf`, SHA-256 `e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4`:

```sh
python3 tools/gen_cjk_font.py
cmake --build build -j4
ctest --test-dir build --output-on-failure -R 'font|gfx'
```

Both faces contain 7445 glyphs, zero clipped glyphs and only the intended blank U+3000. One dashed rule in the smaller face needs the existing grayscale fallback. Removed the obsolete shrinking code and statistics.

All five font/graphics tests passed. The new provider test adds 80 pixel/width assertions across TOP, BOTTOM, BASELINE and VCENTER anchors, direct/fallback providers and 1bpp/coverage formats. The builtin table tests check provider ascents and large fullwidth descender storage. Mixed-script specimens include `Agjy 中文游戏 ＡｇαДǐ，。` in all three sizes, plain and bold, with red baseline guides.

The complete suite passed 83/84 tests; `test_text_input` has 17 failures (Gauge value and SDL synthetic pointer/input ordering). An isolated source/build copy with the four font implementation files restored from HEAD, retaining the pre-existing input changes, reproduced the same **562 checks / 17 failures**. These failures are independent of the font change and remain to be addressed separately.

A separate build with `-DCMAKE_C_FLAGS=-DNJ_ENABLE_FONT_CJK=0 -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_SDL2=OFF` succeeded. The final desktop build had no compiler warnings. Android's descriptor reset was reviewed in source; no Android device/build validation was performed in this run.

## Actual game

Original `tests/games/3D版田园物语_6744.jar`, SHA-256 `8ad0920c1ebf685a1272bfa5f7fc1e0c4415189bcb82d0e28bd82917fca54e63`, 240x320, 8 MiB heap, 30 FPS, 600 frames, fresh isolated RMS directories for each port:

```sh
./build/nj2me --port headless --heap 8 --fps 30 --run-frames 600 \
  --keys '100:soft1,140:fire,180:soft1,230:fire,330:fire,370:fire,410:fire' \
  --rms-dir .local-tools/debug-runs/font-20261019/farm/rms \
  --dump-frames .local-tools/debug-runs/font-20261019/farm/frames/ \
  --dump-every 100 --stats 'tests/games/3D版田园物语_6744.jar'
```

Headless captures show the Chinese start menu at frame 300 and narrative text at frame 600. Subsequent inputs advance past the menu into the story. The headless run averaged 30.2 FPS; SDL2 with dummy video/audio and a separate fresh RMS directory averaged 30.1 FPS. Both ended normally at their frame budget with no uncaught exception. The known optional-resource `DataInputStream.readUTF` exceptions were caught by the game. SDL2 execution was validated by its log, not by a window capture. This establishes the menu/story path, not a full playthrough.

Local evidence lives under `.local-tools/debug-runs/font-20261019/`: `new.png` / `old.png`, `farm/00300.png` / `farm/00600.png`, generation/build/test logs, isolated baseline reproduction and trimmed build. Local tools and captures are excluded from the commit; original JARs and user saves were not modified.
