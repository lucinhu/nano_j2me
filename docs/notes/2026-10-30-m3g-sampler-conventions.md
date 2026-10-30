# M3G sampler conventions: texel centres, level filtering and paletted images

`MinecraftJ2ME_PE_0.6.1_V2.2.jar` rendered a water surface crossed by regular
1 px magenta `(255,0,255)`/green stripes along the triangle grid, and its sky
had no clouds at all, against a FreeJ2ME-Plus reference frame of the same world
(blue sky, white block clouds, clean dark-blue water). Three separate sampler
bugs produced those two symptoms.

## Texel centres: `[i/W, (i+1)/W)`, not `i/(W-1)`

The rasteriser mapped a texture coordinate to the nearest texel with

```
u = (int)(fs * (float)(W - 1) + 0.5f);
```

which puts texel *i* at `i/(W-1)` - the sample at `u=0` and `u=1` both land
inside the image, but every texel in between covers a slightly wrong interval.
Minecraft draws water as one quad per block whose UV box is exactly one 16 px
tile of a 256 px atlas, e.g. `u 0.8125..0.875`, `v 0.750..0.8125`. The tile
edge `v = 0.750` evaluated to `0.750 * 255 + 0.5 = 191.75 -> 191`, the last row
of the tile *above* - which in Minecraft's `gui/items.png` is a solid lavender
`(214,127,255)` fill area. Every tile boundary therefore sampled one row out of
its neighbour and the water got the stripes.

An instrumented run caught the sample directly: `DBGPURPLE lv=0
uvbox=0.812..0.875,0.750..0.812 uv=0.8742,0.7506 uvint=223,191
texel=ffd67fff flat=ffe5e5e5 frag=ffc072e5` - flat white modulated by the
lavender filler. Dumping the texture confirmed the palette fill block at
x216..223, y184..191.

The fix is the GPU's own convention, texel *i* covering `[i/W, (i+1)/W)`:

* nearest: `u = (int)(fs * (float)sw->tex_w);`
* linear: `float x = fs * (float)sw->tex_w - 0.5f;` with an integer weight
  `(1 - frac(x)) : frac(x)` at `x0 = floor(x)`;
* `sw_near_texel()` (the fast RGB565/RGBA paths) lost its `tsx`/`tsy`
  parameters and derives the same bounds from `tex_w`/`tex_h`.

Measured on the same world and frame (`frR2`): pixels matching the stripe test
(`r>130 && b>170 && g<130` inside the water rectangle) went 221 -> 0, and the
final frame's water is uniformly deep blue, matching the reference.

`tests/test_m3g.c` pinned the old convention in its expected values. The 14
sample points x 2 formats x 2 wrap modes (56 expectations) were regenerated
from an independent model of the new arithmetic; the table now also separates
CLAMP from REPEAT, exercises the `-1 -> 0` repeat edge and uses values that
distinguish texel-centre from texel-corner mapping.

## `Texture2D.setFiltering()` level filter was stored and never read

`jr_emit()` chose a mip level from the triangle's texture-to-screen area ratio
whenever the image had mips, regardless of the level filter. JSR 184 says
`FILTER_BASE_LEVEL` (208, the constructor default and what Minecraft asks for)
means level 0 always; `FILTER_LINEAR` (209) and `FILTER_NEAREST` (210) are what
select a mip. Minecraft's atlas is a set of per-tile quads, so mip averaging is
meaningless across tile boundaries: at `lv=3` (32x32) it averaged the
neighbouring tiles and the atlas filler into the water.

`m3g_sw` now carries `filt_level` alongside `filt_image`, `jr_setup()` reads
`Texture2D._nj_filtLevel`, and the LOD gate is

```c
if (r->sw.mips && r->tpos != NULL && r->sw.mip_n > 1 &&
    (r->sw.filt_level == 209 || r->sw.filt_level == 210))
```

With only the texel-centre fix the stripes fell from 221 to 49 px; gating the
LOD took them to 0, and the two are independent (a `-DNJ_M3G_FAST_PATHS=0`
build reproduced the same pixels, so the fast paths were not at fault).

`tests/m3gdemo/M3GDemo.java` had encoded the old behaviour - its minified
checkerboard case expected a mip averaged grey from a texture that had never
asked for mips. It now sets `setFiltering(FILTER_NEAREST, FILTER_NEAREST)`
explicitly and its comment records why.

## Paletted `Image2D` kept the indices as pixels

Minecraft's cloud layer is `new Pix("/environment/clouds.png").image2D()`. With
no image data offset `Pix` builds the texture through JSR 184's five-argument
paletted constructor, `new Image2D(100, w, h, indices, palette)`. The runtime
stored both arguments and expanded neither: `_nj_palette` was written in
`m3g_img2d_init_paletted()` and never read anywhere, and `_nj_pixels` kept the
one-byte-per-pixel indices, so the sampler read them as R,G,B,A. The dumped
cloud texture had an alpha histogram of `{0:57960, 1:6835, 255:730, ...}` and
32 near-black colours instead of the asset's `{0:13356, 1:52180, 255:13356}`
white cloud blocks - which is exactly what the sky showed: a field of
near-black flecks, invisible on blue.

The file format's own loader had expanded paletted images since the container
work; the Java constructor path had not. Both now go through one helper:

```c
static void m3g_expand_palette(const nj_u1 *ix, nj_u4 n, const nj_u1 *pal,
                               nj_u4 bpp, nj_u4 entries, nj_u4 fmt, nj_u1 *out)
```

It widens each texel with `m3g_image_texel()` (ALPHA/LUMINANCE/LUMINANCE_ALPHA/
RGB/RGBA to RGBA), clamps an index past a short table to the table's last
entry, and applies the reserved-byte rule FreeJ2ME-Plus applies: an RGBA
palette whose alpha bytes are all zero is opaque, because an exporter wrote a
reserved byte there and a fully transparent texture is never intended. The
constructor also gained the specification's argument checks
(`NullPointerException` for a null array, `IllegalArgumentException` for a
short image or a palette that is not a whole number of colours).

`tests/test_m3g.c` holds four palette cases: an RGBA table expanded entry for
entry, a LUMINANCE_ALPHA table widened to grey plus alpha, a 3-entry table with
index 7 clamping to the third entry and an all-zero alpha column forcing opaque,
and the three argument errors.

## Evidence

Raw numpy-free PPM analysis and frame dumps are under
`.local-tools/debug-runs/mc-user-20261030/` (`frR2`, `frL` after the texel fix,
`frM` after the LOD gate, `frP`/`frQ` with clouds) and the final frames plus
logs under `.local-tools/debug-runs/minecraft-m3g-fix-20261030/`. The reference
frame is `.local-tools/debug-runs/minecraft-reference-world-r01/world.png`.

`test_m3g` is **818 checks, 0 failures**; `check_m3gdemo` is 146 checks, 0
failures. Full `ctest` **131/131**, sanitized suite **131/131** under
ASan+UBSan+LSan, `tools/check_codec_trim.sh` green, and
`tools/check_m3g_titles.sh` runs its 26 M3G titles with no exception (its deep
pass renders 1,056,779 triangles in 浴火银河2完整版_7689). The temporary probes
(`DBGTEX`, `DBGMIP`, `DBGPURPLE`, `DBGDRAW`, `dbg_*` fields, the
`NJ_M3G_NOMIP`/`NJ_M3G_NOFOG` switches) were removed before the commit.
