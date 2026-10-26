# M3G: a World draws every Mesh node, not the first 64

`毁灭战士3_10598.jar` reached gameplay, but the in-game 3D view was mostly
black: a corridor drew only where the first few dozen level panels were, and
after the player moved forward only a bottom strip (or later, a small room)
drew at all, with hard axis-aligned black edges around it. The title menu's
`World` rendered correctly the whole time, and the crosshair, weapon and ammo
HUD always drew. The cause was a fixed 64-Mesh limit in the World renderer:
Doom 3's level `World` carries 173 `Mesh` nodes, and the World's `Background`
clears the frame to black, so every mesh past the 64th was an invisible black
hole rather than an obviously missing object.

The fix does not special-case the game or the JAR: any scene graph with more
than 64 `Mesh` nodes was truncated.

## Symptom and first measurement

Headless replay, 240x320 at 30 FPS, fresh RMS directory:

```
./build-lead-render/nj2me --port headless --size 240x320 --fps 30 \
  --rms-dir "$RUN/rms" --run-frames 460 \
  --keys '130:down:down,136:down:up,150:fire:down,152:fire:up,200:up:down,240:up:up,300:right:down,330:right:up' \
  --dump-frames "$RUN/frames/" --dump-every 10 --stats tests/games/毁灭战士3_10598.jar
```

The script opens the menu with Down+Fire, holds UP for 40 host frames to walk
forward, then RIGHT to turn. `--dump-frames` numbers presented frames, not host
frames. Fraction of pixels that are exactly black (sampled every 2nd pixel),
before = `.local-tools/debug-runs/doom-gameplay-01/frames`, after =
`.local-tools/debug-runs/render-E/frames`:

| presented frame | before | after |
| --- | --- | --- |
| 00010 - 00150 (title/menu) | 0.936 / 0.829 | byte-identical |
| 00170 (spawn corridor) | 0.327 | 0.057 |
| 00210 - 00330 (after walking forward) | 0.739 / 0.816 | 0.000 |
| 00350 (after turning right) | 0.741 | 0.099 |
| 00460 (corridor) | 0.739 | 0.101 |

Before/after screenshots for frames 00170, 00210, 00310, 00350, 00390, 00460:
`.local-tools/debug-runs/render-E/before-after.png`. The menu frames are not
just visually similar - they hash byte-identical before and after the fix.

## Instrumentation: the black was never covered by geometry

A temporary env-gated build (removed; it was never committed) logged every
`render(World)` and dumped the 3D target plus depth-buffer coverage. The
depth buffer `Graphics3D._nj_zbuf` is cleared on every `bindTarget`, so `z>0`
marks exactly the pixels a render's triangles covered.

Before the fix, at the level's spawn frame (`frames=151`):

```
M3GDBG render=63 frames=151 root=javax/microedition/m3g/World meshes=173 drawn=124 lights=1
      vp=(0,0,240,320) bg=1 bgcol=00000000 cclear=1 dclear=1
M3GDBG dump63 rend=63 surf=240x320 z=55674 zblack=4361 black=25487 zbbox=(0,0)-(238,319)
```

Only 55,674 of 76,800 pixels were covered, and the uncovered ones were all
black because the World's own `Background` clears to `0x00000000` with both
colour and depth clear set. When the UP press moved the camera the submitted
set shrank further - `drawn=124` at `frames=151`, `108` at `frames=201`, `39`
at `frames=221` - and the covered depth bbox collapsed to a bottom strip
(`zbbox=(0,225)-(238,319)`), which is exactly the "black except a floor strip"
frames 210-330.

After the mesh-cap fix the same spawn frame covers the screen:

```
M3GDBG render=63 frames=151 root=javax/microedition/m3g/World meshes=173 drawn=253 lights=1
M3GDBG dump63 rend=63 surf=240x320 z=76148 zblack=2698 black=3350 zbbox=(0,0)-(238,319)
```

- Instrumented pre-fix run: `.local-tools/debug-runs/render-B` (log and 200 PPM
  dumps under `render-B/d3d/`).
- Instrumented post-fix run: `.local-tools/debug-runs/render-C`.
- Post-fix coverage probe at every few frames: `.local-tools/debug-runs/render-D`.

## Root cause

`m3g_render_scene()` in `src/midp/nj_lib_m3g.c` walked the scene into fixed
arrays:

```c
nj_ref meshes[64];
float mats[64 * 16];
n = nj_m3g_collect_meshes_m(vm, root, meshes, mats, 64u);
```

`nj_m3g_collect_meshes_m()` / `collect_walk()` keep counting past the capacity
on purpose (the count is what the caller sizes allocations from), so `n` came
back as 173 - but only 64 entries had been stored. Then `m3g_world_draw()`
looped:

