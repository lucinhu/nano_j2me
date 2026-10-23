# Native M3G targets and fixed-work comparison

Branch `perf/m3g-rgb565`, based on `9247556`. Implementation: `efafb38`.
The numerical archive is [m3g-rgb565-results.json](m3g-rgb565-results.json).
These changes follow the rendering priorities in
[JIT_LOOP_ACCOUNTING.md](JIT_LOOP_ACCOUNTING.md); they do not change the JIT.

## What changes

The existing native triangle pipeline only accepted ARGB8888. Rendering to a
port-owned RGB565 framebuffer instead called Java Graphics methods for flat
triangles, without the equivalent texture and depth processing. A title could
obtain full native rendering by drawing into an ARGB8888 Image and copying that
Image to the framebuffer, at the cost of another color buffer and full-frame
conversion/copy.

The native path now supports RGB565, RGB888 and ARGB8888, including padded row
strides. Texture sampling, lighting, depth testing/writing, alpha testing and
blending use the existing triangle pipeline. RGB565 and RGB888 destinations
have implicit opaque alpha. Pixel helpers use small memcpy operations to avoid
alignment and aliasing assumptions, with inline expansion in the raster loop.
No full-size intermediate color buffer is allocated for direct rendering.

Graphics translation and clipping are captured at bindTarget in six integer
fields. Visible bounds intersect that frozen clip, the surface, and the
viewport; projection retains the full viewport. Empty clips do no work.
Depth indices use physical target coordinates and full target dimensions,
so changing viewport does not reinterpret previous depth entries. Clear uses
the native graphics primitive and clears the visible depth rectangle when the
buffer is current; background images use format-aware writes. The previous
once-per-frame automatic depth initialization is retained.

At 240x320, the 32-bit depth array requires **307,200 bytes** plus its object
header. This is additional memory relative to the old incomplete RGB565 flat
fallback. The color-buffer saving below compares equivalent textured/depth
rendering through an ARGB Image against direct RGB565 rendering. Applications
must account for both facts when choosing their heap budget.

Six square roots whose inputs and results are already floats now use sqrtf.
The target object contains 27 single-precision square-root instructions,
compared with 21 before the change. This confirms code generation; it does not
isolate the square-root change's performance contribution.

## Fixed scene, 240x320

The fixture submits 160 textured, lit triangles per render, with a rotating
model and perspective camera. It waits for an explicit path selection, renders
one warm-up frame, then times **60 actual paint/render calls**. Both path
variants contain identical Java class payloads. The buffered path includes
Graphics.drawImage; the direct path binds the Canvas Graphics.

The paired integration build snapshots all linker inputs and reproduces the
original ELF byte for byte. Only the M3G object is replaced for the candidate;
both test images embed the same selectable fixture. Existing media and UI
objects are identical. JIT and FPS overlay are disabled during measurement,
and each run gets a fresh VM and isolated temporary RMS directory. Three
baseline buffered runs precede three alternating candidate buffered/direct
pairs. No screenshot is taken in a timed interval.

| Version and path | 60 renders, three runs (ms) | Mean render FPS | Peak Java heap (bytes) |
| --- | --- | ---: | ---: |
| Baseline, ARGB Image then copy | 3913, 3916, 3918 | 15.32 | 947312 |
| Candidate, ARGB Image then copy | 3942, 3943, 3945 | 15.22 | 947328 |
| Candidate, direct RGB565 | 3564, 3563, 3564 | 16.84 | 639904 |

Direct rendering improves this fixture's throughput by **9.88%** versus the
baseline buffered route, or **10.65%** versus the candidate buffered route.
Its 60-render time falls by 8.99% versus baseline. The candidate buffered route
takes 0.71% longer than baseline, so this is not a universal renderer speedup.
Within the candidate, peak Java heap falls by **307,424 bytes** (300.22 KiB).
Normal application image size increases by 1,568 bytes in this build.

Every timed run reports 61 M3G renders including warm-up, 7,142 rasterized
triangles, native-framebuffer CRC `48f14000`, and display-region CRC `4ffb9823`.
The direct route makes zero fallback Java Graphics calls. Each run exits
normally with zero external-memory delta, no live hot/code allocation and
successful temporary-RMS cleanup.

