# Fog is a per-fragment quantity, not a vertex one

## What was wrong

Minecraft PE's sky was one flat sheet of colour. Against a FreeJ2ME-Plus frame
of the same saved world the reference sky runs from `(58,116,202)` at the top to
`(105,183,228)` at the horizon; ours was `(96,176,224)` everywhere, which is the
fog colour `0x71c4f4`. The whole skybox had been painted with the fog colour.

The M3G code evaluated the fog factor **per vertex** and left it to the
interpolator. That is wrong for any triangle that spans a large depth range, and
Minecraft's sky is exactly such a triangle: it is a horizontal plane at `y=16`
made of 128-unit steps from `z=-512` to `z=512` (the camera transform cancels
the model transform, so camera-space depth is the plane's own `z`). The plane's
58 vertices sit between 0.1 and 640 units from the eye, the fog is
`near=12.8 far=48`, and every vertex except the one behind the camera is past
`far`. Vertex fog therefore said "zero" for all of them, i.e. fog colour, and
the gradient the reference shows - the factor at the horizon is about 0.1 and at
the top about 0.75 - fell between the vertices. Clouds had the same problem with
their own fog (`near=161.28 far=268.8`).

## What GL ES does

The fog factor is a function of the fog coordinate, which is the depth along the
view axis, and it is evaluated **per fragment**: the fixed-function pipeline
interpolates the fog coordinate and computes the factor at each pixel. The
reference implementation (`.local-tools/freej2me-plus/src/javax/microedition/m3g/Graphics3D.java`)
does the same per 16-pixel span, computing `zEye` from the interpolated `1/w`
and turning it into a 0..256 factor there.

## Implementation

`sw_fog()` in `src/midp/nj_lib_m3g.c` takes the interpolated `1/w` for the pixel
and turns it into a depth once:

* `d = 1/invw`, via the usual bit-trick estimate `v.u = 0x7EF127EAu - v.u`
  followed by **two** Newton steps. A division is the honest way and the
  expensive one; one Newton step leaves a ~0.2% error, which is enough to shift
  an eight-bit channel by one at the far end of a linear ramp, so the second
  step (two multiplies) is worth it.
* `LINEAR`: `f = (far-d)/(far-near)`, else `EXPONENTIAL`: `f = exp(-density*d)`,
  clamped to 0..1.
* The colour is `c*f + fog_colour*(1-f)` in the RGB channels; alpha is kept.

It is called once per pixel from every raster loop that can be fogged - the flat
opaque fill, the flat 565 and ARGB textured fills, and the general Gouraud and
textured loop - with `pg*ia`, the perspective-correct `1/w`. The sprite path
keeps its own `sprite_fog()` (sprites have a different fog treatment in the
JSR). The per-vertex evaluation was deleted from `jr_fetch()`, and `jr_fog()`
with it; `jr_setup()` now copies the `Fog` parameters into the rasteriser state.

The cost is small: on a headless Minecraft world, 3000 in-world frames ran at
244.6-250.1 fps with fog and 259.4-256.8 fps with the fog disabled for
the measurement - about 3-5%.

## Evidence

Same world, same camera, against the FreeJ2ME-Plus reference
(`.local-tools/debug-runs/mc-vis-20261030/refrun/ref.png`):

| row | reference | before | after |
| --- | --- | --- | --- |
| top of the sky | (58,116,202) | (96,176,224) | (56,112,200) |
| y=20 | (62,121,204) | (96,176,224) | (56,120,200) |
| y=40 | (70,132,208) | (96,176,224) | (64,132,208) |
| y=60 | (82,149,215) | (96,176,224) | (80,148,216) |
| horizon | (105,183,228) | (104,180,224) | (104,180,224) |
| mean absolute RGB difference, whole frame | - | 41.4 | 13.8 |

The remaining difference is mostly the clouds (ours read brighter and harder
than the reference's fog-tinted `(201,227,243)`) and the cloud animation phase
(the compared frames are not the same instant). That is written down rather
than hidden by picking a frame that happens to match.

## Tests

`tests/m3gdemo/M3GDemo.java` renders a triangle at `z=-5` through a `4..6`
linear fog and reads the pixel back. The old version asked for exactly
`0xFF808000`; the factor is a float now, and half of 255 lands on a rounding
boundary, so the check allows one unit per channel (and says why). The
distinguishing case - a triangle whose vertices straddle the fog range, where
per-vertex and per-fragment disagree by tens of colour units - is what the
reference comparison above exercises.

Full `ctest` 132/132, corpus gate `fatal 0 / 400, known 4`, no regression in the
Minecraft world or the v3 snapshot.

## The same change: a 60 fps default

The desktop SDL2 port and the Android port both defaulted to a 30 fps cap. A
counter in the Android port showed the emulator drawing a saved world at 59.4
fps when the cap was lifted, so the cap was hiding half of the motion the
runtime could present at no real cost. Both ports now default to 60
(`src/port/posix/nj_port_sdl2.c`, `android_fps_cap()` in
`src/port/android/nj_port_android.c`); `--fps` still overrides it, and the
Android player setting still offers 30 / 60 / uncapped. The ESP32 port keeps
30, where the cost is real.
