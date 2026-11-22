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
   and the m3gtarget fixture.
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
