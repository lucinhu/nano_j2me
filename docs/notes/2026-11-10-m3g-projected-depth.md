# M3G projected depth, range and polygon offset

This follow-up to the LEQUAL correction replaces the old `65536 / clipW`
depth key. Reciprocal W orders ordinary perspective geometry, but it cannot
distinguish near and far surfaces under a parallel camera. It also ignores
`Graphics3D.setDepthRange` and cannot implement `CompositingMode.setDepthOffset`
in screen space.

## Rendering contract

The depth interpolant now comes from clip Z / clip W. Window depth is
`near + (far - near) * (ndcZ + 1) / 2`, independently of the reciprocal W used
for texture perspective correction and fog. The depth buffer uses 24 bits,
with reversed keys `1 + round((1 - windowZ) * 16777215)` after clamping window
Z to [0, 1]. Clear stores zero; even the far plane has a valid nonzero key.
Larger keys remain nearer, so the previous GEQUAL comparisons continue to
implement the required LEQUAL window-depth test.

Polygon offset is constant over a triangle: `m * factor + units / 16777215`,
where `m` is the maximum absolute window-depth gradient along X or Y. It is
applied before both depth testing and writing, without changing UV or fog.
The implementation must cover clipped triangles, Sprite3D, and each of the
six specialized/general raster paths.

New Graphics3D contexts start with depth range [0, 1]. Out-of-range endpoints
throw IllegalArgumentException before either endpoint changes; reversed and
constant ranges remain valid. The one-argument bindTarget enables depth
buffering. The three-argument overload records its depthBuffer argument;
when false, both depth testing and writing are disabled independently of the
CompositingMode flags.

The relevant contracts are in the JSR-184
[Graphics3D documentation](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Graphics3D.html)
and
[CompositingMode documentation](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/CompositingMode.html).

## Fast snapshot compatibility

The serialized arena contains Graphics3D range fields and its cached depth
array. Old reciprocal-W keys cannot generally be converted back into window
Z: successive draws can use different projections and ranges. An old zero
far endpoint is also ambiguous between an uninitialized default and an
explicitly requested constant [0, 0] range.

Snapshot format version 3 therefore gains flag bit 1 for the projected-depth
semantics. New readers accept old snapshots without a Graphics3D instance.
An old snapshot containing a Graphics3D instance is refused explicitly, before
installing its arena; it must be restarted from the game's own save. Class
registration, a class mirror, or an empty Graphics3D reference array alone
must not cause this refusal. Older readers also reject the new flag rather
than silently interpreting new keys as reciprocal W. This policy does not
modify game JARs, world files or RMS saves.

Builtin class fields, method descriptors and ordering remain unchanged;
constructor and overload native function pointers may change.

## Verification

The Release build passes all **151 CTest tests**. An independent public-API
fixture makes **13,740 checks** without reading native depth keys. The same
fixture has 4,904 failures at baseline `ed5931f`; final FAST1, FAST0 and
ASan + UBSan + explicit `float-cast-overflow` each have zero failures.
Production and fixture also pass strict compilation; M3G-disabled compilation
passes. Coverage instrumentation in a local copy confirms populated-depth
comparisons in all six raster sites.

The fixture checks three projections and four targets, both submission orders,
normal/reversed/constant ranges, signed factor/units, biased depth storage,
bindTarget depthBuffer=false, near clipping, finite extreme offsets, and UV/fog
invariance with depth enabled. Sprite coverage is limited to an Image target,
two projections and units offsets; it is not a factor-gradient stress test.
These are selected pixel checks, not complete JSR-184 conformance.

Snapshot tests pass 95 checks under ASan/UBSan with explicit float conversion
checking and leak detection, and 50 checks with M3G disabled. Existing test_save
passes 2,521 sanitizer checks. A real old writer confirms that ordinary old
states, class mirrors and arrays load, whereas actual Graphics3D instances and
subclasses refuse before installing the arena. A new rendered snapshot also
refuses in the old reader.

The original Minecraft JAR was run with copied world/RMS storage, 240x320,
16 MiB heap, 30 fps and 1,600 host frames in SDL2. The world, held block and HUD
were inspected; a frame-1450 snapshot resumed for another 300 frames. A real
old Minecraft snapshot was refused and the MIDlet started fresh. No uncaught
exceptions or invalid chunk errors appeared. Caught UDP receive errors after
resume also occur in the old runtime: native network handles are not restored.
These paced replays are scene checks, not throughput measurements.

A separate final-source diagnostic replays each selected triangle twice from
identical new-format RGB/depth buffers, using the game's actual (-1,-1) offset
and a zero-offset shadow. Of 128 selection draws, 20 change RGB, while all 128
retain identical depth buffers because selection has depth writes disabled.
The first two changed triangles affect 5,440 and 3,776 pixels at frame 1301.
This proves that the game's requested offset is effective, not that every water,
leaf or transparency defect is fixed.

## Fixed-work cost

