# M3G renderer defects found from the corpus feature audit

Date: 2026-10-26
Scope: `src/midp/nj_lib_m3g.c` (M3G loader + software renderer), `tests/test_m3g.c`
Evidence: `.local-tools/debug-runs/{skin-fight-02,skin-fight-03,baltoro-prefix,baltoro-reach3d,red-green,task6-verify}/`

## What this is

After the animation and mesh-cap fixes, the remaining reported symptom class was
"3D scene is wrong / models missing". Rather than guess, the loader was pointed at
the corpus: a throw-away scanner (`/tmp/m3g_audit.py`, results in
`.local-tools/debug-runs/3d-audit-02/`) decoded every `.m3g` in every JAR under
`tests/games` (975 files) and counted which features the titles actually use.
That ranking, not intuition, chose the work below.

Killed by evidence, not fixed (no defect, no code change):

- `CompositingMode` depth offset: **0** objects corpus-wide carry a non-zero
  `depthFactor`/`depthUnits`, so implementing it cannot change a single frame.
- mipmapping / filtering: every `Texture2D` asks for `FILTER_BASE_LEVEL` +
  `FILTER_NEAREST`; no mipmaps are ever requested.
- second texture unit: 30 `VertexBuffer`s declare two texcoord arrays, but every
  `Appearance` in the corpus has **zero or one** texture, so unit 1 is never
  sampled.
- fog, `MorphingMesh`, `Sprite3D`: **0** objects corpus-wide.
- World light cap (`NJ_M3G_LIGHTS`, 16): only 55 `Light` objects exist in the whole
  corpus (32 in one title across 15 files), so the cap is not reachable in practice.
- wrap modes, strip index encodings 128/129/130, camera projection types, and the
  five image formats were already handled; checked, not changed.

## Defect 1 (visible, user-facing): `SkinnedMesh` was never decoded — skinned models are invisible

`nj_m3g_decode_object()` had cases for every object type except 15 and 16, and
`nj_m3g_link_meshes()` only linked `type == MESH`, so a file `SkinnedMesh` never got
`_nj_vb`. `m3g_world_draw()` skips any mesh with a null vertex buffer: the model
did not render at all. 59 `SkinnedMesh` objects exist in 5 titles
(`蓝牙真3D格斗_3424.jar` 5 of 7 meshes, `生化危机3D(CAPCOM经典)_3671.jar` 14 of 15,
`杀戮机器-纳粹僵尸_3610.jar` 38, plus one each in `毁灭战士3` and `CS反恐精英`).

Reproduction: `蓝牙真3D格斗_3424.jar` reaches its 3D arena and draws the floor, sky,
mountains and HUD, but both fighters are absent:

    ./build-lead-render/nj2me --port headless --size 240x320 --fps 30 \
      --rms-dir DIR/rms --run-frames 1400 --dump-frames DIR/frames/ --dump-every 10 \
      --keys '150:soft1,300:fire,450:fire,600:fire,750:fire,1000:fire,1100:fire,1200:fire,1300:fire' \
      'tests/games/蓝牙真3D格斗_3424.jar'

`.local-tools/debug-runs/skin-fight-02` is before, `skin-fight-03` after; frame
`00900.ppm` in both. Before: empty arena. After: both fighters stand on the floor.
`skin-fight-03/before-after.png` is the side-by-side, `fighters-zoom.png` the crop.

What the fix does (all in `src/midp/nj_lib_m3g.c`):

- decode case 16, mirroring the Mesh prefix (`0xFFFFFFFF` marker, vertex buffer,
  submesh index-buffer/appearance pairs) and then the skin tail
  `{skeletonRef, transformRefCount, (boneRef, firstVertex, vertexCount, weight)…}`
  with exact-length validation; a payload that only carries the Mesh prefix is
  accepted as bind-pose-only, never as "invisible".
