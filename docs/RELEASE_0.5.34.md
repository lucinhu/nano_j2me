# nano_j2me 0.5.34

Released 2026-11-22.  This release packages the web input work and the GLES2
diagnostics/investigation branch: the web emulator now supports multi-finger
canvas pointers and touch-down on-screen keys, the Android build exposes GPU
counters and stops rebinding EGL per packet, and the GLES2 backend has been
measured on a real GPU (with an explicit conclusion about what still has to be
redesigned).  There is also a README note telling Android 2.x-4.4 users to use
the legacy build.

## Highlights

* **Web emulator: multi-touch + touch-down keypad.**  Canvas pointers are now
  tracked per `PointerEvent.pointerId` and forwarded through the new
  `njw_pointer_multi()` export, so Canvas keeps its first-finger owner while
  `MultipointTouch` sees every finger.  On-screen keys press on touch-down and
  switch while a finger slides across the pad; `pointerup`/`pointercancel`
  release the key.
* **README: legacy Android note.**  The platform table now states that the
  standard APKs require Android 5.0+ and that Android 2.x-4.4 devices must use
  the legacy APK (ARMv5, minSdk 5, software rendering).
* **Android GLES2 diagnostics.**  `debug.nj2me.stats` now also prints
  `gpu state fills/blits/rasters/declines/triangles/batches`, and
  `debug.nj2me.m3g_accel=force` runs M3G through the GL path for investigation.
  `eglMakeCurrent` is cached per owning thread instead of being issued for
  every packet.
* **GLES2 measured, honestly.**  On a real GPU (RTX 5080 Laptop) the current
  per-transaction backend is slower than the software rasteriser for both a
  mixed 2D/3D title (Galaxy on Fire 2: 5.20 s vs 3.83 s user CPU) and a
  near-pure-3D title (Doom3 loading: 4.30 s vs 3.00 s).  A benchmark switch
  that skips the colour/depth readback still lands at 4.37 s, so presentation
  from GL alone is not enough - the whole frame has to stay GPU-resident.
  Auto-calibration keeps the software path for users; the measurements and the
  staged whole-frame renderer design live in
  `docs/notes/2026-11-22-android-gles2-bottlenecks.md` and
  `docs/notes/2026-11-22-gles2-whole-frame-renderer.md`.

## Artifacts

| target | size | SHA-256 |
|---|---:|---|
| linux-x86_64 | 4,044,728 | `b6ce756446663cb62efc27e1ef064388f8ac9571a4983fd411666e5e6aa16154` |
| linux-aarch64 | 3,517,768 | `a1ad0cbad1c62b91688f3fe7eaff4f07348ebdb5d37b48dd0a4cabb196a7385b` |
| linux-armv7 | 3,172,036 | `c09ef3be740b2361dce7320c7cd55393815b32495d69a3e582cc05f33d658e9a` |
| android-arm64 | 3,763,336 | `07cf27d2dc3f2cac3a2f13e5d331f2066c5f662e8fca8662a7004a30ff3bd4e6` |
| android-armv7 | 2,861,736 | `41694aa1f99659ea025c7c1be4a98e93c458a4a4e851321547cb485297113b91` |
| android-x86_64 | 3,789,448 | `da764399620a7b4a592e59f73d661ecb706626f71d3b2c151aedb3e03b8cbfc3` |
| windows-i686 | 3,596,288 | `6dd95fe529c517da2513628bddd66ea4e471ebfa872a93b4d2f0c8cd9a824d2a` |
| windows-x86_64 | 3,366,912 | `0cee6fc116cc6b280e89cbd9d361626d555b940de896254ff075e9d93762c38c` |

The release zip is `nano_j2me-0.5.34-libretro-cores.zip` (12,164,960 B,
SHA-256 `448cb282cd19b8c1a3f06d46ff2999b68f7484ee53e71a9d4c97d70e0af434cd`).

Android (per-ABI APKs, versionCode 534; the four phone ABIs are GLES2 builds,
the legacy APK is software-only):

| APK | size | SHA-256 |
|---|---:|---|
| `nj2me-0.5.34-arm64-v8a.apk` | 1,054,876 | `580b9f4c18e90af6ebaa61e0a11ef1817cd874f5e2b0ac23fa6fc40979cd1f56` |
| `nj2me-0.5.34-armeabi-v7a.apk` | 1,063,070 | `6aa0d24e7d68b3c8acaebd3ebf1ce80cdfe3d93ead2d3962d04df24705504f98` |
| `nj2me-0.5.34-x86.apk` | 1,042,582 | `1c6f733ddee9b59cf53e4872628c5624ceca9566c7dc1b809c8bc9fd4852faa6` |
| `nj2me-0.5.34-x86_64.apk` | 1,058,969 | `42778d56a07664fa7f60973695a35ceb2b8dc688d6d748b7da821ff38dfda23c` |
| `nj2me-0.5.34-legacy.apk` | 740,139 | `6155e61acd95e115e9ace0730fd5607c587c38662d811ade0881fc0b4c1c29d3` |

## Verification

* full serial CTest: **210/210 passed** (397.23 s)
* libretro mock frontend matrix: **20/20**
* real RetroArch 1.22.2 LoopDemo, 120 frames: **PASS** (core log markers verified)
* Windows x86_64 under Wine, 300 frames: **4/4**
* submission bundle validator: **32/32**

## Upgrade notes

* Save states written by 0.5.8-0.5.33 are rejected by the version CRC, as
  before; only save states from the same version are loaded.
* The GLES2 backend is still opt-in through the Android panel; the automatic
  calibration keeps software rendering when the GPU path is not faster (it is
  currently not, see the notes above).  No title requires GLES2.
* Netplay remains unadvertised: the deterministic timing option repeats
  frame-driven titles, but frame-locked single-step execution is not
  implemented yet.

## Upstream

* super PR: https://github.com/libretro/libretro-super/pull/2204 (head `a6d3f34`)
* docs PR: https://github.com/libretro/docs/pull/1239 (head `95a1d0a`)
