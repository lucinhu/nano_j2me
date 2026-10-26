# Which M3G features 18 J2ME titles actually use

Task-7 audit, 2026-10-26. Question behind it: which parts of the M3G renderer
are worth finishing next? The runtime had just grown a scene-graph path
(`m3g_render_scene`, `collect_walk`, the immediate-mode mesh collector), and
every review of it listed the same suspects — skinning, morphing, fog, sprites,
second texture unit, generic cameras — without knowing which of them any real
title uses. This note answers that from the files themselves.

The short answer: **the corpus uses almost exactly what the runtime already
does**. Of the object types and properties in the 975 files, everything decodes
and renders except four things: `SkinnedMesh` files (59 objects, 5 titles), one
exporter's triangle-strip variant (one whole title), animated `ALPHA`/
`VISIBILITY`/`COLOR` tracks (35 tracks, 2 titles), and the general texture
transform (5 textures, 1 title). The features the reviews worried about most —
fog, sprites, morphing meshes, multi-texturing, generic cameras, background
images, depth offset, mipmapping — occur **zero times** in the corpus.

## The walk

All 399 JARs in `tests/games/` were searched for `.m3g` entries: 18 JARs hold
975 `.m3g` files (10,499,928 B). Every file was extracted to `/tmp` and passed
through the runtime's own object walker, `nj_m3g_scan_objects()`
(`src/midp/nj_lib_m3g.c:1120`), from a throw-away host probe linked against
`build-lead/libnj2me.a` (no source changes, no `./build` use). The walk reports
35,884 objects across 975 files, 397 unique file contents by md5, and one
per-file reference convention that resolves for every file: `M3G_REF_TWO`
("object number + 2", `src/midp/nj_lib_m3g.c:5107-5230`).

Object counts by type: VertexArray 8,396; Group 4,030; TriangleStripArray
3,689; VertexBuffer 2,965; Mesh 2,906; Appearance 2,347; AnimationTrack 2,092;
KeyframeSequence 2,088; Texture2D 1,585; PolygonMode 1,513; Material 1,317;
Image2D 1,054; AnimationController 676; ExternalReference 475; CompositingMode
428; World 97; Camera 80; SkinnedMesh 59; Light 55; Background 32;
MorphingMesh 0; Sprite 0; Fog 0.

## The corpus

| title (JAR) | .m3g files | objects | meshes | skinned | worlds | anim tracks | textures | lights |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 极品飞车14之热力追踪中文版_4160 | 36 | 3,814 | 372 | 0 | 0 | 207 | 0 | 0 |
| 极品飞车16：亡命狂飙_1519 | 36 | 3,494 | 327 | 0 | 0 | 255 | 0 | 0 |
| 浴火银河2瓦尔基里_3093 | 192 | 3,444 | 257 | 0 | 0 | 170 | 255 | 0 |
| 浴火银河2瓦尔基里(汉化)_3067 | 192 | 3,444 | 257 | 0 | 0 | 170 | 255 | 0 |
| 浴火银河2完整版_7689 | 192 | 3,438 | 257 | 0 | 0 | 170 | 254 | 0 |
| 浴火银河2汉化版_6950 | 192 | 3,438 | 257 | 0 | 0 | 170 | 254 | 0 |
| 3D科林麦克雷拉力赛之尘埃_6600 | 16 | 2,413 | 227 | 0 | 16 | 5 | 40 | 0 |
| 世界冠军拉力赛3D_8360 | 37 | 2,292 | 237 | 0 | 0 | 0 | 237 | 0 |
| 杀戮机器-纳粹僵尸_3610 | 28 | 2,231 | 90 | 38 | 28 | 66 | 92 | 0 |
| 生化危机3D(CAPCOM经典)_3671 | 15 | 2,094 | 1 | 14 | 15 | 590 | 36 | 32 |
| CS反恐精英(蓝牙对战)_6858 | 7 | 1,682 | 229 | 1 | 7 | 14 | 45 | 3 |
| 毁灭战士3_10598 | 3 | 1,453 | 205 | 1 | 3 | 54 | 17 | 2 |
| 3D摩托赛车_9287 | 6 | 662 | 58 | 0 | 6 | 0 | 58 | 6 |
| 蓝牙真3D格斗_3424 | 6 | 638 | 2 | 5 | 6 | 130 | 8 | 0 |
| 孤岛余生2时间之谜汉化版_7371 | 2 | 578 | 65 | 0 | 1 | 91 | 1 | 0 |
| 3D拉力赛-进化汉化版_6366 | 10 | 411 | 23 | 0 | 10 | 0 | 26 | 10 |
| 3D跳台滑雪2011_4507 | 4 | 306 | 36 | 0 | 4 | 0 | 5 | 0 |
| 3D城市桌球_8299 | 1 | 52 | 6 | 0 | 1 | 0 | 2 | 2 |

