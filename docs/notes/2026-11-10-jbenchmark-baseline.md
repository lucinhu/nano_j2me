# JBenchmark baseline and compatibility investigation

The four original JARs were exercised on the same private Release CLI from
`2a633ba9439efcd429b658d0485fbc9268372305`, after merging the incoming Item,
Gauge and M3G loader corrections. Only JBenchmark 1.1.1 reached its Results
page in this baseline batch. JBenchmark2, JBenchmark3D and JBenchmark HD did
execute workloads but did not produce final results. This note preserves those
pre-fix observations separately from subsequent corrected-runtime measurements.

## Measured configuration

- Intel Core Ultra 9 275HX, Linux 7.2.11 x86-64, GCC 16.2.1.
- SDL2 2.32.74, X11 under the existing Wayland desktop, software renderer.
- CLI SHA-256: `8eea1c23d5aaa79178f6d6c678156e640c9a70893c880d5cf25bf467c8394955`.
- Java canvas 240×320, heap 16 MiB, CPU affinity 3, `--fps 0`, speed 100%.
- Separate filesystem, RMS and XDG directories; no score submission.
- `--scale 2` was requested, but the actual captured window was 960×1280.
  The SDL port currently keeps a separate automatic scale and does not consume
  the public scale value. All four measurements used the same actual size.
- JIT and engine disabled; threaded dispatch and superinstructions enabled;
  media disabled in this private build. The root build was preserved.

These are desktop software-rendering observations, not Android scores or an
A/B optimization result. An earlier JBenchmark 6608 result used an older
runtime and a separate run and is not a measured speedup comparison.

## Results from the merged CLI

| Program | Version | Final result |
|---|---|---|
| JBenchmark | 1.1.1 | **7843** |
| JBenchmark2 | 2.1.1 | Unavailable: no Results page |
| JBenchmark3D | 3.1.0 | Unavailable: no final four metrics |
| JBenchmark HD | 4.0.3 | Unavailable: no final eight metrics |

JBenchmark's five final components are Text **1511**, 2D Shapes **1625**,
3D Shapes **1432**, Fill Rate **1534**, and Animation **1741**. Their sum is
7843; the Results page also reports 240×320. The process exited normally after
capturing the result, with 64.852 seconds of total supervised wall time. This
includes menu/result handling rather than only the five ten-second tests.

The original benchmark occasionally divides its frame count by a zero elapsed
millisecond value while drawing the FPS overlay. Its own exception handler
catches these `ArithmeticException`s after the drawing workload. Four such
messages occurred in the merged JBenchmark run; none was uncaught. The final
score is accumulated separately by the original Java code.

JBenchmark2's first merged run reached the 300-second supervision limit. A
separate correctly focused XTest run was stopped with F10 after 145.110
seconds without Results. The latter is an incomplete test, not a successful
score run merely because its process exit code was zero. It logged 47 caught
FPS-overlay arithmetic exceptions, no layout rejection and no uncaught
exception. An Expose/resize changed the previously static Welcome screenshot
to a white canvas, establishing that a screenshot can lag the Java state.
Current-display and thread state must therefore be observed directly before
attributing the failure to input, thread joining or painting.

JBenchmark3D reached the 300-second limit after approximately 3.92 million
M3G calls and 95.34 million triangles. The last captured `KTexels: 172240`
was an in-progress display, not a final result. HQ/LQ scores, triangles/s and
KTexels/s all remain unavailable. The earliest effective Start time was not
established precisely; the recorded focused XTest event is not treated as
proof of the first Java transition.

The reported `observed: threads` statistic is the cumulative number created,
not the number of live threads. The original failed runs did not capture
snapshots. A denied debugger attach was not bypassed; subsequent investigation
uses a private synchronous observer without changing the JARs.

## HD baseline failures

JBenchmark HD was stopped with F10 after 211.148 seconds without reaching its
Results page. Its eight final fields—bilinear, trilinear, perspective and depth
quality; smooth and textured triangle throughput; fill rate; and gaming—are all
unavailable in this baseline. The unchanged car screenshots show progress
content, rather than final scores. No missing field is converted into zero or
the benchmark's internal failure value of -1.

The original JAR caught unsupported-mipmap and two-texture-unit checks, a null
Node receiver during its gaming setup, and an IncompatibleClassChangeError from
its static LocalDevice.getProperty call while collecting system information.
The subsequent fixes address static Bluetooth dispatch, the loader's skin-record
truncation and retained skeleton graph, and Display callback delivery. They do
not advertise texture capabilities that the renderer has not implemented.

## Merge validation and scope

Before the merged runs, all four targeted tests passed: `test_lcdui`,
`test_m3g`, `test_m3g_file`, and `test_m3g_load_memory`. CTest's changed summary
wording caused a wrapper assertion after the four tests passed; an offline
metadata correction preserves that failure and the original successful test
log. No successful native workload was repeated to repair the metadata.

The original Minecraft JAR also passed a bounded 1600-frame SDL replay at
30 FPS, plus separate 300-frame old-state and newly-written-state resumes.
The SDL scene matched the previous terrain/HUD reference; both resumed frame
images matched each other. There were no uncaught exceptions, invalid chunks
or fallback cold starts. The existing caught UDP receive loop after snapshot
restore remains a separate limitation. Paced scene timings are not throughput
measurements.

