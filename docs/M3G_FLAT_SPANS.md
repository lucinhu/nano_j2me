# Flat-texture M3G spans: game and fixed-work measurements

Implementation `27bef20`, compared with `a250a37`. The result archive is
[m3g-flat-spans-results.json](m3g-flat-spans-results.json). Measurements use
240x320, a 10 MiB Java heap, and a software RGB565 target.

## Change and reason

A temporary profile of an actual NFS The Run race attributed about 60–67% of
wall time to Graphics3D.render; triangle rasterization alone took about 35–42%.
Most triangles used flat color with nearest-neighbor textures. Faster Java
execution cannot remove this native rendering cost.

The specialized RGB565/ARGB8888 span loops now cover clamp/repeat addressing
(including mixed axes), REPLACE/DECAL texture functions, every existing blend
mode, and alpha testing. Alpha rejection precedes depth writes. White color
modulation skips identity channel arithmetic. Sampling and interpolation keep
the general path's operations and order; linear filtering and Gouraud cases
continue through their existing paths. No resolution reduction or approximate
arithmetic is introduced.

The local port also places the raster function `jr_emit` in internal execution
memory, alongside the already mapped interpreter/JIT functions. That mapping
costs about 7.1 KiB of internal instruction space and is port configuration,
not part of the portable library patch. All profiling instrumentation was
removed before the measurements below. These changes optimize the renderer;
they do not change generated JIT code.

## Actual race scene

NFS The Run, archive SHA-256
`92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5`:
fresh temporary RMS, sound enabled, default quick race/car, start SPRINT, then
hold brake near 1% progress. The player remains near the start while opposing
cars continue to move. Each run warms up for five seconds and takes twenty
one-second samples without screenshots during sampling. Before/after images
verify the scene. JIT is off in these paired runs.

| Configuration | Mean changed-image FPS, run 1 | Run 2 |
| --- | ---: | ---: |
| Baseline | 4.620 | 4.615 |
| Specialized spans plus internal raster placement | 5.265 | 5.220 |

The two-run means are **4.6175 → 5.2425 FPS (+13.54%)**. An exploratory
internal-placement-only run measured 4.790 FPS; one repetition is insufficient
to precisely separate that contribution from the library change.

This metric counts changed game pictures, not Java VM frame-loop iterations.
The short controlled scene does not establish full-race performance or a
universal speedup. The absolute frame rate remains low.

## Fixed work on the device

The existing `tests/m3gtarget/M3GTargetBench.java` fixture uses the direct RGB565
path at 240x320: one warm-up and sixty timed render calls, with 160 submitted
triangles per render. Both images use the same internal code mapping, compiler
options, fixture and integration objects. JIT is off. The baseline M3G object
is replaced with the optimized one; internal code addresses are checked for
both builds. No screenshot interrupts the timed interval.

| Core | Sixty renders, three runs (ms) |
| --- | --- |
| Baseline | 3572, 3570, 3569 |
| Specialized spans | 2907, 2907, 2907 |

Throughput improves **22.82%** and elapsed time drops about **18.58%**. All six
runs have native CRC `48f14000` and display CRC `4ffb9823`, normal exit, zero
external-memory delta and no live hot/JIT allocation. These are fixed-work
render timings, not racing-game FPS. The normal application is restored after
the fixture runs; test game archives and normal saved games are retained.

## Correctness and upstream integration

Fast paths enabled/disabled produce byte-identical color and depth captures
across RGB565, RGB888 and ARGB8888. The target tests cover tinted and white
modulation, opaque/translucent textures, texture functions, all blend modes,
mixed wrap axes, alpha test/write, depth rejection, viewport and padded stride.
The capture is 117,504,000 bytes with SHA-256
`3f9d9caa17f7fed78dcc4368dcdf3e518856d683da9db59db1ab157181bb148f`.
The target suite performs 1,776,885 checks with ASan/UBSan.

Merging upstream `baecaac` exposed a separate startup regression in both NFS
titles. The new dead-thread reaper could free a timer thread while an outer
native call still owned its frames. NFS Hot Pursuit also reproduced this under
host ASan as a heap-use-after-free at `nj_frame_pop_fast`, with the allocation
freed by `nj_thread_reap` during a nested scheduler call.

Fix `5c452cf` defers reclamation until Java frames and nested C calls have
unwound and retains the main-thread handle. A synthetic native callback
reproduces the old ASan failure without a commercial JAR; the fixed lifecycle
test repeats beyond twice the thread-slot limit and also checks parked C calls
and the main handle. Nine relevant host suites pass under ASan/UBSan:
M3G, target formats, M3G files, clipping, VM, thread lifecycle, LCDUI, library,
and MIDI. The earlier paired speed measurements precede this upstream merge;
final integration checks are recorded separately in the numerical archive.

## Final integration spot checks

After the upstream merge and reaper fix, NFS The Run reached the same starting
race scene: **5.355 FPS with JIT off**, **5.160 with JIT on**. These are one run
per setting, separate from the paired optimization experiment. Only seven JIT
blocks compiled and 710 executions were recorded in the JIT-on run; native
rendering still dominates this title. Both runs exited with complete external
memory recovery and no crash.

Bounce Tales reached active 2D gameplay with directional input and JIT on,
averaging **11.43 changed-image FPS** in its ten-second sample. There is no
matching baseline, so this is a functional spot check, not a 2D improvement
claim. All 238,986 offered PCM frames were accepted during sampling, with zero
write errors, but **six underrun events** were recorded; audio under load still
needs work. Exit again recovered all external memory.

Hot Pursuit reached a rendered race after the reaper fix. Its first timing
attempt ended at a paused menu and is excluded from performance comparisons.
A second run, with no screenshot before sampling, averaged **3.93 FPS** over
ten one-second polls (3.5–4.4); the post-sample image shows 00:14 and 93 MPH.
There is no matched baseline for this moving scene. It exits normally with
complete external memory recovery. A pre-existing destroyApp exception on
manual stop is retained as a limitation.
Full races and save/load round trips were not verified in this experiment.

## Reproduction

Build two Debug host trees with `NJ_SANITIZE=ON`, `NJ_BUILD_SDL2=OFF`, and
`CMAKE_C_FLAGS=-DNJ_M3G_FAST_PATHS=1` / `=0`. Build `test_m3g_targets` and run
`test_m3g_targets capture.bin` in each, then compare the complete captures.
Run `test_thread_lifecycle` against the old reaper to observe the ASan failure,
and against the fix to check reclamation and continued slot reuse.

For target timing, build the existing M3G target fixture with
`bash tools/make_m3gtarget_jar.sh build-m3gtarget`, choose its direct path, and
require the same render count, CRCs and teardown balance on both versions.
Keep compiler, memory mapping, framebuffer format, integration code and runtime
settings constant. Host timings are not substitutes for device measurements.
