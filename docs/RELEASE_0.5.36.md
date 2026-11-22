# nano_j2me 0.5.36

**2026-11-23** — runtime fixes: MIDP 2.0 `Choice.POPUP` support and a blocked
C-call pump leak; corpus expanded to 1316 fetched JARs.

## Highlights

- **`Choice.POPUP` (MIDP 2.0)** — `ChoiceGroup(label, Choice.POPUP)` is now
  accepted (`Choice.POPUP = 4`) and behaves as a single-select group; the
  constant is exposed on both `Choice` and `ChoiceGroup`. `IMPLICIT` remains
  List-only and is still rejected by `ChoiceGroup`. Fixes `BT INFOx 1.04`
  (`BT_Info.jar`), which threw `IllegalArgumentException` from `startApp()` and
  installed no Displayable; it now runs its main menu
  (`Pripojit / O programe / Koniec`).
- **Blocked-call pump leak** — the native pump that services a thread blocked
  inside a C call now restores `in_c_call` on every exit path, including the
  embedder-stop path that previously left a thread marked as "parked in a C
  call" forever. TheMummy's second timer task now executes (the splash loader
  advances past the first stages); its remaining stall is a game-state wait,
  documented in `docs/notes/2026-11-21-fetched-black-screen-titles.md`.
- **Corpus** — the Sony Ericsson K700/K800 collection (673 new JARs, cache now
  1316 files / 439 MB) was swept and merged into the compatibility matrix:
  **2462 titles, 2421 running normally**.

## Changes since 0.5.35

- `feat(lcdui): support the MIDP 2.0 Choice.POPUP mode`
- `fix(core): clear in_c_call when the blocked-call pump aborts`
- documentation: K700/K800 core probe, Midnight Pool2 triage correction,
  TheMummy timer/paint traces, 0.5.35 corpus probe.

## Libretro core package (r55)

Built from commit `5af2e3b` (clean worktree):

| target | size (bytes) | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,048,832 | `b97d6baf979d8c016cabaacf86b49ec1a1a44a6a2f9afcbbb2f31b450249afb7` |
| linux-aarch64 | 3,522,376 | `dec146cb1458f4a41a319cb53d0c328cd3bfcca711b39e5190ad5717a0400a57` |
| linux-armv7 | 3,175,192 | `c1268d70e95daed7fc842cad24c3335b2db12cc4bb9e4a24004514e6de7e5b2e` |
| android-arm64 | 3,768,120 | `2816d21a5fc212a7e68a5287a460fd29ff81aa8f46f83032beb288be53d2df9d` |
| android-armv7 | 2,864,284 | `6601f1f1df3189440fcd911798a2026abec5c4d488be8e64dd6b4c1e9643e31e` |
| android-x86_64 | 3,793,792 | `ccbcb93082a9fca4e6ed76e9bb5f8bc7a30c0b60650451a905ef864d8fff095e` |
| windows-i686 | 3,600,384 | `4242665f8ad74678193a999e0c1c13759c4e9963a510e948c584381b191fdd3a` |
| windows-x86_64 | 3,370,496 | `fd3fa8ea57f7b9447ff0f5a8143f5e66568a5941f16de99f9aca7c76f22001c3` |

`.info` metadata: `d8e0ca30d01c16044756765066fa00d1a1abb790186eb23c883176a691de4382`
(`display_version = "0.5.36"`, `cheats = "true"`, `memory_descriptors = "true"`).

## Android package

Split APKs, versionCode 536, `minSdk 21` (legacy ARMv5/API 5 build also
provided):

| package | size (bytes) | SHA-256 |
| --- | --- | --- |
| arm64-v8a | 1,054,876 | `4df8f3ffa1c9c4d1696037b1816bfb00e9fe298467d71f29d75d7f0d60fa6668` |
| armeabi-v7a | 1,067,166 | `7dd75f5d7601e72859d8124f9a84e810412fe37fdc34abf4d8962c8fdcecc8aa` |
| x86 | 1,042,582 | `828224e5e7720823e9273cca85e56c9d11b66ba4a93fa7bd3ee1daa7fe61c0e4` |
| x86_64 | 1,058,969 | `e45abe79347b30b1c05b5def37e9214832e9208384b3dd54fd82fa2e8b6fae68` |
| legacy (ARMv5) | 741,247 | `7dd8bdafdeb02fa4983bff2f9affe0de4e291d3f2d9e5cd600ba5f67c620b0c5` |

## Release assets

- `nj2me-0.5.36-libretro-cores.zip` (128 files, 12,173,152 bytes,
  SHA-256 `ecb047d7d2513c01b19b200ec7e1bd179068e2b0e276a35ae677bf963e521159`)
- five split APKs (above)
- `SHA256SUMS-0.5.36.txt`

## Verification

- full serial CTest: **210/210**;
- libretro mock frontend matrix: **20/20**;
- real RetroArch 1.22.2 LoopDemo, 120 frames: **PASS**;
- Windows x86_64 under Wine, 4 titles × 300 frames: **4/4**;
- submission bundle validator: **32/32**;
- whole-corpus libretro visual probe on the r55 core: see
  `docs/CORPUS.md` round-217/218.

## Upgrade notes

- Save states are refused across releases (`0.5.8 … 0.5.36` states carry the
  version CRC); re-save after upgrading.
- Cheats are applied through the CPU arena and the libretro SYSTEM_RAM memory
  map, as before.
- Software rendering remains the default; the GLES2 backend stays opt-in.

## Upstream

- libretro-super PR: https://github.com/libretro/libretro-super/pull/2204
- libretro/docs PR: https://github.com/libretro/libretro-docs/pull/1239