Two caveats about this table. The four 浴火银河2 builds are four re-packagings
of the same game: 192 files each, and the 975 files collapse to 397 unique
contents, so file/object counts over-weight that title. And seven of the 18
titles contain no `World` object at all (the NFS pair, 世界冠军拉力赛3D and the
four 浴火银河2 builds); they still ship meshes, textures and animation, they
just drive M3G through the API / immediate-mode paths rather than one scene
graph. Both are counted here.

## Ranked: what the corpus asks for, most used first

"Runtime" is this tree: `src/midp/nj_lib_m3g.c` in the current `build-lead`
configuration (M3G on, fast paths on, RGB565).

| # | feature | objects | files | titles | runtime |
| ---: | --- | ---: | ---: | ---: | --- |
| 1 | node transform stored in file (translation/scale/orientation) | 5,955 | 941 | 18 | yes |
| 2 | vertex geometry: positions/normals/texcoords/colors | 8,396 VA, 2,965 VB | 941 | 18 | yes |
| 3 | triangle strips/arrays (8-, 16-, 32-bit indices) | 3,689 | 941 | 18 | yes* |
| 4 | mesh → vertex buffer + submesh appearances | 2,906 | 919 | 18 | yes |
| 5 | textures (LUMINANCE/RGB/RGBA/paletted images) | 1,585 | 900 | 16 | yes |
| 6 | materials (diffuse/ambient/specular/emissive/shininess) | 1,317 | 846 | 13 | yes |
| 7 | transform animation (translation/orientation/scale) | 2,092 tracks, 2,868 attachments | 93 | 13 | yes |
| 8 | keyframe curves (all interps and encodings) | 2,088 | 93 | 13 | yes |
| 9 | polygon mode: culling, shading, winding | 1,513 | 907 | 17 | yes |
| 10 | compositing: depth test/write, blending, alpha test | 428 | 120 | 10 | yes |
| 11 | worlds + cameras (all perspective) | 97 worlds, 80 cameras | 97 | 11 | yes |
| 12 | backgrounds (colour / clear flags) | 32 | 32 | 7 | yes |
| 13 | lights (ambient/directional/omni/spot) | 55 | 35 | 6 | yes |
| 14 | **SkinnedMesh** | 59 | 25 | 5 | **no** |
| 15 | **animated ALPHA / VISIBILITY / COLOR** | 35 | 2 | 2 | **no** |
| 16 | **TSA exporter variant (encoding 0)** | 50 | 4 | 1 | **no** |
| 17 | **Texture2D general transform** | 5 | 3 | 1 | **no** |

\* the three standard index encodings work; the fourth byte value (0) from one
exporter does not — row 16.

Details behind the rows that matter:

* Row 1: 2,023 of 2,906 Meshes and 3,831 of 4,030 Groups carry a transform in
  the file. The decoder's transform heuristic (`data[12]&1`, transform at byte
  13, `src/midp/nj_lib_m3g.c:502-511`) covers it; the remaining Meshes are
  identity.
* Row 2: every VertexArray in the corpus is `componentType == BYTE` (8,396 of
  8,396; quantised, with bias/scale words where needed) and 99% are 1 or 2
  bytes per component. Positions are 3 components; normals 3; texcoords 2;
  vertex colours occur in only 230 arrays (12 files, 2 titles).
* Row 3: encodings are 8-bit (1,836 objects / 124 files), 32-bit (1,432 / 785)
  and 16-bit (371 / 84). Strip counts run 1..379; the old 64-strip ceiling was
  already removed for rally terrain.
