# Android: hardware acceleration (GLES2) is on by default (2026-11-22)

The Android release APKs used to be software-only: the GLES2 backend existed
(`src/port/android/nj_android_gles2.c`, `src/port/gles2/`), but it was reachable
only when the binary was built with `--gles2` *and* the player turned on the
"experimental acceleration" row in the panel.  The release pipeline built the
plain software binaries, so most players never saw the GPU path at all.

This round flips both switches:

* `tools/build_android_apks.sh` (the per-ABI release script) now passes
  `--gles2` by default; `--no-gles2` still produces the old software binaries.
* `Settings.gles2Experimental()` defaults to **true**: a build that carries the
  backend requests it on the next game launch.  The panel row
  **硬件加速（GLES2）：开/关（下次启动）** remains the per-device fallback; a
  title that misbehaves on the GPU path can still be run with software drawing.
* The legacy ARMv5 edition is unchanged (software only).

## What "acceleration" covers

The backend creates a private EGL pbuffer context and replaces the port's
`gfx_raster`, `gfx_triangles*` and `gfx_begin/end/abort` callbacks, with a
one-time calibration that falls back to the CPU path per device.  That
accelerates the M3G/Micro3D triangle paths (3D titles).  The optional 2D
fill/blit wrappers (`NJ_ANDROID_GLES2_2D_ACCEL`) stay **off**: they need a
synchronous readback per operation, which measured slower than the software
blitter on the reference host, so 2D canvases keep the software renderer and
only the final presentation is scaled/presented by the surface path.

## Verification (AVD `Medium_Phone_API_36.1`, API 36, x86_64)

```
I nj2me : gles2: enabled renderer=Android Emulator OpenGL ES Translator (Google SwiftShader) (private pbuffer)
I nj2me : running LoopDemo (LoopDemo ?)
```

The in-game panel reported:

```
硬件加速（GLES2）：开（下次启动）
本局状态：GLES2 已就绪，等待支持的绘制
本局启动选择：硬件加速（GLES2）
GPU 填充 0 · 图像 0 · 栅格 0
```

LoopDemo is a 2D title, so the triangle counters are legitimately zero; the
"ready, waiting for supported drawing" status is the correct state.  The
per-draw counters and the final `gles2: final ...` line are the evidence to
check on a 3D game.