The timer covers the normal paint/scheduling and preceding presentation costs
between the start of render 2 and completion of render 61. VM creation,
teardown and the fixture's final 100 ms wait are outside it; the last display
presentation can follow the timing marker. VM frame and paint counters include
startup/blank work and must not be divided by this interval. These are fixed
scene render rates, **not measured racing-game FPS or display refresh rates**.
Only three repetitions per group were collected; no statistical confidence
interval or result for other scenes is claimed.

## Correctness and lifecycle validation

Four checks pass in both Release and Debug with ASan/UBSan: test_m3g,
test_m3g_targets, test_m3g_file and test_clip. The new target test performs
1,091,328 assertions per configuration. It exercises three pixel formats on
240x320 port-owned buffers with row padding and surrounding guards, including:

- Two-color RGBA textures and overlapping near/far geometry.
- Five blend modes, alpha-test rejection of both color and depth, and disabled
  depth writes.
- Translated and empty Graphics clips, negative/partial viewports, viewport
  changes and partial depth clears that preserve a neighboring region.
- Background images, full-image comparison and allocation/free balance after
  VM destruction.

Opaque output is compared with ARGB8888 after target-format quantization.
Blend cases use an already quantized initial destination and one triangle;
repeated translucent layers can accumulate RGB565 rounding and need not match
an ARGB8888 intermediate bit for bit. Assertions also reject per-triangle Java
Graphics fallback. This assertion fails against the pre-change library.

The normal application was restored after the embedded-fixture runs and its
full image hash and five segment headers verified. The original demo completes
90 VM frames; Asphalt 4 completes a 300-frame startup run; Tetris completes a
300-frame input run. All three exit with balanced external memory and no live
hot/code allocation. Asphalt logs caught title exceptions and does not reach
measured racing gameplay. Tetris accepts all 75,744 offered PCM frames, with
zero write errors or underruns; no listening or audio-quality result is claimed.

Two larger racing archives fail to create the 6 MiB Java heap before gameplay;
one is retried immediately after reset with the same outcome. Their failures
remain unresolved, and they supply no M3G/gameplay comparison. The existing
archive reader grows by doubling and copying buffers before VM creation;
known-size preallocation and largest-free-block telemetry are useful follow-up
work, but fragmentation has not been established as the cause. An incidental
non-benchmark title abort in the baseline media path is also excluded from the
paired timing samples.

The final application restores the original demo, retains all 25 menu entries
and the existing game partition, and leaves the launcher idle at 240x320 with
FPS enabled and JIT disabled. Test operations do not erase the game volume or
delete game archives. Raw target logs are not included in this review package.

## Reproduction

From this checkout, with a C compiler, CMake, Python and a JDK installed:

```sh
cmake -S . -B build-m3g -DCMAKE_BUILD_TYPE=Release -DNJ_BUILD_SDL2=OFF
cmake --build build-m3g --target test_m3g test_m3g_targets test_m3g_file test_clip nj2me-cli
ctest --test-dir build-m3g --output-on-failure -R '^(test_m3g|test_m3g_targets|test_m3g_file|test_clip)$'
cmake -S . -B build-m3g-asan -DCMAKE_BUILD_TYPE=Debug -DNJ_BUILD_SDL2=OFF -DNJ_SANITIZE=ON
cmake --build build-m3g-asan --target test_m3g test_m3g_targets test_m3g_file test_clip
ctest --test-dir build-m3g-asan --output-on-failure -R '^(test_m3g|test_m3g_targets|test_m3g_file|test_clip)$'
bash tools/make_m3gtarget_jar.sh .local-tools/m3gtarget
```

Use a 240x320 RGB565 port and run m3g-select.jar in a fresh VM. Deliver key 6
at VM frame 5 for the buffered case, or key 5 for direct rendering. Verify the
M3G_TARGET_DONE marker, render/triangle counts, output checksums and teardown
balance. The standalone direct/buffered JARs select without input and can also
validate matching work counts on the host; host timings are not target FPS.
Reproduce the paired baseline with its buffered route: its old direct fallback
does not provide equivalent rendering. Keep integration code, compiler flags,
fixture classes and runtime settings identical between builds.

Clip/translation capture follows the binding model discussed in items 16 and
19 of the [JSR 184 maintenance clarifications](https://jcp.org/aboutJava/communityprocess/maintenance/jsr184/jsr184-proposed.html).
The tests cover this patch's behavior, not full JSR 184 conformance.
