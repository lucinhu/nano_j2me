# Android: cheat codes from the in-game panel (2026-11-19)

The cheat engine has been shared between the runtime, the CLI and the libretro
core since 0.5.14 (`src/core/nj_cheat.c`). On Android it could only be used by
pushing a text file next to the game; this note documents the in-app editor
that closes that gap: the game's panel has a **作弊码** row that opens the
cheat file for the running title, saves it, and reloads the table without
restarting the game.

## Where the file lives

The editor writes `<app external files>/jars/cheats/<jar stem>.txt` — next to
the game itself (`JarStore.dir()` + `cheats`), so it is reachable over adb:

```sh
adb shell cat /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/cheats/loopdemo.txt
```

On startup the port looks for the file in this order
(`android_resolve_cheat_file()` in `src/port/android/nj_port_android.c`):

1. `<jar dir>/cheats/<stem>.txt`,
2. `<jar dir>/files/cheats/<stem>.txt`,
3. `<jar dir>/rms/cheats/<stem>.txt`,
4. `<files dir>/fs/cheats/<stem>.txt` (the runtime's `fs_dir`),
5. `<files dir>/cheats/<stem>.txt` (where the first version of the editor
   wrote; kept as a fallback).

The format is the runtime's one code per line:

```
# comments start with #
<arena offset in hex>:<hex bytes>
1A2B0:05
```

The arena is the same window the libretro core exposes as `SYSTEM_RAM`; a
frontend cheat search is still the way to discover offsets (the APK itself
does not search), but the file can be edited and re-applied live.

## Editor flow

`GameActivity`'s panel (`showPanel()`) got one row after the graphics details:

- row title **作弊码**, hint either "已启用 `<file>`：点开可以编辑并立即重新加载"
  (file exists) or "点开编辑这个游戏的作弊码文件（每行一条，例如 `1A2B0:05`）";
- tapping it opens a monospace multiline `EditText` prefilled from the file
  (hint repeats the format), with buttons **保存并重新加载** / **清空** / **取消**;
- saving writes the file as UTF-8 and then calls the new JNI
  `nativeCheatReload(handle, path)`; the port serves it at the next frame
  boundary (clear table → `nj_vm_cheat_load()` → `nj_vm_apply_cheats()`), logs
  `cheats: reload from <path> -> N code(s)`, and returns N so the UI can toast
  "已重新加载 N 条作弊码" (or "作弊码已清空" for 0). The wait is bounded
  (4 s) exactly like `nativeSaveState`, so a stalled VM cannot hang the UI.

## Evidence (emulator, Medium_Phone_API_36.1, Android 15/16 image)

- Built `tools/build_android_apk.sh` (4 ABIs, 3,283,306 B) and installed it;
  `classes.dex` contains `作弊码` and `nativeCheatReload`.
- Opened LoopDemo through the library, opened the panel with the ≡ handle,
  scrolled to the row and opened the dialog (screenshot kept during the run).
- Typed `0:1234`, hid the IME, tapped **保存并重新加载**:

```
I nj2me : cheat file /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/cheats/loopdemo.txt:1: slot 0, 0:1234
I nj2me : cheats: reload from /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/cheats/loopdemo.txt -> 1 code(s)
```

- Force-stopped and relaunched the game; startup picked the file up:

```
I nj2me : cheats: /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/cheats/loopdemo.txt
I nj2me : cheat file ...:1: slot 0, 0:1234
W nj2me : applied 1 cheat code(s), re-applied at every frame
```

- **清空** writes an empty file and reloads it:
  `cheats: reload from ... -> 0 code(s)`.
- After the file exists the row hint reads "已启用 loopdemo.txt：点开可以编辑并立即重新加载".
- `tools/check_android_java.sh` → 33 checks passed (includes `PadPressTest`);
  `tools/build_android_legacy.sh` → `nj2me-legacy.apk` 702,868 B builds;
  `check_android_java` and `check_port_sources` ctest gates pass.

## Emulator notes for the next person

- `adb shell mkdir` for the app's `files/jars` directory creates it owned by
  `shell:ext_data_rw` and the library shows `0 个游戏`; let the app create it
  (force-stop → `rm -rf` the directory → relaunch), then `adb push` the jar.
- The headless emulator's hwui occasionally aborts the app with
  `drawRenderNode called on a context with no surface!` when dialogs are
  opened/dismissed in quick succession; retry from a clean launch — that
  backtrace lives in `libhwui`, not in the runtime.
- After tapping the `EditText` the keyboard shifts the dialog: dump the UI
  again (or hide the IME with `input keyevent KEYCODE_BACK`) before tapping
  **保存并重新加载**, otherwise the tap lands on the keyboard.
