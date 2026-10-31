# The vertex colour was averaging itself, and other small per-vertex costs
# (2026-10-31)

After the fog saturation, the fused reciprocal and the faint-alpha skip landed
(-18% user CPU), the flat textured span loops still dominate an in-world
Minecraft PE frame.  This note is about what is left around them: the
per-vertex fetch, and two small pieces of arithmetic that turned out to be
doing more work than the answer needs.  Everything here is bit-exact; the
frame dumps are identical, not merely close.

All measurements are the same saved world (a 240x320 Minecraft PE session
paused in game, resumed from the saved `v3` snapshot), 1200 frames, headless,
`--heap 32 --size 240x320 --fps 0`, pinned with `taskset` to one core, user CPU
from bash `time`, alternating baseline/candidate rounds because the host load
moves by up to 10% between minutes.

## Profile first: the profiling build has to match the release flags

The release build is `-O3 -DNDEBUG` (the project's `CMAKE_BUILD_TYPE=Release`
default).  A `-O2 -pg` profiling build is *not* a stand-in: at -O2 the compiler
leaves loop-invariant loads and branches in the hot loops that -O3 unswitches
and hoists, so a -O2 profile points at costs that the shipping binary does not
pay.  Two candidate changes were measured as wins on the -O2 profile and as
noise on the -O3 binary (see "ruled out").

A `-O3 -DNDEBUG -pg -g -fno-pie` / `-no-pie` profile of 1200 frames is sane
(the bogus `m3g_tsa_getStripLengths` call count and the `_init` bucket that a
PIE + `-pg` build produces are gone; the function's self time, 3.6%, is real,
its call count is not - `vm_invoke` is called only 2.8 M times in the same
run).  Total sampled time 2.80 s, of which the snapshot load
(`nj_vm_load` + `nj_crc_update` inside it) is 0.41 s = 14.6%.  Frame work is
therefore 2.39 s of samples for 1200 frames:

| function | self s | % run | % frame work | what it is |
| --- | --- | --- | --- | --- |
| `jr_emit` (holds the inlined `sw_tri` span loops) | 1.18 | 42.1% | 49.4% | triangle setup + span fill |
| `nj_interp_run` | 0.31 | 11.1% | 13.0% | Java/M3G Java-side per frame |
| `jr_fetch` | 0.27 | 9.6% | 11.3% | per-vertex fetch |
| `jr_project` | 0.12 | 4.3% | 5.0% | per-vertex projection |
| `m3g_tsa_getStripLengths` | 0.10 | 3.6% | 4.2% | Java `getStripLengths()` bridge |
| `nj_field_lookup` | 0.08 | 2.9% | 3.3% | Java field resolve |
| `nj_region_draw` | 0.08 | 2.9% | 3.3% | MIDP image blits (HUD) |
| `vm_invoke` | 0.06 | 2.1% | 2.5% | native-method dispatch |
| `collect_lights_walk` | 0.04 | 1.4% | 1.7% | per-frame scene walk |
| `jr_triangle_v` / `jr_strip` | 0.06 | 2.2% | 2.5% | clip test / strip walk |

