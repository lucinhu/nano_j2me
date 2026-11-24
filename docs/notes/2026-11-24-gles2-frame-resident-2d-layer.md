# GLES2 frame-resident 2D layer (Stage B1)

Status: **implemented, experimental, opt-in, default off.**
Pixel parity with software is exact on the tested titles; the CPU gain is small
(≈2.5% user) because the remaining cost is per-operation GL context switching,
not readback. This note records what was built, what was measured, and what the
next step has to be.

## Why this layer

Round 5 measured the synchronous 2D accelerator path: on Galaxy on Fire 2,
560 frames at 30 fps produced 154,388 2D commit readbacks worth **4,543 ms**
(29.4 µs per operation, ≈8.1 ms per frame) and the 2D accel path as a whole
raised user CPU from 6.05 s (software 2D) to ≈14.1 s. The whole-frame design
(`docs/notes/2026-11-22-gles2-whole-frame-renderer.md`) predicted that making
2D frame-resident (B1: one GL texture per CPU surface, one readback per frame)
would remove most of that readback cost.

## What was built

- `src/port/gles2/nj_gfx_gles2.c`: up to four tracked CPU surfaces, each with a
  GL colour texture + FBO. Fills are scissored `Clear`s; blits are textured
  quads; a dirty-union rectangle is committed with one `glReadPixels` on flush.
  The layer tracks `stale` surfaces (CPU writes, 3D commits) and re-uploads the
  full surface lazily. Unsupported operations (rotated blits, alpha blends,
  XOR, masks, non-SET/OPAQUE rops) decline to the synchronous path.
- `src/port/gles2/nj_gfx_triangles_gles2.inc`: 3D transactions flush deferred
  2D before they read back, and mark the target surface stale after committing
  so later 2D operations re-upload the 3D result. The persistent session path
  (`nj_gles2_triangles_begin`) flushes at begin and marks stale at end.
- `include/nj2me/nj_gfx_gles2.h`: `nj_gles2_frame_enable`,
  `nj_gles2_frame_flush`, `nj_gles2_frame_active`.
- `src/port/posix/nj_port_sdl2.c`: flushes the layer before
  `SDL_UpdateTexture` in `sdl_display_present` (the software framebuffer must
  be current for presentation) and before destroying the backend; enables the
  layer only when `NJ_SDL2_GLES2_FRAME=1` is set and the 2D accel path is on.
- Presentation-proof tooling: `NJ_SDL2_CAPTURE_COUNT` and a capture tick were
  added to the existing `NJ_SDL2_CAPTURE` facility so a run can dump several
  distinct presented frames (the old behaviour captured only the first one).

## Environment flags

| variable | meaning |
| --- | --- |
| `NJ_SDL2_GLES2_2D=1` | route 2D accelerable ops through GL (existing) |
| `NJ_SDL2_GLES2_FRAME=1` | enable the frame-resident 2D layer (requires 2D accel) |
| `NJ_SDL2_CAPTURE=<prefix>` | dump presented frames as `<prefix>NNNNN.ppm` |
| `NJ_SDL2_CAPTURE_COUNT=<n>` | number of dumps (default 1) |
| `NJ_SDL2_CAPTURE_AT=<n>` | presented frames between dumps |

The layer is development-only: it is not enabled by any shipped default.

## Verification

- Full serial CTest with the patch: **210/210 passed in 364.78 s** (default
  environment, layer off).
- Presentation parity, window backbuffer captures (960x1039 PPM), three
  distinct presented frames per title, comparing software vs GLES2 vs
  GLES2+frame layer:
  * LoopDemo (240x320, 260 frames): **0 / 997,440 pixels differ** in all three
    captures for both GL paths.
  * Galaxy on Fire 2 (240x320, 240 frames, recorded keys): **0 / 997,440
    pixels differ** in all three captures for both GL paths.
- Galaxy on Fire 2, 560 frames @30 fps, forced GL, real RTX 5080 laptop:

| mode | user CPU | sys CPU | wall |
| --- | --- | --- | --- |
| 2D accel, layer off (1) | 14.10 s | 2.33 s | 19.18 s |
| 2D accel, layer off (2) | 14.23 s | 2.22 s | 19.16 s |
| 2D accel, layer on (1) | 13.74 s | 2.41 s | 19.20 s |
| 2D accel, layer on (2) | 13.90 s | 2.38 s | 19.16 s |

GPU counters: layer off — fills 744/724, blits 101,081/101,849, declines
40,381/37,351; layer on — fills **0/0**, blits 105,693/104,205, declines
46,706/45,714.

## Interpretation

- The deferral works as designed: all fills were absorbed into the layer, blits
  were deferred, 3D/2D interleaving was handled by flush + stale tracking, and
  the presented frames stayed pixel-identical to software.
- The CPU win is only ≈2.5% user. Removing per-operation readback does not
  remove the dominant cost of the synchronous 2D accel path: every callback
  enters and leaves a private GL context and performs per-operation GL setup.
  Going from 6.05 s (software 2D) to ≈14.1 s with 2D accel shows the overhead is
  per-operation, not per-pixel.
- The frame path also declines more operations than the synchronous path
  (46.7k vs 40.4k here) because it supports a narrower operation set; every
  decline falls back to a synchronous transaction, cancelling part of the win.

## Next step (B1.5)

Keep the private GL context current for a whole frame and submit deferred 2D
operations without per-operation `SDL_GL_MakeCurrent`, then widen
`frame_blit` coverage so declines shrink. That attacks the real cost measured
here; only after that does B2 (triangles into the same frame FBO) become worth
measuring against software.

## B1.5: holding the GL context across a frame

The per-callback cost was confirmed to be the private-context switch, so the
port gained a hold mode (`NJ_SDL2_GLES2_HOLD=1`, requires the private context
to remain valid): the first callback of a frame switches context, later
callbacks reuse it, and `sdl_display_present` flushes the deferred layer and
restores the presentation binding once. Measured on Galaxy on Fire 2 (560
frames @30 fps, forced GL, real RTX 5080 laptop, runs in one session):

| mode | user CPU | sys CPU |
| --- | --- | --- |
| 3D GL + software 2D (reference) | 5.90 s | 1.12 s |
| 2D accel, no hold | 11.64 / 11.79 s | 2.71 / 2.61 s |
| 2D accel + hold | 11.13 / 11.06 s | 1.86 / 1.80 s |
| 2D accel + hold + frame layer | **10.62 / 10.57 s** | 2.04 / 2.01 s |

Doom3 (600 frames, pure 3D): 3D GL 3.12 s user, +hold 2.94 s, +hold+frame
2.73 s (software 2.10 s). Presentation parity with hold+frame: LoopDemo and
Galaxy captures again identical to software (0/997,440 px, three frames).

## Revised conclusion for the plan

Holding the context plus deferring readback recovered ≈1 s of the ≈5.7 s that
the GL 2D path costs over software 2D, but **routing every 2D operation through
GL is still a net loss against software 2D** (10.6 s vs 5.9 s user with 3D in
GL). The per-operation GL command/upload cost is larger than a software blit
for these small, numerous operations.

The next architectural step therefore changes: rather than B2 (triangles into
the same frame FBO with per-op 2D capture), build the **CPU 2D overlay
composite**: 3D renders into the frame FBO, 2D drawing stays on a software
overlay surface (damage-tracked), the overlay is uploaded once per frame as a
texture, GL composites FBO + overlay, and EGL presents the result without any
per-frame readback. CPU observers (getRGB/motion probes) force one composite
readback on demand. That keeps the fast software 2D path and removes the 3D
readback + presentation cost that made forced GL lose to software.
