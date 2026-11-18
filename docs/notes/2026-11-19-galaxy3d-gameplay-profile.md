# Real 3D gameplay profile: Galaxy on Fire 2 on 0.5.14 (2026-11-19)

## Why

The M3G fast paths added in rounds 32/67 were measured on the Doom3 *loading
animation*. Now that the corpus ships per-title key scripts, a real 3D game can
be driven to gameplay and profiled: `浴火银河2完整版_7689.jar` (Galaxy on Fire 2)
with `tools/corpus_keys/浴火银河2完整版_7689.txt`.

## Measurement setup

- gprof CLI built from `4ca20ca` (`-pg -O2 -g`, Release, CLI only).
- `--port headless --size 240x320 --heap 32 --fps 30 --run-frames 1200
  --stats --keys tools/corpus_keys/浴火银河2完整版_7689.txt`, `taskset -c 5`.
- Counters: `NJ_SPAN_COUNTS` instrumentation in a scratch worktree (the
  instrumentation is not committed).

## Result

```
stats: frames=1200 paints=2 gfx=1271055 tris=4626483 m3g=671475
       time=40.004s fps=30.0 bytecodes=1040448490 (26.01 M/s) natives=4628280
```

gprof flat profile (sampled CPU ≈ 7.0 s):

| share | self | calls | function |
|---|---|---|---|
| 27.47% | 1.92 s | 133,362 | `nj_interp_run` (game/Java execution) |
| 18.74% | 1.31 s | 36,568,601 | `sw_span` (span loop, fast paths inlined) |
| 12.16% | 0.85 s | 17,920,755 | `sw_tri_band` (per-triangle/band setup) |
| 10.30% | 0.72 s | 47,082,246 | `jr_lit` (per-vertex lighting) |
| 9.44% | 0.66 s | 1,068,859 | `nj_region_draw` (blits/compositing) |
| 3.58% | 0.25 s | 61,351,638 | `jr_fetch` (vertex fetch/transform) |
| 2.29% | 0.16 s | 8,974 | `mid_add_note` |
| 1.72% | 0.12 s | 48,743,528 | `nj_field_lookup` |
| 1.57% | 0.11 s | 61,412,174 | `jr_project` |
| 1.14% | 0.08 s | 65,761,348 | `nj_interp_const_arith` |
| 0.86% | 0.06 s | 99,062,128 | `nj_array_data` |
| 0.86% | 0.06 s | 20,450,546 | `jr_strip` (strip expansion) |

Span class counters (600 frames, same title): `flat_texargb=3,259,327`,
`gouraud_texargb=5,457,073`; `flat_opaque`, `flat_tex565`, `general`, `sprite`,
`alpha_ramp`, `near_tex`, `filt_tex`, `st_outer` all **0**.

## Interpretation

- The named span classes are already covered: even in real 3D gameplay every
  span is served by the flat/gouraud ARGB8888 fast paths. There is no
  uncovered span class to add here.
- The native M3G pipeline (span loop + band setup + vertex lighting/fetch/
  project/strip + region blits) is about 57% of the sampled CPU; the game's
  Java execution is 27%.
- Per-band setup is relatively heavy because gameplay triangles are small:
  17.9 M band calls for 36.6 M spans over 1200 frames (~15 k bands/frame,
  ~2 spans/band, ~47 ns/band). The round-67 hoist removed triangle invariants
  from the per-row loop; the remaining cost is the per-band `m3g_span`
  field copy and edge setup.
- Vertex work (`jr_lit` ~10% alone) is float math; any SIMD or reordering
  change risks pixel parity and was not attempted.

## Candidate levers (not attempted)

1. Restructure the band loop so the per-band `m3g_span` copy shrinks (share a
   persistent span and pass only varying fields to `sw_span`). Expected single
   digit percent; needs deterministic-prefix validation (this title is
   wall-clock driven, so use the Doom3 loader prefix or a deterministic scene
   for pixel parity).
2. Interpreter dispatch (~27%) — threaded dispatch is already on; a baseline
   JIT remains the documented open direction (`docs/JIT.md`), not a quick win.
3. Batch small `nj_region_draw` blits — changes the drawing order and needs a
   careful pixel-parity argument.

## Probe lesson

`--fps 0` is not usable for this title: it relies on wall-clock pacing, so at
uncapped speed it advances 1800 frames in 7 ms with 2 paints and no 3D work.
Gameplay probes of timed titles must use `--fps 30` (20 s of wall time for 600
frames).
