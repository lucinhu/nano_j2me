# Background image coverage, and the three narrow texture formats

2026-11-03

The 3D fidelity sweep (docs/notes/2026-11-03-3d-fidelity.md) left two
candidates: the ski-jump title's black sky above the mountain ridge, and
runtime `Image2D` images in ALPHA/LUMINANCE/LUMINANCE_ALPHA formats being
silently ignored when bound as textures.  This note settles both, and fixes
one real bug the probe found on the way: `Background.setImage()` never reset
the crop rectangle, so a title that binds a background image and never calls
`setCrop()` gets no background image at all.

## What the specification actually says

From the JSR 184 `Background` javadoc (M3G 1.1):

- *"The background image must be in RGB or RGBA format... This is enforced by
  the `render(World)` and `clear` methods."*
- *"The contents of the crop rectangle are scaled to fill the entire viewport.
  The crop rectangle need not lie within the source image boundaries. If it
  does not, the source image is either considered to repeat indefinitely... 
  (`REPEAT`) or to not repeat at all, with pixels outside the source image
  having the background color (`BORDER`)."*
- *"If either of them [crop width/height] is zero, the color buffer is cleared
  with the background color only."*
- Defaults: color clear enabled, depth clear enabled, color `0x00000000`,
  image `null`, image mode `BORDER, BORDER` (`BORDER` = 32, `REPEAT` = 33).
- `setImage()`: *"The crop rectangle is set such that its top left corner is
  at the top left corner of the image, and its width and height are equal to
  the dimensions of the image."*

So the modes are `BORDER`/`REPEAT` (not `FIT`): the crop rectangle is always
stretched over the whole viewport, and the remainder question only arises for
crop rectangles that stick out of the image.

For textures, `Texture2D` says the opposite of a restriction: *"The image may
be in any of the formats defined in Image2D"*, and its blending table defines
what each format contributes (`ALPHA`: Cv = Cf, Av = At; `LUMINANCE`:
Cv = Cf·Ct; `LUMINANCE_ALPHA`: both).

## Probe

A temporary `BGPROBE` instrumentation (no longer in the tree) printed every
`Background.setImage`, `setCrop`, container-loader Background and background
blit, with the image format/size, the crop rectangle and the viewport.  It
ran against the 3D titles in the corpus and against synthetic cases.

### Background probe results

Viewport 240x320, 2x2 RGB image with red/green/blue/yellow quadrants:

| mode | crop | written pixels (pre-fix) | expected | pre-fix | post-fix |
|---|---|---|---|---|---|
| default after `setImage` | 0,0 2x2 (was **0,0 0x0**) | none (crop 0x0 skip) | all 76,800 | mismatch | match |
| `BORDER`, crop -2,-2 6x6 | -2,-2 6x6 | image in 80..159 x 107..213, background colour elsewhere | same | image missing | match |
| `REPEAT`, crop -2,-2 6x6 | -2,-2 6x6 | wrapped image everywhere | wrapped image (mod N) | image missing | match |

The BORDER/REPEAT pixel loops were already correct; what was missing was the
crop reset, and with it the image itself.  The test checks the crop rectangle
right after `setImage` (0,0,2,2) and then the rendered pixels.

### A real title that hits it: 毁灭战士3 (Doom 3)

Probe output from the title's menu:

```
BGPROBE setImage fmt=100 w=128 h=128
BGPROBE setImage fmt=100 w=249 h=334
BGPROBE blit fmt=100 bpp=4 iw=249 ih=334 crop=0,0 0x0 modes=0,0 vp=0,0 240x320
```

It binds two RGBA images and never calls `setCrop()`, so the crop stayed (0,0)
and the runtime skipped the image entirely on every clear.  With the fix the
crop reads 0,0 249x334 and the blit runs.  The menu frames are unchanged
because the animated 3D scene covers the whole viewport there; the sky the
image supplies appears wherever geometry does not (Doom 3's gameplay/load
screens).  Nine menu frames (PPM, every 10th of a 90-frame run) are
pixel-identical to the pristine build, so the fix does not disturb this title.

### The ski-jump black sky: not a background-coverage bug

The `3D跳台滑雪2011` asset loads this Background:

```
BGPROBE loader-det color=ff000000 flags=00032020 nrefs=1 ref0=0 img=0
```

No image, opaque **black** clear colour, BORDER/BORDER.  The black region
above the ridge is therefore the title's own colour clear wherever its 3D
geometry does not cover the frame; a correct renderer must show it too.
(The FreeJ2ME reference shot that showed a light-blue sky came from a
different screen/phase: the title's intro and run animation is wall-clock
driven, and the two emulators were not frame-aligned.  The candidate is
reclassified: the black sky is the asset, not our coverage.)

## Fix 1: `setImage` resets the crop rectangle

`src/midp/nj_lib_m3g.c`:

- `m3g_bg_setImage()` writes the new image and then `(cropX, cropY, cropW,
  cropH) = (0, 0, image.width, image.height)`, exactly as the spec says.
- The container-loader path that binds a Background's image from a `.m3g`
  file resets the crop the same way.

No format validation was added to `setImage`: the spec's "must be RGB or
RGBA" is a constraint on the caller, and the runtime stays lenient (a
non-RGB/RGBA image is simply not drawn) rather than throwing on titles that
are already out of spec.  That is the one deliberate deviation left here.

## Fix 2: ALPHA/LUMINANCE/LUMINANCE_ALPHA textures

