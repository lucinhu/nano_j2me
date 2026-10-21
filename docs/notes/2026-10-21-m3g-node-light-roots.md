# The node-light list rooted two arrays and never let go

Status: fixed. Latent defect: no collected title takes the path yet, so the
evidence is the regression assertion, not a game.

## The path

`Graphics3D.render(Node, Transform)` on a scene whose `Light`s are nodes (the
title never called `addLight`) makes the renderer build the Graphics3D's own
light list from the graph: `src/midp/nj_lib_m3g.c`, `m3g_render_scene`, the
block guarded by `!slightp && _nj_nlights == 0`. It allocates a `Light[]` and a
parallel `float[16 * n]` matrix array, fills them from
`collect_lights_walk()`, roots both, writes them into `_nj_lights` /
`_nj_lmats`, then writes `_nj_nlights`.

The two `nj_root()` calls had no matching release.

## Why it matters even without a title

* A root that is never released pins the arrays - and every Light they
  reference - for the life of the VM. That is a leak of two ref roots plus
  their arrays per Graphics3D that takes the path.
* Worse, `nj_vm_can_save()` refuses to snapshot while `vm->nroots != 0`
  ("temp root held"). A title that collected its scene lights this way would
  lose `--save-state` entirely, and - until commit `8133cc4` - the runner would
  have written nothing and said nothing about it. That is the same silent
  failure the `Image2D(int, Object)` constructor had, and the same reason it
  was fixed in `46d75fc`.

## The fix

Mark the root stack before the two pushes and release it after the three field
writes (`src/midp/nj_lib_m3g.c`, about eight lines including the comment). The
arrays are still live while the fields are being written - `nj_rput` takes the
reference from the C stack, and the roots make the GC leave the arrays alone
across the `nj_array_set_ref`/`memcpy` loop - so the release cannot move
earlier.

## Evidence

`tests/test_m3g.c` gained a block that constructs a 16x16 target, a
`Graphics3D`, a `Camera`, a `Transform` and a `Group` holding one `Light`,
renders the group with `render(Node, Transform)`, and asserts:

    render(Node) collects the scene's own Light node
    render(Node) with scene lights leaves no ref root behind

On the pre-fix code the second assertion is the only failure in the file:

    FAIL  render(Node) with scene lights leaves no ref root behind
    312 checks, 1 failures

After the fix: `312 checks, 0 failures`.

A root scan of all 26 JARs that reference `javax.microedition/m3g`
(`.local-tools/debug-runs/rootscan-r6/summary.txt`, 240 frames each) stayed at
`start_roots=0 final_roots=0` both before and after this change, so no
collected title currently reaches the block; the assertion is what keeps the
next one from discovering it as a broken snapshot.

## No rendering regression

* `3D拉力赛-进化汉化版` at 176x208, 3650 frames with the recorded key script:
  `tris=143531 m3g=16368`, exit 0 - the same counts as before the change
  (`rally-newtimer-015`).
* Doom 3, 700 frames: `tris=189110 m3g=1348`, exit 0.
* Full ctest on a clean Release build: **87/87** (`/tmp/nj-def`, 59.70 s).

## Limits

The fix is preventive: no collected title was observed failing because of it,
so there is no game screenshot that changes. The unit test is the evidence, and
the note says so rather than implying a title was repaired.
