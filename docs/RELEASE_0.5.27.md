# nano_j2me 0.5.27 — libretro core r46

Release date: 2026-11-22

This release adds the Sprint PCS carrier API (which unlocks the Pac-Man
mobile archive), tightens the corpus triage rules, and refreshes the whole
fetched-corpus evidence.  Same libretro feature surface as 0.5.26.

## Highlights

* **Sprint PCS carrier wrappers** — new `src/midp/nj_lib_sprint.c` implements
  `com.sprintpcs.media.{Clip,Player,PlayerListener,Vibrator}` and
  `com.sprintpcs.util.{Muglet,System}`, the exact member surface seven corpus
  JARs reference.  The Pac-Man family (five builds, 128x160 through 240x400)
  now links and paints; it was previously `title-exception`/`failure` because
  `MsPacman` died on `NoClassDefFoundError`.  The wrapper is deliberately
  silent (the titles pass `resource:` URIs the media Manager cannot open as
  audio) and exposes no vibration motor; both are documented limitations.
* **Corpus triage refinement** — `tools/sweep_compat.py` now retries a
  `title-exception` row once without input, mirroring the existing
  `title-exit`/`timeout` rules.  Six TRANSFORMERS builds draw their menus and
  reach 600 frames, but a scripted tap threw inside game code; all six are
  correctly classified as running titles now.  The same pass also fixed the
  Ms. PAC-MAN demos and re-measured the 316 newly fetched archive.org JARs.
* **Compatibility matrix** — merged to **1817 titles, 1796 `运行正常`**
  (98.9%), byte-deterministic, with the remaining non-ok rows individually
  explained (title-side save data, missing bundled classes, licensing exits,
  self-paced 3D we cannot accelerate from the runtime).
* **Full validation** — CTest **206/206**, mock frontend matrix 20/20, real
  RetroArch 1.22.2 LoopDemo PASS, Windows Wine 4/4 at 300 frames, submission
  bundle 32/32.

## Artifacts

Linux:

| target | size (bytes) | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3991472 | `7db83b48f1e3ffbc51327afc7b508c5ec1e8d2268e88a383be24643c27e1d30a` |
| linux-aarch64 | 3470584 | `624f50af400818af8a886d962c75df2e9045942d4aae0d07fb72a0c1e13e21ea` |
| linux-armv7 | 3138492 | `2321892688c17a7373b88837ffade9f60a618f6ba2efb3c22c78f5697656638b` |

Android (16 KB page size, SDK 30 toolchain, NDK r30):

| target | size (bytes) | SHA-256 |
| --- | ---: | --- |
| android-arm64 | 3712016 | `f342eed6b6069fa0873eb9b8b3dafd6e91b1fc0a4dfcb3e16975dbf6512a4444` |
| android-armv7 | 2833088 | `49c1a9fc5eb757eb921c403b59c4451f2ff97aedde615a4afc56e6c251232c02` |
| android-x86_64 | 3743112 | `4597ee2a0d7c442a0fdf38d032210d4d15249f2a629e4b4561c433632ee2e4a0` |

Windows:

| target | size (bytes) | SHA-256 |
| --- | ---: | --- |
| windows-i686 | 3563008 | `15ed44eea82c7860bc6cec21c80a67f921951add3ff673c55490293f5b8dc50b` |
| windows-x86_64 | 3334656 | `7f0e0ca37dba0bf7eb471aac8013684cc2a991088ce1c555d06f3bc6fe83b3ca` |

The `.info` file is identical in every directory (SHA-256
`ad619b1aa09830cad2af82ff59d87616774da993f57e27b4075109716c942873`,
`display_version = "0.5.27"`).

## APK split by architecture

One APK per ABI; no universal APK in this release.

| APK | size (bytes) | SHA-256 |
| --- | ---: | --- |
| nj2me-0.5.27-arm64-v8a.apk | 1005724 | `e971553921677b303bb2573662bc0cbd96800de324c46b3dc4d8c5922f79a725` |
| nj2me-0.5.27-armeabi-v7a.apk | 1013918 | `3740851a5af945f319acbb28f2af469e8b8b069316e6c403ed27810d53b70a3d` |
| nj2me-0.5.27-x86.apk | 993430 | `a59cdc00fcabd8ba6a66b24b24f5477bc3282a01246624501d0e1415eddd3262` |
| nj2me-0.5.27-x86_64.apk | 1009817 | `19314ef1a6f33f8c7ed03cd8fd26f6768591acbd602d44807c4f80ecaf52428c` |
| nj2me-0.5.27-legacy.apk (ARMv5, API 5) | 730792 | `758cfce4c6df92eb1fe84c9fbea97e88115a381a87297707a93289f3491b7743` |

All APKs are debug-signed, `versionCode 527`, minSdk 21 for the four modern
ABIs (legacy is minSdk 5), and ship **no** game content.

## Verification

* Full serial CTest: **206/206 passed** (includes the new `test_sprint`
  marker/pixel gate).
* libretro mock frontend matrix: 20/20; real RetroArch 1.22.2 LoopDemo 120
  frames: PASS; Windows x86_64 Wine 4/4 at 300 frames; submission bundle
  32/32.
* Corpus: 735-row full sweep plus a 316-title fetched-corpus sweep; matrix
  regenerated twice with identical output.

## Upgrade notes

* Save states from 0.5.8–0.5.26 are refused by the version CRC; states are
  not portable across releases.
* The Sprint PCS media wrapper plays no audio.  Titles that only use the
  carrier player run silently; their own MIDI/WAV paths are unaffected.
* The core remains `is_experimental = "true"`: no determinism, netplay or
  runahead claim.

## Upstream submission

* `libretro-super` PR: https://github.com/libretro/libretro-super/pull/2204
  (fork branch `add-nano_j2me` at `87b32c6`, parent upstream master
  `55e50a8`).
* `libretro/docs` PR: https://github.com/libretro/docs/pull/1239.
* Nothing is claimed merged; both PRs are open for review.

## Full validation evidence

See `docs/LIBRETRO.md` (release history), `docs/LIBRETRO_SUBMISSION.md`
(packaging/provenance) and `docs/CORPUS.md` (corpus rounds).  Release
checksums are attached next to this file.
