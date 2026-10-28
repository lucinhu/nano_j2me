# M3G pixel-centre coverage and the NFS road seam

`极品飞车14之热力追踪中文版_4160.jar` showed a dark dotted diagonal through the yellow road marking and adjacent asphalt in tutorial 1/11. The seam was already present in the offscreen Canvas image; presentation and RGB565 conversion only copied it. The reference FreeJ2ME-Plus frame had a continuous marking.

## Cause and change

In `src/midp/nj_lib_m3g.c`, `sw_tri` seeds its edge equations at the sample centre `(minx + 0.5, miny + 0.5)`. On a fixed scanline, an edge is therefore

```
W(pixel_index) = w + sd * (pixel_index - minx)
```

Its zero, `minx - w / sd`, is already a **pixel index**, rather than a physical screen coordinate. The old span conversion subtracted another half pixel (`ceil(lo - 0.5)`, `floor(hi - 0.5)`). This admitted samples outside one triangle and omitted samples inside its neighbour.

The road's narrow strips have a texture boundary at `u = 0`. An outside sample extrapolated to a small negative u, wrapped to the other end of the repeated texture, and read dark edge texels. For example, the instrumented pre-fix run at frame 1600, pixel `(93,146)`, had `u = -0.000347329` and sampled `0xFF4C581B`. With corrected coverage the neighbouring triangle owns the sample, its u is positive, and it samples yellow. Enlarging triangles, changing the sampler, quantizing crossings, or rewriting LCDUI `fillTriangle` was unnecessary; those experiments were removed.

The final span uses `ceil(lo)` for the inclusive left bound and `ceil(hi) - 1` for the exclusive right bound. The initial exclusive viewport bound is `maxx + 1`. A sample exactly on a horizontal edge belongs to the top edge, not the bottom edge. This top-left ownership rule also prevents two transparent triangles from blending twice on their shared edge when depth writes are disabled. The interpolation, sampler, depth calculation and pixel loops retain their existing arithmetic.

## Regression tests

`tests/test_m3g.c:test_triangle_pixel_centres` renders through the Java Graphics3D API:

- A quarter-pixel triangle in a 64x64 viewport, in both windings. An independent edge-equation oracle checks 969 interior sample centres and every exterior centre, excluding exact edge ties. It covers an untextured flat span, a flat textured span and the Gouraud/general span. A repeated yellow/black texture exposes extrapolated negative u as black.
- A half-alpha red rectangle composed of two triangles, and two adjoining rectangles composed of four triangles, in both windings. Depth writes are disabled. Centre-aligned diagonal, horizontal and outer edges must cover the 48x48 rectangle exactly once: red 128 over black, with neither red 191 from double blending nor a gap.

The old span equations fail 12 coverage assertions. Correcting only the extra half pixel makes the isolated triangle pass but fails all four shared-edge alpha cases. The final correction passes **805 checks, 0 failures**, with both `NJ_M3G_FAST_PATHS=1` and `=0`. Full canonical `ctest`: **122/122**.

An independent detached worktree at `f835b44`, with only the final `src/midp/nj_lib_m3g.c` and `tests/test_m3g.c` copied in, also passes `test_m3g` (805), `test_gfx` (324 assertions) and `test_m3g_file` (105). Its corrected frame 1600 is byte-identical to the shared working-tree build (SHA-256 `4d6b7f5a8725db4dac43f4becc7458a7055bf84f4f948032f2cc9e9c9708e8bf`). Thus neither the result nor the regression tests depend on the pending media work.

## Game replay and evidence

The local evidence directory is `.local-tools/debug-runs/nfs-seam-20261028/`. It retains frame dumps, logs, `frame-metrics.json`, the before/after comparison, and the external `replay.c` embedding harness. The harness overrides the public port's `millis` and `sleep_ms` callbacks, advancing time by the requested sleep duration. This allows identical simulation state across builds without a test clock in the shipping headless port.

The replay uses 240x320, a fresh RMS directory, a 16 MiB Java heap, 30 fps and 1610 frames. Its key script is:

```
150:soft1,320:down,380:soft1,700:soft1,1000:soft1,1300:soft1,1450:soft1,1600:soft1,1750:soft1,1900:soft1,2050:soft1,2200:soft1,2350:soft1,2500:soft1
```

Frame 1600 is tutorial 1/11. The corrected yellow marking is continuous and the dotted line across the asphalt is gone. Against a clean original `f835b4415262aa38dd82d7361089ded95ef74027` build, the corrected frame changes 1,482 pixels. For example, `(93,146)` changes from RGB `(56,72,16)` to `(216,180,24)`, and `(92,147)` from `(32,44,16)` to `(200,196,72)`. Default fast and general rasterisers produce identical corrected frames (0 differing pixels). A normal-clock run of the final `build/nj2me` also reaches the tutorial and exits normally at the 1610-frame budget.

The temporary pixel probes, skip/grow switches, crossing-quantization helpers, Canvas dumps, and fake-clock environment switch were removed. Unrelated pending media changes were preserved separately from this graphics fix.
