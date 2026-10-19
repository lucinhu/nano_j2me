# 2026-10-20 - Antialiased Latin for the built-in bitmap font

Follow-up to `4843cb3` (packed 2bpp CJK): the CJK faces were antialiased but
the fixed ASCII/Latin-1 table was still 1bpp, so mixed text showed smooth
Chinese next to blocky Latin ("字母字体也要做, 和中文保持一致").

## Why Liberation Mono and not Source Han's halfwidth Latin

Using the CJK font's own Latin would keep one family, but Source Han Sans CN
renders Latin-1 through the `hwid` (halfwidth) feature and its ink does not
fit the fixed cells the layout contract depends on (`nj_font_char_width ==
glyph_w`, fixed advance; asserted in `tests/test_font_png.c`). Measured with
PIL against the shipped cells:

| size | cell (asc/desc) | Source Han hwid overflow |
|------|-----------------|---------------------------|
| small  | 6x13 (10/3)  | 17 (Regular) / 19 (Bold) glyphs past the cell |
| medium | 8x16 (12/4)  | 17 / 24 |
| large  | 12x20 (16/4) | 8 / 9 (Bold `\|` descends 5 > 4) |

Widening the cells would change string widths for every MIDlet. Instead the
Latin AA tables reuse the existing Liberation Mono family (already the source
of the mono tables) through the same 4x supersample + Lanczos + quantize
pipeline as CJK, which fits every cell: small asc9 desc2/3, medium asc11
desc3, large asc15/16 desc4.

## Implementation

* `tools/gen_fonts.py --bpp {1,2}` (default 1): `--bpp 2` renders at 4x,
  Lanczos-downscales and quantizes coverage to 2 bits, packs 4 pixels per byte
  (MSB first, 0..3 scaled to 0/85/170/255). Envelope measurement and the
  `NEVER_BLANK`/stem checks are bpp-aware. Sizes reuse the committed
  ascent/descent (small 10/3, medium 12/4, large 16/4) so MIDP line layout is
  identical in both modes.
* `--header` now writes `nj_font_data.h` for `--bpp 1` and
  `nj_font_data_aa.h` for `--bpp 2`; both define
  `NJ_FONT_DATA_COVERAGE` (0 or 2).
* `nj_gfx.h`: `nj_font` gained `int bitmap_coverage` after `bitmap`
  (0 = 1bpp fixed rows, 2 = packed 2bpp; hooks keep using `coverage`).
* `nj_font.c`: includes `nj_font_data_aa.h` under `NJ_ENABLE_FONT_CJK_AA`
  (same switch as CJK AA, no new knob) and passes
  `NJ_FONT_DATA_COVERAGE` through `NJ_FONT_ENTRY`.
* `nj_text.c`: the fixed-table path now takes its stride and coverage from
  `bitmap_coverage` instead of forcing 1bpp; the blit already decoded packed
  2bpp (added for CJK) so it needed no change.

## Cost (Release, `size` on `nj2me`)

| build | text | delta |
|-------|------|-------|
| `NJ_ENABLE_FONT_CJK_AA=0` (both mono) | 2250205 | - |
| AA on (CJK + Latin) | 2689085 | +438880 B (~429 KiB const) |

Latin AA alone adds 21952 B (small 5824->11648, medium 7168->14336,
large 17920->26880; six tables, plain+bold). No heap use either way; cell
metrics and advances are identical, so layout cannot change with the flag.

## Validation

* Clean-build ctest 84/84; mono trim build (`=0`) font/savedgfx/text 5/5.
* Stranded II (`孤岛余生2时间之谜汉化版_7371.jar`) isolated-RMS replay 1800f
  headless and 600f SDL2 (30.1 fps) exit 0; language menu A/B (mono vs AA)
  shows identical positions with visibly smoothed Latin, and the CJK
  "按任意键继续" prompt is unchanged.
* Specimens: `.local-tools/debug-runs/font-aa-20261020/latin/{mono,aa}-specimen.png`
  (gitignored); comparisons `.local-tools/debug-runs/font-aa-20261020/compare-{00900,01800}.png`.

Note for bisecting: `check_savedgfx` (and `--stats` paints/gfx counters) can
be poisoned by a stale incremental object after header-struct changes - rebuild
cleanly before trusting a failure (this cost one detour here).
