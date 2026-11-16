# Post-release verification and next-step analysis, 2026-11-16

After the 0.5.11 release (r30) this note records follow-up checks that do not
change the core: one flake was root-caused as environmental, the M3G span mix
was re-measured to choose the next performance target, and two longer corpus
probes were run for robustness evidence.

## Wine audiodemo "no nonzero samples" at 120 frames is environmental

`tools/check_libretro_windows.sh --frames 120` (windows-x86_64 DLL under Wine)
occasionally fails only the audiodemo nonzero-audio check (`audio: frames=735
nonzero=0 peak=0`) while every other check passes. Re-running the identical
command passes; 300 frames passed 3/3.

To separate core change from host/Wine scheduling, the same check was run
against the already-shipped **0.5.10** DLL (`/var/tmp/nj2me-dist-r29`,
pre-dating every 0.5.11 change): 6 runs at 120 frames -> 5 pass, 1 fail with the
same zero-audio signature. The flake therefore exists before 0.5.11, and the
0.5.11 DLL shows the same rate (6/7 pass at 120 frames, 3/3 at 300). Treat a
lone zero-audio sample in this Wine smoke test as Wine/host scheduling jitter,
not a core regression; keep 300 frames for authoritative audio validation.

## M3G span-class mix: shipped fast paths cover the loaded scene

An instrumented scratch build (TEMPORARY counters in `sw_span()`, scratch
worktree, not committed) measured `毁灭战士3_10598.jar`, 600 frames at
`--fps 0`, heap 32 MB:

```
SPANSTAT total=5503357 flat_opaque=0 flat_tex565=0 flat_texargb=2527454
         gouraud_texargb=2975903 sprite=0 gen_gouraud_tex=0 gen_gouraud_notex=0
         gen_flat_tex=0 gen_flat_notex=0 fmt565=0 blend_ne64=0 fog=0
         alpha_ramp=0 st_outer=5503357
```

- 46% flat + nearest-textured + ARGB8888, 54% Gouraud + nearest-textured +
  ARGB8888: exactly the two fast paths added in rounds 32 and 34.
- **Zero** spans fall through to the general loop: no fog, no alpha ramp, no
  sprite, no RGB565 target, no blend variants. So there is no remaining
  general-loop class to optimize on this title; the earlier "general class
  ~49% of spans" figure predates the Gouraud fast path.
- The only other raster cost centres are `m3g_clear` (memory-bandwidth bound:
  ~24 GB of color+depth memset per 20k-frame run; non-temporal stores measured
  slower) and `sw_tri_band` setup (~74 ns/band, 40.6 M bands ~ 3.0 s of a
  29.6 s run). A next step would have to attack edge setup incrementally, not
  another span class.

## 3D probes: `m3g=0` rows are menu-bound, not rendering hangs

`浴火银河2完整版_7689.jar` run 1800 frames at 30 fps: identical frames
(99.1% non-black), `paints=2`, `m3g=0`, millions of `gfx` operations. The
dumped frame is the title's "Music On / Music Off" selection screen; it waits
for input, and simple key scripts did not dismiss it. So a 600-1800 frame
headless probe that reports `m3g=0` for a 3D title is a scene/input artifact,
not a defect; per-title input scripts (pointer where needed) are required
before judging 3D rendering on such titles. Earlier M3G validation used
titles that render during their loaders (毁灭战士3_10598) for this reason.

## Deep soak: 24 titles x 1800 frames

`tools/check_corpus_deep.py <rows> --count 24 --seed 71 --frames 1800` (r30
tree, 8 jobs): **23 passed, 1 early exit, 0 failed, 0 uncaught**. The early
exit is `SlimeVolleyBallMidlet` (frames=470, paints=472): the title calls
`notifyDestroyed()` itself, consistent with game-side flow. No heap growth or
crash was observed.

## Audio probe: 40 titles x 600 frames

`tools/check_corpus_audio.py <rows> --count 40 --frames 600 --seed 31`:
**26 titles delivered audio** (tones or PCM), **14 silent with no Player
started** in the sampled window (`silent-nostart`, e.g. 火影忍者_11531,
都市赛车_11517, 精品美女斗地主2013_104). Same caveat as the 3D probe: those
titles are at menus when the frame budget expires; the earlier adjudications
(美女滑雪胜地_2 needed pointer presses, 3D拉力赛 needed a longer window) show
that a per-title script or a longer window is required before calling silence a
defect. No crash, timeout or exception occurred in either group.

## Follow-up candidates

1. `sw_tri_band` setup cost (12% of the profiled 3D run) with an incremental
   edge walk; must keep the deterministic-prefix parity check.
2. `m3g_clear` is memory-bandwidth bound; only worth revisiting with a
   different buffer layout (e.g. avoiding the separate depth memset when the
   depth test is disabled for the whole frame).
3. Per-title input scripts for menu-bound 3D/audio titles if their behaviour
   ever needs judging in the corpus probes.
