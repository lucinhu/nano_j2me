# M3G: a Transformable keeps translation, rotation, scale and matrix apart

Two `retro` titles from the bundled 3D corpus showed the same class of bug
from opposite ends. In `毁灭战士3_10598.jar` the level's sliding door leaves
stood *on the floor* beside their door frames instead of hanging in them: the
arch was a black hole, and a grey panel sat in front of the wall, lower than
the frame base. In `极品飞车14之热力追踪中文版_4160.jar` the opponents were
missing from the grid - the HUD read `排名第4/4` while the road ahead was
empty, and on the player's Android handset the same nodes appeared as tiny
models instead of full-size cars.

Both are the same defect: `javax.microedition.m3g.Transformable` kept one
matrix, and `translate()`, `scale()`, `postRotate()` and `setTransform()`
composed that matrix in the wrong order (or overwrote what should not be
overwritten). The JSR-184 model is five pieces - translation, rotation, scale,
the `setTransform()` matrix, and the product `T * R * S * M` - and a node's
own rotation must not turn the translation the game asks for.

## Symptom: the door leaf is on the floor

Doom 3's `Doom.m.matemDoor()` places both leaves of the respawn door every
frame:

```java
worlddoor.setTranslation(dirCamD1, 0, dirCamD2);  // dirCamD2 = 4.8
worlddoor.translate(0, 2.7f, 0);                  // eye height
```

The leaves are authored in `/doors/mars.m3g` with a `-90` degree rotation about
X (`userID` 823 and 824), so the second call is meant to lift the leaf from its
own local frame to world height. Our `translate()` post-multiplied the
translation into the node's matrix:

```
old:  M = T_setTranslation * R_file * T_translate
new:  composite = T * R * S * M  with T = T_setTranslation * T_translate
```

With `R_file` = `-90` about X, `(0, 2.7, 0)` became `(0, 0, 2.7)`, so the leaf
landed at world `y = 0` (on the floor) and `z = 4.8 + 2.7 = 7.5` (closer to the
camera) instead of `y = 2.7`, `z = 4.8` (in the frame). A temporary probe of
the mesh model translations in `m3g_world_draw()` confirmed the fix: at
presented frame 160 the two leaves report `t = (-1949, 2700, 4800)` and
`(1949, 2700, 4800)` in millimetres - exactly the game's own numbers - and the
third sliding door `worlddoor01` (userID 889, `translate(0, 2.0f, 0)`) reports
`y = 2000`. `.local-tools/debug-runs/doom-doorfix-xf2`.

The before/after pair is `.local-tools/debug-runs/doom-door-prefix-01/frames/00210.ppm`
(pre-fix `HEAD`, built in a worktree) against
`.local-tools/debug-runs/doom-doorfix-03/frames/00210.ppm`: in the first the
arch is black and the leaf stands on the floor at bottom left, in the second
the leaf fills its frame. Replay script (240x320, 30 FPS, headless):

```
--run-frames 340 --keys \
'130:down:down,136:down:up,150:fire:down,152:fire:up,170:down:down,200:down:up' \
--dump-frames "$RUN/frames/" --dump-every 2 tests/games/毁灭战士3_10598.jar
```

The same script at the same frame against the pre-fix build is
`.local-tools/debug-runs/doom-door-prefix-01`; only 541 pixels differ at the
spawn view (frame 170), all in columns 0-6 - the leaf's edge.

## Symptom: the opponents are invisible or tiny

Need for Speed HP clones a car node (its `ah.b(Node)` deep-copies the node
tree, then round-trips `getTranslation()` / `getOrientation()` / `getScale()`)
and places each tick with `Transformable.setTransform(Transform)`. Two things
went wrong with the old single-matrix model:

* `setTransform()` overwrote the whole matrix, so a node that had been given a
  scale or orientation before the call lost it - `ad.class` itself does
  `setScale(0.0035f, 0.0035f, 0.0035f)` then `setTransform(...)` every frame.
* `getScale()` read the column lengths of the *composite*, so a copied node
  could come back with the wrong size, and `getOrientation()` read a rotation
  that still had the translation baked into its basis.

The fix restores the five-piece model, so `setTransform()` only replaces the
matrix component and the scale/rotation survive it. Re-running the same race
script (3200 frames) after the fix:
`.local-tools/debug-runs/nfs-fix-cop-01/frames/01800.ppm` shows three rivals
on the grid next to the player car (an orange sports car and two silver cars),
where `.local-tools/debug-runs/nfs-lead-cop-04/frames/01800.ppm` had empty
road. `stats tris` went from 271584 to 355838 (+31%) because the rivals now
submit their triangles. Car files are not at fault: every Mesh/Group node in
`car_cop_standard.m3g` and `car_bmw_m3_coupe.m3g` carries an identity
translation, unit scale and 0-degree rotation, and a scratch loader check
reports 27 objects / 3 Mesh for the police car with nothing refused.

