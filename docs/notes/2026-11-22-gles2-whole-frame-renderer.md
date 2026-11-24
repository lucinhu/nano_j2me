# GLES2 structural rework: whole-frame renderer design (round 197)

The current GLES2 backend is a per-transaction accelerator: M3G triangles are
drawn into a private FBO, read back with `glReadPixels`, and committed into the
CPU colour/depth buffers so that the 2D pipeline, canvas blits and platform
presentation (ANativeWindow / SDL window) keep working unchanged.  The
round-196 profiling (see `docs/notes/2026-11-22-android-gles2-bottlenecks.md`)
showed that this shape cannot beat software on a real GPU:

- Galaxy on Fire 2 gameplay: forced GL 4.84-5.47 s user CPU vs 3.83-3.90 s
  software; auto-calibration correctly declines (`gpu=641us software=251us`).
- Per transaction: `read_c`+`read_d` ~36%, `prep`+`setup` ~27%, `commit` ~10%,
  texture/depth uploads ~20%, actual draw ~2%.
- Flush tracing (`NJ_FLUSH_TRACE` return-address experiment) shows the
  transactions are forced by 2D `Graphics` use: every `gfx_load()` flushes
  pending M3G work so the 2D draw composites over committed pixels.  With
  mixed 2D/3D content that is **two full-screen transactions per frame** on
  Galaxy (one 3D scene, one UI batch), i.e. the round-trip is structural, not
  an implementation detail.

## Target architecture

A GL-owned frame, one transaction per frame:

```
2D canvas (CPU RGB565) --upload texture-->  \
M3G scene --resident FBO, resident textures--> composite (GL) --> present (EGL window or blit)
```

- **M3G scene**: one frame-resident colour+depth FBO; seed from the CPU canvas
  once per frame (or only the changed rectangle), draw all M3G passes, no
  intermediate readback; readback only when a CPU observer really needs it
  (save state, cheat search, screenshot, pixel-based game logic).
- **2D canvas**: keep the existing software renderer as the source of truth;
  upload the RGB565 canvas as one texture per frame and composite it under the
  M3G layer (or over it when the 2D overlay is on top).  A dirty-rect texture
  update avoids full uploads.
- **Presentation**: GL swapchain/EGL window when the platform provides one
  (Android), otherwise a single `glReadPixels` + blit into the existing
  surface (SDL/offscreen), which restores the current behaviour as fallback.
- **Compatibility**: keep the CPU-only path and the current per-transaction
  backend for titles/frontends where the GL frame renderer is unavailable;
  auto-calibration decides, and a core/APK option can force software.

## Phases

1. **Frame FBO** (biggest single win): hoist the triangular scene into a
   frame-resident FBO, keep the current readback at frame end, so only one
   transaction per frame instead of two.  Expected: remove ~50% of `read_c`,
   `prep`/`setup`, seed and upload costs; measure against software on Galaxy
   and the m3gtarget fixture.  **Round-5 measurement changed the priority:**
   the 2D accelerator's per-operation transaction readback is the dominant
   cost on Galaxy (154,388 commits, 436M px, 4.54 s in one 560-frame run; see
   `2026-11-22-android-gles2-bottlenecks.md`), so implement **B1 screen-canvas
   2D deferral** before/with the triangle frame FBO -- the two must share one
   frame surface (B2) to matter.
2. **Resident textures**: version-stamp M3G textures, upload only on change;
   remove per-transaction `up_c`/`up_d`/`import_c`.
3. **GL presentation**: render the final frame into the platform GL surface
   (Android first).  Removes the remaining colour readback; SDL can keep the
   readback fallback.
4. Keep the software path the default until phase 1-3 win on the measured
   workloads; then flip the default on capable devices and keep the setting.

## Exit criteria

- Galaxy forced-GL user CPU below software on the same run (target: <3.5 s vs
  3.83 s) with identical rendered frames in the visual probe.
- Doom3 loading animation and the m3gtarget fixture at least parity.
- Whole-corpus libretro/CLI probes unchanged for software-mode titles.
- Save states, cheats, screenshots and 2D-only titles keep working through the
  CPU observer path.

## Progress

- 2026-11-24: phase-1 B1 (frame-resident 2D layer) implemented and validated
  for pixel parity, but measured only ≈2.5% user-CPU gain because the
  per-operation private-context switch dominates; see
  `2026-11-24-gles2-frame-resident-2d-layer.md`. B1.5 (hold the GL context for
  a whole frame, batch submissions, widen blit coverage) is the next milestone
  before B2.
- 2026-11-24 (B1.5): holding the GL context across a frame plus the deferred
  2D layer recovers ≈1 s of the ≈5.7 s GL-2D overhead (Galaxy: 10.6 s user vs
  11.6 s without hold, 5.9 s with software 2D), but per-op GL 2D remains a net
  loss. Plan revised: CPU 2D overlay composite (3D frame FBO + one overlay
  upload/composite per frame + EGL present) replaces per-op 2D GL capture.
