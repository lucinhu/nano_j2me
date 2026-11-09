# Nokia packed pixels and shape semantics — 2026-11-09, round 5

Source baseline: `4d82ba933d3b8a4b72bff29649345a962c2b8695`. This batch completes the byte-pixel and explicit-shape portions deferred in round 4. It adds no builtin classes, methods or fields and preserves their order for save-state compatibility.

## Packed byte pixels

The old byte overload treated bytes as individual greyscale pixels, ignored the transparency mask and manipulation, and used overflow-prone array arithmetic. The implementation now supports the SDK literals `1`, `-1`, `2`, `4`, `8` and `332`:

- Horizontal grey formats pack pixels most-significant field first; offset and scanlength count pixels, including unaligned, overlapping, zero and negative horizontal strides.
- Vertical 1-bit grey uses the SDK page formula: eight rows per page, least-significant bit at the top. Positive scanlength is required; zero and negative vertical scanlength throw `IllegalArgumentException`, including empty rectangles.
- Grey values increase from white to black, following Nokia-device behavior documented in the local FreeJ2ME-Plus reference. RGB332 remains RGB. Masks use the same field width as pixels, with zero transparent and the maximum fully opaque.
- Draw validates the complete source and optional mask before rendering, clips in 64-bit device coordinates, and applies all sixteen rotation/flip combinations. Temporary drawing colors do not overwrite Graphics state.
- Get applies translation and ignores the clip, validates both output arrays before writing, and preserves padding and untouched packed fields. Screen masks are opaque; image masks reflect alpha. RGB-to-grey uses inverted integer luma; alpha quantization preserves a nonzero alpha as at least one mask unit.

`test_directgraphics_bytes` includes both SDK vertical examples as literal arrays, independent bit-by-bit packing checks, padding sentinels, all formats/manipulations, masks, translation, clipping, invalid ranges and huge clipped coordinates. The CTest target supplies `--bounded` to enable screen and huge-coordinate cases that are unsuitable for the old implementation.

- Old implementation, ordinary cases: 3,263 passed / 2,370 failed; UBSan also finds three signed overflows in byte indexing.
- Fixed implementation, including bounded cases: 5,651 passed / 0 failed in both Release and ASan+UBSan. Both changed translation units compile with `-Werror`.

## Alpha and explicit shapes

`setARGBColor` previously forced opaque alpha. `fillTriangle` ignored its color argument, and polygon/triangle calls could replace the current Graphics color. Concave polygons were reduced to one left-to-right span per row, filling holes and concavities. The changes preserve the supplied alpha, use the explicit shape color without storing it as current state, and fill sorted crossing pairs according to even-odd parity.

Fills sample integer coordinates with half-open edges: top/left included, bottom/right excluded. The SDK does not specify exact edge rasterization; triangle fill now uses the same convention as polygon fill. Outlines blend each covered pixel once, including shared vertices and self-intersections, using a bit mask cropped to the visible shape bounds. Coordinate differences and products are widened to handle the full Java `int` range; iteration is bounded by the visible clip. Temporary allocations fail before touching pixels.

Existing shape tests that passed zero-alpha colors but expected opaque output now pass explicit `0xFF...` colors. New tests separately exercise transparent drawing. The underlying graphics primitives and the existing int/short pixel implementation are unchanged.

The exact ordinary shape subset fails against the old implementation with 20,309 passed / 2,878 failed, and passes against the fixed implementation with 23,187 / 0. Separate old sanitizer probes catch `INT_MAX + 1` in polygon array validation and signed coordinate-difference overflow in filling. `test_directgraphics_shapes` passes 23,405 checks with zero failures in both Release and ASan+UBSan with leak detection. Its oracle uses point/ray parity for fills and full-path Bresenham coverage for outlines, independently of production sorting and bounded interpolation. It includes all 256 current-color alpha values, concave and self-intersecting paths, shared vertices, dotted outlines, 96 deterministic random polygons, font/state preservation, extreme coordinates and eight injected scratch-allocation failures. Allocation failures leave pixels, roots and live host allocations unchanged. Both modified translation units pass `-Werror`.

## Java and game checks

