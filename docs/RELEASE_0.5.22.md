# nano_j2me 0.5.22

Released 2026-11-21.

## Highlights

* **Class-heavy titles link again**: the static field area default grew from
  4096 to 16384 slots (`NJ_MAX_STATICS`).  *Plants vs Zombies (240x320)* pushed
  the runtime past the old limit while linking `Layer`; linking then reported a
  bare `NoClassDefFoundError` naming a class that is present and the game
  silently half-rendered (`gfx=132`).  It now draws its layers (`gfx=13,479`)
  and the libretro core renders it (`visual_nonblack` 779 px).
* When the static area *is* exhausted the runtime now throws
  `java/lang/OutOfMemoryError: static field area exhausted: increase
  NJ_MAX_STATICS in nj_config.h` instead of the misleading link error.

Everything else is unchanged from 0.5.21; no API or save-format change.

## libretro core artifacts (r41)

| target | size | sha256 |
|---|---:|---|
| linux-x86_64 | 3,856,264 | `c86689e430e7160e0dc7362117d93a6f6c72e216b5c397b57ba5e6fadaea9708` |
| linux-aarch64 | 3,333,136 | `12e8cae15bb1c8513b9c0b29ef81f755b69bea0b77713d3982dc157e39823288` |
| linux-armv7 | 3,040,632 | `cfc55f43c5f0d91ccd63629bd96e963a53f4d9cab04bebeee7d4dba082437c15` |
| android-arm64 | 3,567,920 | `7d8969de3c32886cd58ea17b31e3583e5697925715269d576bf768c22a7e673a` |
| android-armv7 | 2,752,680 | `1dd820ba5f2c2e8282df3df2617e8e6964bc39f7bebdfdbaba9b23e88707bb4d` |
| android-x86_64 | 3,608,520 | `b01e40db9da31f39971d03b77737c65c6d6689a7214c2de722cd887192b8f4c7` |
| windows-i686 | 3,462,656 | `35f76814e2caf41ec75c31a3d22bcc1ae22b7368e84436868794c586d00306fb` |
| windows-x86_64 | 3,238,400 | `4b5e0dcac7b7656af50d3d0f92cf20781fa4bb8a851a3bbfa67bcad27cd40954` |

`.info` SHA-256 `ceb5330d62247e8b29fef8f718272241ccde09fe73754d909446801d3b563b37`
(`display_version = "0.5.22"`).

## Android APKs

| file | versionCode | size | sha256 |
|---|---:|---:|---|
| `nj2me.apk` | 522 | 3,299,690 | `70e385c2c3a98b2262edad5f375bd14022b511d5bacb5c9e3d2ae5d950a5e1f5` |
| `nj2me-legacy.apk` | 522 (0.5.22-legacy) | 704,564 | `8c04af583cc01b0e8d87442ee00470dcbea98d16b9b91bd6ee76d4adf7a27083` |

Release zip `nj2me-0.5.22-libretro-cores.zip` 11,715,020 B SHA-256
`5c8ae265cd1e8438cf73e1065cd784e27b28e711616022a79e5b69509766a322` (128
entries, eight targets x 16 files).

## Verification

* full serial CTest: **199/199 passed in 394.58 s** (`/tmp/r41-ctest2.log`);
* libretro mock matrix **20/20**; real RetroArch 1.22.2 LoopDemo 120 frames
  **PASS**; Windows x86_64 Wine **4/4 at 300 frames**; submission bundle
  **32/32**;
* combined corpus sweep on the fixed CLI (899 unique files from `tests/games`
  + four fetched archive collections): **879 ok / 3 title-exception / 5 failure
  / 5 timeout / 3 bad-jar / 2 title-exit / 2 no-frame**; `Plants vs Zombies`
  and `JBenchmark3D` recovered from `title-exception`, `GT赛车2_36` is the
  documented slow loader (130 s solo, flaked under 10-way sweep load);
* `docs/COMPATIBILITY_MATRIX.md` refreshed: **1459 titles, 1438 `运行正常`**;
* libretro core on PvZ: `PASS visual_nonblack: max non-black pixels=779`.

## Upgrade notes

* Save states from 0.5.8 through 0.5.21 are refused by the version CRC; start a
  new state after upgrading.
* Each VM reserves 96 KB more for static fields (32 KB -> 128 KB).  Embedded
  ports can keep the old size by defining `NJ_MAX_STATICS` in their config.

## Upstream submission

* `libretro/libretro-super` PR #2204 (`.info` for 0.5.22)
* `libretro/docs` PR #1239 (library page)