GCC 16.2.1, x86_64, `-O3 -DNDEBUG`, FAST1, CPU0, process CPU clock; one warmup
pair and three alternating before/after pairs. Each case draws 1,200 triangles
covering 38,400 pixels each. Values are median ns/pixel including triangle setup.
Colors and row padding have identical checksums. Constant-depth writes use
independent expectations for each encoding, which intentionally differs.
Projected sloped depth uses a pixel-centre oracle with one tick of incremental
rounding tolerance. The old sloped control checks coverage and color/padding,
not its individual depth keys. Fully disabled cases require no depth writes.

| Case | Before | After | Time change |
|---|---:|---:|---:|
| test off, write on: flat ARGB | 0.868537 | 0.739972 | -14.80% |
| test off, write on: flat RGB565 | 1.082492 | 0.996729 | -7.92% |
| test off, write on: CLAMP tint ARGB | 4.032373 | 4.303237 | +6.72% |
| test off, write on: REPEAT tint RGB565 | 5.240493 | 5.406964 | +3.18% |
| test off, write on: Gouraud nearest | 5.882040 | 5.667473 | -3.65% |
| test off, write on: LINEAR | 13.212290 | 13.083425 | -0.98% |
| test off, write on: RGB REPLACE | 3.098860 | 3.132685 | +1.09% |
| non-tie test/write on: flat ARGB | 0.881510 | 0.829988 | -5.84% |
| non-tie test/write on: flat RGB565 | 1.096094 | 0.962484 | -12.19% |
| non-tie test/write on: CLAMP tint | 4.338230 | 4.174036 | -3.78% |
| non-tie test/write on: REPEAT tint | 5.221732 | 5.359136 | +2.63% |
| non-tie test/write on: Gouraud nearest | 6.011082 | 5.833532 | -2.95% |
| non-tie test/write on: LINEAR | 13.249727 | 13.248662 | -0.01% |
| non-tie test/write on: RGB REPLACE | 3.229726 | 3.292783 | +1.95% |
| sloped test/write on: flat ARGB | 0.883022 | 1.038937 | +17.66% |
| sloped test/write on: CLAMP tint | 4.356964 | 4.539598 | +4.19% |
| test/write both off: flat ARGB | 0.830739 | 0.702734 | -15.41% |
| test/write both off: CLAMP tint | 4.109108 | 4.208842 | +2.43% |

Constant-depth controls range from -14.80% to +6.72%. Sloped flat drawing costs
another 0.156 ns/pixel (17.66%); sloped textured drawing costs 4.19% more. This
compatibility fix is not a claim of universal raster speedup or whole-game FPS.
Splitting the existing fast sampling branches into small span functions avoided
the roughly 2.58x flat cost of the initial single-function candidate. No unsafe
casts, approximate fixed-point depth or compiler-specific attributes were used.

The retained [benchmark](../../tools/bench_m3g_depth.c) uses the exact measured
logic: its old/new object text sections match the measured objects byte for
byte. The following reads flags from an existing Ninja build, writes a fresh
temporary directory and runs one verification sample:

```sh
python3 - src/midp/nj_lib_m3g.c projected <<'PY'
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r = Path.cwd(); b = r / 'build'
out = Path(tempfile.mkdtemp(prefix='nj-m3g-depth-'))
src = Path(sys.argv[1]).resolve()
assert sys.argv[2] in ('projected', 'inverse-w')
lines = subprocess.check_output(['ninja', '-C', str(b), '-t', 'commands', 'test_m3g'], text=True).splitlines()
c = shlex.split(next(x for x in lines if ' -c ' in x and '/src/midp/nj_lib_m3g.c' in x))
for flag in ('-MF', '-MT'):
    if flag in c:
        i = c.index(flag); del c[i:i+2]
c = [x for x in c if x not in ('-MD', '-MMD')]
c[c.index('-c')+1] = str(r / 'tools/bench_m3g_depth.c')
c[c.index('-o')+1] = str(out / 'bench.o')
c += ['-Werror', '-I'+str(r / 'src/midp'), '-DNJ_M3G_SOURCE="'+str(src)+'"']
if sys.argv[2] == 'projected': c += ['-DNJ_SCREEN_DEPTH=1']
l = shlex.split(lines[-1]); l = l[2:-2] if l[:2] == [':', '&&'] else l
l = [x for x in l if not x.startswith('-Wl,--dependency-file=')]
l[next(i for i,x in enumerate(l) if x.endswith('/tests/test_m3g.c.o'))] = str(out / 'bench.o')
l[l.index('-o')+1] = str(out / 'bench')
for cmd in (c, l):
    print(shlex.join(cmd), file=sys.stderr)
    subprocess.run(cmd, cwd=b, check=True)
cpu = min(os.sched_getaffinity(0)); os.sched_setaffinity(0, {cpu})
print('output:', out, 'CPU:', cpu, file=sys.stderr)
subprocess.run([str(out / 'bench')], check=True)
PY
```

For the old variant export `ed5931f:src/midp/nj_lib_m3g.c` with git show into a
separate file and supply its path plus `inverse-w`. Compare names and color
checksums, not raw depth arrays. The formal table used one immutable baseline
archive for both variants; this convenient command links the existing build's
archive. Repeat alternating samples with a discarded warmup for timing claims.
