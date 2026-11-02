# The vertex path wakes up: a heavy landscape scene at clang -Os (2026-11-02)

Round 5 tuned the span rasteriser for the codegen the APK actually uses
(`tools/build_android.sh --opt` defaults to `s`), but its profile ran the
saved *water-start* view: about 9 k submitted triangles per frame, most of
the screen taken by a handful of large ground quads.  In that scene the
raster pixel loop dominated and the per-vertex projection was a twelfth of
the run.  This round replays a real landscape view instead - shoreline,
terraced hillside, trees, sky - and the ranking changes: the per-vertex
path and the per-triangle setup together are now twice the pixel loop.

Everything below is measured against `dfd6522` (the round-5 integration) on
the desktop with clang 23.1.4, `-Os -DNDEBUG`.  The one source change is
bit-exact and is committed with this note.

## The heavy scene

`bh.state` is a 34 MB snapshot taken 150 frames into the normal Minecraft PE
0.6.1 world, after two pointer drags turned the camera towards the
landscape:

```
./build-os/nj2me --port headless --heap 32 --size 240x320 --fps 0 \
  --load-state .local-tools/mc051/world.state --run-frames 150 \
  --save-state bh.state --save-state-frame 150 \
  --keys '5:pressed@120,160 8:dragged@220,160 10:released@220,160' \
  tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar

./build-os/nj2me --port headless --heap 32 --size 240x320 --fps 0 \
  --load-state bh.state --run-frames 1200 --stats \
  tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar      # tris=3,939,668 for 1200
```

`--stats` counts the triangles that wrote at least one pixel: **3,283 per
frame**, 2.3x the water-start view (about 1.4 k).  The scene is stable
enough for A/B: two 600-frame replays differ by 0.17 % in triangle count and
by 0-37 pixels in the cloud/light band; the title's wall-clock animation can
also flip the whole scene between two lighting phases (10,278 pixels), and
that flip appears between two *baseline* runs as well (see Correctness).

## Where the -Os time goes

`-Os -pg -fno-pie` profile, 1200 frames from `bh.state`, pinned to one core,
gprof flat profile (total 3.57 s of samples; `nj_crc_update` and `nj_vm_load`
are the snapshot load, not frames):

| function | self s | % | calls | role |
|---|---|---|---|---|
| jr_emit (with sw_tri inlined) | 1.56 | 43.70 | 20,607,932 | span setup + pixel loops |
| jr_project | 0.60 | 16.95 | 44,094,896 | per-vertex projection |
| jr_fetch (with jr_vertex inlined) | 0.29 | 8.12 | 43,798,040 | per-vertex fetch |
| nj_crc_update | 0.19 | 5.32 | 2 | snapshot load, not frames |
| nj_interp_run | 0.16 | 4.48 | 8,218 | Java |
| jr_triangle_v | 0.12 | 3.50 | 21,899,020 | near/far test |
| jr_strip | 0.10 | 2.80 | 10,949,510 | strip walker |
| nj_field_lookup | 0.09 | 2.52 | 20,708,040 | field memo |
| sw_fog | 0.07 | 1.96 | 37,276,632 | sky-plane fog |
| nj_region_draw | 0.07 | 1.96 | 21,204 | MIDI HUD blits |

`gprof -l` buckets the same samples by source line.  The ranges are the
loops in `src/midp/nj_lib_m3g.c`:

| range | self s | % of run | what |
|---|---|---|---|
| sw_tri setup (11403-11687) | 0.65 | 21.7 | bbox, edge functions, interpolants |
| flat_texargb pixel loop (11776-11980) | 0.68 | 22.7 | terrain span loop |
| jr_project (12206-12223) | 0.61 | 20.3 | projection |
| jr_vertex (12224-12597) | 0.23 | 7.7 | position/normal/texcoord fetch |
| jr_strip + jr_triangle_v | 0.21 | 7.0 | |
| flat_opaque (11688-11706) | 0.04 | 1.3 | sky plane |
| flat_tex565, generic | 0.00, 0.05 | - | not used by this title |

Two artifacts are worth rejecting explicitly, because a previous profile
ranked them high:

* **`nj_crc_update` is the 34 MB snapshot load.**  A 60-frame profile from
  the same state shows it at 0.19 s with the *same* two calls and the same
  absolute cost as the 1200-frame run, so it is startup, not frame work.
* **`sw_fog`'s 37.3 M calls are real** (31 k per frame: the flat-opaque sky
  plane asks the fog for every pixel), but its line samples are 0.07 s and
  the flat-opaque loop around it adds 0.04 s - a little under 3 % of the
  run.  A specialisation of that loop was therefore not the right first
  move in this scene, even though it is real work.

## The change: hoist the viewport half-size out of the projection

`jr_project()` is called once per submitted vertex (44.1 M times here) and
its last two arithmetic statements were

```c
out[0] = (x / ww + 1.0f) * 0.5f * (float)w;
out[1] = (1.0f - y / ww) * 0.5f * (float)h;
```

At `-Os` the compiler leaves `jr_project` out of line (it does so at `-O3`
too, see the round-5 note) and converts the two `int` viewport dimensions to
float inside every call.  The half-size is constant for a draw, so
`jr_setup()` now stores `r.hw = 0.5f * (float)r.vw` and
`r.hh = 0.5f * (float)r.vh` in the `m3g_jr` and the body is