- `nj_m3g_scan_objects()` copies those 16-byte bone records (no VM access in
  `decode`, so they live in the object's `extra` array until linking).
- `nj_m3g_link_meshes()` links vertex/index/appearance data for type 16 as well,
  resolves the skeleton Group (and makes the mesh the skeleton's parent when the
  file left it rootless, so bone→mesh chains resolve), and calls the Java
  `SkinnedMesh.addTransform(bone, weight, firstVertex, vertexCount)` for each
  record — the existing Java class gained the per-bone range/weight arrays and
  the bind transform captured at `addTransform` time
  (`_nj_skin_first/_nj_skin_count/_nj_skin_w/_nj_skin_bind/_nj_skin_n`).
- `m3g_skin_pose()` poses once per mesh per frame: for each bone
  `S = bone.getTransformTo(mesh) × bind`, accumulate `weight × S × vertex` (normal
  with the rotation part only), divide by the sum of weights, and leave unweighted
  vertices in bind pose. `jr_vertex()` consumes the posed arrays when present and
  the quantised buffer otherwise; `m3g_world_draw()` frees them after the last
  submesh. The result is the same skinning definition FreeJ2ME-Plus uses.
- If anything fails to resolve (bad refs, no bones, allocation failure, degenerate
  data) the mesh draws its bind pose exactly as before this change.

## Defect 2 (load-time, found while making defect 1 visible): file `VertexBuffer.defaultColor` was dropped

An untextured, vertex-colour-free surface with no material renders
`default_colour`. File-loaded `VertexBuffer`s never decoded the field (it sits at
payload offset 12, ahead of the positions ref), so such surfaces rendered black
even when the file said `0xFFFFFFFF`. Fixed in the decode case (`out->d`) and the
linker's VB branch (`_nj_defColor`); `jr_setup()` already consumed that field.
This is what made the crafted SkinnedMesh quad grey/black instead of white while
debugging defect 1, and it affects ordinary (non-skinned) unlit geometry too.

## Defect 3 (visible): `TriangleStripArray` encoding 0 was refused — geometry read as one long strip

50 strip arrays in `3D跳台滑雪2011_4507.jar` use encoding 0, which has no index
table: `[Object3D header][encoding 0][u32 firstVertex][u32 stripCount][lengths]`,
with the indices implied as `first, first+1, …`. The loader rejected them, so
`jr_draw()` fell back to "draw the whole vertex buffer as one strip", which joins
unrelated strips into long triangles — the ski jump renders as an explosion of
pink/white shards instead of a ramp.

Reproduction (2,200 frames so the game reaches its 3D jump):

    ./build-lead-render/nj2me --port headless --size 240x320 --fps 30 \
      --rms-dir DIR/rms --run-frames 2200 --dump-frames DIR/frames/ --dump-every 20 \
      --keys '120:soft1,400:fire,600:fire,800:fire,1000:fire,1200:fire,1400:fire,1600:fire,1800:fire,2000:fire' \
      'tests/games/3D跳台滑雪2011_4507.jar'

Before (`.local-tools/debug-runs/baltoro-prefix`, encoding-0 branch disabled for the
A/B): `tris=954103` but the jump is shattered shards. After
(`.local-tools/debug-runs/baltoro-reach3d`): `tris=895833`, coherent ramp, trees,
mountains and the jumper. Side-by-side: `baltoro-reach3d/before-after.png`
(frame `02200.ppm` from each run). Note both totals are large; the difference is
what the strips cover, not how many triangles are submitted.

Fix: decode special-cases `encoding == 0` into the same in-memory form the other
encodings produce (`a=0` marks implicit indices, `b=count`, `c=stripCount`,
`d=firstVertex`), and the linker synthesises `index[k] = firstVertex + k`. Exact
consumption (`21 + 4*stripCount == len`) validates it.

## Defect 4 (decode-only): `Texture2D` general transform was refused

Five map textures in the CS titles carry a general (4×4 matrix) transform; the
decode only accepted the component-transform form, so those textures were refused
and the link step had nothing to apply. Decoding now accepts
`[component flag (+40)][general flag][64-byte row-major matrix][12-byte sampler]`
and stores the matrix into the `Transformable` general slot (`_nj_x`) followed by
`xable_recompose()`, exactly what `setTransform()` does; the sampler already
multiplies by `_nj_m`, so no renderer change was needed. Decode-level only: the
affected CS title was not driven to those maps in a headless run.

## Defect 5 (animation): ALPHA and VISIBILITY tracks were silently ignored

`m3g_animate_object()` filtered every property except TRANSLATION/ORIENTATION/SCALE
in two places (the per-track sampling loop and the blend step), so files animating
fading or visibility did nothing. 35 tracks in the corpus use them
(`孤岛余生2时间之谜汉化版_7371.jar` 17 ALPHA + 17 VISIBILITY). `m3g_anim_apply()`
now writes ALPHA into `_nj_alpha`/`_nj_alphaSet` (clamped 0…1) and VISIBILITY into
`_nj_render`/`_nj_hidden`, and both filters accept 256 and 276. COLOR (258) remains
unimplemented (1 track in one title; no visible evidence obtained).

## Tests

`tests/test_m3g.c` additions (all run by `test_m3g`):

- `test_skinned_mesh` — crafted SkinnedMesh file loads through `Loader.load`; the
  vertex buffer and skeleton link (null before the fix); the one bone owns four
  vertices with normalised weights; the quad renders; moving the bone by +30 x
  moves the lit pixels by >20 px; a second file with no skin tail still renders
  its bind pose.
- `test_triangle_strip_encoding0` — crafted encoding-0 file with two 3-vertex
  strips: the Mesh links, and the rendered area matches the two strips
  (7,000…14,000 px) rather than the joined six-vertex strip (~19,000 px).
- `test_animation` — new sub-block asserts an ALPHA track produces
  `getAlphaFactor() == 0.5` and a VISIBILITY track produces
  `isRenderingEnabled() == false`.

Counts: a clean pre-task-6 `test_m3g` was 693 checks; the suite is now 728 checks,
0 failures. Red/green for the encoding-0 test (same test binary, branch disabled):
`red-green/tsa0-prefix.log` → `FAIL the strips render from their own implicit
indices`, 1 failure; `red-green/tsa0-fixed.log` → 0 failures.

## Verification

- `cmake --build build-lead-render -j6`: clean (only the unrelated `nj_m4v`
  warnings; that file is another task's).
- `ctest --test-dir build-lead-render -R 'm3g|quit'`: 9/9 passed.
- `ctest --test-dir build-lead-render -j6`: 117/118 passed. The only failure is
  `no_statics`, and it reports `src_midp_nj_m4v.c.o` writable globals
  (`nj_m4v_iac_ready`, `nj_m4v_iac_lut`, …) from the concurrently developed M4V
  decoder — none of the files changed here are listed. That gate should be re-run
  when the M4V work lands; it is not a regression from this change.
- `tools/check_m3g_titles.sh "$PWD/build-lead-render/nj2me"`: "titles that did not
  run cleanly: 0"; the deep pass on `浴火银河2完整版_7689.jar` still reaches the
  rasteriser (`tris=44004 m3g=12876`). Log:
  `.local-tools/debug-runs/task6-verify/titles.log`.
- `git diff --stat`: src/midp/nj_lib_m3g.c +897 lines, tests/test_m3g.c +627 lines.

## Limitations / follow-ups

- Animation properties: only TRANSLATION, ORIENTATION, SCALE, ALPHA and VISIBILITY.
  COLOR is not applied. Animation recursion still follows Group children only.
- `Object3D.duplicate()` deep-copies the mesh and its skin arrays but not the
  skeleton/bones; a game that duplicates a skinned mesh and poses the copy would
  pose it against the original bones. No corpus title is known to do this.
- `m3g_skin_pose()` allocates its posed arrays per mesh per frame through the host
  allocator; fine for the corpus's counts (≤ 813 bone records, small vertex sets)
  but not tuned.
- The two decode-only fixes (TSA encoding 0 is visually verified; Texture2D general
  transform is not) deserve a title run when the CS maps are reachable.
- No hardware/F2P frame comparison was done for these scenes; the evidence is
  within-emulator A/B plus the file-format reading validated against the corpus.
