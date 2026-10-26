# Skinned poses are cached on the mesh, not allocated per frame

Date: 2026-10-27
Scope: `src/midp/nj_lib_m3g.c` (`skinned_fields`, `m3g_skin_pose`,
`m3g_world_draw`), `tests/test_m3g.c`
Evidence: `.local-tools/debug-runs/m3g-pose-cache/`; ctest 118/118.

## The cost, measured on the corpus

A `SkinnedMesh` is posed on every frame it renders: `m3g_skin_pose()` turned
the bind vertices through `bone.getTransformTo(mesh) * bind`, then the
rasteriser read the posed positions and normals.  The position array, the
normal array and the weight-sum array were three `nj_host_alloc()` calls per
mesh per frame, freed again at the end of `m3g_world_draw()`.

That is a small number per frame and a large one per second.  An independent
walk of every `.m3g` in the corpus (type 16 -> `VertexBuffer.positions` ->
`VertexArray.vertexCount`) counts the skinned work the collected titles
actually ship:

| title | skinned meshes | vertices | host bytes/frame (min) |
| --- | ---: | ---: | ---: |
| 生化危机3D(CAPCOM经典)_3671 | 14 | 12,525 | 200,400 |
| 蓝牙真3D格斗_3424 | 5 | 5,426 | 86,816 |
| 杀戮机器-纳粹僵尸_3610 | 38 | 3,445 | 55,120 |
| 毁灭战士3_10598 | 1 | 891 | 14,256 |
| CS反恐精英(蓝牙对战)_6858 | 1 | 174 | 2,784 |
| **total** | **59** | **22,461** | **359,376** |

The minimum is position (12 B) + weight (4 B) per vertex; a mesh that ships
normals allocates another 12 B per vertex, so the true figure is up to 28 B
per vertex - 629 KB per frame across the corpus, 18.9 MB/s at 30 fps - plus at
least 118 allocator round trips per frame.  This is churn rather than a leak
(the arrays were freed after the draw), which is why it never showed up as a
"leak"; it is still allocator traffic in the middle of painting, exactly what
the runtime avoids elsewhere.

## What changed

The three arrays became one float array cached on the `SkinnedMesh` object:

* two new fields, `_nj_pose` (`[F`) and `_nj_pose_nv` (`I`), in the class's
  field table - seven floats per vertex: position xyz, normal xyz, accumulated
  weight.  Class metadata is `const` data, so the `no_statics` gate is not
  affected;
* `m3g_skin_pose()` reads the cached array, grows it only if this frame's
  vertex count is larger than the cached capacity (a title may replace a
  vertex buffer with a bigger one), and writes into it in place;
* the caller no longer frees anything, and the pose path makes no
  `nj_host_alloc()` call at all after the first frame for that mesh;
* the array is referenced by the mesh object, so the collector reclaims it
  with the mesh, and a title that stops drawing a model does not keep its
  pose data alive.

Memory retained is bounded by the skinned vertices a title actually renders:
the worst title above is 12,525 vertices -> 350 KB of cached pose arrays, in
exchange for 200 KB of host allocation churn every frame (6 MB/s at 30 fps).
The other four titles retain 11 KB - 243 KB.  Meshes are posed one at a time
and their arrays live as long as the meshes, so the cache also removes the
fragmentation the alloc/free pairs could cause.

## Verification

**The new assertions fail on the old source.**  The test renders the crafted
skinned quad twice, then:

* checks that `_nj_pose` exists on the `SkinnedMesh` and is non-null;
* renders the same scene a third time and requires the host allocation count
  to be **unchanged**, then requires the same array object to be reused and
  the frame to still contain >1000 lit pixels.

The count is not guessed from the source: the test replaces the port's
`alloc`/`free` hooks (the same hooks `nj_host_alloc()` calls) with a counting
`malloc`/`free` pair, so every host block allocated during the render is
counted.  On a worktree at HEAD `b721ea4` with the new test file the run
reports **2 failures** (`the pose buffers are cached on the SkinnedMesh`,
`a repeated skinned render allocates no host memory`); on this tree
`test_m3g` is **762 checks, 0 failures**, and the full suite is **118/118**,
`no_statics` included.  Logs:
`.local-tools/debug-runs/m3g-pose-cache/{head-source,fixed}-test_m3g.log`;
the corpus table above is in `corpus-skin-stats.txt` next to them.

The render assertions stay in the same test as the earlier skinning checks, so
a regression that breaks posing (bad bind matrix, missing bone, bind-pose
fallback) still fails before the caching claim can pass.
