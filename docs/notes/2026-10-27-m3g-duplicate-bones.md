# SkinnedMesh.duplicate() gives the copy its own skeleton and bones

Date: 2026-10-27
Scope: `src/midp/nj_lib_m3g.c` (`m3g_copy_object`, new `m3g_matching_node`),
`tests/test_m3g.c`
Evidence: `.local-tools/debug-runs/m3g-duplicate-bones/`; ctest 118/118.

## The rule, and what the runtime did instead

JSR 184's `duplicate()` copies an object's state and shares its references -
except for `SkinnedMesh`, where the specification spells out that the duplicate
gets **its own skeleton** and its bone records are remapped onto the copied
rig.  The reference implementation (`.local-tools/freej2me-plus`,
`SkinnedMesh.duplicateImpl()`) does exactly that: it duplicates the skeleton
Group, makes the copy its parent, and for every bone record finds the node at
the same place in the copied skeleton (`Node.matchingNode()` walks the child
indices down), keeping the at-rest transform captured by `addTransform()`.

The runtime's generic `m3g_copy_object()` copied arrays and slot vectors but
left `_nj_skeleton` as a shared reference, so a duplicate pointed at the
source's skeleton whose parent was the source mesh.  The copied `_nj_bones`
array held the source's bone nodes.  Two visible consequences, both reproduced
by the new test on the old source:

* `copy.getSkeleton()` returned the source skeleton (and `getChild(0)` the
  source bone);
* the copy rendered its **bind pose** and could not be posed at all: the bone
  is not reachable from the copy's root, so `getTransformTo(bone, copy)` has no
  path and the renderer falls back to bind pose.  Moving the copy's own bone
  changed nothing on screen.

No corpus title is known to duplicate a skinned mesh (the limitation was
recorded in `docs/notes/2026-10-26-m3g-render-defects.md` on the strength of
that), so this fix is correctness-first; the test is the evidence.

## What changed

* `m3g_matching_node(vm, src_root, src_node, dst_root)` implements the
  reference's `matchingNode`: walk up from the bone to the skeleton root
  collecting each node's index among its parent's `_nj_kids`, then walk those
  indices down the copied tree with `Group.getChild()`.  A bone that is not in
  the source skeleton maps to 0; a tree shape that differs also maps to 0.
* After the generic copy, a `SkinnedMesh` gets
  * its skeleton duplicated through the same `m3g_copy_object()` (so the whole
    bone tree, transforms and children are copied) and its `_nj_parent` set to
    the duplicate;
  * every `_nj_bones` entry replaced by the matching node in the copied
    skeleton (the array itself was already a copy, so the source is untouched);
  * a bone that cannot be mapped gets its `_nj_skin_count` entry zeroed, so it
    contributes no vertices instead of silently indexing the old rig;
  * `_nj_pose`/`_nj_pose_nv` cleared, so the duplicate allocates its own cached
    pose on first render rather than inheriting the source's (see
    `docs/notes/2026-10-27-m3g-pose-cache.md`).
* Everything the JSR says to share stays shared: vertex buffer, index buffers,
  appearances.  At-rest transforms are not recomputed (the copied skeleton has
  the same geometry).

## Verification

The new block in `test_m3g`'s skinned-mesh test:

1. duplicates the crafted SkinnedMesh and requires a distinct object that
   still shares its `VertexBuffer`;
2. requires the duplicate's skeleton to be a non-null copy, its child count to
   be 1, and its child bone to be a **different node** from the source bone;
3. calls `getBoneVertices()` with the copied bone and requires the four-vertex
   record (the remap produced a usable bone, not a dangling reference);
4. renders the duplicate in its own World and checks the pose is independent at
   the pixel level: moving the **source** bone to -30 leaves the duplicate's
   frame bit-identical (`lit_d == lit_c`, centroid difference < 1e-3 px), then
   moving the **copy's own** bone to 60 shifts its lit centroid by more than 20
   px.

On a worktree at HEAD `d492dae` with the new test file the run fails exactly
three of these - `its skeleton is a copy, not the source's`, `and the copied
skeleton owns a copied bone`, `moving the duplicate's own bone moves its pose` -
and renders the copy in bind pose; on this tree `test_m3g` is **772 checks, 0
failures** and the full suite **118/118**.  Logs:
`.local-tools/debug-runs/m3g-duplicate-bones/{head-source,fixed}-test_m3g.log`.

## Remaining difference from the reference

`AnimationTrack` objects are still shared between an original and its
duplicate: the generic copy duplicates the `_nj_tracks` vector but adds the
same track references, while the reference implementation deep-copies each
track.  No corpus title is known to duplicate an animated node; if one turns
up, that is the next refinement.
