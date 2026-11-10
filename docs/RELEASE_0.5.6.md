# nano_j2me 0.5.6

This patch release collects the compatibility and correctness work since 0.5.5.
It covers Java Math and Random edge cases, `System.arraycopy` semantics, live
RMS enumerations and shared stores, allocation-size overflow guards, and an M3G
file-loader crash that stopped Stranded 2 at the dinosaur scene.

## Compatibility and correctness

- `java.lang.Math` handles its defined numeric edges: integer `abs(MIN_VALUE)`
  returns the same negative value, floating `abs(-0.0)` returns positive zero,
  `min`/`max` propagate NaN and order negative zero below positive zero, and
  `round` ties toward positive infinity and saturates at the `long` limits. The
  old code relied on signed negation overflow and an out-of-range
  double-to-long cast.
- `System.arraycopy` copies overlapping ranges within one array as if the source
  range were first saved to a temporary array, and a reference copy that fails
  on element *k* leaves exactly the first *k* elements copied. Null, type and
  bounds checks follow the CLDC order and leave the destination unchanged when
  they fail.
- `java.util.Random` honors the subclass extension points: inherited generation
  methods dispatch through an overridden `protected next(int)`, both
  constructors call the virtual `setSeed(long)`, and `nextBoolean`/`nextGaussian`
  follow their `next`/`nextDouble` contracts. Bounded `nextInt` evaluates Java's
  rejection sum with defined unsigned arithmetic instead of signed C overflow;
  seeded base-class sequences are unchanged.
- The interpreter's inline constant-pool cache probes the same collision chain as
  the resolver, so a cached field or method in a later slot resolves without a
  second lookup. The previous fast path still produced correct results through
  the resolver fallback; VM, cache and snapshot layouts are unchanged.
- `Integer.toString(int, int)` computes the magnitude of `Integer.MIN_VALUE` in
  unsigned arithmetic; the previous signed negation was undefined behavior. The
  formatted text, including decimal output, is unchanged for every input, and no
  API, state or save format changes.
- `RecordEnumeration` is live: it retains its filter and comparator, `rebuild()`
  reapplies them and `keepUpdated(true)` refreshes membership, successful
  add/set/delete operations update subscribed enumerations, and backward
  traversal starts at the last record. Reopening a store by name shares one live
  object and an open count, so only the final close closes it and deleting an
  open store fails. Availability ends with the last close; `destroy()` releases
  callbacks and later enumeration calls throw `IllegalStateException`.
- Heap allocation rejects requests whose block header and alignment addition
  would wrap near 4 GiB, and the arena capacity test no longer overflows. The
  UTF-16 constructor checks its object, characters and terminator before
  multiplying `(len + 1) * 2`; the old path could write a terminator roughly
  4 GiB past a tiny object for `len = INT_MAX`.
- The M3G file loader no longer recognizes a component transform by its 40-byte
  data length alone. A VertexArray holding 20 two-component byte texture
  coordinates has the same length, so the loader wrote Transformable references
  through its field offsets and left a stale `first` index that the texcoord
  fetch used as a vertex index; Stranded 2 (孤岛余生2时间之谜) crashed in
  `jr_vertex` at the dinosaur scene. Node and Texture2D records must now hold a
  real Transformable, and drawing rejects attributes whose
  `(first + numVerts) * components` range does not fit their data array, measured
  in 64 bits so a corrupt index near `Integer.MAX_VALUE` cannot pass.

Detailed evidence: [Math](notes/2026-11-10-math-edges.md),
[arraycopy](notes/2026-11-10-arraycopy.md),
[Random bounds](notes/2026-11-10-random-bounds.md) and
[dispatch](notes/2026-11-10-random-dispatch.md),
[CP cache](notes/2026-11-10-cp-cache-collisions.md),
[RMS](notes/2026-11-10-rms-enumeration.md),
[heap bounds](notes/2026-11-10-heap-bounds.md),
[integer radix](notes/2026-11-10-integer-radix.md), and the M3G probes in
[test_m3g.c](../tests/test_m3g.c).

## Performance

- RMS record lookup reads `elementData`, `elementCount` and `value` directly for
  exact builtin `Vector` and `Integer` objects instead of crossing into a native
  bridge for every candidate. First-match order, the overflow-record path, and
  the virtual fallback for Vector subclasses and other boxed objects are
  retained; the scan is still O(n) with O(1) extra space and adds no Java
  fields, classes or on-disk state.
- In a fixed-work in-memory benchmark, scanning 4,096 records for a mix of
  lengths used 10.363 ms instead of 997.433 ms (96.25× faster) and dropped
  native calls from 16,801,824 to 8,192; querying only the first record improved
  2.24×. These are lookup microbenchmarks, not game throughput: the profiled
  Minecraft load window performs zero RMS lookups, so no loading or frame-rate
  gain is claimed for it. Reproduction:
  [bench_rms_lookup.c](../tools/bench_rms_lookup.c) and
  [RMS lookup evidence](notes/2026-11-10-rms-lookup.md).

## Snapshot compatibility

