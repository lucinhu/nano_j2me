# Two odd M3G payloads: a Baltoro triangle-strip variant and a texture matrix

Task-8 note, 2026-10-26. Follow-up to
[the corpus audit](2026-10-26-m3g-corpus-features.md). That walk found two
payloads the runtime rejects; this note parses the bytes and names the smallest
implementation. Both turn out to be file-format questions, not renderer
questions: the renderer already applies a `Texture2D` matrix, and the
triangle-strip variant turns out to carry an implicit identity index buffer.
Only the decoder and the object loader need to learn the layouts.

Everything below was read from the files themselves: the four
`3D跳台滑雪2011` `.m3g` files and the three CS map files were extracted to
`/tmp`, then parsed by a throw-away Python reader that mirrors the object walk
in `src/midp/nj_lib_m3g.c`. No source file was changed and `./build` was not
used. Evidence is in `.local-tools/debug-runs/m3g-odd-payloads/`.

The short answer:

* **(a) Baltoro TriangleStripArray.** Encoding byte `0` is not a JSR index
  width: the payload is `[u8 0][u32 baseVertex][u32 stripCount][u32 lengths…]`,
  length `21 + 4*stripCount`. `baseVertex` is the cumulative vertex count of
  the mesh's earlier submeshes and the indices are implicit:
  `base, base+1, …, base+sum(lengths)-1`. All 50 objects verify (7,084 indices
  over four files, every Mesh's strips sum exactly to its positions array).
* **(b) Texture2D general transform.** The 64-byte matrix sits after the
  general-transform-present flag and before the normal 12-byte sampler
  payload. These five matrices are *not* identity: they are 2D scale+translate
  (15×, 2×, 18×, diag(1,4)), i.e. tiling. The sampler path already applies a
  texture matrix; the fix is to decode the matrix into the object loader's
  `_nj_x` slot and let the existing `T*R*S*M` composition run. Apply it rather
  than ignore it.

## (a) The Baltoro triangle-strip variant

### Where it ships

`3D跳台滑雪2011_4507.jar` is the only title among 18 in the corpus whose
triangle-strip arrays use encoding `0` — 50 objects across all four of its
`.m3g` files (the whole title):

| file | objects | TriangleStripArrays | total indices |
| --- | ---: | ---: | ---: |
| `jumper.m3g` | 63 | 8 | 3,184 |
| `rozbieg.m3g` | 24 | 3 | 72 |
| `visibleGeometry.m3g` | 195 | 36 | 3,760 |
| `zeskok.m3g` | 24 | 3 | 68 |
| **four files** | **306** | **50** | **7,084** |

The file-header sections of these four files carry the exporter string
`M3GExport Standard v1.5.0 Licensed to Baltoro Media …`; the other 971 corpus
files use only the JSR encodings (8-bit 1,836, 32-bit 1,432, 16-bit 371).

The three files that carry geometry store their object sections
zlib-compressed (scheme `1`); the container parser has to inflate them before
the object walk, which is why a raw byte scan misses them.

### The layout

```
offset 0   Object3D header       12 bytes (userID, track refs, parameters; all zero here)
offset 12  u8  encoding = 0
offset 13  u32 baseVertex        first vertex index this submesh uses
offset 17  u32 stripCount
offset 21  u32 length[stripCount]
           end = 21 + 4*stripCount == object length
```

Worked examples, all 50 objects consume their payload exactly:

| file / object | length | baseVertex | strips | Σ lengths | arithmetic |
| --- | ---: | ---: | ---: | ---: | --- |
| `jumper.m3g` idx 3 | 309 | 0 | 72 | 394 | 309 = 21 + 4·72 |
| `visibleGeometry.m3g` idx 31 | 545 | 0 | 131 | 606 | 545 = 21 + 4·131 |
| `visibleGeometry.m3g` idx 32 | 245 | 606 | 56 | 282 | 245 = 21 + 4·56 |
| `zeskok.m3g` idx 2 | 41 | 0 | 5 | 20 | 41 = 21 + 4·5, lengths 4 4 4 4 4 |
| `rozbieg.m3g` idx 11 | 33 | 0 | 3 | 24 | 33 = 21 + 4·3, lengths 5 4 15 |

The first bytes of `jumper.m3g` object 3 (little-endian):

```
0000  00 00 00 00 00 00 00 00 00 00 00 00   Object3D header: userID 0, no tracks
000c  00                                 encoding 0
000d  00 00 00 00                        baseVertex = 0
0011  48 00 00 00                        stripCount = 72
0015  06 00 00 00                        length[0] = 6
      ...
```

### Why byte 13 is a base vertex, not an index count

Three independent relations hold for all 50 objects and not for any
alternative reading:

1. **Exact consumption.** Every object satisfies `len == 21 + 4*stripCount`
   (`jumper` 309 = 21+288, `zeskok` 41 = 21+20, …). If the u32 at 17 were an
   index count, the words that follow would have to be indices and the length
   would scale with it; instead the same relation holds for arrays with 5 and
   8 strips as for one with 131.
2. **Cumulative base within a Mesh.** The u32 at 13 equals the sum of the
   triangle-index counts of the *earlier submeshes of the same Mesh*:
   `visibleGeometry.m3g` Mesh 40 has submesh 0 = TSA 31 (base 0, 606 indices)
   and submesh 1 = TSA 32 (base **606**, 282 indices), and its positions
   VertexArray declares 888 vertices = 606 + 282. Mesh 49: 764 + 8 = 772 =
   positions count. Three-submesh meshes: bases 0/48/98, totals 48/50/20 =
   positions count 118.
3. **Strips cover the vertex range exactly once.** For every Mesh,
   `Σ(strip lengths) == the position array's vertex count`; the largest count
   in the four files is 888, so the largest index is 887 and every index fits
   16 bits. Total across the title: 7,084 indices.

If the implicit sequence is wrong the numbers do not close: no other
assignment of the words at 13/17/21+ explains both the object lengths and the
positions counts.

This is therefore the exporter's "no index array" extension to JSR 184
§11.27: the standard encodings 128/129/130 store explicit indices; encoding 0
stores only *where* the indices start (the submesh's slice of the mesh's
vertex array) and how to split that slice into triangle strips. Every strip
length is at least 3.

### Minimal implementation

Decode, in the `TriangleStripArray` case (`src/midp/nj_lib_m3g.c:841`, next to
the existing `isz != 0` branch):

```c
else if (enc == 0u && len >= 21u) {
    nj_u4 base   = le32(data + 13u);
    nj_u4 nstrip = le32(data + 17u);
    if (nstrip >= 1u && 21u + 4u * nstrip == len) {
        nj_u4 k, total = 0u;
        for (k = 0u; k < nstrip; k++) {
            total += le32(data + 21u + 4u * k);
        }
        out->a = 0u;            /* marker: indices are implicit */
        out->b = total;         /* index count */
        out->c = nstrip;
        out->d = base;          /* first index */
        out->data = data + 17u; /* non-NULL so the loader keeps this object */
        out->data_len = len - 17u;
        out->ref_off = 17u;     /* lengths are 4*(k+1) bytes into this view */
        out->decoded = 1u;
    }
}
```

`fields.a = 0` is today's "no encoding" value, and the loader's index copy
must branch on it. In `m3g_link_geometry()` (`src/midp/nj_lib_m3g.c:6181`,
the `TRIANGLE_STRIP_ARRAY` branch), replace the index fill with:

```c
if (ob->fields.a == 0u) {
    nj_s4 base = (nj_s4)ob->fields.d;
    for (k = 0u; k < n; k++) ip[k] = base + (nj_s4)k;
} else {
    /* existing per-encoding copy (isz 4/2/1) */
}
```

The strip-length loop below it needs no change: it reads
`fields.data + (fields.ref_off - 17) + 4 + 4*k`, and with
`data = payload+17`, `ref_off = 17` that lands on `payload+21+4*k`, the first
length. The synthesized array is `[I` regardless of index width; the runtime's
`IndexBuffer` stores integer indices, so no width decision is needed and a
future title with more than 255 vertices works as well. About 25 lines of
decode and 8 of loader.

Because the decode's exact-consumption check guards the branch, a future file
that uses encoding 0 for something else is still refused rather than
misread.

## (b) The Texture2D general transform

### Where it ships

Five `Texture2D` objects, all exactly 90 payload bytes, in the three CS map
files:

| file | object idx | userID | sampler image ref |
| --- | ---: | ---: | ---: |
| `res/map/construction/construction.m3g` | 15 | 60 | 3 |
| `res/map/depository/depository.m3g` | 16 | 48 | 2 |
| `res/map/military/military.m3g` | 30 | 61 | 2 |
| `res/map/military/military.m3g` | 36 | 67 | 8 |
| `res/map/military/military.m3g` | 38 | 69 | 10 |

Corpus-wide the payload-length histogram is `26`: 1,421 textures (no
transform), `66`: 159 (component transform only), `90`: 5 (general transform
only). No file combines both, so the five are the only general-transform
payloads in the corpus.

### The layout

```
offset 0   Object3D header                   12 bytes
offset 12  u8 component-transform-present flag
offset 13  [40-byte component transform]     only if the flag at 12 is 1
             10 floats: translation xyz, scale xyz, angle degrees, axis xyz
offset g   u8 general-transform-present flag; g = 13 or 53
offset g+1 [64-byte general transform]        only if the flag is 1
             4x4 row-major floats, translation in column 3
offset s   12-byte sampler payload; s = 14 + (comp?40) + (gen?64)
             u32 image ref, 3-byte blendColor RGB, blend function,
             wrapS, wrapT, levelFilter, imageFilter
length = s + 12
```

For the five CS objects there is no component transform, so the layout
collapses to `[12 header][byte12=00][byte13=01][64-byte matrix @14][sampler @78]`.
Hex of `construction.m3g` object 15:

```
0000  3c 00 00 00 00 00 00 00 00 00 00 00   Object3D header, userID 60
000c  00                                    component-transform flag = 0
000d  01                                    general-transform flag = 1
000e  00 00 70 41  =>  15.0                 matrix row 0: 15  0  0  -7
0012  00 00 00 00
0016  00 00 00 00
001a  00 00 e0 c0  =>  -7.0
001e  00 00 00 00  =>   0.0                 matrix row 1:  0 15  0  -7
0022  00 00 00 00
0026  00 00 00 00
002a  00 00 e0 c0  =>  -7.0
002e  00 00 00 00  =>   0.0                 rows 2/3: identity
0032  00 00 00 00 00 00 00 00 00 00 80 3f  (1.0 at diagonal entry 2)
003e  00 00 00 00 00 00 00 00 00 00 80 3f   (1.0 at diagonal entry 3)
004e  03 00 00 00  image ref 3
0052  00 00 00     blendColor RGB = 0
0055  e3           blend function 227 = MODULATE
0056  f1           wrapS 241 = REPEAT
0057  f1           wrapT 241 = REPEAT
0058  d0           level filter 208 = BASE_LEVEL
0059  d2           image filter 210 = NEAREST
```

### The matrices are scale + translate (tiling), not identity

| file / object | u row | v row | 2D affine |
| --- | --- | --- | --- |
| construction idx 15 | 15, 0, -7 | 0, 15, -7 | u' = 15u − 7, v' = 15v − 7 |
| depository idx 16 | 2, 0, −0.5 | 0, 2, −0.5 | u' = 2u − 0.5, v' = 2v − 0.5 |
| military idx 30 | 18, 0, −8.5 | 0, 18, −8.5 | u' = 18u − 8.5, v' = 18v − 8.5 |
| military idx 36 | 1, 0, 0 | 0, 4, −1.5 | u' = u, v' = 4v − 1.5 |
| military idx 38 | 2, 0, −0.5 | 0, 2, −0.5 | u' = 2u − 0.5, v' = 2v − 0.5 |

The third row/column of each 4×4 is identity and there is no rotation, but
these are definitely not identity: they scale the texture coordinates by
2–18× and centre the result with a negative offset. With the wrap mode the
same objects ask for (`REPEAT`), that is how a repeating pattern is tiled
across a big surface; a texture drawn with these ignored would be stretched
once over the whole surface instead of repeating.

### The resolver is already there — apply, don't ignore

The runtime already samples with a 2D affine texture matrix:

* the raster setup reads the texture's composite matrix:
  `r.tmat = mat_of(vm, tex, NJ_C_XABLE, "_nj_m")`
  (`src/midp/nj_lib_m3g.c:12311`);
* per-vertex sampling applies exactly the six entries above:
  `st[0] = tmat[0]*u + tmat[1]*v + tmat[3]`,
  `st[1] = tmat[4]*u + tmat[5]*v + tmat[7]`
  (`src/midp/nj_lib_m3g.c:11336-11337`, field declared at :11140);
* `Texture2D` is a `Transformable` in this runtime's class table
  (`{ NJ_C_TEX2D, NJ_C_XABLE, … }`, `src/midp/nj_lib_m3g.c:13423`), so the
  matrix slots `_nj_t/_nj_r/_nj_s/_nj_x/_nj_m` exist on it;
* the composite is `T * R * S * M` (`xable_recompose()`,
  `src/midp/nj_lib_m3g.c:1767-1782`, using `mat_mul()` :1574).

That is also why the *component* transforms the corpus does use already work,
even though the decoder ignores them: the decode heuristic stashes the 40-byte
record (`data[12]&1` → `data+13`, 40 bytes, :502-529) and the object-creation
loop applies it through `xable_set_file_xform()` whenever `data_len == 40`
(`src/midp/nj_lib_m3g.c:6388-6403`, helper :1802, record parser
`m3g_node_components()` :277). Of the 159 component-transform textures, 67 are
non-identity — translations such as (0,1,0) in `蓝牙真3D格斗`, and scales such
as (6,1,1), (1,10,1), (1,20,1) plus small y offsets in `3D摩托赛车`'s three
files, and five scale values in `3D拉力赛`'s files — and they are already
composed. The only missing piece is the general matrix, which the decoder
rejects on length before any of that runs.

### Minimal implementation

Decode, in the `Texture2D` case (`src/midp/nj_lib_m3g.c:1102`):

```c
nj_u4 xf   = m3g_xform_data(data, len);  /* 12 or 52, :150-158 */
nj_u4 base = xf + 2u;                   /* past the transform flags */
nj_u4 gen  = 0u;

if (len > xf + 1u && (data[xf + 1u] & 0x01u)) {   /* general-transform flag */
    gen = 64u;
    base += 64u;
}
if (len != base + 12u) {
    out->decoded = 0u;
    return NJ_OK;
}
/* ... existing sampler field reads at base ... */
if (gen != 0u) {
    out->data = data + xf + 2u;   /* 4x4 matrix, row-major */
    out->data_len = gen;
}
```

(`out->data_len` is 40 for component-transform textures, so the existing
object-creation branch leaves them alone; 64 marks the general matrix.)

Load, in the `Texture2D` case of `m3g_link_misc()` (`src/midp/nj_lib_m3g.c:5911`),
after the existing sampler settings:

```c
if (ob->fields.data && ob->fields.data_len == 64u) {
    float *x = xable_part(vm, o, "_nj_x");
    nj_u4 k;
    if (x) {
        for (k = 0u; k < 16u; k++) {
            x[k] = bits2f((nj_s4)le32(ob->fields.data + 4u * k));
        }
        xable_recompose(vm, o);
    }
}
```

`xable_part()` returns the `_nj_x` slot (identity if the object has none yet),
`bits2f()` is the file-float reader at :251, and `xable_recompose()` composes
`T*R*S*M`; the component arrays are the identity here, so the result is the
file matrix. The `r.tmat` read needs no change. About 10 lines of decode and
10 of loader.

If only the decoder change is taken (accept the object, skip the matrix), the
five textures load and draw, but scaled 1× instead of 15/18/2×: visibly one
big stretched map instead of tiling. Since the sampler path already exists,
applying the matrix is the cheaper correct option.

## Evidence

`.local-tools/debug-runs/m3g-odd-payloads/`:

* `raw/ski-tables.txt` — all 50 Baltoro TSAs with base/stripCount/total and
  every Mesh's cumulative check; ends with the 7,084-index total.
* `raw/ski-payload-hex.txt` — first 48 payload bytes of representative TSAs
  (`enc=0`, base u32 @13, stripCount @17, lengths @21).
* `raw/cs-textures.txt` — the five 90-byte CS textures: hex, flags, 4×4
  matrix, 2D affine form, sampler bytes.
* `raw/corpus-histograms.txt` — TSA encodings and Texture2D length histograms
  over all 975 files, plus the component-transform identity split.
* `raw/odd.out`, `raw/corpus_odd.out` — exploratory dumps (Mesh →
  IndexBuffer → VertexArray links, per-file histograms).
* `scripts/parse.py`, `verify.py`, `analyze_odd.py`, `corpus_odd.py`,
  `note_data.py` — the read-only parser and checks; `README.md` explains how
  to reproduce.

Inputs are under `/tmp/m3gaudit/jars/` (regenerated, not part of the repo).
Line numbers are from the working tree at audit time; the concurrent
MPEG-4/Part-2 session is editing `src/midp/nj_lib_m3g.c`, so function names
and comments are the stable anchors.

## Still open

* The Baltoro layout is decoded on paper only; no render check was done (the
  emulator is being edited concurrently). Once implemented, the four ski
  files should turn from empty to the title's visible geometry.
* A texture that carries *both* a component and a general transform would be
  118 bytes (40 + 64). No corpus file has one; the decoder formula above
  accepts it, but the loader would need to apply the component transform first
  and the matrix second, and the current `data_len == 40` object-creation
  branch is not enough for that case.
* General-transform matrices with rotation or a non-identity third row are not
  in the corpus; they would still compose correctly through `T*R*S*M`, but
  they have not been observed.
* The encoder's choice of encoding 0 as an implicit-index marker is only
  proven for these four files; the exact-consumption check keeps unknown
  users of encoding 0 refused rather than misread.
