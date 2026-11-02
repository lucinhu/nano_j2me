# .m3g mutation fuzzing: corpus assets through Loader, duplicate and the raster path (task-31)

Date: 2026-11-03.  Worktree: `r7-m3gfuzz`, branch `r7-m3gfuzz` from `f515371`.
Build: ASan + UBSan + LSan (`-DNJ_SANITIZE=ON`, Debug) plus the normal Release tree.
Result: **no new product bug**.  Seven thousand mutated containers, all 975
real corpus `.m3g` assets, the object copies, the getters and 3000+ rendered
scenes produced no sanitizer diagnostic, no leak report, no crash, no hang and
no unexpected Java exception.  This note records the harness, the corpus, the
mutation schedule, the runs, and the API-contract gaps that were found,
investigated, and deliberately **not** changed (with the reason).

Evidence: `~/workspace/wt/evidence-r7-m3gfuzz/` (seed inventory, run
directories, logs).  The corpus jars and the saved state are untouched:
`MinecraftJ2ME_PE_0.6.1_V2.2.jar` sha256 `fbe72c5c...`, `world.state` sha256
`1e3171e8...`.

## 1. Corpus: 975 real assets, not 27

The earlier 3D sweep counted 27 **JARs** that use M3G; those 27 jars contain
975 individual `.m3g` files, 10,499,928 bytes in total:

- seeds extracted to `evidence-r7-m3gfuzz/seeds/` with the jar stem and asset
  path in the name; `seed-inventory.json` keeps jar, asset path, byte count,
  sha256 and seed filename.
- largest: 生化危机3D (CAPCOM经典) character models 93–127 KB and 蓝牙真3D格斗
  fighter models 33–75 KB; 浴火银河2 contributes hundreds of 0.3–13 KB ship and
  effect models; Minecraft, 极品飞车, 毁灭战士 and CS assets are also in the set.
- 846 of the 975 load standalone.  The other 129 throw
  `java.io.IOException` from `Loader.load` because they reference external PNG
  textures (e.g. `start_finish_64x32.png`, `citroen.png`, `wheel.s3.png`) that
  live in the JAR next to the model and were not extracted with it.  That is
  the JSR 184 Loader contract ("an I/O error ... a referenced object cannot be
  loaded"), not a bug, so the mutation waves use the 846 valid seeds.

## 2. Harness

`tests/m3gmutdemo/M3gMutDemo.java` (built by `tools/make_m3gmutdemo_jar.sh`
into `tests/fixtures/m3gmutdemo.jar`):

1. reads `file:///root/mut/manifest.txt` from the runtime `--fs-dir`;
2. for every listed file: reads the bytes, prints `MUT <n> <name>` and calls
   `Loader.load(byte[], 0)`; malformed inputs must come back as a Java
   exception, and an ASan/UBSan abort is the finding (the `MUT` line names the
   file);
3. every returned object is exercised: `Object3D.duplicate()`, then the main
   getters (`Mesh.getSubmeshCount/getAppearance/getIndexBuffer/getVertexBuffer`,
   `VertexBuffer.getVertexCount`, `Texture2D.getImage`, `Image2D` dimensions and
   format, `Sprite3D.getImage`, `Group.getChildCount/getChild`);
4. with `file:///root/mut/render.on` present it also renders: a 32x32 RGB
   `Image2D` is bound as the `Graphics3D` target, each loaded `World` is
   rendered (a synthetic perspective `Camera` is added and made active when the
   asset has none), and every other loaded `Node` is rendered with the current
   camera.

The synthetic camera matters: most loose `.m3g` assets are meshes/groups
without a Camera, so without it only a handful of scenes would ever reach the
rasteriser.  With it, a real asset's geometry, vertex/index formats, textures,
fog and blending all run under the sanitizers.

`tools/m3g_mutation_fuzz.py` is the deterministic driver:

- `--mode baseline` copies the seeds and writes the manifest, so "asset is
  invalid" is separated from "mutation is a finding";
- `--mode mutate --count N` applies one or two mutation operations per file
  from a fixed LCG (`0x5eed31` by default): bit flip, byte overwrite,
  truncation, arbitrary 32-bit overwrite, section header corruption
  (compression scheme / compressed length / uncompressed length), object
  header corruption (object type byte / object length), a 32-bit write inside
  an object payload (counts, component sizes, strides, flags), byte-range
  deletion and garbage append;
- output names are ASCII (`m0000.m3g`) and `map.tsv` maps each mutant to its
  seed and the exact operations, so a crash is attributable and minimisable;
- everything is reproducible: same arguments give byte-identical run dirs.

## 3. Runs (ASan + UBSan + LSan, headless)

Environment:
`ASAN_OPTIONS=abort_on_error=1:detect_leaks=1:print_stacktrace=1`,
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, `--heap 64/96`,
`--fps 0`, `--run-frames 5`.

| run | inputs | load OK | load failed | exercise | sanitizer |
| --- | --- | --- | --- | --- | --- |
| baseline | 975 real assets | 846 | 129 (external PNG IOException) | load only | clean |
| wave 1 | 2000 mutants | 464 | 1536 | load only | clean |
| wave 2 | 3000 mutants | 790 | 2210 | duplicate + getters: 2043 copies, 2043 getter sets, 0 failures | clean |
| wave 3 | 2000 mutants | 552 | 1448 | render smoke with the earlier harness: 6 renders, 0 failures | clean |
| wave 4 | 3000 mutants | 790 | 2210 | render on: 1756 rendered scenes, 0 failures | clean |
| real render | 975 real assets | 846 | 129 | 1409 copies + 1322 rendered scenes, 0 failures | clean |

All expected failures were `java.io.IOException` with a loader message; no
`NullPointerException`, `ArrayIndexOutOfBoundsException`, `ClassCastException`,
`NumberFormatException` or `OutOfMemoryError` escaped from loader internals.
The load-only, duplicate/getter and render waves each ran with leak detection
on and produced no LeakSanitizer report: the loader does not leak the objects,
`byte[]`s or native blocks of malformed or valid containers on this corpus.

## 4. Investigated but not changed

These are real divergences from the JSR 184 javadoc, but none is a
memory-safety or leak bug, and each fix would either change rendering behaviour
or reject assets that currently load.  They are recorded for the P3
compatibility decision rather than fixed here.

1. **`Texture2D(Image2D)` / `setImage` accept null, non-power-of-two and huge
   images.**  The spec requires `NullPointerException` for null and
   `IllegalArgumentException` for non-POT or over-max dimensions; the runtime
   stores whatever it is given (`src/midp/nj_lib_m3g.c:5278`, `:5290`).  The
   rasteriser is safe with such textures: `sw_texel()` returns the untextured
   colour when `tex` is null or the dimensions are non-positive
   (`src/midp/nj_lib_m3g.c:11309`) and clamps every sample to
   `[0, tex_w-1] x [0, tex_h-1]` (`:11406-11411`), so non-POT or null textures
   draw (clamped/untextured), they do not read out of bounds.  Making the check
   strict would reject non-POT textures that some exporters write and that
   currently load; worth doing only together with a corpus compatibility pass.
2. **`VertexBuffer.setPositions/setColors/setNormals/setTexCoords` skip the
   spec's IAE validation** (component count/type, vertex-count consistency
   "when at least one other VertexArray is set", `scaleBias` length).  Again
   safe at draw time: `jr_vertex()` rejects `v >= pcount/tcount/ncount`, and
   `jr_colour1()` requires `ccomp >= 3` and `v < ccount`, so a malformed buffer
   samples defaults instead of reading out of bounds.  Tightening these would
   risk rejecting Minecraft PE's incremental terrain updates, which build the
   arrays piecewise, so it needs its own compatibility measurement.