```c
for (i = 0u; i < n && i < 64u; i++)
```

(that line is in the pre-fix source, `git show fd02094^:src/midp/nj_lib_m3g.c`,
line 11544). So the whole scene graph was walked, and then only the first 64
`Mesh` nodes were drawn. The 173:1 ratio is not exotic: a level builder splits
rooms and props into one `Mesh` node each, and Doom 3's level World is one
`Mesh` per wall/floor/door panel.

Both entry points were affected, because `render(World)` and
`render(Node,Transform)` share `m3g_render_scene()`.

The 64-entry bound existed to keep the render path allocation-free on the
stack; 64 was an arbitrary "big enough" pick. The earlier note
`docs/notes/2026-10-21-m3g-world-lights.md` had already traced
`meshes=173 drew=124` and read the numbers as healthy - at the time lighting
was the bug, and the trace counted submitted triangles, not mesh nodes.

## Fix

`src/midp/nj_lib_m3g.c`:

- `#define M3G_SCENE_MESHES_STACK 64u` (line 3731): the stack arrays stay, so
  the common case still allocates nothing.
- `m3g_render_scene()` (line 3878 onwards): walk once into the stack arrays;
  when the returned count does not fit, guard against a size overflow, allocate
  `n * (16 * sizeof(float) + sizeof(nj_ref))` bytes with `nj_host_alloc()`,
  walk a second time with `cap = n`, draw, and `nj_host_free()` at the end.
- `m3g_world_draw()` (line 11590, loop at line 11597): iterates all `n`
  meshes; the `i < 64u` clamp is gone.
- Degenerate cases are explicit: 0 meshes walks and draws nothing; allocation
  failure draws the stack-array prefix (better than nothing); a second walk
  that returns more entries than the first is clamped to the allocation.
- Scenes with 64 or fewer `Mesh` nodes take the stack path unchanged: same
  arrays, same walk, same draw loop bounds - and the menu frames are
  byte-identical before and after.
- One more thing removed while finishing: two stray blank lines left at
  line 3079 by the temporary instrumentation strip.

Cost: one host allocation per render for a big World. For 173 mesh nodes that
is `173 * (16 * 4 + 8) = 12,456` bytes per frame. Whole-run A/B with the
replay above (same script, `--stats`):

| | before | after |
| --- | --- | --- |
| `tris` | 55,859 | 118,520 |
| `m3g` | 40,357 | 107,209 |
| `frames` / `paints` | 460 / 117 | 460 / 117 |
| `fps` | 18.3 | 16.5 |

Drawing 2.1x the triangles costs ~10% of the headless frame rate; the visible
result is the level instead of black.

## Results

- Gameplay renders: `before-after.png` (cited above) is the same replay before
  and after. Frames 210-330, previously clear-black except a floor strip, show
  the camera walked into a wall; 390-460 show the corridor with the weapon and
  HUD.
- Menu regression: frames 00010-00150 hash byte-identical before and after, so
  the <=64-mesh path is untouched.
- Determinism: two clean post-fix runs (`.local-tools/debug-runs/render-E` and
  `render-F`) produced 46 of 46 dumped frames byte-identical.

### Movement was never broken

Before the fix the submitted set did change with the camera
(`drawn=124 -> 108 -> 39` across `frames=151 -> 221`): input was reaching the
camera the whole time. Frames 210-300 looked frozen because, at the new camera
position, the drawn prefix (first 64 nodes) had almost nothing on screen and
the rest of level was never drawn - the unchanged part of the image was the
black clear. With the fix, the same script shows forward movement (frame 210:
wall close-up), then a right turn (frame 330), consistently.

### The residual black rectangle is content, not missing geometry

Post-fix frames 350-460 still have ~10% black: a hard-edged rectangle right of
centre. A coverage probe at `vmf=340`
(`.local-tools/debug-runs/render-D/runtime.log`) shows it is covered:

```
M3GDBG rend=65862 vmf=340 z=76405 zblack=7530 black=7925 zblackbbox=(108,132)-(238,227)
```

76,405 of 76,800 pixels are covered (99.5%), and the black rectangle's bbox is
inside `zblackbbox`, i.e. the pixels have depth written to them: the rasteriser
did render geometry there and that geometry shaded to black. The ASCII coverage
map marks rows 16-28 x columns 19-29 - exactly the rectangle - as `b`
(covered black), not `.` (uncovered). So it is a dark surface: the level is lit
by one ambient `Light` of intensity 3.0 (see the 2026-10-21 note) and Doom's
J2ME level uses black faces for door openings / the darkness beyond a door.
The remaining ~4% of black (`black - zblack`) is the rightmost pixel column
`x=239`, uncovered in every frame before and after this fix - a rasteriser
viewport-edge artefact, 0.4% of the frame, unrelated to the mesh cap and not
addressed here.