The native GEN profile is a separate frozen measurement on the older
`a1543020135830d41db017e08c74894479748149` runtime; see
[the sampling report](2026-11-10-minecraft-gen-profile.md). It is not relabeled
as a measurement of this merged runtime.

Original JAR SHA-256 values:

- JBenchmark.jar: `d96f92052f68afe09f48841d4c232fe1396d64da01445bf44653b9440a583f03`.
- JBenchmark2.jar: `058b2c170586a92706a08619a303f61180c49ed13f40fd28ac038ebc914ceb4d`.
- JBenchmark3D.jar: `147b28fad9680856ce124f786000e6e4437accbe93846bccb226a5beb36de074`.
- JBenchmark HD.jar: `58cd27a6fe6fc539fabb3bb0f593cd45ce236e3e242e5d64e7f01b1db07f4890`.

Raw commands, input events, logs, original screenshots, failed-run metadata,
private configurations and an independently checked 111-file hash manifest
are retained locally under `.local-tools/debug-runs/jbenchmark-20261110/`.
No commercial JARs, user saves or private binaries are included in this note.

## Corrected-runtime measurements

The corrected CLI is a separate binary: SHA-256
`1733fc812bbfaf0031df891d25d7c656fb921fe2a666f18c86d162cee1a985d1`,
built from `6a135819c026183cf8409e9d79948c735ad54dd6` plus the Display,
Bluetooth/native-slot and M3G skin repairs described in the linked notes. Its
17-file source manifest and generated constant fixtures are retained unchanged.
A later tool-only correction makes the SDK's Bluetooth declaration static;
independent `javac` and `javap` checks confirm the proper `invokestatic` without
rebuilding or relabeling the measured runtime.

The four JARs run sequentially with the same CPU 3 affinity, 240×320 Java canvas,
16 MiB heap, `--fps 0`, 100% speed, software SDL/X11 renderer and actual 960×1280
window. No other emulator or compilation workload runs concurrently. Results
come from the original final Results pages, not process-exit status or an
in-progress counter. These are single desktop observations; the different source
versions and repairs do not establish a stable percentage speed improvement.

JBenchmark 1.1.1 reaches **10769**, with Text **2230**, 2D Shapes **2097**,
3D Shapes **2117**, Fill Rate **2084**, and Animation **2241**. The components
sum to 10769. Its process exits normally after F10; supervised wall time is
127.834 seconds, including result-page handling.

JBenchmark2 2.1.1 reaches **7428**, with Image manipulation **1997**, Text
**43280**, Sprites **58823**, 3D Transform **2133**, and User interface **2085**.
Its result is not a sum of these five components. A screenshot at approximately
71 seconds still shows Welcome, but a later capture from the same single-Start
run contains the complete Results page. No resize, second Start or debugger
attach intervenes. The early image is therefore not evidence of a new deadlock.
The process exits normally after F10; 209.889 seconds of supervised wall time
includes result-page handling.

JBenchmark HD 4.0.3 reaches its Results Form. Six of its eight fields are
visible: Bilinear filtering **Success (91%)**, Trilinear filtering **Failed/Not
supported**, sub-pixel perspective correction **Success (99%)**, Z-buffer depth
**Failure (7 bit)**, Smooth triangles **7,669,405 tri/s**, and Textured triangles
**6,637,914 tri/s**. The quality test's reported 7 bit is not a measurement of the
internal buffer's storage width. The lower Fill rate and Gaming fields remain
unavailable because navigation does not reveal them in this runtime; they are
not zero or inferred failure scores. Their values, and the lower-page version
and dimensions, remain null in the raw record. The manifest independently
identifies version 4.0.3. The run exits normally after F10 at 558.848 seconds,
including extensive Results navigation. No F5 snapshot is taken because no
private snapshot path was configured. Mipmap and two-texture-unit requirement
exceptions remain recorded; no unsupported capability is advertised as working.

JBenchmark3D has **no reportable final scores** on this runtime. Its first
attempt is interrupted by supervisor SIGTERM at 127.103 seconds; it has no
Results capture and is not a wall timeout. A separately authorized, single-Start
recovery reaches the 300-second observation boundary (300.311 seconds of
supervised lifetime) without an observed Results page. Its process teardown
returns zero, while the supervisor correctly records a timeout. Scene-loading
messages confirm some execution but do not establish the final phase. HQ, LQ,
Triangles/s and KTexels/s remain unavailable. The two attempts are preserved
separately; neither a stale Welcome image nor the timeout establishes a Java
hang or its cause. This remains an open compatibility investigation.

Corrected-run commands, settings, original screenshots and raw logs are retained
separately under `.local-tools/debug-runs/jbenchmark-fixed-20261110/compat/`.
An 81-file final evidence manifest and the unchanged 16-file preparation manifest
cover the four original attempts and the separate 3D recovery. The earlier
baseline images and measurements above remain unchanged. These measurements
precede the upstream Form scrolling/layout changes and do not describe that
later runtime.
