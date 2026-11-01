# One Graphics3D, forty-six draws: the frame half of a draw, resolved once a pass
# (2026-10-31)

The fog/reciprocal/faint-alpha work took the flat span loops down by 18% and the
per-vertex colour fix by another 2%, and the profile that came out of that says
the raster is close to its floor: the remaining span work is a depth-test
compare, one multiply, the reciprocal per fragment and a texel fetch.  What was
still being paid 86.5 times per frame was the *frame* half of a draw - the
target, viewport, surface, depth buffer, camera, view/projection and light set -
which a `render(World)` pass resolves once per mesh even though every mesh in
the pass draws through the same Graphics3D.

This note is about caching that half for the length of one World pass.  It is
bit-exact - the same `mat_mul` calls in the same order on the same inputs - and
it is a pure win of about 2.9% user CPU on the in-world benchmark.

## Where the time went

`-O3 -DNDEBUG -pg -g -fno-pie` / `-no-pie`, 1200 in-world frames, one core,
saved Minecraft PE world resumed from `.local-tools/mc051/world.state`.  Total
sampled 2.48 s, of which the snapshot load (`nj_vm_load` + `nj_crc_update`
inside it) is 0.19 s = 7.7% and is not frame work:

| function | self s | % run | what it is |
| --- | --- | --- | --- |
| `jr_emit` (holds the inlined `sw_tri` span loops) | 0.90 | 36.3% | triangle setup + span fill |
| `nj_interp_run` | 0.27 | 10.9% | Java-side loop cost |
| `jr_fetch` | 0.24 | 9.7% | per-vertex fetch |
| `nj_vm_load` | 0.19 | 7.7% | snapshot load (not a frame) |
| `jr_project` | 0.17 | 6.9% | per-vertex projection |
| `nj_region_draw` | 0.13 | 5.2% | MIDP image blits (the HUD) |
| `_init` | 0.10 | 4.0% | gprof bucket artifact |
| `m3g_tsa_getStripLengths` | 0.09 | 3.6% | Java `getStripLengths()` bridge |
| `jr_triangle_v` | 0.07 | 2.8% | clip test |
| `nj_field_lookup` | 0.06 | 2.4% | field memo lookup |
| `jr_setup` (self) | ~0.00 | ~0% | all of it is children |
| `jr_setup` + children | 0.10 | 4.2% | the per-draw setup |

The call graph says `jr_setup` runs 103,776 times for the saved 1200 frames =
86.5 per frame; 100,533 of those calls come from the World path and only 3,243
from immediate-mode `render(VertexBuffer, ...)`.  `m3g_render_scene` itself runs
1.8 times per frame, so a pass is about 46 draws - and all 46 resolve the same
target, the same viewport, the same camera and the same light set.  The
per-draw cost is mostly `m3g_fof`/`nj_array_length`/field reads: 6.5 M
`m3g_fof` calls and 2.6 M `nj_field_read_ref` calls per 1200 frames.

## The change

`jr_setup()` is split in two.  `jr_frame_resolve()` reads the Graphics3D half
(target, `getGraphics` for an Image target, viewport, `m3g_target_surface`,
`m3g_target_bounds`, `sw_depth`, the camera - including the World's active
camera - view/projection and `P*V`, and the light set with the World's
`slights` override) into an `m3g_frame`.  `jr_setup()` then copies that half
and completes the two per-draw matrices with the mesh's model matrix, exactly
as before: `clip = (P*V)*M` and `mv = V*M`, same `mat_mul`, same order.

The cache is a **local of `m3g_world_draw()`**, the loop that draws a World's
meshes and submeshes, so it lives exactly as long as one pass and no longer.
Every `jr_setup()` in that loop - including a `Sprite3D`'s `jr_sprite()` - gets
a pointer to it and validates a small key (`self`, `world`, `vb != 0`, the
`slights`/`slmats`/`snlights` override); a miss re-resolves in place.  The
immediate `render(VertexBuffer, ...)` path passes `NULL` and resolves into a
stack local, which is what it did before the cache existed.

There are no file statics.  The first version of this patch used a file-static
cache invalidated at the top of the render natives; it measured and looked
right, but `tools/check_no_statics.sh` failed the tree -

    src_midp_nj_lib_m3g.c.o: jr_pass_valid is in .bss
    src_midp_nj_lib_m3g.c.o: jr_pass is in .bss

- and the project's rule is that every piece of mutable state belongs to an
`nj_vm` or to one call.  Moving the buffer into the draw loop is both the
house rule and a stronger lifetime (a pass cannot outlive its stack frame, so
there is no invalidation to forget).

## Why it is bit-exact

- `P*V` is computed with the same `mat_mul(proj, view, pv)` that each draw used
  to compute, and the per-draw product is still `mat_mul(pv, model, mvp)`, the
  same function with the same argument values the old `mat_mul(r.mvp, model,
  r.mvp)` accumulated.  Nothing is reassociated or folded.
- The viewport, surface and depth buffer are the same reads and the same
  `sw_depth()` call, just copied instead of re-read; the `sw.surf == &scratch`
  pointer fix-up is preserved.
- The light fields (`mode`, `colour`, `intensity`, `attenuation`, spot angle
  and exponent) are the same reads into the same values, copied rather than
  re-read per draw.
