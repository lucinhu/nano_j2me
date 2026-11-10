# SkinnedMesh transform records and Loader roots

JBenchmark HD's Gaming stage threw a null-receiver exception at
`Node.setRenderingEnable` after loading its original M3G scene. An independent
asset-graph inspection found 955 objects and three skins with 482 transform
records apiece. Each skin referenced only 42 distinct bones, with at most three
influences per vertex. A transform record names a bone, a vertex range and a
weight; its total count is not a limit on distinct bones or per-vertex influences.

The loader incorrectly accepted only 1–64 records. Rejecting a longer tail also
dropped its skeleton reference, making the referenced skeleton appear to be a
separate root. The graph inspection predicted additional returned roots; it did
not inspect the real VM's root array. Independent public Loader tests reproduce
the erroneous root and skeleton behavior without using the game's assets.

The [file format's SkinnedMesh section](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/file-format.html)
uses a UInt32 record count and 16-byte records, without that 64-record limit.
The decoder now proves that the entire submesh table fits before reading pairs,
then compares the count with the remaining payload divided by 16 and checks its
remainder. It accepts zero records while keeping the skeleton reference. The
scanner independently checks the normalized object header, submesh table and
exact record extent before allocating precisely the proven byte count. Forged
counts cannot wrap a multiplication into a smaller allocation.

The [SkinnedMesh API](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/SkinnedMesh.html)
also specifies the vertex-range boundary for `addTransform`. The implementation
now validates `firstVertex` and `numVertices` using subtraction against 65535
before changing the roster. It preserves the legal `65534 + 1` case even when
the current VertexBuffer is smaller: the API defers that extent check until
rendering or picking. Bind-array growth and getter output-length accumulation
also reject unrepresentable lengths before multiplying or adding.

There is no game-specific shortcut, capability-property change or general
`Object3D.find` rewrite. Existing parallel raster code is byte-identical. The
broader `getBoneVertices` behavior for overlapping transforms, per-vertex weights
and short output arrays remains outside this repair. Malformed optional tails
retain the existing bind-pose tolerance; this is not a general strict-validation
rewrite of the file loader.

The independent regression constructs M3G files with checksums and calls public
Loader, Group, Mesh, SkinnedMesh and Object3D methods. Its 18 cases cover 0, 1,
64, 65 and 482 records, a variable Object3D header and component transform,
truncated tables and tails, forged UInt32 counts, and rejected range/count/weight
records. Valid cases require one authored World root, the correct skeleton and
bone references, and a successful lookup from the last returned root. Vertex
checks use disjoint ranges with one influence per vertex and retain output-array
tail canaries.

On the real old `2a633ba` archive, compiled with that archive's original headers,
the fixture reports 289 checks with 15 failures. The corrected runtime reports
2798 checks with zero failures. These totals differ because missing old skeletons
prevent subsequent checks, and an unsafe range accepted by old code is not
passed to its getters. Old failures include zero/65/482-record graph linkage and
invalid range acceptance. Independent whole-library strict sanitizer builds on
both merged `27432df` and `6a13581` baselines also pass all 2798 assertions,
including address, undefined-behavior and explicit float-cast-overflow checks.
The final combined runtime also passes the original Minecraft SDL scene and
genuinely resumes both the original `2a633ba` state and a new state. The
[benchmark report](2026-11-10-jbenchmark-baseline.md) records the combined suite
and the original HD's actual results and unsupported subtests separately.
Loading a longer skin roster does not add mipmap or multitexturing support.