```c
out[0] = (x / ww + 1.0f) * hw;
out[1] = (1.0f - y / ww) * hh;
```

with every other statement untouched.  The other four call sites (sprite
centre, the two clip extremes) compute the same halves inline from their own
`vw`/`vh`; they are once per draw.

**Bit-exactness.**  `0.5f * (float)w` rounds once and is exact for every
integer `w < 2^24`, so `hw` *is* `w/2` as a float.  Both forms therefore
evaluate `round(t * w / 2)` with a single rounding for every reachable `t`
(`t = x/ww + 1` is zero or a normal float near 1, never subnormal), and both
overflow to the same infinity.  The `ww == 0` early-out, the `inv[0] = 0`
initialisation and the `ww > 1e-6f` guard are unchanged.  The frame dumps
below confirm it pixel for pixel.

**What did not work.**  Writing the whole body inline at the per-vertex call
site (to remove the call and its argument setup) made the frame **2.7 %
slower** at `-Os` in a 3-round ABBA: `jr_fetch` grows, and the extra live
values cost more than the call.  That is the same lesson as the round-5
`always_inline jr_project` experiment (+2.5 %), now measured with the heavy
scene; the body stays out of line and only its inputs are hoisted.

## Measurement

1200 in-world frames from `bh.state`, one pinned core (the quietest sampled
from `/proc/stat`), user CPU from `bash time`, order A B B A per round.
A = pristine `dfd6522` clang `-Os`, B = this change.

| round | A | B | B | A |
|---|---|---|---|---|
| 1 | 4.340 | 4.099 | 4.122 | 4.226 |
| 2 | 4.212 | 4.094 | 4.091 | 4.208 |
| 3 | 4.297 | 4.053 | 4.168 | 4.182 |
| 4 | 4.178 | 4.101 | 4.138 | 4.228 |
| 5 | 4.215 | 4.138 | 4.065 | 4.231 |
| 6 | 4.219 | 4.106 | 4.147 | 4.226 |
| 7 | 4.248 | 4.071 | 4.102 | 4.180 |
| 8 | 4.179 | 4.065 | 4.078 | 4.185 |

Mean **4.2221 s → 4.1024 s = -2.84 %** (median -2.77 %), B faster in 8/8
rounds.  A first 8-round run taken while the test suite was running still
showed -1.95 % mean / -3.00 % median, and the quick 3-round direction check
before that -2.84 %; the sign and size are stable.

`-O3` desktop side effect (gcc 13 Release, same source, 4 ABBA rounds,
`abba-o3.out`): mean **4.0081 s -> 3.8831 s = -3.12 %** (median -3.17 %), so
the hoist pays off at `-O3` as well - the out-of-line call is kept there too.

## Correctness

* `build/test_m3g`: **878 checks, 0 failures**.
* Same binary under ASan + UBSan (`-DNJ_SANITIZE=ON`, Debug): **878 checks,
  0 failures**, no diagnostics.
* `tools/check_m3gdemo.sh`: `M3G checks=148 failures=0`.
* `tools/check_no_statics.sh`: OK (75 object files, read-only tables only).
* `ctest --test-dir build`: **137/137 passed** (246.95 s; a fresh worktree
  registers 137 of main's 140 - the `test_port_*` ones need port fixtures).
* Frame dumps: 12 frames (`--run-frames 61 --dump-every 5`) from `bh.state`
  for two candidate runs against **four** interleaved pristine `-Os` runs.
  Every candidate frame is pixel-identical to at least one pristine frame on
  9/12 frames, and the worst match is 3 pixels (frame 15); the
  same-phase baseline-vs-baseline jitter on those frames is 0-37 pixels.
  The 10,272-10,280-pixel differences that appear between some runs are the
  title's wall-clock lighting phase, and they divide the *baseline* runs
  the same way (bbox (26,2)-(239,292), i.e. not a renderer edge).  No
  renderer-path pixel difference was found.

## What is left, and what was ruled out

The profile now says the remaining per-triangle setup (21.7 %) and the span
pixel loop (22.7 %) are the two big blocks, with the vertex path at ~31 %
split between projection, fetch and strip walking.

* **Not taken:** the flat-opaque sky-plane specialisation suggested by the
  profile's 37 M `sw_fog` calls - the loop is only ~3 % of this scene, and
  the per-pixel fog is a short inline already; a specialisation would need
  per-triangle fog saturation to be hoisted, and that is already done
  (`fog_state` in `sw_tri`).
* **Not taken:** caching near/far clip distances per vertex (the fast path
  recomputes six dot products per triangle for vertices that the strip
  already fetched).  It would save roughly 1-2 % by this profile, but it
  changes `jr_vtx` and the strip cache, and the round-4 pointer-ring
  experiment showed that touching that code path costs more than it looks.
* **Next:** the largest single line is still the second projection row
  (`jr_project`'s last statement, 0.23 s) - a division, so no further
  bit-exact re-association is available; the next practical step is looking
  at the per-triangle setup for the ~80 % of submitted triangles that write
  no pixel, which is where the 0.65 s of setup is spent.