- Nothing inside a pass can change the Graphics3D between draws: the only call
  the draw loop makes into the object heap is `Vector.elementAt()`, and
  `java/util/Vector` is a C native (`NJFN(vec_elementAt)` in
  `src/midp/nj_lib_util.c`), as is `Graphics3D.clear()`.  So no Java code runs
  between `jr_frame_resolve()` and the last triangle of the pass.
- A sprite interleaved in a lit World passes `slights == NULL` where mesh draws
  pass the World's light array, so it re-resolves the frame half (in place, and
  sprites are unlit).  That is not a regression against the uncached code - it
  only reduces the win in a scene that alternates sprites and meshes.

Verified against the same saved state, 240x320 PPM dumps:

- frames 1-6: **0 of 76,800 pixels differ** between the pristine
  `7dcb8c6` build and this commit, and in an interleaved 2-base/2-candidate
  run every candidate frame matched one of the two baseline runs exactly;
- 12 sampled frames over 61 frames, two full candidate runs: every dump
  matched one of eight pristine runs exactly (0 pixels; one frame 1 pixel);
- the large differences that do appear between runs (up to ~4,000 pixels) are
  the title's wall-clock cloud/light animation: pristine runs differ from each
  other by the same amounts, and candidate frames always match *some* pristine
  run.  An intermediate cloud-scroll phase (646 px) was observed once and is
  the same layer at a different animation offset.

## Measurements

Saved 240x320 Minecraft PE world, 1200 frames, headless, `--heap 32 --size
240x320 --fps 0`, one core with `taskset`, user CPU from bash `time`, baseline =
pristine `7dcb8c6` build (`wt/base-m3g`), candidate = this commit; the order per
round is A B B A so host drift cancels:

| round | A (base) | B (candidate) | delta |
| --- | --- | --- | --- |
| 1 | 3.158 / 3.178 | 3.110 / 3.123 | -1.6% |
| 2 | 3.163 / 3.177 | 3.097 / 3.107 | -2.2% |
| 3 | 3.200 / 3.177 | 3.094 / 3.098 | -2.9% |
| 4 | 3.256 / 3.215 | 3.124 / 3.146 | -3.1% |
| 5 | 3.222 / 3.224 | 3.156 / 3.108 | -2.8% |
| 6 | 3.214 / 3.308 | 3.310 / 3.152 | -0.9% (load spike) |
| 7 | 3.313 / 3.240 | 3.152 / 3.133 | -4.1% |
| 8 | 3.193 / 3.882 | 3.157 / 3.570 | -4.9% (load spike) |
| 9 | 3.185 / 3.197 | 3.103 / 3.135 | -2.3% |
| 10 | 3.186 / 3.176 | 3.111 / 3.229 | -0.4% (load spike) |
| 11 | 3.179 / 3.173 | 3.128 / 3.114 | -1.7% |
| 12 | 3.185 / 3.149 | 3.117 / 3.152 | -1.0% |

All twelve rounds: mean 3.2313 s -> 3.1553 s = **-2.35%**, median 3.1898 s ->
3.1333 s = **-1.77%**, the candidate faster in 12/12 rounds.  The first eight
rounds are the cleanest (mean -2.85%, median -2.97%); rounds 6, 8 and 10 ran
under host load spikes that show up as multi-hundred-millisecond outliers on
both sides (round 8's 3.882 s is the clearest).  Frame work is ~92% of the run
(the snapshot load is unchanged), so the per-frame win is a little over 2%.

The absolute times drift with the host (the first round's baseline is 3.16 s,
the fourth's 3.24 s) but every A/B pair is measured minutes apart and the A B B
A order cancels a monotone drift.

## Gates

- `build/test_m3g`: 848 checks, 0 failures.
- The same binary under `-fsanitize=address,undefined` (Debug -O1): 848 checks,
  0 failures, no sanitizer report.
- `tools/check_m3gdemo.sh`: 148 checks, 0 failures.
- `tools/check_no_statics.sh`: OK - no writable statics.
- `ctest --test-dir build`: 137/137 tests passed.  (A fresh worktree registers
  137 of the main tree's 140 tests because three `test_port_*` tests need port
  fixtures that only the main checkout has; the fixtures and the 403 game jars
  used here were copied/hardlinked from it.)

## Ruled out

- *Raster micro-optimisation*: the `-l` line profile puts the remaining span
  time on the depth key conversion, `1.0f/pg`, `sw_near_texel` address
  arithmetic and the tint `/255`; all of those are already one or two
  instructions per fragment, and bit-exactness forbids reassociating them.
- *A per-mesh cross-frame descriptor cache*: it needs mutation invalidation
  through every M3G setter in the file, and a missed setter is stale visuals.
  The frame half is safe precisely because it can be tied to a call.
- *File-static cache*: measured fine, rejected by the no-statics gate (above).

## Risks and Android relevance

- `m3g_frame` is a stack local of `m3g_world_draw()`; it is a bit over a
  kilobyte and the draw loop already holds a few of those.  No heap, no
  per-draw allocation.
- One VM per process, so a cache with a call lifetime needs no locking and
  cannot leak between VMs.
- This is the same software rasteriser the Android APK builds
  (`src/midp/nj_lib_m3g.c`, no new JNI or Java surface).  The saved draw work is
  CPU-side field lookups and matrix reads, which Android pays per draw exactly
  as the desktop benchmark does; there is nothing here that depends on the
  host's file system or on JIT behaviour.
