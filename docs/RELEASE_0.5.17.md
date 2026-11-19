# nano_j2me 0.5.17

Released 2026-11-19. This release polishes the in-game cheat tools: the
candidate list from the value search now shows the **live value** at every
candidate address, and picking an address keeps exactly one line per offset in
the cheat file.

The libretro core is feature-identical to r35/0.5.16; only the Android panel and
the shared metadata changed, so save states stay compatible with 0.5.16.

## Highlights

- **Candidate values in the Android cheat search.** After a value search, the
  candidate dialog reads each offset's current bytes through `nj_vm_cheat_read`
  and renders `0x1F0  = 1 (0x1)`. The player can see which address still holds
  the number on screen and which ones changed since the scan.
- **Writes use the live value.** Tapping a candidate generates the code from the
  value read at that moment, not from the number typed for the search.
- **One line per address.** Re-picking an offset rewrites its line instead of
  appending duplicate codes that would fight over the same bytes.
- Everything from 0.5.16 remains: core cheat-search API, in-game cheat editor,
  multi-touch, memory-map cheat search in libretro, rewind sizing, save-state
  buffer fix, and the M3G fast paths.

## Libretro package r36

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3,848,064 | `1bf54556c63a37d07e122f6a4cbd3713874a19b20caeedede506bdd48dc04b62` |
| linux-aarch64 | 3,331,296 | `615a6096b92e53ba07c2260753d33cf3c9935919ecc74a9ea04b9fcce3636435` |
| linux-armv7 | 3,038,512 | `03ddbbff442074726b021080f1bfb9101500e269bd3aa7fd5ce1cc652fcc4813` |
| android-arm64 | 3,565,904 | `321a3cf14fac7a9e9c64d065cd6b9e3a43a14b6ffb4d14f37da4893a6316b356` |
| android-armv7 | 2,751,136 | `d31cbb80a6b70868cfe35000f65c69fa189ff57b36551c926b401c223e20a711` |
| android-x86_64 | 3,606,040 | `5fbd02a5f648286d5937e542f1cea6316e2d2aee5aa14581cbbfc65bddf50c52` |
| windows-i686 | 3,459,584 | `561ad8726dfed1c6f4f3eef438411db2814ed0273950b0e8866f208d1d8aea43` |
| windows-x86_64 | 3,235,328 | `e31236e1eb7784b2a343bf9dda5627033473e9703dc4e2f766895a9a2f1ad91e` |

The `.info` in every directory is byte-identical with SHA-256
`93c4862a4974e134aaf3311b0e1be811dd1c8f1d8962881ed5c794b043525b85`
(`display_version = "0.5.17"`).

## Android APKs

| file | bytes | SHA-256 |
| --- | ---: | --- |
| `nj2me.apk` (`versionCode 517`, `versionName 0.5.17`, arm64-v8a / armeabi-v7a / x86 / x86_64, minSdk 21) | 3,295,594 | `b5e8dc8190d2a9924c996e020dc9a7de86212566966ec8696d8c291ff1efc081` |
| `nj2me-legacy.apk` (`versionName 0.5.17-legacy`, minSdk 5) | 704,229 | `e4158a2f6c7c2baa329b40693fd4d42a295ac75433a847770746d4cabe4f71e8` |

## Verification

- Mock libretro frontend conformance matrix: **20 passed, 0 failed**.
- Real RetroArch 1.22.2, LoopDemo 120 frames: **PASS**.
- Windows x86_64 core under Wine 11.22: **4 passed, 0 failed** at 300 frames.
- Submission bundle check: **32 passed, 0 failed**.
- Full serial CTest on the bumped tree: **197/197 passed in 347.29 s**.
- Android: `tools/check_android_java.sh` 33 checks; in-game candidate dialog
  verified on the API 36 emulator (list shows `0x1F0  = 1 (0x1)`, tapping wrote
  exactly one `1F0:01` line and reloaded **1 code**).

## Release package

`nj2me-0.5.17-libretro-cores.zip` (11,347,105 B), SHA-256
`5876c9f162aa528d0186fca3fcc70d2015f61b637bc99422da84f82c6d122651`,
containing all eight core directories and their `.info` files.

## Upgrades

- Save states: 0.5.16 and 0.5.17 use the same state format; states from 0.5.8
  through 0.5.15 are still refused by the version CRC.
- Libretro core: identical features to r35; frontends and cheat-search
  behaviour are unchanged.
- Android: install over 0.5.16 (`versionCode 517`); existing jars and cheat
  files stay in place.

## Upstream submission

- `libretro/libretro-super#2204` — `info: add nano_j2me (Mobile - J2ME)`
  (OPEN / MERGEABLE, branch `add-nano_j2me` in `lucinhu/libretro-super`,
  rebuilt on the current master with the 0.5.17 `.info`).
- `libretro/docs#1239` — `docs: add nano_j2me (Mobile - J2ME)`
  (OPEN / MERGEABLE).
- Both await upstream review; no merge or acceptance is claimed here.