* Row 5: image filters are `NEAREST` and level filter `BASE_LEVEL` *always*
  (1,580 of 1,580 valid textures); wrap is `REPEAT` always. LUMINANCE 2x2 UI
  images dominate (1,018 of 1,054 Image2D). Paletted images exist (31, 4
  titles) and are expanded by the loader.
* Row 7: only translation (663), orientation (1,740) and scale (430) are
  animated. 822 tracks are attached to nothing.
* Row 8: interpolation values are LINEAR 600, SLERP 1,053, SPLINE 370, SQUAD
  19, STEP 46; encoding 0: 1,302, encoding 1: 767, encoding 2: 19. Every
  combination the corpus uses has a sampler path (`m3g_kfs_getSample`,
  `src/midp/nj_lib_m3g.c:8360-8440`); SLERP/SQUAD all carry 4 components and
  LINEAR/SPLINE 3, as the format expects.
* Row 9: cull NONE 1,203 / BACK 310 (never FRONT), shading SMOOTH 1,509 /
  FLAT 4, winding CCW 1,512 / CW 1 — all valid.
* Row 10: blend REPLACE 328 / ALPHA_ADD 57 / ALPHA 41 / MODULATE 2; depth test
  428, depth write 407, alpha write 330; alpha test threshold is non-zero in 245
  objects (2 titles). Blend modes 64-68 are implemented in the raster path.
* Row 13: ambient 33 (5 titles), directional 17 (1 title), omni 2 (1),
  spot 3 (2). Spot angle/exponent and attenuation are used and are implemented
  in `jr_lit` (`src/midp/nj_lib_m3g.c:10959-11100`).
* Row 14: see below. MorphingMesh is 0 objects, so skinning is the only
  character-animation gap.