## The fix

`src/midp/nj_lib_m3g.c` (`NJ_C_XABLE`), five lazily allocated `[F` fields:

| field | meaning | written by |
| --- | --- | --- |
| `_nj_t` | translation component T | `setTranslation`, `translate` |
| `_nj_r` | rotation component R | `setOrientation`, `postRotate`, `preRotate` |
| `_nj_s` | scale component S | `setScale`, `scale` |
| `_nj_x` | matrix component M | `setTransform` |
| `_nj_m` | composite `T * R * S * M` | nothing directly; rebuilt by `xable_recompose()` |

* `xable_recompose()` at `src/midp/nj_lib_m3g.c:1574` puts the product back
  together; the renderer and `getTransform()` read `_nj_m` exactly as before.
* `m3g_node_components()` at `src/midp/nj_lib_m3g.c:274` decodes the loader's
  40-byte node transform record into the three components (translation,
  angle-axis rotation, scale), and `xable_set_file_xform()` at
  `src/midp/nj_lib_m3g.c:1609` stores them and recomposes.  This is the same
  split `Loader.applyToTransformable()` does in the JSR-184 reference
  implementation (`freej2me-plus`'s `Loader.java`).  A general matrix record
  (if a file ever carries one) still lands in `_nj_x` through `setTransform`.
* `translate()` at `src/midp/nj_lib_m3g.c:2298` adds to `_nj_t`;
  `scale()`/`setScale()` edit `_nj_s`; `postRotate()`/`preRotate()` edit
  `_nj_r` (`postRotate` = `R * R2`, `preRotate` = `R2 * R`);
  `setTransform()` at `src/midp/nj_lib_m3g.c:2172` replaces only `_nj_x`.
* `getTranslation()`, `getScale()`, `getOrientation()` read their own
  component; `getTransform()` and `getCompositeTransform()` still report the
  composite.

One deliberate divergence from the letter of the spec: `getMatrix()` at
`src/midp/nj_lib_m3g.c:2212` reports the *composite* rather than the matrix
component alone.  For every node placed with the component calls the two are
the same matrix, `getMatrix()` was the composite before the split, and
`tests/test_m3g.c` has asserted the composite (translation included) since
the feature landed; no corpus title calls `getMatrix()` on a node that mixes
the two, while `Node.getTransformTo` (which walks `_nj_m`) is what the games
use to place models.  The comment there records this.

## Regression coverage

`tests/test_m3g.c` gained three checks next to the existing transform tests
(663 checks, 0 failures):

* `setOrientation(90, 1, 0, 0)` then `translate(0, 2, 0)` -> composite
  translation `(0, 2, 0)`: a translate after a rotation moves in the world
  axes, which is exactly the Doom door case.
* `setTranslation(7, 0, 4)` then `translate(0, 2, 0)` -> composite `(7, 2, 4)`.
* `setScale(0.5, 0.5, 0.5)` then `setTransform(identity)` -> composite scale is
  still 0.5: `setTransform` must not clobber the components (NFS's
  scale-then-transform pattern).

Verification for this change:

* `ctest --test-dir build-lead -j6`: 118/118 tests passed, 82.21 s.
* `tools/check_m3g_titles.sh "$PWD/build-lead/nj2me"`: 26 3D titles, 400
  frames each, `titles that did not run cleanly: 0`; `毁灭战士3_10598` draws
  160473 triangles, and the deep pass on `浴火银河2完整版_7689.jar` reaches the
  rasteriser with 40272 triangles.
* Doom and NFS before/after frames as cited above.

## Not part of this note

* The 64-Mesh World truncation (also Doom 3) is
  `docs/notes/2026-10-25-doom3-render.md`.
* NFS HP's uniform (128,128,96) screen before a race is the game's authored
  `World` `Background` clear colour shown while its own pre-race camera
  look-at degenerates (two car positions still zero, division by a zero
  length producing NaN): the world submits nothing for ~7 s of game time on
  any M3G implementation.  It is not a renderer bug; the tan frame is the
  authored background.
* The Doom 3 intro video is MPEG-4 Part 2, which this runtime still has no
  decoder for, and `VideoControl` has no object model yet; skipping the intro
  is the game's own fallback.  See
  `docs/notes/2026-10-25-doom3-media-audit.md`.
