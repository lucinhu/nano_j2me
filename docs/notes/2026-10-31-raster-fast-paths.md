# Faster software rasterisation: one reciprocal per fragment, and two kinds of
# work a triangle can skip (2026-10-31)

Minecraft PE ran at ~405 uncapped frames a second in the saved world after the
fog and lookup work, and the flat textured path still took 40% of a frame.  The
biggest single item was arithmetic the rasteriser did not need to do twice, and
the other two are cases in which a whole triangle cannot change the result.

Everything below was measured on the same saved world (a 240x320 Minecraft PE
session paused in game, loaded from a `v3` snapshot), 1200 frames, headless,
`--heap 32 --size 240x320 --fps 0`, pinned with `taskset -c 5`, user CPU from
bash `time`, alternating rounds against the unmodified v0.5.3 binary because
the host load moves by 5-10% between minutes:

```sh
taskset -c 5 ./build/nj2me --port headless --heap 32 --size 240x320 --fps 0 \
    --load-state /tmp/njperf/world.state --run-frames 1200 Minecraft.jar
```

## Where the time went

A phase profile (`-pg` build, `gmon.out`) put `jr_emit` (which inlines
`sw_tri`) at 39% of the run, texture fetch at 6%, the interpreter at 10% and
the snapshot load at 20% (load time is not a frame cost).  The draw census
from a temporary `NJ_DRAWINFO=1` print, over 700 frames, split the fill:

| draw (all ARGB8888, alpha writes on, 4-byte textures) | triangles | what it is |
| --- | --- | --- |
| REPLACE, alpha test 128, fog 38.4/64 | 2,932,650 | terrain cut-outs |
| ALPHA, no alpha test, fog 38.4/64 | 1,720,950 | water, leaves, ice |
| ALPHA, no alpha test, fog 161/269 | 142,800 | clouds |
| REPLACE, no texture, fog 12.8/48 | 51,975 | sky plane |
| ALPHA, no texture, no fog | 6,300 | HUD quads |

Ablation builds (one `-D` each into a scratch worktree) confirmed the split:
skipping the whole flat textured path removed 40% of the run, removing only the
texel fetch from it 21%, removing the fog from it 8%, writing it as opaque 6%,
and skipping fragments whose source alpha is 1/255 5.3%.  The Gouraud path
(with the general colour interpolation) is not hot for this title.

## 1. One division per fragment, not three

The flat textured loop used to divide three times per fragment:

```c
tp  = sw_near_texel(sw, ph / pg, pk / pg);          /* two divisions */
... sw_fog(sw, pg * ia, ...)                        /* one more, inside  */
```

All three answers come from the same `1/pg`, and the fog's depth is
`area/pg` - the pixel loop already carries `pg` (interpolated `1/w`).  The loop
now computes `rcp = 1.0f / pg` once and multiplies, and calls `sw_fog_d()`,
which takes the depth directly instead of the reciprocal of a reciprocal.
Measured on its own, this is **-13%** of the whole run.

The fused reciprocal is not bit-identical to dividing each coordinate: one
more rounding in the last place.  That is harmless for CLAMP wrapping (both
roundings land on the same texel edge), and it *is* visible at a REPEAT tile
boundary, where a 1-ulp difference picks the other tile.  `tests/test_m3g.c`
caught exactly that in the wrap test; the fix is to keep the two divisions for
triangles whose texture uses REPEAT, which is cheap because the titles that
repeat are mostly the cloud plane:

```c
st_outer = has_tex && (sw->wrap_s != 240 || sw->wrap_t != 240);
tp = sw_near_texel(sw, st_outer ? ph / pg : ph * rcp,
                       st_outer ? pk / pg : pk * rcp);
```

Minecraft's terrain uses `setWrapping(240, 240)` (CLAMP) and its clouds
`(241, 241)` (REPEAT), so the terrain keeps the fast path and the clouds keep
the exact one.

## 2. A triangle that is entirely inside the fog's near plane, or entirely past
##    its far plane, does not need per-fragment fog

The fog is a line in depth: at or nearer than `fog_near` the factor is 1 (the
colour is unchanged) and at or past `fog_far` it is 0 (the colour is exactly
the fog colour).  The depths of the three vertices are the reciprocals of the
`1/w` values the setup already has, so the saturated cases can be decided once
per triangle:

```c
if (inv[0] >= sw->fog_inv_near && inv[1] >= ... ) fog_state = 1;  /* no fog */
else if (inv[0] <= sw->fog_inv_far && ... )      fog_state = 2;  /* full fog */
```

For the sky plane (fog 12.8/48, the plane itself 0.1..640 deep) most visible
triangles are past `far`, so the opaque loop writes the fog colour directly;
near terrain below `near` skips the fog arithmetic.  Measured **-4%**.

## 3. A 1/255 source alpha moves the destination by at most one unit

The cloud texture is an RGBA palette of two entries: RGB all white, alpha 1 and
255, with 80% of its texels at alpha 1.  The rasteriser wrote depth, sampled
the texture, blended, and wrote the result for each of those, changing the
destination by at most one channel unit.  Those fragments are now skipped after
the depth write:

```c
if (blend_alpha && ta <= 1u) { if (dwrite) { z[o] = key; } continue; }
```

The depth write stays because later geometry tests against it.  This is the one
place where the output is not bit-exact by design: at most one unit in one
channel of the union of those fragments, which is below the 8-bit dithering a
real device applies anyway.  Measured **-6%**.

## Result

| run | v0.5.3 | this change | delta |
| --- | --- | --- | --- |
| 1 | 2.996 s | 2.297 s | -23.3% (host was loaded) |
| 2 | 2.760 s | 2.261 s | -18.1% |
| 3 | 2.766 s | 2.274 s | -17.8% |
| 4 | 2.773 s | 2.252 s | -18.8% |

About **-18%** user CPU for 1200 in-world frames, i.e. roughly 81 -> 99 frames
a second of CPU time.  Attribution by ablation: the fused reciprocal about
-13%, the fog saturation about -4%, the faint-alpha skip about -6% (the three
overlap slightly because they all shorten the same loop).

Correctness: `tests/test_m3g.c` is 818 checks / 0 failures (`M3GDemo` 148
checks / 0 failures), the worktree suite is 134/134 and the main tree's is
140/140, and the corpus gate stays at `fatal 0 / 400, known 4`.  Frame dumps of
the same snapshot differ from the old binary only in the sky/cloud band the
title animates from the wall clock, exactly as two runs of the old binary do;
the near terrain, ground and hotbar are pixel-identical.

The `NJ_DRAWINFO` print, the `-D` ablation hooks and the gprof build were all
temporary and are not committed.  The per-triangle fog state and the fused
reciprocal are kept behind `NJ_M3G_FAST_PATHS` like the rest of the flat loops.
