# Texture2D's transform matrix: the corpus measurement and a pixel-level test

Date: 2026-10-27
Scope: `tests/test_m3g.c` (new `test_texture_transform_matrix`),
`tests/test_m3g_file.c` (general-transform decode),
`tools/check_m3g_texture_transforms.py`, `docs/m3g-texture-transform-results.json`
Evidence: `.local-tools/debug-runs/m3g-texture-transform/`; ctest 118/118.

## What was already there, and what was missing

The general (4x4) `Texture2D` transform was decoded in `c34d101` and stored
the way `setTransform()` stores one: the loader writes the matrix into
the `Transformable` general slot `_nj_x` and calls `xable_recompose()`, so the
composite `_nj_m` is `T * R * S * M`.  The software renderer reads it in
`jr_setup()` (`r.tmat = mat_of(vm, tex, NJ_C_XABLE, "_nj_m")`) and
`jr_vertex()` maps the interpolated texture coordinates through its 2D part
before sampling:

    st[0] = tmat[0] * u + tmat[1] * v + tmat[3];
    st[1] = tmat[4] * u + tmat[5] * v + tmat[7];

That was decode-level work only: no test showed a pixel changing because of a
texture matrix, and no real file was ever checked beyond "the object decodes".
So the implementation was a claim, not a verified behaviour - the gap recorded
in `docs/notes/2026-10-26-m3g-render-defects.md`.

## The corpus, measured

`tools/check_m3g_texture_transforms.py` walks every `.m3g` in the collected
corpus, decodes the two transform flags and prints the 4x4s:

* **975** `.m3g` files, **1,585** Texture2D objects;
* exactly **5** carry a general transform, all in
  `CS反恐精英(蓝牙对战)_6858.jar`'s map files.  All five are scale + translate:

| map | scale (s, t) | translation (s, t) |
| --- | --- | --- |
| `construction.m3g` | 15, 15 | -7, -7 |
| `depository.m3g` | 2, 2 | -0.5, -0.5 |
| `military.m3g` (1) | 18, 18 | -8.5, -8.5 |
| `military.m3g` (2) | 1, 4 | 0, -1.5 |
| `military.m3g` (3) | 2, 2 | -0.5, -0.5 |

Numbers in `docs/m3g-texture-transform-results.json`.  A decode-level test in
`tests/test_m3g_file.c` now builds the eighteen-plus-seventy-two-byte payload
shape the five objects use (no component transform, general flag, 4x4, sampler)
with the construction map's 15x/-7 matrix and requires the decoder to expose
exactly that matrix and the sampler bytes after it.

## The pixel-level proof

`test_texture_transform_matrix` (in `test_m3g`) uses the standard textured
quad the other texture tests use - 240x320 target, vertices `+-4` at z=-4 so
the quad is exactly the viewport - with a 4x1 texture whose texels are red,
green, blue and yellow, REPLACE blending, WRAP_CLAMP and NEAREST filtering.

The sampler picks `u = (int)(s * (tex_w - 1) + 0.5)`, i.e. it **rounds** to the
nearest of three steps for a four-texel texture, and the quad's raw s is
`x / 240`.  With an identity texture matrix that puts the colour changes at
x = 40, 120 and 200; the test pins x=90 green and x=210 yellow.

* **Scale 0.5 in s**: `s' = x/480`, so the index is 0 until x is about 80 and
  1 after it - the whole quad samples the left half of the strip.  The test
  requires x=30 red and x=150/210 green (the last two were blue and yellow
  before the matrix).
* **Translate +0.5 in s**: `s' = s + 0.5`, clamped at 1 by WRAP_CLAMP, so
  x=30 samples the blue texel and everything from x=90 on samples the last
  (yellow) one - a translate of the window, not a shift of colour.
* **Reset to identity**: x=90 green and x=210 yellow again, which proves the
  matrix - not the binding or some leftover state - is what changed the
  pixels.

The sampler's rounding rule is the reason the expectations are 30/90/150/210
rather than a naive `floor(s*4)`: the first version of this test assumed
flooring and was wrong about two pixels, which is exactly the kind of mistake
only a real rendered check catches.

## Red/green

The test is mutation-proven: a build of the same tree with the texture-matrix
application disabled (`if (0 && r->tmat)` in `jr_vertex`) fails exactly the
scale and translate checks (`779 checks, 2 failures`) while the identity and
reset checks still pass; this tree is `test_m3g` **779 checks / 0 failures**,
`test_m3g_file` **105 / 0**, full ctest **118/118**.  Logs:
`.local-tools/debug-runs/m3g-texture-transform/{mutation,fixed}-test_*.log`.

## Not claimed

No Counter-Strike gameplay frame evidence: a 1,600-frame headless run of
`CS反恐精英(蓝牙对战)_6858` reaches only its own loading screen
(`.local-tools/debug-runs/tex2d-target/cs-run.log`: `tris=0 m3g=0`,
`bytecodes=351`), so the maps cannot be driven without understanding the
title's network/Bluetooth gate first.  What is verified here is the real files'
matrices (decoded from the corpus) reaching pixels through the actual renderer;
the gameplay frame is the remaining step, and it is blocked on the title, not
on the renderer.