`jr_setup()` used to accept only RGB (99) and RGBA (100) images as texture
sources; the three narrow formats rendered untextured (the flat vertex
colour), which contradicts the "may be in any format" rule.

New `m3g_image_rgba(vm, image)` widens a narrow image to the RGBA byte stream
the sampler reads, once per image:

- ALPHA (96): `RGB = 255,255,255`, `A = a`
- LUMINANCE (97): `RGB = L,L,L`, `A = 255`
- LUMINANCE_ALPHA (98): `RGB = L,L,L`, `A = a`

`jr_setup()` calls it for formats 96..98 and samples the result with
`tex_bpp = 4`.

The widened buffer is cached in the Image2D's existing `_nj_mips` field, not
in a new field.  That field is unused for these formats (`m3g_image_mips()`
only builds chains for RGBA and returns immediately otherwise) and
`m3g_image_dirty()` already clears it, so an upload (`Image2D.set()`)
invalidates the cache.  This matters: a new `_nj_rgba` Image2D field was
tried first and **broke snapshot resume** - the state loader compares each
saved class's field count, and every existing save state then refused to
load:

```
class javax/microedition/m3g/Image2D: saved inst=128 f=7 m=10 ... have inst=128 f=8 m=10
nj_vm_load: refused (NJ_ERR_FORMAT)
cannot resume ...; starting the MIDlet instead
```

Behaviour after widening:

- MODULATE (227, the default): exactly correct for all three formats - the
  RGBA path multiplies by the widened RGB and multiplies alpha.
- LUMINANCE/LUMINANCE_ALPHA under REPLACE (228): exactly correct
  (`Cv = Ct`, `Av = At/ Af`).
- ALPHA under REPLACE: **approximate**.  The widened white replaces the
  fragment colour instead of the spec's `Cv = Cf`.  Titles that use
  REPLACE with a pure-alpha texture are rare; making it exact needs a
  source-format-aware span loop, which is not worth a per-pixel branch on the
  hot path for this.  Documented as a known limit.
- No mip chain for widened images: level filtering samples level 0.  The
  default filtering (FILTER_BASE_LEVEL) is exact.

## Tests

Two new groups in `tests/test_m3g.c`, both synthetic:

- `test_background_image`: crop reset after `setImage`; the 2x2 image
  stretched over the four quadrants of a 240x320 target (exact ARGB at five
  sample points, no unpainted pixel); BORDER paints the background colour
  outside a crop larger than the image; REPEAT tiles instead.
- LUMINANCE and LUMINANCE_ALPHA textures in the existing textured-quad
  harness: a 1x1 `0x80` LUMINANCE image renders `0x808080`, a 1x1
  `0x40,0xFF` LUMINANCE_ALPHA image renders `0x404040`.

`test_m3g` check count goes 917 → 933.  Against the pristine source the new
checks fail exactly as expected:

```
FAIL  a LUMINANCE texel widens to grey and renders
FAIL  a LUMINANCE_ALPHA texel widens to grey and renders
FAIL  setImage resets the crop rectangle to the whole image
FAIL  the image quadrants stretch over their quarters of the viewport
933 checks, 4 failures
```

and pass with the fix: `933 checks, 0 failures`.

## Gates (this tree, gcc Release)

- `build/test_m3g`: 933 checks, 0 failures, PASS.
- ASan+UBSan (`-DCMAKE_BUILD_TYPE=Debug -DNJ_SANITIZE=ON`, target
  `test_m3g`): 933 checks, 0 failures, no sanitizer reports.
- `tools/check_m3gdemo.sh`: 148 checks, 0 failures.
- `tools/check_no_statics.sh`: OK (75 object files, read-only tables only).
- `ctest --test-dir build`: 137/137 passed (fresh worktree registers 137 of
  main's 140).
- Minecraft, saved state `bh.state` (heavy landscape view), 6 frames
  dumped: **0 differing pixels** against the pristine main binary.
- 毁灭战士3 menu, 9 frames every 10th: 0 differing pixels.
- Original JAR hashes unchanged (`jar-hashes.sha256` from the fidelity
  sweep re-checked).

## Evidence

`/home/lucinhu/workspace/wt/evidence-r7-background/`:

- `fix-src-final.patch` - the source change under test
- `test-prefix.log`, `test-postfix.log` - 4 failures before, 0 after
- `gate-*.log` - test_m3g (Release and ASan), M3GDemo, no_statics, ctest
- `mc-bh`: `bh-base/`, `bh-cand/` - Minecraft frames, pristine vs fixed
- `doom-base/`, `doom-cand/` - Doom 3 frames, pristine vs fixed
- probe outputs are quoted above; the instrumentation itself was removed
  before the commit (it was a few lines in `m3g_bg_setImage`, `m3g_bg_setCrop`,
  the Background loader case and `m3g_background_image`)

## Next candidates

1. ALPHA + `FUNC_REPLACE`: make the widened ALPHA texture keep the fragment
   colour (would need a source-format-aware REPLACE path, or widening ALPHA to
   a mask the rasteriser recognises).
2. Mip chains for widened textures: today a lum/alpha texture with
   `FILTER_NEAREST`/`FILTER_LINEAR` level filtering samples level 0; building
   a chain from the widened base is a few lines in `m3g_image_mips()` but
   changes LOD pixels for titles that ask for it, so it needs a corpus trigger
   first.
3. The ski-jump title's geometry/sky gap: still worth one deterministic
   replay (fixed key script, fixed frame count, same wall-clock phase) against
   FreeJ2ME if it ever gains deterministic input timing; otherwise leave it
   as an asset observation.
