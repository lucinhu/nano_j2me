# nano_j2me 0.5.18

Release date: 2026-11-19

0.5.18 is a performance release.  The runtime now resolves the `Image` field
handles used by the drawing path once per VM instead of once per `drawImage()`
(`src/core/nj_internal.h` hot slots + `src/midp/nj_lib_lcdui.c`), which removes
~78 M field lookups in a 300-frame Minecraft PE profile.

## What changed since 0.5.17

- **Blit-path field cache**: `Image.imgWidth` / `imgHeight` / `imgPixels` are
  resolved once per VM (`nj_hot_field`), shared by every `drawImage()` call site
  and invalidated on state load.  Measured on the deterministic unpaced
  Minecraft PE loader: 600 frames 0.1913 s -> **0.1833 s (-4.2 %)**, 3,000
  frames 0.5345 s -> **0.5071 s (-5.1 %)**; eight fixtures are 22/22 PPM
  byte-identical (`docs/PERFORMANCE.md`).
- **Submission material refresh**: both upstream PR branches were rebased onto
  the current masters (`libretro-super` `55e50a81`, `libretro/docs`
  `9399ee32`); the tracked patches, bundle baselines, README, PR drafts and the
  posted PR bodies are in sync.  No upstream merge claimed.
- No behaviour, save-format or metadata changes beyond the version stamp; the
  libretro core gains the same runtime optimization.

## Artifacts

Eight libretro cores (`linux-x86_64`, `linux-aarch64`, `linux-armv7`,
`android-arm64`, `android-armv7`, `android-x86_64`, `windows-i686`,
`windows-x86_64`; `.dll`, `.so`), each `.info` identical to
`src/port/libretro/nano_j2me_libretro.info`
(SHA-256 `85f96f9526abd3073d0b60db153c8c1362b0131e434ef3fd7e6555e0fbf97ca7`):

| target | file | bytes | sha256 |
| --- | --- | --- | --- |
| linux-x86_64 | `nano_j2me_libretro.so` | 3,848,064 | `da3093f0dd9de8a1412d1f6078c84c11fd79f0ba66c306567288bff679a5d85b` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,331,792 | `17461bfab128c71fac1330f7ddb12a5ab578940560615d0aa1f32f766820f529` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,039,216 | `d1456f309ca8b93221c042a740786bffde69edeeb14f2b7b0756723e1fd62866` |
| android-arm64 | `nano_j2me_libretro.so` | 3,566,448 | `5ee3b2b97f32fc7750f2118c7671ac4ad044f0d67607b898b7f4975938828775` |
| android-armv7 | `nano_j2me_libretro.so` | 2,751,760 | `0607c5365c87e9273e3926ebe8a3fc37db2b653f7942ef0a41b4c774ef5efbdd` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,606,968 | `8607eac5cf10e078a69b7d806e4b43f6232f3a724f7165082f0559c3422c5e4b` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,460,096 | `7ff8fa2cf9173e91347fe420151c60b8ae042de9926fa84b5b2ade4f35b5bee1` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,235,840 | `c722d746ecfa4c57f66776146a656d352cbe0b8805929e436562bbcc0e8cf96a` |

Release zip: `nj2me-0.5.18-libretro-cores.zip` 11,351,007 B
SHA-256 `2c35d71b3415b84d681c4ceaf757ce69140cb94b1827adca08517548672bea33`.

Android: `nj2me.apk` 3,295,594 B
SHA-256 `9ab524b1eb9e9b1759aed27108e967052adaa49b0c364347996b4ea26b583204`
(versionCode 518, versionName 0.5.18, ABIs `arm64-v8a armeabi-v7a x86 x86_64`,
minSdk 21); `nj2me-legacy.apk` 704,234 B
SHA-256 `5de5fcacc2b8665b8e8eb478d172a6d19a1ce9d3426f9787c006c86dd3a5173e`
(versionName 0.5.18-legacy, minSdk 5).

## Verification

- Full serial CTest on the 0.5.18 tree: **198/198 passed in 360.07 s**.
- libretro mock frontend matrix: **20 passed, 0 failed, 0 skipped**.
- Real RetroArch 1.22.2 headless LoopDemo 120 frames: **PASS** (core log
  markers verified).
- Windows x86_64 core under Wine 11.22: **4 passed, 0 failed** at 300 frames.
- Submission bundle: **32 passed, 0 failed**; with clean upstream clones and
  `mkdocs` → **41 passed, 0 failed**.
- Byte-exactness of the optimization: 22/22 PPM identical fixture dumps and
  identical deterministic Minecraft counters (`docs/PERFORMANCE.md`).

## Upgrade notes

- Save states from 0.5.8-0.5.17 are refused by the version code in the state
  envelope; states written by 0.5.18 and newer are accepted by later versions
  unless the format version changes.
- The Android cheat editor and value search from 0.5.16/0.5.17 are unchanged.
- The libretro `.info` and buildbot integration are in the open PRs
  (libretro/libretro-super#2204, libretro/docs#1239); nothing is merged
  upstream at the time of this release.
