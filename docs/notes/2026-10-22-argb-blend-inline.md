# Inline the ARGB8888 -> ARGB8888 blend Doom 3 uses for its 2D layers

Status (2026-10-22): shipped.  `nj_region_draw()` now blends a translucent
ARGB8888 source pixel over an ARGB8888 destination in place, with the same
arithmetic as `nj_gfx_blend_pixel()` and byte-identical output.  This is the
pair a Doom 3 frame actually pushes 14.9 million pixels through.

## Where the pixels go

The 2026-10-21 glass note (`2026-10-21-blend565-inline.md`) read the profile as
"Doom blits a full ARGB8888 screen onto the RGB565 framebuffer".  The
destination is neither RGB565 nor a framebuffer blit:

* `gprof` call graph of a 900-frame Doom 3 run: `nj_region_draw()` is called
  3765 times - 2330 from `g_drawImage()` (255 per second, the game's own
  `Graphics.drawImage` calls), 1076 from `gc_blit()` (the GameCanvas flush) and
  359 elsewhere - and it is the only caller of `nj_gfx_blend_pixel()`, with
  14,947,022 pixels.
* 2330 x ~6.4 k pixels: the per-pixel path is the game drawing small translucent
  ARGB8888 images onto its own ARGB8888 surface (HUD, menus, fades); those are
  `drawImage`, not full-screen composites, and the previous round's shortcut -
  which requires an RGB565 destination - never engaged on this title.
* The 1076 `gc_blit()` calls are the framebuffer flush and are opaque copies;
  they do not reach the blend.

So the hot pair is `src_format == NJ_PIXFMT_ARGB8888`,
`g->surf->format == NJ_PIXFMT_ARGB8888`, no transform, no mask, no xor, caller
alpha 255.  That branch already existed for opaque pixels; the change is to
inline its translucent half as well.

## Change

`src/gfx/nj_gfx.c`:

* `nj_blend_argb(dst, src)` - one translucent ARGB8888 pixel over another, the
  same integer arithmetic as the general path with the coverage taken from the
  source alpha (the caller's alpha is 255, so `nj_gfx_blend_pixel()` reduces to
  `a = (argb >> 24)`);
* the ARGB-to-ARGB branch calls it on the destination row word instead of
  `nj_gfx_blend_pixel(g->surf, x, y, c, 255)`, which cost a bounds check, a
  surface format switch and two `nj_raw_set()`/`nj_surface_get()` calls per
  pixel to rediscover a format that cannot change inside the blit.

No allocation, no per-draw setup; other source/destination pairs, ROP `SET`,
masks, transforms and caller alpha keep the old paths.

## Verification

**Equivalence test** (`tests/test_gfx.c`): an ARGB8888 image of 16 colour/alpha
combinations is blitted with `NJ_ROP_PAINT` onto two ARGB8888 backgrounds and
every pixel is compared with the same pixel drawn through the general
`nj_gfx_blend_pixel()` path.  Eight of the sixteen combinations are deliberately
on the rounding boundary (their channel sums are 128 modulo 255), which the
first eight were not: a mutation of one channel's `+127` to `+126` went
uncaught before they were added and now fails exactly one assertion.  The
suite reports 324 assertions, 0 failures.  A sentinel return from the new helper
fails 13 of them, so the branch is genuinely exercised.

**Frame identity**: the same 900-frame Doom 3 command with
`--dump-frames --dump-every 150` gives six byte-identical frames on both sides
(`cmp -s`, frames 150..900).  The accumulated `tris=` counter alternates
between 288735 and 289235 across *both* builds - a scheduling-dependent game
decision, not the change - while the dumps agree.

**CPU A/B**, Release, same-source trees, isolated RMS, five interleaved pairs
(`user` seconds from bash `time`):

| pair | before | after |
| --- | --- | --- |
| 1 | 2.670 | 2.404 |
| 2 | 2.988 | 2.644 |
| 3 | 2.931 | 2.624 |
| 4 | 2.546 | 2.753 |
| 5 | 3.113 | 2.955 |
| mean | 2.850 | 2.676 |
| median of runs | 2.931 | 2.644 |

Paired mean -0.174 s (-6.1 %), median of runs -9.8 %; one pair is positive, so
read it as "about five percent", not as the median.  The call-count evidence is
not noisy: a `-pg` build of the same 900-frame workload reports
`nj_gfx_blend_pixel()` **14,947,022 -> 1,120** calls, and the 1,120 left come
from the line/plot path (`nj_plot()`), not from this branch.

**Cost**: `size` on the Release binary, `text` 2,708,270 -> 2,708,454 B
(+184 B), `data`/`bss` unchanged.  Full `ctest` 87/87 (50.90 s); trim
`-DNJ_WERROR=ON` build clean; `tools/check_no_statics.sh` passes.

## Real-game coverage

A temporary counter build (`-DNJ_GFX_DIAG`, not committed) counted the translucent
pixels each inline path handles, over a 57-title sample: every seventh JAR in
`tests/games/`, 240 frames each, 240x320, isolated RMS.  Ten titles reported
counters at all (the others drew no translucent pixels of either kind in that
window); **all ten had `blend565 = 0`**, and every one had ARGB-to-ARGB traffic:

| title | ARGB8888 -> ARGB8888 translucent pixels, 240 frames |
| --- | --- |
| 浴火银河2瓦尔基里(汉化)_3067 | 20,148,950 |
| 周末的沙滩_7453 | 1,766,400 |
| 反恐精英2正式版_5836 | 1,208,261 |
| 真三国无双横扫千军_12449 | 921,600 |
| 三国杀2012_809 | 213,062 |
| 美女滑雪胜地_2 | 92,640 |
| three more titles | 57,840 / 54,900 / 6,501 / 2,035 |

So the pair this round inlined is the one 2D-heavy titles actually use, while
the ARGB-to-RGB565 shortcut from 2026-10-21 has no measured title yet: the scan
found zero translucent pixels on that pair.  It stays because it is exercised by
the equivalence test and is the correct fast path for a title that blits alpha
sprites straight to the framebuffer, but its benefit is currently unmeasured.
The comment above that branch still names 浴火银河2完整版_7689; that title's
translucent layers go to its own ARGB surface (3,463,200 pixels in 600 frames,
`blend565 = 0`), so read the comment as "this is the branch that would handle
it", not as a measured workload.

**Second A/B workload** - 浴火银河2瓦尔基里(汉化)_3067, 900 frames, same five
interleaved pairs against the pre-change binary:

| pair | before | after |
| --- | --- | --- |
| 1 | 2.304 | 1.677 |
| 2 | 2.285 | 1.406 |
| 3 | 2.014 | 1.563 |
| 4 | 1.926 | 1.439 |
| 5 | 2.107 | 1.741 |
| mean | 2.127 | 1.565 |

Every pair is negative: paired mean -0.562 s (**-26.4 %**).  All six dumped
frames are byte-identical, and both sides run at the 30 fps cap (paints = 1..3
in 900 frames - the title paints into its own ARGB surface while loading, which
is exactly the blend traffic being measured).  Doom gains -6.1 % because its
frame is dominated by 3D rasterisation; a title whose frame is dominated by 2D
compositing gains an order of magnitude more.

The same scan also explains the 2026-10-21 correction: the 14.9 M Doom pixels
are the game's own ARGB surface, not the RGB565 framebuffer.

## Evidence

`.local-tools/debug-runs/argb-blend-r13/` keeps the ten A/B logs, the six
identity dumps' logs, the interleaved `time` output and the `-pg` flat/call-graph
excerpts.
