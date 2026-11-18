# nano_j2me 0.5.14

Released 2026-11-19. This release shares one cheat engine between the runtime,
the libretro core, the headless CLI and the Android build, and lets the APK read
cheat codes from a text file. The emulator core is otherwise the 0.5.13 release
(with the same multi-touch, state and rendering work).

## Highlights

- **One cheat engine for every frontend.** The cheat parser and table moved
  from the libretro port into the runtime (`src/core/nj_cheat.c`), so the same
  `<arena offset>:<hex bytes>` codes now work in the libretro core, the headless
  CLI (`--cheat "<code>"`, repeatable, and `--cheat-file <path>`, one code per
  line with `#` comments) and the Android APK. Table per VM: up to 32 codes of
  16 bytes, re-applied at every frame start so a game writing the old value back
  does not cancel the cheat. See [LIBRETRO.md](LIBRETRO.md#cheats) and
  [the design note](notes/2026-11-19-core-cheat-engine.md).
- **Android cheat files.** The APK has no cheat UI; it looks for
  `<jar dir>/cheats/<stem>.txt`, then `<files dir>/cheats/<stem>.txt` and
  `<rms dir>/cheats/<stem>.txt` on the run thread before starting the MIDlet.
  Push the file with adb or a file manager; a missing file means no cheats.
- **New test.** `tests/test_cheat.c` (33 checks: parsing, slot management,
  apply/re-apply, bounds, file load) runs as CTest `test_cheat`.

## Libretro cores (r33)

Eight targets built from the `0.5.14` tree. All eight are stripped and carry the
same `.info` (SHA-256
`497b24a6eff931e6cf109ba1f582d8a5c4c477b906ee5b99326cbcdb50ab2fd8`).

| target | file | bytes | SHA-256 |
|---|---|---|---|
| linux-x86_64 | `nano_j2me_libretro.so` | 3,848,056 | `e101c57322c5e8339a483849dec67c2035b2f02d795bd0be22845c36a61a7ef4` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,330,056 | `5dd50fd7c755e47f2fabeb5816ec50feabfbe73808d3f58bedd4d7e5406f89a0` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,037,516 | `3f8c3735f877b4800d0989ae035d54e577c409551fcf0a7d597c9a4e54685d4e` |
| android-arm64 | `nano_j2me_libretro.so` | 3,564,440 | `3d7fbc0e78dbd677e19ae0752d089aae37e926c990db80c3f525bb24a79ece06` |
| android-armv7 | `nano_j2me_libretro.so` | 2,750,188 | `8086f46d93ee2c401bcdf02fe21665a0420a74561bf1af6cad71e2d8b0051e6e` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,604,560 | `1cdc779ea33e430fc0450df3b382514b4fbddcd5065899d74a7724062d70c2f0` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,458,560 | `a621e8597ebb8faa74fb16784e689ca284b5b0d9d742ada6d65a6b3bdd992fbc` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,234,304 | `56dba9dd9883a386e62911d643930374e7c967bfc7caa64ab5610195b2b74d6e` |

## Android APKs

Debug-signed, no game bundled.

- `nj2me.apk` — 3,283,306 bytes, versionCode 514, versionName `0.5.14`,
  ABIs arm64-v8a, armeabi-v7a, x86, x86_64.
- `nj2me-legacy.apk` — 702,555 bytes, versionName `0.5.14-legacy`, API 5,
  ARMv5 (old devices, reduced feature set).
- Cheats: see the file locations above; there is no in-app cheat editor.

```sh
sha256sum nj2me.apk             # 942821e997b167640f11c4a1f141e557b71aa8b1727e38e72da4c639e3e6edf3
sha256sum nj2me-legacy.apk      # 0ce77956a46ee999b5f49fa124c22da79a9c3aa8236f9278fafc4be7f4b5cc05
sha256sum nj2me-0.5.14-libretro-cores.zip
                                # 65eceb678860caf2c74b00cfd8b7888e98a15f135e0731b0d038a8b3a8e56878
```

## Verification

- Mock libretro frontend matrix: **20/20 passed** (linux-x86_64 core).
- Real RetroArch 1.22.2, LoopDemo 120 frames: PASS.
- Windows x86_64 core under Wine, 300 frames: **4/4**.
- Option matrix (6 titles x 11 profiles): **66/66 passed**; libretro state round
  trips: **12/12**.
- Full serial CTest: **197/197 passed in 366.35 s**.

## Upgrade notes

- Save states: the state envelope carries a version CRC, so states from earlier
  releases are refused rather than restored incorrectly.
- The cheat format is unchanged from 0.5.13; only its implementation moved.
  Codes are raw arena writes and can corrupt objects if the offset is not from
  a deliberate cheat search. A state restore replaces the arena.
- Upstream submission: `libretro-super` and `libretro/docs` pull requests are
  open; the pull requests are refreshed to 0.5.14 metadata. No merge is claimed.
