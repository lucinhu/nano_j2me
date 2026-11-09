# M3G equal-depth fragment comparisons

## Problem and change

JSR 184 requires the depth test to use `LEQUAL`: an incoming fragment at the
same depth as the stored fragment must pass. The software rasterizer instead
used strict comparisons in all six depth-testing loops. Coplanar later layers
could disappear, including selection overlays in Minecraft J2ME.

The current rasterizer stores a reversed depth key, `trunc(65536 / w)`, where
larger keys are nearer. This change accepts `key >= stored`, or equivalently
rejects only `key < stored`, in all six loops. It preserves the existing
positive-key guard, depth interpolation, alpha tests, depth-write ordering,
texture sampling, fog, and builtin class tables.

References:

- [CompositingMode implementation guidelines and setDepthTestEnable](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/CompositingMode.html)

## Regression evidence

`tests/test_m3g_depth_compare.c` renders through the public Java APIs and checks
RGB with an independent barycentric and source-over oracle. It explicitly sets
both depth-offset parameters to zero and uses positive perspective depth keys.
The screen backing fixtures select RGB565, RGB888 and ARGB8888 targets; the
fourth target is a mutable MIDP Image read back through `Image.getRGB`.

Seven rendering configurations cover the six comparison sites: flat opaque,
nearest CLAMP/REPEAT, the near-fog tinted ARGB specialization, textured Gouraud,
LINEAR filtering and untextured Gouraud. Checks include later coplanar opaque
replacement, multiple coplanar alpha layers with depth writes enabled/disabled,
strictly nearer/farther controls, disabled depth testing and writing, and alpha
rejection preserving both color and occlusion behavior. Samples lie inside the
triangles, away from shared edges.

On baseline `f9c945d`, the same fixture reports **2,016 checks / 896 failures**.
The corrected Release build reports **2,016 checks / 0 failures**.

The same **2,016 / 0** result passes with `NJ_M3G_FAST_PATHS=0` and with
AddressSanitizer + UndefinedBehaviorSanitizer. Test and production overrides
compile with the normal project flags plus `-Werror`; the test also compiles
with M3G disabled. Sanitizer runs use no explicit `ASAN_OPTIONS` or
`UBSAN_OPTIONS` override. A separate local instrumented copy confirms every
comparison site is exercised: 73,728 / 98,304 / 36,864 / 94,208 / 65,536 /
548,864 fragment visits. Instrumentation is not part of production.

The initial full run reports 147/149 passing and exposes two old fixtures:

- `test_m3g` explicitly expected the first opaque coplanar triangle to win.
  Its expectation now follows LEQUAL; the disabled-test draw uses blue after
  green so it still proves a color write. The following position-bias fixture
  explicitly initializes its green material instead of inheriting prior state.
- `test_m3g_targets` used a parallel camera for a farther-occlusion assertion.
  The current reciprocal-w representation maps both distances to the same key,
  so strict comparison had made that assertion pass without distinguishing
  distance. This format/stride fixture now uses perspective 90 degrees, keeping
  its original four-unit coverage at z=-2 and preserving the farther-occlusion
  expectation. The revised fixture also passes on the frozen baseline runtime:
  **1,711,777 checks**, all three surface formats. Parallel depth encoding is
  still a real limitation; the camera change does not claim to fix it.

After the fixture corrections, the full serial CTest run passes **149/149**
in 369.77 seconds, including M3G Java fixtures, reference-frame comparisons,
SDL2 client/save-state checks and dispatch checks. The core API audit also
resolves all **982** reference members (20 inherited/delegated declarations).
Builtin class/member tables are unchanged.

A separate local shared-edge observation draws an alpha-128 red strip forming
an axis-aligned 32x32 quad twice over black. Its shared diagonal passes through
pixel centers. All 1,024 interior pixels show one blend after the first draw
and two after the second, for four targets, FAST0/FAST1 and both depth-write
settings. No holes or double application occur within a single draw in this
bounded observation. This is not exhaustive coverage of arbitrary/clipped edges.

## Original Minecraft evidence

The original `MinecraftJ2ME_PE_0.6.1_V2.2.jar` is unchanged, SHA-256
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
A diagnostic build runs the original selection draw twice from identical
pre-triangle color/depth buffers: the original strict comparison on the live
buffer, and the inclusive comparison on a disposable shadow. This avoids
comparing different animation times or game-thread schedules.

At frame 1,301, draw 21,640, triangle 3,023,269 uses `MODULATE_X2`, depth testing
on and depth writing off. Of 5,583 depth tests, 3,829 keys are exactly equal to
the stored keys. Old/new passing counts are 0 / 3,829, and exactly 3,829 RGB
pixels change in x=191..239, y=106..307. The next triangle restores another
2,464 pixels; the two changed sets are disjoint. All shadow depth buffers stay
identical, as required with depth writes disabled. Of 128 captured selection
triangles, 20 execute depth tests and change color; in each, changed pixels
match the exact ties newly admitted by LEQUAL.

The game requests `setDepthOffset(-1,-1)` for this selection material. That
setting is still ignored by the rasterizer. Non-equal coplanar key differences
and residual selection stripes remain possible; this evidence proves the
missing equal-depth fragments, not complete decal or flicker correctness.

A normal, uninstrumented SDL2/X11 run loads a fresh copy of the test world,
selects a block at frame 1,300, renders 1,600 paced host frames and saves on
teardown. Its 960x1280 window capture was inspected: terrain, held block and
HUD are present. There are no uncaught exceptions or chunk errors; caught
optional Android/DatagramSocket class probes remain. Reported 29.9 FPS is a
30-FPS pacing check, not a throughput result.

## Performance controls

Seven fixed-work opaque raster configurations run with depth testing disabled
and with depth testing enabled using strictly increasing positive keys, so
baseline and patched builds perform the same work. Each case draws 1,200
triangles at 240x320 into an unaligned surface with five padding bytes per row.
Whole color/padding and depth-buffer checksums agree for all 14 controls.

GCC 16.2.1, C99, `-O3 -DNDEBUG`, Intel Core Ultra 9 275HX, pinned CPU 0,
process CPU time, one warm-up pair then three alternating measured pairs:

| Controls | Median CPU-time change, patched vs baseline |
| --- | ---: |
| Depth test disabled, seven cases | -0.58% to +0.19% |
| Strictly nearer keys, seven cases | -0.69% to -0.003% |

These short controls show no material regression. They do not establish a
rendering speedup or measure the extra work now correctly drawn at equal depth.

Local raw logs, source snapshots, exact commands and paired captures are under
`.local-tools/debug-runs/mc-20261110-lequal-tests/`,
`.local-tools/debug-runs/mc-20261110-lequal-game/` and
`.local-tools/debug-runs/mc-20261110-lequal-main/`. Build/world/log artifacts are
excluded from the commit.

## Scope

This fixes equal-depth comparisons in the existing reversed-key representation.
It does not implement `setDepthOffset`, replace that representation with full
projected screen Z, or fix the existing parallel-camera/depth-range limitations.
The existing guard that rejects keys quantized to zero also remains. Consequently
this is not a claim of complete depth-buffer conformance or a fix for every cause
of transparent-object flicker. The earlier world-generation speed, depth-buffer
lifetime, default-background clearing and mesh-alpha fixes remain separate.

Game validation uses the original Minecraft JAR and disposable copies of the
test world's filesystem and RMS. Original games and user saves are preserved.
