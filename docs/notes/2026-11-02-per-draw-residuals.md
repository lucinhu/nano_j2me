# Where the in-world frame goes after the per-pass cache (2026-11-02)

After the per-pass Graphics3D cache (`2026-10-31-per-pass-frame-state.md`, main
`9ee12ac`) the question was what is left of the per-draw M3G cost, and whether
the `m3g_tsa_getStripLengths` entry that the gprof flat profile reported (2.2 %
of the run) was real.  It is not.  This note records the profile, the call
counts, and two bit-exact micro-optimisations that were measured and rejected.

## Profile

Production is `-O3 -DNDEBUG`; the profile build is `-O3 -DNDEBUG -pg -g
-fno-pie` with `-pg -no-pie` on the link, and `gmon.out` is always regenerated
with the same binary in its own directory (reading it with a different binary
produces a plausible-looking but entirely wrong table).  1200 in-world frames
from the Minecraft PE saved state, pinned to one core: 2.27 s of samples.

| function | self s | % | calls | note |
|---|---|---|---|---|
| `jr_emit` (contains inlined `sw_tri` span loops) | 1.07 | 47.1 | 10,791,620 | the rasteriser |
| `nj_interp_run` | 0.32 | 14.1 | 13,890 | Java side, not M3G |
| `jr_fetch` (contains inlined `jr_vertex`) | 0.26 | 11.5 | 25,544,644 | vertex transform/light |
| `nj_vm_load` | 0.20 | 8.8 | 1 | snapshot load, not frame work |
| `jr_project` | 0.08 | 3.5 | 25,829,492 | |
| `jr_strip` | 0.07 | 3.1 | 6,386,161 | |
| `nj_field_lookup` | 0.06 | 2.6 | 19,025,466 | memo hits from `jr_setup` |
| `nj_region_draw` | 0.06 | 2.6 | 21,007 | HUD image blits |
| `m3g_tsa_getStripLengths` | 0.05 | 2.2 | 45,995,887 | **artifact, never called** |
| `jr_triangle_v` | 0.02 | 0.9 | 12,772,322 | |

Line level: `sw_tri`'s affine textured ARGB span (the hot one) is one
`divss` (`cp = 1.0f / pg`) plus one texel fetch per pixel and is already
close to minimal; `sw_near_texel` (inlined) is 6 %, `jr_vertex` 7 %,
`jr_project` 4 %.  `jr_setup` itself is 112,226 calls / 0.03 s = 1.1 % after
the per-pass cache, so per-draw setup has almost nothing left to give.

## `m3g_tsa_getStripLengths` is a profiling artifact

Temporary counters in `m3g_tsa_getStripCount`/`getIndexCount`/`getStripLengths`
reported once at exit.  `test_m3g` drives each native once
(`striplen=1 stripcount=1 indexcount=1`), while a 1200-frame Minecraft run
printed no counts at all: the in-world path reads `_nj_lengths` directly in
`jr_setup` and never calls the native.  The 46 M call arcs (one every ~50 ns)
and the 2.2 % self time are gprof arc/cycle noise.  There is nothing to
memoise here.

## Draw topology (temporary counters, 120 frames)

79.2 draws/frame, all explicit-index strips; 4,514 `jr_strip` calls/frame;
each strip table entry is 4 indices = 2 triangles (a quad), 9,028 triangles
and 18,057 `jr_fetch` calls per frame - 2.0 fetches per triangle, which is the
minimum for a 4-index quad strip.  No triangle-list draws at all.  82.8 % of
triangles take the clip fast path.  Fog is on in 99 % of setups.

## Micro-optimisations measured and rejected

Both would have been bit exact (same `jr_fetch` results, same values).

1. **Pointer ring in `jr_strip`.**  Replaced the four 52-byte `memcpy`s per
   strip (two cache-hit copies, two keep updates) with a three-slot ring whose
   retired slot is refilled with the next triangle's new vertex, so triangles
   take pointers into the ring instead of copies.  ABBA vs pristine `b18d0a2`,
   4 rounds, 1200 frames, user CPU: 3.096/3.082/3.102/3.101 s baseline vs
   3.146/3.129/3.132/3.147 s candidate - **+1.3-1.6 % slower** (8/8 rounds
   slower).  The copies are cheaper than the added tag checks and the
   compiler was already forwarding most of the traffic.
2. **`jr_vertex` fast path for `pcomp == 3`, non-fixed components.**  Three
   straight-line component loads instead of a two-bound loop with the 16.16
   test per component.  Same 4-round ABBA: 3.101 s vs 3.091 s, **-0.33 %**,
   inside run-to-run noise (only 2/4 rounds faster).  Not significant.

Both reverted; the branch stays at pristine `b18d0a2` with no change and no
commit.

## What is actually left

- Rasterisation (`jr_emit`/`sw_tri`) is 47 % and per-pixel minimal: the
  division and the texel fetch are required for bit-exact output.  A further
  win needs a structured rasteriser change (specialised opaque loop, SIMD),
  not a micro-edit.
- The vertex pipeline (`jr_fetch`/`jr_vertex`/`jr_project`) is 15 % and
  already fetches each vertex once per strip.
- `nj_interp_run` (14 %) is Java execution and the snapshot CRC load (8.8 %)
  is teardown, both outside the M3G renderer.

## Verification gates on the unchanged tree

`build/test_m3g` 848 checks / 0 failures; `tools/check_m3gdemo.sh` 148 checks
/ 0 failures; `tools/check_no_statics.sh` OK.