Could not determine which `Mesh` node / material that surface is: the loader
keeps no per-mesh name for this file, and narrowing it would need a temporary
per-mesh colour trace. Nothing in the evidence points at a renderer defect
(`zblack` proves coverage, and neighbouring panels of the same world shade
normally).

## Regression test

`tests/test_m3g.c` (fixture `world_many_meshes_scene()` line 1981, test
`test_world_mesh_count()` line 2077, called from `main()` line 4792):

- Builds one `World` with 70 `Mesh` nodes sharing one vertex buffer, one
  `TriangleStripArray` (a quad) and one `Appearance`, each node translated to
  its own column of a 240x320 viewport. With `Camera.setPerspective(60, 0.75,
  1, 1000)` and the quad at `z = -200`, one camera-space unit is 1.3857 pixels,
  so each node is 3 pixels wide at its own column; at most one node owns each
  sampled column centre.
- Renders into `Image.createImage(240, 320)`: `getGraphics()`, `bindTarget()`,
  `setViewport(0,0,240,320)`, `setCamera()` with an identity Transform,
  `clear(black Background)`, `render(World)`, `releaseTarget()`.
- First asserts the clear painted all 76,800 pixels black. `Image.createImage()`
  starts white, so without this a render that draws nothing at all would pass
  the "column is not black" checks vacuously.
- Then asserts all 70 column centres are non-black.

Red/green evidence, both with the same final test source:

- Pre-fix source (`git show fd02094^:src/midp/nj_lib_m3g.c`):
  `lit 64 of 70 columns: the truncating render draws 64` -
  `660 checks, 1 failures`, `FAILED`. Log:
  `.local-tools/debug-runs/render-fix/test_m3g-prefix.log`.
- Fixed source: `660 checks, 0 failures`, `PASS`. Log:
  `.local-tools/debug-runs/render-fix/test_m3g-fixed.log`.

The 70-node World also exercises the heap-growth path (>64 nodes).

## Verification

```
cmake -S . -B build-lead-render -DCMAKE_BUILD_TYPE=Release
cmake --build build-lead-render -j"$(nproc)"
cd build-lead-render && ctest -R m3g --output-on-failure   # 8/8 passed
./test_m3g                                                 # 660 checks, 0 failures
```

`check_m3g_reals` (the real-JAR m3g corpus probe) is part of that ctest set and
passes. One more real 3D title, `浴火银河2完整版_7689.jar`, replayed with the
deep key script from `tools/check_m3g_titles.sh`: pre-fix `tris=39186
m3g=11446`, post-fix `tris=40084 m3g=12340`, no exception, both runs still draw
the space scene and menus; 12 of 28 dumped presents differ. That frame-index
difference is not a clean A/B (the extra geometry shifts the game-loop phase);
no failure or corruption appears in either run.

## Where the fix landed

The renderer change is in commit `fd02094` (a concurrent codec session's
`git add -A` committed the working tree at the time); that commit's message
only describes the m4v parser and the removal of the instrumentation.
`git show fd02094^:src/midp/nj_lib_m3g.c` is the pre-fix source used for the
red test above. The finished patch, including the blank-line cleanup, is
`.local-tools/wip/render-meshcap-fix.patch`; the committed part alone is
`.local-tools/wip/render-meshcap-fix-committed.patch`, and the regression test
is `.local-tools/wip/render-meshcap-test.patch`.

## Remaining limitations

- World light collection still has a hard 16-entry cap (`nj_ref slights[16]`,
  `src/midp/nj_lib_m3g.c:3740`, and `collect_lights_walk(..., 16u, ...)` at
  line 3817). A `World` with more than 16 `Light` nodes silently loses the
  rest - the same class of fixed-array truncation as this bug. Doom 3 has one
  light here, so it was not hit; worth a follow-up.
- The mesh arrays are reallocated and the graph re-walked on every render of a
  World with more than 64 `Mesh` nodes (12,456 bytes per frame for this level).
  That is within run-to-run noise here, but caching the array on the
  `Graphics3D` would remove it.
- The black rectangle described above is assumed authored (a doorway/dark
  room); the level's palette is genuinely dark. This note claims the level is
  fully drawn, not that it matches a handset's look.
- The rightmost pixel column is uncovered in every frame (rasteriser edge,
  0.4% of the frame), before and after this fix.
