# nano_j2me 0.5.37

This release ships the first-paint delivery fix, the full 0.5.36 corpus
evidence, and a hardening of the sweep tool.

## Highlights

- **First paint before the first background timeslice.** `Display.setCurrent()`
  now records a one-shot first-paint request for the newly current
  Displayable; the frame pump serves it after input and before the first
  background-thread quantum.  `Skater_by_BFLM` (previously black, every paint
  throwing `NullPointerException` on a null image) now renders its splash:
  8,064 non-black pixels, `paints=301 gfx=2406`, no null-image exceptions.
  A/B over 150 corpus titles shows zero verdict changes and pixel-identical
  frames on the inspected titles.  See
  `docs/notes/2026-11-23-first-paint-delivery.md`.  The early delivery is
  enabled only for ports that run `startApp()` to completion before pumping
  frames (the CLI/headless runner); the libretro core's asynchronous loader
  keeps the frame-boundary paint, because delivering during load made two
  Midnight Pool Christmas variants throw from `paint()` and go black in the
  core (caught by the whole-corpus probe and fixed by the port gate before
  this release).  The core-side scheduler-level first paint remains open.
- **Full 0.5.36 corpus evidence.** The whole expanded corpus was re-swept:
  1,720 rows -> 1,684 `运行正常`, 9 title-exception, 15 bad-jar, 5 timeout,
  2 failure, 3 no-frame, 2 title-exit.  Against the previous merged row set
  there are six verdict changes, all improvements (3D_Tetris, BT_Info,
  Gangstar2, Tetris 1.0.0, Slide a Lama, epub_reader) and no regressions.
  `docs/COMPATIBILITY_MATRIX.md` refreshed to 2,478 titles / 2,438 ok.
- **Sweep hardening.** `tools/sweep_compat.py` decodes child output with
  `errors="replace"`: a title logging raw non-UTF-8 bytes no longer aborts a
  sweep, and the tool resumes from an existing `rows.jsonl`.

## Core packages (r56, build revision `31610d4`)

| target | size (bytes) | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,048,832 | `86dca38c47711ae94d3a2997e230c49dd480d38510bae9d26cfa4edcc67e28d8` |
| linux-aarch64 | 3,522,664 | `45e73a9a9760a2975b442e6cf96d334c51a7d870db967a597b24e287b369f631` |
| linux-armv7 | 3,175,416 | `c6b382b599b6a7fa30d0237046e120753fada9d6690749df8c2dd75d0200ff07` |
| android-arm64 | 3,768,312 | `a2df8aebfe72573fbbb49fb327b69ed8b8e2727f3a424b7c983aaf6ef919e2bf` |
| android-armv7 | 2,864,604 | `8716c04863f533e86943e1d7d03aa4cb2dbb894ab73dab51c28513df783a5258` |
| android-x86_64 | 3,794,256 | `cbeb6e316501ff7a8d3b16312d265c43da48975033e42c0e9fe111e915845353` |
| windows-i686 | 3,600,896 | `5ed5332ba95f001ad95cb3d1c44ed8ee908ad6db49f22ec6fde24f6ed0060037` |
| windows-x86_64 | 3,370,496 | `756e2c976f939d2b7d1fdd85ef7115b20bc6316e83d199ec10543d0c39268718` |

`.info` SHA-256: `71af9f4689705a59fd817a2e4549c7d9f340d7ea5a8f426a04766d30e66780c2`
(display_version `0.5.37`, same file in every target).

Core archive: `nj2me-0.5.37-libretro-cores.zip`, 12,174,904 bytes,
SHA-256 `5d5314833f770525aeb708d91e8ba5e51c3abb0edc3b181c4d275d3997701394`
(128 entries under `nano_j2me-0.5.37-libretro/`).

## Android APKs

Per-ABI packages, `versionCode 537`, `versionName 0.5.37`, minSdk 21
(legacy: ARMv5 / API 5):

| package | size (bytes) | SHA-256 |
|---|---|---|
| `nj2me-0.5.37-arm64-v8a.apk` | 1,058,972 | `b5c8dfa29f424f3b696b9215a842f634b6222dd18cc5f31dccb45fa42eabb5af` |
| `nj2me-0.5.37-armeabi-v7a.apk` | 1,067,166 | `f673448e1fcef564985425c2f1b35d645c8632454ecc9359295fdfb6310dce3b` |
| `nj2me-0.5.37-x86.apk` | 1,046,678 | `5992e847705dbe43aaf61581e7f1f8728dfa87497a38c9f72be4536c614c565d` |
| `nj2me-0.5.37-x86_64.apk` | 1,058,969 | `4a8713befa1fd6d8eb003a22e14a6679f73f12907c4ee0fbe5d13cddc8ef96c7` |
| `nj2me-0.5.37-legacy.apk` | 741,354 | `92b076ff11c7cecb1b64c331f6c44610254bc1b303c3449511607f6c48bcfd8c` |

## Verification

- Full serial CTest: **210/210 passed in 383.55 s**.
- Libretro mock frontend matrix: **20 passed, 0 failed**.
- Real RetroArch 1.22.2 LoopDemo, 120 frames: **PASS**.
- Windows x86_64 core under Wine, 300 frames: **4 passed, 0 failed**.
- Submission bundle validator: **32 passed, 0 failed**.
- Whole-corpus libretro visual probe on r56: **1664 titles → 1654 passed, 1 known limitation (`毁灭战士3_10598` slow constructor), 9 failed — the same documented set as r55 (`MidnightPool1`, `Midnight Pool`, `Midnight Pool2`, `MidnightPool_K700_EN`, `TheMummy_K700F`, `PAC-MAN (176x220)`, `assault`, `Skater_by_BFLM`, `Asphalt4EliteRacing`)** (the pre-gate build failed two additional Midnight Pool
  Christmas variants; both pass after the gate, confirmed by this probe).

## Upgrading

Save states from 0.5.8 through 0.5.36 are refused by the version check; start
from a fresh save state.  Core options and frontend integration are unchanged.

## Upstream

- libretro/libretro-super <https://github.com/libretro/libretro-super/pull/2204>
  (`dist/info/nano_j2me_libretro.info`, branch commit `f83ca18`)
- libretro/docs <https://github.com/libretro/libretro-docs/pull/1239>
  (library page, core list, licenses, see-also, nav)
