# The fog fast paths

*2026-10-31*

The 0.5.1 and 0.5.2 releases fixed what Minecraft PE looked like (per-fragment
fog, uncapped default) but the frame cost of fog stayed: every fogged pixel
computed `(fog_far - d) / (fog_far - fog_near)` with a divide, mixed the colour
with three float multiplies per channel and two float-to-int conversions.  This
note is the measurement that led to keeping only the useful part of that work
and the change itself.

## What the time actually went into

`perf` and `valgrind` are not installed here, and `gdb -p` is refused because
`kernel.yama.ptrace_scope` is 1, so two measurement methods were used:

* **ablation**: build a variant binary, swap one `return`/`if`, measure user CPU
  of 1200 in-world frames pinned with `taskset -c 5` (the host runs other work
  and wall time is noisy by 20-40%; user CPU holds to about 1%);
* **in-process sampling**: a throwaway `SIGPROF` handler in `apps/nj2me_main.c`
  (`setitimer(ITIMER_PROF, 2 ms)`, `backtrace_symbols_fd`) in its own
  `-fno-omit-frame-pointer -rdynamic` build, then `addr2line` over 14,135
  samples.  The instrumentation is not committed.

A 1200-frame saved-world run at 240x320 costs about 3.0 s of CPU in 0.5.2.  Ablation:

| variant | user CPU, 1200 frames | delta |
| --- | --- | --- |
| 0.5.2 clean | 3.03-3.10 s | - |
| fog off in the raster loops | 2.49-2.57 s | fog is ~18% |
| texture sampling off | 2.42-2.52 s | sampling is ~20% |
| blend forced to REPLACE | ~3.05 s | blending is not a hotspot |
| depth test/write off | slower | more pixels survive and get shaded |

The texture share is almost entirely the *latency* of the two `divss` per
textured pixel (`ph / pg`, `pk / pg`, depth-correct sampling), not the wrap,
indexing or the texel read: replacing those with a constant texel changed
nothing, while removing the divides changed everything.  That cost cannot be
removed without changing the sampled texel by an ulp, which the pixel-exact
tests exist to prevent, so it was left alone.

Sampling steady state (last quarter of a 12000-frame run, i.e. after class
loading): `jr_emit` 36.8% (with `sw_tri` inlined into it), `class_resolve_from_source`
8.7% (inlined `nj_class_find` hash lookups, not source loading - instrumenting
the loader showed each class loaded exactly once), `sw_fog` 7.4%, `jr_project`
6.0%, `vm_invoke` 5.7%, `field_lookup_walk` 5.7% (M3G's per-draw field lookups),
`jr_fetch` 4.0%, `sw_blend` 3.8%.

## The change

`sw_fog()` now has saturating endpoints.  JSR-184's LINEAR fog factor is
`(far - d) / (far - near)` clamped to [0, 1] - the clamp is part of the
specified result, not an approximation - so:

* a fragment at or nearer than `fog_near` (`invw >= 1/near`) keeps its colour
  and never touches the ramp (that is `f = 1`);
* a fragment at or past `fog_far` (`invw <= 1/far`) is exactly the fog colour
  (that is `f = 0`);
* only the pixels between the two planes evaluate the ramp, and those now
  multiply by a `1 / (far - near)` prepared in `jr_setup` instead of dividing,
  with `1 - f` computed once for the three channels.

`m3g_sw` grew `fog_inv_span`, `fog_inv_near` and `fog_inv_far`, filled next to
`fog_near`/`fog_far`.  Minecraft PE's sky (fog 12.8..48, sky depth 22.9..640)
and most of the near terrain fall entirely into the two saturated cases, which
is where the win comes from.

## Result

Same saved world, 1200 in-world headless frames, `taskset -c 5`, user CPU,
alternating runs on the same host:

| build | round 1 | round 2 | round 3 |
| --- | --- | --- | --- |
| 0.5.2 | 3.263 s | 3.217 s | 3.266 s |
| fast paths | 2.946 s | 2.890 s | 2.908 s |

**-9.2% to -11.6%** of the frame's CPU, about half of the fog share; what is
left is the pixels whose depth really lands inside the ramp (clouds, far
terrain) and the exact maths they need.  Uncapped headless throughput in the
world moves from ~370 to ~405 fps on this host.

Correctness gates: `ctest` 127/127 in a clean v0.5.2 worktree with fixtures
built, `M3G checks=148 failures=0`, corpus gate `fatal 0 / 400, known 4`, and a
frame dump comparison of the same state - the static parts of the image
(near terrain, ground, hotbar) are pixel-identical, only the sky/cloud band
differs, and running the *unmodified* binary twice differs by the same amount
because the title animates clouds from the wall clock.

## What was not done, and why

* one reciprocal per textured pixel instead of two divides: 1-2%, inside the
  noise, and it changes the sampled texel by an ulp;
* bit-trick plus Newton reciprocal instead of the divides: slower than `divss`
  on this CPU (2.97-3.20 s), so it would be a pessimation;
* integer fog mixing: not measured, and the ±1 channel rounding it introduces
  is exactly what the pixel tests check;
* the class-hash lookups (8.7%) and M3G's per-draw field lookups (5.7%) are the
  next candidates, but both are interpreter/reflection-level changes with a
  much wider blast radius than a leaf function in the rasteriser.
