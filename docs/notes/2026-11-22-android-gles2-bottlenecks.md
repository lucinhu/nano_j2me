# Android GLES2 acceleration: where the speed actually goes

Investigation for the report that "OpenGL hardware acceleration is still not fast
enough".  The short version: the Android backend is not a renderer.  It is an
opportunistic accelerator for two M3G hooks (`gfx_raster`, `gfx_triangles`); the
2D pipeline, M3G `Sprite3D`, the software fallback and presentation all stay on
the CPU.  On the test emulator (SwiftShader, a CPU GL driver) the accelerated
path measured no gain at all; the counters also showed why.

## What is and is not accelerated

- `src/port/gles2/nj_gfx_gles2.c` renders a packet into a private framebuffer,
  calls `glReadPixels`, then commits the result into the CPU colour/depth
  buffers.  Nothing is presented through GL: `src/port/android/nj_android_gles2.c`
  says so in its first comment ("ANativeWindow remains the existing CPU
  framebuffer presentation path").
- The 2D fill/blit callbacks exist (`NJ_ANDROID_GLES2_2D_ACCEL`) but are off in
  release builds because the synchronous readback made them slower than the
  software fills/blits (`nj_android_gles2.c:262`).
- M3G's `jr_draw_accel()` deliberately skips acceleration for `Sprite3D` scenes
  (`r->sw.sprite` guard, `src/midp/nj_lib_m3g.c`): sprite-heavy menus and many
  `Sprite3D` titles never touch the GL path.
- The M3G journal batches a frame's triangles into one session when the port
  offers `gfx_triangles_begin/end`; that is the only place where the
  per-packet readback should be amortised.
- A one-time calibration decides whether the GPU path is kept
  (`m3g_calibrate()`, `src/midp/nj_lib_m3g.c:15840-15890`).  It requires the
  GPU batch to be more than 15% faster than software (`gpu_us * 100 < sw_us*85`).

## Measurements (Android emulator, API 36 x86_64, SwiftShader)

`setprop debug.nj2me.stats 1` prints a five-second line; the GPU counters were
added in this round (`stats: gpu state=... fills=... blits=... rasters=...
declines=... triangles=... batches=...`).

| Title | Path | FPS | App CPU | GPU counters |
| --- | --- | --- | --- | --- |
| Galaxy on Fire 2 (menu) | GLES2 | 60.0 (cap) | 37.6% | 0 / 0 / 0 / 0 / 0 / 0 |
| Galaxy on Fire 2 (menu) | software | 60.0 (cap) | 36.4% | n/a |
| m3gdemo self-test | GLES2, auto | exits | n/a | calibration declined (`software=172us`), `rasters=16 declines=1` |
| m3gdemo self-test | GLES2, forced | exits | n/a | `rasters=16 declines=46 triangles=0` |
| Doom3 loading animation | GLES2, forced | 4.6 | 99.4% | `rasters=0`, `declines` +~144/5 s, `triangles=0` |

The Galaxy menu renders through the 2D/sprite path, so the GL backend is idle
(counters all zero).  Doom3's 3D work reaches the backend and is declined packet
by packet; every decline replays that packet in software, which is why the
forced GPU run is *not* faster.  m3gdemo shows the automatic decision: the
calibration batch measured software at 172 us and the native transaction was
slower (or unsupported), so the run stayed software-only.

## Why the ceiling is low even when the GPU path works

1. The CPU work around a packet is not offloaded: interpreter/JIT, M3G
   transform/lighting and span setup still run on the CPU.  The desktop profile
   of Galaxy on Fire 2 measured `nj_interp_run` 27.5%, `sw_span` 18.7%,
   `sw_tri_band` 12.2%, `jr_lit` 10.3%, `nj_region_draw` 9.4% — removing the
   raster part alone caps the gain well below 2x.
2. Each accelerated transaction is synchronous: FBO setup, texture upload,
   draw, `glReadPixels`, a CPU validation/commit loop, and a second pass plus
   readback when the packet writes depth.  On tile-based mobile GPUs readback is
   the expensive operation; the same transaction also has to hand the result
   back to the run thread and cache it.
3. Declines are silent to users (and were until this round).  One unsupported
   format/flip/depth combination makes the whole packet fall back to software
   after the GPU already started, which is worse than not trying at all.
4. Presentation is a CPU `ANativeWindow` buffer, so no frame ever completes on
   the GPU.

Other J2ME emulators mostly do not rasterise J2ME primitives with GL either:
J2ME Loader draws its 2D output through Android's hardware-composited Canvas
(Skia), KEmulator/FreeJ2ME are pure software.  For this runtime the equivalent
useful target is the presentation/composition step, not per-primitive GL.

## What changed in this round

- `debug.nj2me.stats 1` now also prints the GPU counters (state, fills, blits,
  rasters, declines, triangles, batches), which answers "is the GL path doing
  anything" without a debugger.
- `eglMakeCurrent` is cached per owning thread instead of being re-issued for
  every packet (`nj_android_gles2.c`); it is invalidated on context loss, stop
  and release.
- New `debug.nj2me.m3g_accel=force` property bypasses the one-time calibration
  (useful when a device's calibration batch is unrepresentative); the runtime
  logs `m3g: accel forced by debug.nj2me.m3g_accel`.
- The counters made the emulator result above observable; they also make a real
  phone profile possible (`adb shell setprop debug.nj2me.stats 1`).

## Recommendations (next rounds)

1. Measure on a real phone, not the emulator: SwiftShader is a CPU GL driver
   and cannot show GPU throughput.  The new stats line is the cheapest way.
2. When counters show `declines` growing while fps is low, inspect the packet
   shape (format/flip/depth) before adding more shaders.
3. For 2D-heavy games, GL cannot help until the whole 2D pipeline is rewritten
   as a command buffer with one upload/readback per frame; the regressions to
   avoid are per-blit readbacks (already why `NJ_ANDROID_GLES2_2D_ACCEL` is off).
4. For 3D, the next structural win is removing readback: render M3G 3D into a
   GL texture, keep depth on the GPU, and composite that texture once per frame
   (requires the presentation path to become EGL-window based).
5. Keep the CPU image: the interpreter is the biggest single component of a 3D
   frame; the optional baseline JIT (`NJ_ENABLE_JIT`) is worth measuring on a
   device with the new stats line.

## Real-GPU measurements (host SDL2 port, round 195)

The emulator numbers above are CPU-GL (SwiftShader).  To separate "the GPU is
slow" from "the backend architecture makes the GPU slow", the same code was
built for the desktop SDL2 port and measured on a discrete GPU
(`NVIDIA GeForce RTX 5080 Laptop GPU`, `OpenGL ES 3.2 NVIDIA 620.73.04`):

| workload | software | GLES2 | result |
| --- | --- | --- | --- |
| `loopdemo`, 1200 frames (`NJ_SDL2_GLES2_2D=1`) | 4.83 s / 249 fps | 7.05 s / 170 fps | **2D accel is 46% slower** (fills=2385, blits=1200) |
| `m3gtarget` 160 tris/frame, direct path (median of 3) | 0.232 s | 0.248 s | GL slightly slower |
| `m3gtarget`, buffered path (median of 3) | 0.211 s | 0.250 s | **+18%** |
| Galaxy on Fire 2 gameplay, 560 frames @30fps, forced GL | user 3.83–3.90 s, sys 0.17–0.21 s | user 4.84–5.12 s, sys 0.93–1.09 s | **25–30% more CPU** (1.0–1.24 M triangles, 0 declines) |

The important row is the first one that is *not* a table entry: with the
default auto-calibration (no `NJ_M3G_FORCE_ACCEL`), the real GPU measures the
reference batch as `gpu=641us software=251us -> disabled (software is faster)`
and the runtime stays on software.  So the shipped default is not hurting
users; the forced setting is the only way to observe the GPU path, and on this
hardware it is slower.

### What that proves

The cost is not EGL context switching (already cached per thread) and not the
context-validation probes.  Caching the validation probes (`glGetString`,
`glIsTexture` checked every 1024 packets instead of every packet) changed the
Galaxy run by less than the run-to-run noise (4.84 s before, 4.95–5.12 s
after), which rules out that housekeeping as a bottleneck.  The remaining cost
is exactly the design: each raster/triangle packet renders into a private FBO,
reads the result back with `glReadPixels` and re-commits it to the CPU
colour/depth buffers, and the optional 2D callbacks do the same per fill/blit.
On a fast GPU the extra round trip costs more than the software span code it
replaces.

### What a real speed-up requires

Reference implementations of J2ME 3D acceleration keep the **frame** on the
GPU (one scene render into a GL texture, depth kept on the GPU, presentation
through an EGL/window surface) instead of accelerating individual software
raster packets.  For this runtime that means:

1. M3G renders its whole frame in GL (or at least one transaction per frame),
   the result is presented directly from a GL texture — no per-packet readback.
2. 2D remains a separate CPU canvas that is uploaded once per frame and
   composited in GL (or a command-buffer renderer); per-blit readback must go.
3. Only then does the `NJ_ENABLE_GFX_ACCEL` path have a chance to beat the
   well-optimised software rasteriser.  Until then, auto-calibration disabling
   the path is the correct default and `debug.nj2me.m3g_accel=force` should
   stay a diagnostic switch.

## Phase breakdown on a real GPU (2026-11-22, round 196)

To answer "what limits the speed", the existing compile-time profiler
(`-DNJ_GLES2_PROFILE=1`, `src/port/gles2/nj_gfx_triangles_gles2.inc`) was built
on the host SDL2/GLES2 CLI (`/tmp/r195-gles2-prof`) and run on the
Galaxy on Fire 2 gameplay workload (560 frames @30 fps, deep key script,
heap 24) with the backend forced:

```
PROF final flushes=2366 px=123259843 tri=982266 total=3172.4ms
  read_c=1031.6 commit=329.0 prep=431.2 setup=417.4 import_c=278.0
  up_d=307.3 read_d=112.8 seed=92.1 draw=66.9 pack_d=43.7 up_c=35.5
  views=12.9 val=7.1 init=3.4 alloc=0.8 vbuf=2.7 per_flush_us=1340.8
```

That is ~3.17 s of GPU-transaction time for 2366 sessions (≈4.2 per frame);
software rendering of the same workload is 3.83 s, forced GL 4.84–5.47 s.  In
wall-order terms:

| Phase | Time | Share | Meaning |
|---|---:|---:|---|
| `read_c` + `read_d` | ~1.14 s | 36% | synchronous `glReadPixels` of colour (always) and depth (when picked) |
| `prep` + `setup` | ~0.85 s | 27% | per-session framebuffer/program/state preparation |
| `commit` | 0.33 s | 10% | CPU copy of the readback into colour/depth buffers |
| `import_c` + `up_c` | 0.31 s | 10% | texture import/upload for the session |
| `up_d` | 0.31 s | 10% | depth-texture upload for the session |
| `seed` + `draw` + rest | ~0.22 s | 7% | initial rectangle seed, actual draw calls |

Conclusions:

- The synchronous readback is the single largest item, but removing it alone is
  not enough: an earlier benchmark-only switch that skipped readback *and*
  commit saved only ~0.5 s of the ~1.4 s forced-GL overhead, because the
  remaining time is spread over ~2366 per-session transactions (framebuffer
  preparation, texture uploads, depth round-trip) and material-grouped draws.
- The backend calls the driver ~1341 us per session; a whole-frame renderer
  would do one such transaction per frame instead of four.
- The 2D path and M3G `Sprite3D` never use GL, so those titles (Pac-Man menus,
  board games) cannot benefit at all from the current backend.

### Proposed structural plan

1. **Frame-resident M3G target** — keep one GL colour+depth FBO for the whole
   M3G frame; seed from the CPU buffer once per frame, flush session-boundary
   transactions, read back once at frame end.  This removes 3 of 4 sessions per
   frame (the majority of `prep`/`setup`/`import_c`/`up_d`) and shrinks the
   readback rectangle to the real dirty area.
2. **Resident textures** — count `import_c`/`up_c` only on texture content
   change instead of per session (needs a small content-version stamp from the
   M3G texture objects).
3. **GL presentation** — for builds whose frontend can present a GL surface,
   skip the colour readback entirely and let the compositor show the FBO;
   this is the only way to remove the remaining ~36% readback share.
4. Keep the software path as the default for every title where calibration
   says GPU is not faster (it already declines correctly on the real GPU:
   `gpu=641us software=251us -> disabled`), so the structural work can land
   behind the existing capability check without regressions.

Step 1 is the next concrete milestone; step 3 requires a frontend/EGL
integration change.

## Pure-3D workload check (round 198, host RTX 5080)

To separate "mixed 2D/3D round-trip" from "GPU path is slow", the Doom3
M3G loading animation was measured on the host SDL2/GLES2 build (1200 frames
uncapped, heap 32): software user CPU **2.998 s** (0.166 s sys) vs forced GL
**4.303 s** (0.222 s sys), with GL committing 1,855,454 triangles in 4331
batches and one decline.  So even a workload with almost no 2D drawing loses
in the current backend.

Per-transaction accounting (2672 transactions on Galaxy, full-screen 76,800 px
each): the synchronous colour+depth readback alone costs ~0.45-0.77 ms per
full-screen readback on this host, i.e. ~0.9-1.5 ms per frame across the two
transactions per frame - the same order as the measured 1.1-1.6 ms/frame loss.
Conclusion: the loss is dominated by the readback round trip, and removing it
requires presenting from GL (EGL window on Android) and, for mixed content,
also compositing the 2D canvas in GL.  Stages:

- **A (pure M3G frames)**: keep triangle passes in the resident FBO, composite
  the 2D canvas texture in GL, present with EGL window swap; no per-frame
  colour readback.  CPU observers (save state, getRGB, screenshots) keep the
  existing readback path on demand.
- **B (mixed 2D/3D frames)**: capture 2D draw calls into a small GL command
  list (fill/blit/region; text and complex paths rasterised into scratch
  textures), so the frame never returns to CPU mid-frame.
- Both stay behind the existing capability/option checks; software remains the
  default until measurements show the win on real devices.

## No-readback upper bound (round 199, host RTX 5080)

Before building EGL presentation, the Stage-A payoff was measured by adding a
benchmark-only switch that skips the colour/depth readback + CPU commit in both
transaction paths (`NJ_GLES2_SKIP_COMMIT`), keeping all upstream work (texture
import/upload, seed, draw).  Galaxy on Fire 2, 560 frames @30 fps, forced GL:

| variant | user CPU | note |
|---|---:|---|
| software | 3.83-3.90 s | reference |
| GL, full transaction | 5.20 s | readback + commit included |
| GL, readback+commit skipped | 4.37 s | 0.83 s saved |

So EGL presentation (removing readback and the CPU commit) would still leave the
GPU path ~14% slower than software on this workload: the remaining per-flush
costs (rect import, texture uploads, seed upload, GL state setup) stay around
0.7-1.0 s.  Consequence: presenting from GL is necessary but not sufficient; the
renderer must also keep state/textures resident across a whole frame and stop
re-seeding per transaction, i.e. Stage A+B of the design note are one project,
not two independent wins.