Game JARs, world files and RMS disk saves are preserved, and the persistent RMS
format is unchanged. Fast VM snapshots written by 0.5.5 load into this runtime
without conversion; their old base-class stores acquire the new RMS metadata on
demand. An enumeration saved by 0.5.5 cannot recover callbacks that the old
writer never stored, so it keeps its frozen membership until the application
creates a new enumeration.

Snapshots written by 0.5.6 require 0.5.6. Their builtin class table contains two
internal RMS classes, and 0.5.5 rejects those names before installing the saved
heap; there is no flag that makes them available to the old reader. The Math,
Random, `System.arraycopy`, CP-cache and heap changes add no fields or classes
and do not change existing snapshot layouts. Native network handles remain
outside the restore contract, and the caught Minecraft UDP receive exceptions
after resume are unchanged.

## Build and platform

- The standalone Android CMake component used by `externalNativeBuild`
  configurations now compiles the complete 68-unit runtime instead of 34. It
  supplies the runtime include directories and links `android`, `log`,
  `OpenSLES`, `z`, `m` and `dl`, with `--no-undefined` keeping missing
  dependencies visible at link time. arm64-v8a, armeabi-v7a, x86 and x86_64 all
  build, as does an x86_64 configuration with `NJ_ENABLE_JIT=1`; every library
  exports the port's 21 JNI entry points, and AAudio remains dynamically loaded
  so API 21 gains no load-time requirement. The APK shell builder already used
  the full source set. See the
  [Android CMake note](notes/2026-11-10-android-cmake.md).

## Validation and packages

- **All 161/161 CTest tests pass** in the Release build. The new
  fixtures report Math 363 checks, `System.arraycopy` 1,817, Random bounds
  24,051, Random dispatch 439, CP cache 2,161, RMS lookup 787, live RMS
  enumeration 776, RMS 1,005, heap bounds 664 and M3G 935.
- The independent Debug sanitizer build (ASan, UBSan and leak detection)
  passes the same CTest suite; the existing snapshot fixtures pass 2,521
  sanitizer checks, the M3G snapshot fixture 95, and the new RMS-enumeration
  snapshot fixture 70. A real 0.5.5 ordinary snapshot and a 0.5.5 RMS store
  snapshot load into this runtime; snapshots written here are rejected safely by
  the 0.5.5 reader with unchanged target state.
- The pre-fix runtime fails the new fixtures as intended: 30 of 363 Math checks,
  37 `System.arraycopy` assertions plus two `INT_MAX + 1` UBSan overflows, 93
  live-enumeration checks and 111 Random-dispatch checks; the allocation fixture
  segfaults in its separate NULL-VM, arena-wrap and UTF-16 processes. The
  40-byte VertexArray and wild-first-index M3G probes also fail before the
  loader fix.
- Stranded 2 (孤岛余生2时间之谜) now replays past the dinosaur-catch scene in
  headless and SDL2 runs. The unchanged Minecraft JAR still completes 1,600
  paced SDL2 frames with the same terrain, held-block and HUD captures, and both
  a 0.5.5 snapshot and a new snapshot resume for 300 frames with identical
  sampled images.
- The Android CMake component builds arm64-v8a, armeabi-v7a, x86, x86_64 and
  JIT-enabled x86_64 libraries. On API 21 and API 26 x86_64 emulators,
  `dlopen(RTLD_NOW)` probes loaded the x86, x86_64 and JIT-enabled libraries six
  times, created and destroyed a VM, resolved the RMS/FileConnection/M3G/PIM/
  Nokia builtin classes and ran the ArrayDims fixture with a passing result.

This release does not claim a general speedup. The RMS lookup numbers are
microbenchmark results; the Math, Random, `System.arraycopy`, CP-cache, heap and
M3G changes are correctness repairs. RMS callback-driven store mutations,
callback ordering and transactional rollback, record listeners and the constant
version getter remain unimplemented, and a pre-existing record-add failure can
still consume a next record ID before insertion. Minecraft's generator uses its
own `Gen$Rnd`, so the Random dispatch work has no measured effect on world
generation.

| Download | Contents |
|---|---|
| Linux x86_64 | SDL2 desktop launcher/CLI and five project-authored sample MIDlets |
| Windows x86_64 | Desktop launcher/CLI, SDL2 DLL and the same samples |
| Android universal | arm64-v8a, armeabi-v7a, x86 and x86_64 APK |
| Android arm64-v8a | Smaller arm64-only APK |
| libretro cores | Linux x86_64/aarch64/armv7 and Android arm64/armv7/x86_64 |

Android packages have versionName 0.5.6, versionCode 56, minSdk 21 and targetSdk
34, and retain the preceding release's signing certificate for upgrades.
Linux ELF binaries target glibc 2.17; desktop Linux also needs the distribution's
SDL2, libcurl and zlib runtime libraries. The Linux CLI and native libretro core
reuse the tested GCC objects with Zig's 2.17 link stubs. The host checks do not
certify every older distribution, a physical ARM device or a physical Windows
machine.

The attached build manifest binds packages to the release commit and records
platform checks; SHA256SUMS covers the downloads. Android APKs and libretro
archives contain no game JARs, RMS or worlds. Desktop samples are authored
fixtures, not redistributed commercial games. GitHub also provides source
archives for the release tag.
