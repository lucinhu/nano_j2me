# The Android build's optimisation level is not a lever on this workload

`tools/build_android.sh` has always compiled the runtime with `-Os`, because the
APK ships one `.so` per ABI and the smallest library that runs is the one worth
shipping.  After 0.4.0 the obvious question was whether that choice costs the
player anything: the interpreter is the hot path on a phone, and `-O2` is the
level a desktop release build uses.  `--opt` was added so the question could be
answered by measurement instead of by preference.

Answer: on this project's benchmarks the level moves the interpreter by nothing
measurable, and `-O2` is slightly *slower* on a real title's frame loop.  The
default therefore stays `-Os`.

## Method

Two kinds of measurement, because each answers a different question:

* **Desktop, clang, CPU-pinned, min of N runs** (`tools/bench.sh`'s method:
  `taskset` to one core, user CPU time, report the minimum - noise only ever
  adds time).  Two workloads:
  * `tests/fixtures/bench.jar`, the interpreter's own dispatch benchmark
    (`--stats`, 70.5 M bytecodes per run);
  * a Minecraft PE world resumed from a snapshot with `--load-state` and run for
    600 frames at `--fps 0`, which is what the frame loop actually costs: the
    interpreter plus the software M3G rasteriser, in the ratio a title uses
    them.
* **The x86_64 API-26 emulator, end to end**, because the question was about
  phones.  Minecraft PE was launched from the launcher, its saved world loaded,
  and the title's own `MCJ: LOAD read N ms, prepare N ms` lines recorded;
  installs were interleaved between the two builds so a slow patch of the
  emulator could not be mistaken for a slow build.

The desktop A/B used `-Os`, `-O2` and `-O3` from the same source and the same
compiler (`clang 23`), changing nothing but `CMAKE_C_FLAGS_RELEASE`.  The
Android libraries are built by the NDK's clang; the same LLVM back end, so the
desktop A/B is the low-noise instrument and the emulator run is the sanity
check.

## Numbers

`tests/fixtures/bench.jar`, min CPU time of 8 runs:

| flags | min | median | throughput |
| --- | --- | --- | --- |
| `-Os` | 0.235 s | 0.237 s | 300.1 M bytecodes/s |
| `-O2` | 0.234 s | 0.236 s | 301.3 M bytecodes/s |
| `-O3` | 0.230 s | 0.232 s | 306.6 M bytecodes/s |

The spread is 2-3%; `-O2` and `-Os` are the same number.  Dispatch is a computed
switch with a table lookup; no LLVM level changes that, which is consistent with
the threaded-dispatch result in `docs/PERFORMANCE.md`.

Minecraft PE in world, 600 frames, min of 5 runs (`time=`/`fps=` from `--stats`):

| flags | min time | fps | vs `-Os` |
| --- | --- | --- | --- |
| `-Os` | 1.738 s | 345.2 | - |
| `-O2` | 1.804 s | 332.6 | **-3.7%** |
| `-O3` | 1.698 s | 353.4 | +2.3% |

`-O2` losing on the rasteriser is not a fluke - five runs of each landed in a
0.2% band (1.804-1.806 s vs 1.738-1.742 s), and the run-to-run spread inside a
build is far smaller than the gap between builds.  `-O3` wins about 2%, which is
inside the noise of the bench above and buys nothing for a release gate.

Size cost, x86_64 `libnj2me.so`, measured through `tools/build_android.sh`:

| flags | `.so` | debug APK |
| --- | --- | --- |
| `-Os` | 1,941,912 B | 895,129 B |
| `-O2` | 2,295,024 B (+18%) | 1,042,585 B (+16%) |

The emulator confirms that these are not the numbers that decide a player's
experience: the same world load, seven interleaved runs per build, ranged
`read 7.5-14.4 s / prepare 4.8-10.9 s` for `-O2` and `read 8.3-12.8 s /
prepare 7.7-11.8 s` for `-Os`.  The spread is 40% - the AVD shares a host and
rasterises through SwiftShader - and it is larger than any difference between
the builds.  Neither median nor minimum separates them reliably.

## What this means

* The Android `.so` stays `-Os`.  It is also the smallest, and nothing measured
  says a bigger one is faster in the frame loop a player sees.
* `--opt` stays, on both scripts, as the knob for the case this note does not
  cover: a size-limited embedded target, or an ARM device where the answer could
  genuinely differ.  The next person to try `-O2` for speed should bring an ARM
  A/B with `MCJ: LOAD` lines or a frame counter, not an expectation.
* The measurement that would settle the phone question is an on-device run on
  real ARM hardware with the same workload; the emulator cannot answer it, and
  neither can a desktop x86-64.
