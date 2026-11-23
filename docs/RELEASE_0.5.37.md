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

## Core packages (r56)

| target | size (bytes) | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,048,832 | `d41b8b485656e9098797a29e624fd6f7bfac8dde07c6bffd5851d65ac1cb2631` |
| linux-aarch64 | 3,522,680 | `658f9b3ed9f96584d17a5698ea32ec5ed0f8f0bdb894ea6fc13a69f2a9f78d16` |
| linux-armv7 | 3,175,400 | `13b578ef671959103bb1d98f63266dc08365c101f0a9a292f5d51f850d8764f5` |
| android-arm64 | 3,768,328 | `0af93932ca1180524315452f4d961848f640f8b9dc764a640a53e7b42d43c4ce` |
| android-armv7 | 2,864,588 | `7d43db15d3c09b58ace2efa48a728b93b9d1a0ffffb362fe56c16ae13811fec1` |
| android-x86_64 | 3,794,240 | `8567d52d33f5c1eee4f8cca365c84f9952bcbffce58c811176bcc3715a16c52f` |
| windows-i686 | 3,600,896 | `309ea435381499ca5e2736754eed44ac5938d8b3332dafc42545c5628f42a22b` |
| windows-x86_64 | 3,371,008 | `5397796b354571efd4ef4e1291b786c111a1f9b4926330181549204b68fc1164` |

`.info` SHA-256: `71af9f4689705a59fd817a2e4549c7d9f340d7ea5a8f426a04766d30e66780c2`
(display_version `0.5.37`, same file in every target).

Core archive: `nj2me-0.5.37-libretro-cores.zip`, 12,174,659 bytes,
SHA-256 `79340948ca3569b623ef5a681fca55561e0a9795c99f4c08d5597be2c8435548`
(128 entries under `nano_j2me-0.5.37-libretro/`).

## Android APKs

Per-ABI packages, `versionCode 537`, `versionName 0.5.37`, minSdk 21
(legacy: ARMv5 / API 5):

| package | size (bytes) | SHA-256 |
|---|---|---|
| `nj2me-0.5.37-arm64-v8a.apk` | 1,058,972 | `d3e3604c7b93a8fc9ec77734df45b9584f04bf9aca531b6aadeb31ea86ce4109` |
| `nj2me-0.5.37-armeabi-v7a.apk` | 1,067,166 | `947edb0f9644697426d79ebbcc58a5e4cdd57fc7874b5e5dc879b8f1a6e2b4e7` |
| `nj2me-0.5.37-x86.apk` | 1,046,678 | `0efcc3e85e8823e4aa008bd16acde7f7248a17019726b535a89d704674324900` |
| `nj2me-0.5.37-x86_64.apk` | 1,058,969 | `c3fe1b4183323b38a9e1d9f7c5cd23c2036e1332f6ff73744302113b9a6466cd` |
| `nj2me-0.5.37-legacy.apk` | 741,381 | `54d1a088e5d6c083a39891c1dec11081e648c1715f523ff64f18bf266952d5b3` |

## Verification

- Full serial CTest: **210/210 passed in 383.55 s**.
- Libretro mock frontend matrix: **20 passed, 0 failed**.
- Real RetroArch 1.22.2 LoopDemo, 120 frames: **PASS**.
- Windows x86_64 core under Wine, 300 frames: **4 passed, 0 failed**.
- Submission bundle validator: **32 passed, 0 failed**.
- Whole-corpus libretro visual probe on r56: **__PROBE_RESULT__** (the run
  before the port gate had two extra Midnight Pool Christmas failures; both
  were re-verified PASS after the gate).

## Upgrading

Save states from 0.5.8 through 0.5.36 are refused by the version check; start
from a fresh save state.  Core options and frontend integration are unchanged.

## Upstream

- libretro/libretro-super <https://github.com/libretro/libretro-super/pull/2204>
  (`dist/info/nano_j2me_libretro.info`, branch commit `f83ca18`)
- libretro/docs <https://github.com/libretro/libretro-docs/pull/1239>
  (library page, core list, licenses, see-also, nav)