3. **`Graphics3D` remains permissive** where the spec mandates NPE/IAE/ISE
   (target lifecycle, camera presence, render without a target).  This is the
   already documented P3 behaviour-only gap (docs/notes/2026-11-02-m3g-api-gaps.md
   §7); nothing in this corpus relies on the exceptions and nothing crashes.
4. **An index stream that references vertices outside the VertexBuffer is
   silently skipped** instead of throwing the spec's `IllegalStateException`:
   `jr_vertex()` returns 0 for `v >= pcount` (`src/midp/nj_lib_m3g.c:12430`), so
   the triangle is dropped.  Bounds-safe and invisible to a title that ships
   valid geometry; not worth a behaviour change.

## 5. Gates

- `test_m3g` Release: 933 checks, 0 failures.  ASan+UBSan build: 933/0.
- `ctest`: 137/137 (Release).
- M3GDemo fixture: `M3G checks=148 failures=0`.
- `tools/check_no_statics.sh`: OK.
- Minecraft resume under ASan from `world.state` with `--heap 32` and 120
  frames: `resumed MinecraftMIDlet`, frame budget reached, exit 0, no
  sanitizer diagnostic; run on copies, original state and JAR hashes unchanged.

## 6. Reproduction

```sh
# harness
./tools/make_m3gmutdemo_jar.sh

# corpus inventory (already produced)
python3 tools/m3g_mutation_fuzz.py --seeds ~/workspace/wt/evidence-r7-m3gfuzz/seeds \
    --run ~/workspace/wt/evidence-r7-m3gfuzz/run-baseline --mode baseline
touch ~/workspace/wt/evidence-r7-m3gfuzz/run-baseline/fs/mut/render.on
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1:print_stacktrace=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
./build-asan/nj2me --port headless --heap 96 --size 240x320 --fps 0 \
    --rms-dir RUN/rms --fs-dir RUN/fs --run-frames 5 tests/fixtures/m3gmutdemo.jar

# mutation wave
python3 tools/m3g_mutation_fuzz.py --seeds .../seeds --seeds-file .../seeds-ok.txt \
    --run RUN --mode mutate --count 3000
```
