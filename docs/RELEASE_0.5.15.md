# nano_j2me 0.5.15

**Released 2026-11-19.** This release brings the cheat support to the Android
app's own UI: the in-game panel can now open, edit and reload a game's cheat
file while it is running. The libretro core is feature-identical to 0.5.14
(r34 repackages the same core because the version string lives in `.info`).

## Highlights

- **Cheat codes from the game panel (Android).** The panel gained a **作弊码**
  row: it opens `<jar dir>/cheats/<jar stem>.txt` in a monospace editor,
  writes it and asks the running VM to clear, re-read and re-apply the table at
  the next frame boundary, so a code takes effect without restarting the game.
  **清空** writes an empty file and reloads zero codes; the row hint switches
  to "已启用 …" once the file exists. The file is next to the game in the app's
  external files directory, so it is reachable with `adb push`/`adb shell cat`;
  the port also still accepts the runtime's files/RMS directories.
- The same `<arena offset>:<hex bytes>` format is used by the libretro core
  (frontend cheat search supplies offsets) and the headless CLI
  (`--cheat`, `--cheat-file`) — one engine (`src/core/nj_cheat.c`) shared by
  every frontend.
- Everything from 0.5.14 is unchanged: shared cheat engine, multi-touch,
  memory-window/memory-map export, rewind sizing, save-state fixes.

## Artifacts

### libretro cores (r34)

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3,848,056 | `89ab581ee64d99cb0e3e77f63cadd22bef4b0d485474e3280ff6b478260c1c71` |
| linux-aarch64 | 3,330,056 | `7b8d08ad0b08f3a2187e34d2bbdc5b21a7f7171678ee1a7d9eb29596f541ef64` |
| linux-armv7 | 3,037,516 | `722bc149c4b6e08e4f74d4a410228ede260b72be09162439cd4e320b6f4e1b2f` |
| android-arm64 | 3,564,440 | `a5fead1519576a35d898e1716f2f1b4e47c95c7fd1a704fe6c38dd501f3c11e5` |
| android-armv7 | 2,750,188 | `570de7007f501ad9e125c06ef9805057a894ec72edbbb77d700db4c455c0400c` |
| android-x86_64 | 3,604,560 | `05b1161c6c80c74c15a8ae2ad875c3eff9b43971684cd122f29af4549fd5e450` |
| windows-i686 | 3,458,560 | `9112cc0ec9295680ad295fe41d842c8d099dbb0adf5bbfbffbc7fa662761bd51` |
| windows-x86_64 | 3,234,304 | `c5637d075d5d2c9513d1b6372b7700536e66a0fbf3ddabae28efd622b5b88028` |

Metadata `.info` SHA-256:
`d9f107094647cbbd206dbbc3148bfdf032d1341d18e53590939820ebef59f6c4`
(`display_version = "0.5.15"`).

Package: `nj2me-0.5.15-libretro-cores.zip`, 11,340,156 bytes, SHA-256
`f5a0b0562b5048e6955bb928393090f80b35c64103c2f34fcdea655a1bf27899`.

### Android APKs

| file | version | bytes | SHA-256 |
| --- | --- | ---: | --- |
| `nj2me.apk` | 0.5.15 (versionCode 515, arm64-v8a/armeabi-v7a/x86/x86_64, minSdk 21) | 3,283,306 | `a81cceb187a677bfb3bd74f36e2788f4739ce199e78281166f4e3933561aa4f8` |
| `nj2me-legacy.apk` | 0.5.15-legacy (versionCode 515, minSdk 5) | 702,862 | `005ab3891e866860e931c1a086d34c9af889cf441d0fd4bd1a1837220805f7cc` |

`SHA256SUMS-0.5.15.txt` carries the four release-asset hashes.

## Verification

- On an Android emulator (Medium_Phone_API_36.1, Android 15/16 image) with a
  freshly installed APK: launched LoopDemo through the library, opened the panel,
  typed `0:1234` in the 作弊码 dialog and saved →
  `cheats: reload from …/jars/cheats/loopdemo.txt -> 1 code(s)`; relaunched the
  game → startup log `cheats: …/jars/cheats/loopdemo.txt` and
  `applied 1 cheat code(s), re-applied at every frame`; **清空** →
  `… -> 0 code(s)`. Details: `docs/notes/2026-11-19-android-cheat-editor.md`.
- `tools/check_android_java.sh` — 33 checks passed (includes `PadPressTest`);
  legacy APK builds.
- r34 cores: mock frontend matrix **20/20**, real RetroArch 1.22.2 LoopDemo
  120 frames **PASS**, Windows x86_64 Wine **4/4 at 300 frames**.
- Submission bundle self-check **32/32** with the canonical
  `build/libretro-dist` tree.
- Full CTest on the release tree: **197/197 passed in 378.79 s**.

## Upgrade notes

- Save states carry a version CRC: states written by 0.5.8–0.5.14 are refused
  by 0.5.15 and vice versa, as with every metadata bump. libretro core
  behaviour and options are unchanged from 0.5.14, so frontend configurations
  and cheat files keep working.
- The libretro core and `.info` are rebuilt only because the metadata records
  the version; the implementation is identical to 0.5.14 (r33).
- No merge or acceptance is claimed for the upstream submissions:
  `libretro/libretro-super#2204` and `libretro/docs#1239` remain open for
  review and are tracked by the repository's submission kit.