* Row 15: 孤岛余生2 has 17 ALPHA + 17 VISIBILITY tracks (1 file, 1 title);
  蓝牙真3D格斗 has 1 COLOR track. `m3g_animate_object()` decodes the track,
  then skips the property (`src/midp/nj_lib_m3g.c:2485-2493`, comment "a
  property this engine leaves alone").
* Row 16: 3D跳台滑雪2011 — the whole title's geometry. See below.
* Row 17: 5 textures in the three CS map files. See below.

## The four gaps, in order of value

### 1. SkinnedMesh files: 59 meshes in 5 titles

68 % of the corpus by object count does not contain a single one, but the
titles that do cannot draw their characters without it:

| title | skinned meshes | files |
| --- | ---: | ---: |
| 杀戮机器-纳粹僵尸_3610 | 38 | Data/m3g/ENEMY1.m3g 8, ENEMY2.m3g 9, qiang.m3g 21 |
| 生化危机3D_3671 | 14 | 15 files (jill_*, zombie, nemesis, ...) |
| 蓝牙真3D格斗_3424 | 5 | azrael / kexue / leech / sadan / wolf |
| 毁灭战士3_10598 | 1 | rol1/s.m3g |
| CS反恐精英_6858 | 1 | res/role/terrorist.m3g |

All 59 are referenced by a Group and all 59 are reachable from the title's
`World`. The file decoder has no case for type 16: it falls into the default
branch of `nj_m3g_decode_object()` and comes back with `decoded = 0`
(`src/midp/nj_lib_m3g.c:1064-1069`). The loader still creates the Java object
(`m3g_class_of()` :6475, the per-object creation loop :6205) but links nothing
into it (`link_misc()` skips undecoded objects :5510; the mesh link path only
accepts `NJ_M3G_T_MESH` :5423). The Java class and its API natives exist; only
the file → skeleton/weights → index-buffer path is missing, plus the renderer's
per-vertex skinning.

### 2. One exporter's triangle-strip variant: 3D跳台滑雪2011

The title has 36 meshes and 102 vertex arrays and is the only title whose
geometry does not come from any of the three decodable TSA layouts. Its 50
TriangleStripArray objects — jumper.m3g 8, rozbieg.m3g 3, visibleGeometry.m3g
36, zeskok.m3g 3 — start their payload with `enc = 0`, `indexCount = 0`, then
`stripCount` and `stripCount` lengths. The lengths are present and sum to the
real index count, but `indexCount` is 0 and the sums disagree with 0 by
construction; the object lengths still consume exactly (`jumper` 309 =
17 + 4 + 4×72). The four files are the only ones in the corpus whose section 0
contains the exporter banner `M3GExport Standard v1.5.0 Licensed to Baltoro
Media`; the other 971 files use encodings 128/129/130 only. The runtime rejects
the layout and `m3g_link_geometry()` builds empty index buffers, so the title
has no visible 3D geometry. Needs reverse-engineering of that exporter's
variant before a fix; one title (4 files) is the stake.

### 3. Animated ALPHA / VISIBILITY / COLOR: 35 tracks in 2 titles

孤岛余生2 animates 17 nodes' alpha and 17 nodes' rendering-enable flag;
蓝牙真3D格斗 animates one colour (the one Background-targeted track in the
corpus). The decode path already builds these AnimationTrack objects and the
renderer already consumes both effects at runtime: node alpha is applied to the
ancestor chain (`src/midp/nj_lib_m3g.c:12228-12229`) and `_nj_hidden` is
checked by the scene walk (:3839-3847, set by `setRenderingEnable` :2852).
Only the whitelist in `m3g_animate_object()` (:2485-2493) needs to route
256/276 (and the Background colour 258) to those fields. This is the cheapest
of the four gaps and touches two titles.

### 4. Texture2D general transform: 5 textures in 3 CS map files

Five `Texture2D` objects (all 90 bytes) in
`res/map/construction/construction.m3g` (idx 15),
`res/map/depository/depository.m3g` (idx 16) and
`res/map/military/military.m3g` (idx 30/36/38) carry a 64-byte 4x4 general
transform between the two transform-present flag bytes and the normal 12-byte
sampler payload. The decoder's Texture2D case
(`src/midp/nj_lib_m3g.c:1035-1052`) only accounts for the node/component
transform plus two flag bytes and rejects these on length. Effect: those map
textures stay untextured in one title. Parsing the matrix (and applying it to
texture coordinates, or ignoring it while still loading the texture) is small.

## What the runtime already matches, so don't rebuild it

Beyond the table rows: node transforms including the "transform begins at byte
13" quirk; unaligned component transforms; all four light modes including spot
cone (`cos(spotAngle/2)` limit, `pow(cos, spotExponent)`) and the
`1/(att0+att1·d+att2·d²)` attenuation; material lighting with specular,
emissive and vertex-colour tracking; per-vertex fog (EXPONENTIAL and LINEAR)
and sprite fog; background colour/clear flags and image modes; blend modes
64-68, alpha test, depth test/write, colour/alpha writes; texture
NEAREST/BASE_LEVEL sampling, REPEAT wrapping, mip chains; LUMINANCE/RGB/RGBA and
paletted Image2D; all five keyframe interpolations and all three encodings;
`ExternalReference` textures (475 objects, 11 titles) resolved by
`m3g_external_image()` (:6080); animated transform nodes through
`AnimationController` active intervals, speed and weight.

## What the corpus never uses

Zero objects, so these can wait unless a specific title needs them from the API
(these counts are file data only — a title can still call
`setBlending()`/`setPolygonMode()` from code, which this audit does not see):

* Fog: 0 objects (the renderer supports it; nothing ships it).
* MorphingMesh: 0; Sprite / Sprite3D: 0.
* Second texture unit: 0 Appearances with 2 units (2,116 have one, 231 have
  none) — multi-texturing is not needed by the corpus.
* Generic 4x4 cameras: 0 (80/80 cameras are perspective).
* Background images: 0 (32 backgrounds have image modes set, all with a null
  image ref); only colour + clear flags are used.
* CompositingMode depth offset: 0 (328 REPLACE, 57 ALPHA_ADD, 41 ALPHA, 2
  MODULATE; no depth-offset values).
* Texture filtering other than NEAREST (FILTER_LINEAR 0), level filter beyond
  base level 0, wrap CLAMP 0.
* Cull FRONT 0 (one CW winding object in the corpus, cull BACK 310, NONE
  1,203).
* Blend MODULATE_X2 0; texture functions ADD/BLEND/DECAL/REPLACE 0 (MODULATE
  1,337, REPLACE 243).
* Vertex-colour tracking animation 0 (the flag is used as data on 0 objects).
* Animated properties other than the ones listed above: 0.

## Top three to fix first

1. **SkinnedMesh file support.** 5 of 18 titles, 59 meshes, all reachable
   from the scene graph, and no decode path at all today. Characters in
   杀戮机器-纳粹僵尸-生化危机3D-蓝牙真3D格斗 are the corpus's most visible
   missing thing. MorphingMesh is unused, so this does not have to become a
   general skinning/morphing subsystem.
2. **The Baltoro-exported TSA variant** (encoding 0). One whole title currently
   has zero drawable geometry; the layout is nearly understood (strips and
   lengths present, index count zeroed) and affects only 4 files.
3. **Animated ALPHA / VISIBILITY / COLOR.** 35 tracks in 2 titles, and both
   target fields already exist and are already read by the renderer — the
   change is in the animation whitelist, not in the renderer. Cheapest
   user-visible win in the set.

The texture general transform (5 textures, 1 title) is the natural next small
item after these. The five matrices are scale+translate (15×, 2×, 18×,
diag(1,4)), so they are worth applying rather than dropping — and the sampler
already applies a texture matrix, so the work is decode + loader only; see the
odd-payloads note linked from "Still open".

## Evidence

`.local-tools/debug-runs/m3g-corpus-audit/`:

* `scan.tsv` — 4,003,834 B; `F` line per file (`file bytes sections objects
  decoded bad status`), `O` line per object (type, decoded, user id, length,
  animation refs, the decoded fields and refs).
* `scripts/m3gscan.c` — read-only host probe over `build-lead/libnj2me.a`
  (compile line in `README.md`; needs `-DNJ_USE_LIBC=1` because
  `include/nj2me/nj_config.h` includes `nj_libc.h` before defining the switch).
* `scripts/analyze2.py` (reference-mode inference, reference resolution,
  feature table; `results/analyze2.out`, `results/stats2.json` — 518 feature
  keys) and `scripts/analyze3.py` (per-title views; `results/analyze3.out`).
* `README.md` — what was run, the extraction pitfall (Galaxy on Fire 2 entry
  names contain `[ ]`, so `unzip -p` in a loop silently wrote 124 zero-byte
  files; `unzip -o '*.m3g'` per JAR fixed it), and caveats.

The 975 files and 18 JAR list are `/tmp/m3gaudit/jars/` and
`results/m3g-list.txt` (regenerated, not part of the repo). The repo itself was
not modified by the audit except for this note and the evidence directory.

## Still open

* The Baltoro TSA layout is now decoded — see
  [the odd-payloads note](2026-10-26-m3g-odd-payloads.md): encoding 0 stores a
  base vertex index and a strip-count/length table, with implicit identity
  indices between them, so the title can be loaded by a decode+loader change.
  That note also decodes the five 90-byte CS `Texture2D` general transforms
  (scale+translate) and gives the smallest implementation for both.
* The corpus contains 2 Meshes with zero submeshes in
  `3D拉力赛/res/tr2/tr2.m3g` (objects idx 77 and 152); they are empty objects,
  harmless but the title shape suggests exported placeholders.
* VertexBuffer bias/scale (quantisation) was not measured: the float words
  live in ref slots 8..15 that the O line does not print; source-level support
  exists (`src/midp/nj_lib_m3g.c:5994-6018` loader, :4102-4108 renderer).
* This is a file-level audit, not a render check. The titles that use the
  decoded-but-dropped animation properties were identified from files; whether
  the missing animation is the only reason a title looks wrong is not claimed.
* Runtime API calls that set features at run time (e.g. a title creating a
  fog object in code) are invisible here; the "never uses" section is about the
  shipped .m3g files only.

Both odd payloads are decoded in
[Two odd M3G payloads](2026-10-26-m3g-odd-payloads.md): byte tables, the
arithmetic that proves the Baltoro encoding-0 words are a base vertex plus a
strip-length table (7,084 implicit indices over the four files), the five CS
texture general transforms (scale+translate, not identity, and the sampler
already has a matrix path), and the concrete decode+loader sketch
render-diagnosis can apply. Line numbers there are from the working tree at
audit time; function names are the stable anchors.