The Nokia Java fixture now checks transparent and half-transparent MIDP drawing after `setARGBColor`, an explicit green triangle, a concave polygon with an unfilled central gap, unchanged Graphics state, packed two-bit greys with a mask, and a vertical packed get/draw round trip with padding preservation. The old binary fails these checks, including an array-bound exception when interpreting vertical output as one byte per pixel. After rebuilding the fixture, the integrated modified CLI passes all checks.

The original `tests/games/宠物王国2紫_5550.jar` (SHA-256 `4e852ab961a3e5cf50cb62c61dad60a40b59fb5000026583a48d4ca5395977d8`) contains both `fillPolygon` and `setARGBColor` method references. Both baseline and modified 1,200-frame replays reach the same indoor scene and dialogue; all ten sampled frames are pixel-identical, and the final dialogue image was visually inspected. Both runs have no thrown or uncaught exception, 446 paints and 14 collections. Method references do not establish that those branches execute in this scene. This checks this replay, rather than complete game compatibility or throughput.

Replay settings: headless, 240×320, 16 MiB heap, paced 30 fps, fresh isolated RMS/filesystem directories, frame dumps every 120 frames. Keys:

```text
100:soft1 160:fire 220:soft1 280:fire 340:fire 400:fire 540:fire 660:fire 720:soft1 780:fire 840:fire 900:fire 960:fire 1020:fire 1080:right:down 1140:right:up 1160:fire
```

An initial Bounce Tales candidate called `System.exit()` after 90 frames in the baseline; it was not used as evidence for these fixes. All game JARs and existing user saves remain untouched. Replays and evidence stay under `.local-tools/debug-runs/opt-round5-*` and are not committed.

## Scope and reproduction

The integrated build succeeds. Serial CTest reports 144/144 passed in 297.26 seconds, including the new byte/shape tests, Nokia Java fixture, three FreeJ2ME reference-frame comparisons and SDL2 client/save-state checks. Core API audit still resolves all 982 reference members. All 49 builtin field/method table blocks checked by the integration script match the baseline byte-for-byte.

CTest success includes existing internal limitations: `check_threaded_dispatch` performs its two default-configuration checks but skips runtime comparison because its hand-written source list omits client helpers (`nj_client_settings_load`, `nj_library_new`, `nj_lib_scan`, `nj_library_free`). The engine test skips with the default `NJ_ENABLE_ENGINE=0`; SDL2-compat synthetic text replay and platform-SDK-only sources also retain their documented skips. The normal CMake runtime and affected interface tests execute successfully. The manual dispatch-script omission is recorded for follow-up.

Run the registered tests after building and regenerating the Nokia fixture:

```sh
cmake --build build -j4
tools/make_nokiademo_jar.sh
ctest --test-dir build --output-on-failure -R '^(test_directgraphics|test_directgraphics_bytes|test_directgraphics_shapes|test_directutils|check_nokiademo)$'
```

An isolated sanitizer run can be repeated with:

```sh
cmake -S . -B .local-tools/debug-runs/round5-san -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNJ_SANITIZE=ON -DNJ_WERROR=OFF
cmake --build .local-tools/debug-runs/round5-san \
  --target test_directgraphics_bytes test_directgraphics_shapes -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir .local-tools/debug-runs/round5-san \
  --output-on-failure -R '^test_directgraphics_(bytes|shapes)$'
```

`NJ_WERROR=OFF` avoids an existing GCC 16 warning in the unrelated socket source; the changed source/test objects were also compiled separately with `-Werror`. The byte sanitizer comparison uses an isolated CMake sanitizer build and the same source/test pair against the baseline; huge-coordinate and screen tests run only with the fixed implementation. This batch has no timing benchmark and makes no whole-game FPS claim. Correct packed decoding and clipping reduce needless work, while correct polygon coverage can require more work than the old inaccurate fill.

The prior shared MIDP encoded-image validation and total-heap-exhaustion exception fallback remain outside this batch. Precise grey/alpha quantization and unsupported vertical scanlength policy are implementation choices where the SDK does not define them.

Normative API: [Nokia UI 1.1 DirectGraphics](https://nikita36078.github.io/J2ME_Docs/docs/Nokia_UI_API_1_1/com/nokia/mid/ui/DirectGraphics.html). Grey polarity cross-check: `PlatformGraphics.java:1348–1351` in the local FreeJ2ME-Plus reference; its unrelated packed-offset bugs were not copied.
