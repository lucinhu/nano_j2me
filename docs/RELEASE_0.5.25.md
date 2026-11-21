# nano_j2me 0.5.25

Android APKs (per ABI), the libretro cores for eight targets and the
compatibility evidence for the 2026-11-22 corpus.

Release date: 2026-11-22

## Highlights

### J2ME API coverage

* **`resource:` streams** — `Connector.openInputStream("resource:<entry>")`
  (and `openDataInputStream`) now serve JAR entries. This was a real missing
  feature: nine corpus JARs reference the scheme, and it blocked titles such
  as Wizardry from loading their sprite data.
* **Siemens `color_game` / `game` APIs** — `Layer`, `LayerManager`, `Sprite`,
  `ExtendedImage` and `Light`, matching the OEM reference semantics used by
  the Siemens handset SDK. `LayerManager.paint()` composes its layers with the
  reference clip/translate bookkeeping; `Sprite` draws single frames through
  `drawRegion`; `ExtendedImage.blitToScreen()` writes into the current canvas
  back buffer.
* **Immersion VibeTonz** — honest no-motor implementation of the vibration API
  (`com/immersion/VibeTonz`).
* `nano_j2me/ResourceStream` implements the handset stream contract the
  Siemens resource loader depends on: a ranged read returns 0 (not -1) when
  the current chunk is exhausted, while single-byte reads keep the standard
  -1 EOF. `openDataInputStream` stays standards-compliant.

Two previously failing corpus titles now run: **Wizardry** (menu, gameplay
frames) and **Dig Dug** (`Dig_Dug_240x400`).

### Compatibility corpus

* The published compatibility matrix now merges the 2026-11-22 combined sweep
  (`tests/games` + the fetched cache) with the historical table:
  **1502 titles, 1482 running normally**.
* Wizardry and Dig Dug were re-measured as `ok` after the API work.
* A curated archive.org item list (`tools/corpus_items.txt`) and
  `fetch_corpus.py --items-file` make the fetched corpus reproducible; the
  cache lives in `~/.cache/nano_j2me/corpus` and the sweeps pick it up
  automatically.

### Other

* The sweep and matrix generators now reject unknown options instead of
  treating `--help` as an output directory.

## Verification

* Full serial CTest on the release tree: **203/203 passed** (370.74 s),
  including `test_siemens` (LayerManager/Sprite/ExtendedImage/`resource:`
  stream fixture, pixel-checked) and all previous gates.
* Release-core checks on the r44 binaries: mock frontend matrix **20/20**;
  real RetroArch 1.22.2 `LoopDemo` 120 frames **PASS**; the real-RetroArch
  corpus probe (8 titles, 2D/3D/audio/save-state users, 600 frames each)
  **8/8 PASS**; Windows x86_64 core under Wine **4/4** at 300 frames;
  submission bundle **32/32**.
* Whole-corpus libretro visual probe (300 frames, 8 jobs, paced 16.67 ms):
  **717 titles → 711 passed, 3 known limitations, 3 failed**. The two render
  failures are the documented black titles (`NeonRunner`, `GoldenSolitaire`);
  the third row failed only its save check under 8-job load and passes all
  45 checks in 11 repeated isolated/parallel runs, so it is recorded as a
  probe-time flake, not a save-state regression.
* CLI smoke: `loopdemo`, `alphademo`, Wizardry and Dig Dug run clean; the
  Wizardry menu was captured as frame evidence during the round.

## Artifacts

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| `nj2me-0.5.25-libretro-cores.zip` | 11,623,703 B | `30efc4a2c442950de90c2669fc40c93ee86846a8aa8538995e1a44b046e3a85a` |
| `nj2me-0.5.25-arm64-v8a.apk` | 1,005,724 B | `f4f3b21c69de5b1447faaf8015cf98a9036f9d45d34d2d6baf6f641ddccd1dae` |
| `nj2me-0.5.25-armeabi-v7a.apk` | 1,013,918 B | `a276b00c4dd6ae0c1eea4435294205a09d940a23b00f7cf104f80d99de358903` |
| `nj2me-0.5.25-x86.apk` | 989,334 B | `f2e6cd9c6eddd9daacf181f4d0971da510e07371e520ade23d1e387f297b2641` |
| `nj2me-0.5.25-x86_64.apk` | 1,005,721 B | `d6f8dd1eb6705779f246e62833302015aeb75cc93dc3eabf33ec6c9befb19964` |
| `nj2me-0.5.25-legacy.apk` | 729,500 B | `ab5d331f2394fcae7ed681a359a2a154cecd3334c0387e9313c19b296c9dd647` |

APK versionCode 525, versionName 0.5.25 (legacy: 0.5.25-legacy, ARMv5/API 5);
each non-legacy APK contains exactly one ABI.

Core artifacts inside the zip (`.info` SHA-256
`057df4d930ca66d145e6e1e8c2ee395eed77814af8d352daa852df63d3dfd7bf`):

| Target | Size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,983,280 B | `d11eb5ce7434b413df877d2e1c9fed06ee887e86afb170a17c69e965ced150df` |
| linux-aarch64 | 3,461,520 B | `1ee7ed7126176952a90a53b506eaa7aed0d3fd843d68c4be125ff8730bc9b9f9` |
| linux-armv7 | 3,133,388 B | `ddcc7eee2eca525b8ccf3164de381edc24d04da2afa70a395a56b95e3bfeedf7` |
| android-arm64 | 3,702,792 B | `c3e981d35c923619bb1d3695c856e87074ff99db82c7efff71547dace94dae75` |
| android-armv7 | 2,828,572 B | `9c512d5b70aabfe7442bdddf4cc80491e7620fab920ac710d747b0834f885a65` |
| android-x86_64 | 3,734,224 B | `91f0e0be437ffbef4363a22a751c6738b65d37bc03e14e98c0d93d83c43080d1` |
| windows-i686 | 3,557,376 B | `ee06234d2a32a22037939b6806374515c59e03899094f47a87b84249cc29c583` |
| windows-x86_64 | 3,329,024 B | `de97ec787d28b342fdb825b07c82ca311806c57dad0fc593fffe96b90c4c70e1` |

## Upgrade notes

* Save states created by 0.5.8 through 0.5.24 are refused by the 0.5.25 core
  (state format version CRC), as before; export/import through in-game saves
  or start a fresh state.
* Android releases ship per-ABI APKs; install the one matching the device
  (`arm64-v8a` for current phones, `armeabi-v7a` for 32-bit ARM, `x86`/`x86_64`
  for emulators, plus the separate `legacy` APK for ARMv5/API 5 devices).
* The cheat engine, SYSTEM_RAM memory map, multi-touch pointer reporting and
  the libretro core option set are unchanged.

## Upstream

The tracked libretro-super patch was regenerated against `libretro-super`
master `55e50a81195eaf8dd1b7720a601282459832ced1` for the 0.5.25 `.info` and
the fork branch is refreshed so it can be re-published under PR
libretro/libretro-super#2204; the libretro/docs page PR is
libretro/docs#1239. Neither PR is merged.