Line-level (-pg -g) attribution inside the frame path adds: `sw_tri`'s own
lines 0.96 s, `sw_near_texel` (inlined into `jr_emit`) 0.20 s = 6.8%,
`sw_fog` 0.13 s = 4.4%, `jr_vertex` 0.24 s = 8.2%, `jr_project` 0.12 s.  (The
old `jr_colour()` shows only 0.01 s of its own at -O3 - it is inlined into the
fetch, so its cost lives in `jr_vertex`'s 0.24 s.)  So the fill is still the
biggest single block, but
the per-vertex path - fetch, project, colour - is now 17% of frame work, and
that is where this change goes.

## 1. The unlit vertex colour was a three-way average of itself

`jr_fetch()` called `jr_colour(r, v, v, v)` for every unlit vertex.  That
function averages the colours of three vertices (JSR 184's flat-shaded
fallback), and it did so with integer divides:

```c
sum[k] += s[k] & 0xFF;                       /* three times, same vertex */
...
return ((sum[3] / 3u) << 24) | ((sum[0] / 3u) << 16) | ...
```

Three copies of the same component sum to `3*x`, and `3*x/3 == x` exactly in
integer arithmetic (the mask keeps `x` in 0..255), so the answer is simply the
vertex's own RGB.  The rasteriser keeps no alpha for an unlit vertex, so the
alpha term was computed and thrown away as well.  `jr_colour1()` reads the
three components directly.  25.5 M vertices in 1200 frames means 77 M integer
"divisions" (they are constant divides, so the compiler turns each into a
multiply, but there are still three of them and the loads and the masks around
them) removed.  The ablation that measured this item together with item 2 put
the pair at -1.2%.

## 2. jr_vertex zeroed its outputs a second time

`jr_fetch()` zeroes every field of the `jr_vtx` it is about to fill, because a
vertex it cannot fetch has to come back as zeros.  `jr_vertex()`, its only
callee, then zeroed the same fields again - eight float stores per vertex
across the whole scene.  The function now relies on the documented caller
contract and only initialises `p`, the local position, which genuinely needs
zero padding for a position with fewer than three components.

## 3. floorf()/ceilf() and the clamped bounds

The span loop rounds the triangle's edges to whole pixels twice per scanline
and its bounds four times per triangle.  At -O3 without SSE4.1 these are
already inlined by the compiler (the six remaining `ceil`/`floor` PLT calls in
the binary belong to the TrueType scaler, not the rasteriser - check before
"optimising" them away), so there is no libm call to remove; what is left is
the conversion itself.  The bounds conversion is now done after clamping the
triangle's extents to just outside the viewport - which cannot change a pixel,
because the resulting integers are clamped to the viewport immediately
afterwards - and that keeps the conversion inside the range of an `int`,
which is what makes the explicit `sw_floor_i()`/`sw_ceil_i()` sequences safe
for any coordinate the near-plane clip can produce.  The two scanline
conversions are unchanged in behaviour: `lo`/`hi` start at the already
viewport-clamped `fminx`/`fmaxx` and only move inward.

## Measurements

Alternating rounds, baseline = the main tree's `build/nj2me` at `6a20fe0`,
candidate = the worktree build, both from the same saved state, 1200 frames,
`taskset -c 10` (the quietest core of the minute):

| round | baseline user s | candidate user s | delta |
| --- | --- | --- | --- |
| 1 | 3.243 | 3.199 | -1.4% |
| 2 | 3.277 | 3.141 | -4.2% |
| 3 | 3.205 | 3.191 | -0.5% |
| 4 | 3.203 | 3.162 | -1.3% |
| 5 | 3.274 | 3.165 | -3.3% |
| 6 | 3.245 | 3.222 | -0.7% |
| 7 | 3.227 | 3.189 | -1.2% |
| 8 | 3.224 | 3.156 | -2.1% |
| mean | 3.237 | 3.178 | **-1.8%** |

Eight of eight rounds are negative.  Ablation: the `jr_colour1()` + zero-store
part alone measured -1.2% (six rounds, five of six negative, same method), so
item 3 contributes roughly the remaining half percent.

## Correctness

* `tests/test_m3g.c`: 818 checks, 0 failures.  The wrap/alpha/fog/blend
  regressions are untouched by this change (nothing here is behind
  `NJ_M3G_FAST_PATHS`; the arithmetic is the same arithmetic).
* `M3GDemo`: `M3G checks=148 failures=0` (`tools/check_m3gdemo.sh`).
* `ctest`: 137/137 in this worktree.  A fresh worktree registers 137 of the
  main tree's 140 tests: the three `test_port_drop`/`test_port_binds`/
  `test_port_gamepad` cases need generated port fixtures that only the main
  tree has, and they do not touch M3G.  (The tests only registered at all once
  the gitignored `tests/games` and `tests/fixtures` from the main tree were
  copied in; a worktree without them reports missing-jar failures that have
  nothing to do with this change.)
* Frame dumps of the same snapshot (PPM, 240x320), first three frames:
  **0 of 76800 pixels differ** from the baseline binary, and a baseline
  rerun also differs by 0 - the frame path is bit-exact.
* Frames 80/160/240 (the clouds are animated from the wall clock): baseline
  against its own rerun 11/17/16 pixels, baseline against candidate 8/14/16
  pixels, all inside the sky band y = 6..100, x = 27..228 in both cases.  The
  candidate changes no pixel outside the band the title animates by itself,
  and the count inside it is within the run-to-run jitter of two baseline
  runs.

## Ruled out (measured, not committed)

* **Hoisting the `sw->` fields out of the flat textured loops** (athresh, fog
  colour, texture pointer/size/wrap passed to an inlined sampler): looked like
  -8% on a `-O2 -pg` profile and measured -0.0% (five ABBA rounds: +0.7% mean)
  on the -O3 release binary.  At -O3 the compiler already unswitches those
  loops and hoists the invariant loads; the -O2 profile was measuring a cost
  the shipping binary does not pay.
* **`floorf()`/`ceilf()` libm calls**: already inlined by -O3 in this file
  (see item 3); the two hot source lines are the conversions, not calls.
* **`stats_printf()` called 5.6 M times from `vm_invoke()`**: 0.8% self, a
  guard belongs in `src/core/nj_vm.c` and is left for the VM work.
