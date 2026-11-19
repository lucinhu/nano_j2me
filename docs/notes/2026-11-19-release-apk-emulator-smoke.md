# Release APK smoke test on the Android emulator (0.5.18)

The release APK is what the user actually installs, so it is smoke-tested as a
*release artifact* (not a debug build) after each release tag.  This note
records the 0.5.18 run.

## Artifact

- `build/android/nj2me.apk`
- SHA-256 `9ab524b1eb9e9b1759aed27108e967052adaa49b0c364347996b4ea26b583204`
- This is the exact file published in the 0.5.18 GitHub release
  (`SHA256SUMS-0.5.18.txt`).
- `aapt2 dump badging` reports `versionCode='518' versionName='0.5.18'`,
  ABIs `arm64-v8a armeabi-v7a x86 x86_64`, minSdk 21.

## Environment

- AVD `Medium_Phone_API_36.1` (Android 16 / API 36), headless
  (`-no-window -no-audio -no-boot-anim -gpu swiftshader_indirect -no-snapshot`).
- `adb install -r build/android/nj2me.apk`.
- Fixture JARs pushed into the app-owned external directory
  `/storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/`
  (`loopdemo.jar`, `multitouchdemo.jar`).  The directory must be created by the
  app itself: an `adb shell mkdir`-created directory is owned by `shell` and
  the library shows "0 个游戏".

## What was verified

1. Library lists both fixture games (`J2ME 运行时 · 2 个游戏`).
2. Launching a game starts the runtime:
   `running LoopDemo (LoopDemo ?)`, `display_init: logical 240x320, window
   1080x2400`, `window buffer 1080x2400 stride 1080 format 1, 4 bytes/px`;
   the game renders (canvas plus the on-screen keypad).
3. The in-game panel (`≡`) contains, in this order, the cheat and state rows:
   `作弊码` (in-place cheat file editor), `搜索作弊数值` (numeric value search),
   `隐藏菜单按钮`, `保存状态`, `退出游戏`.
4. `保存状态` writes the snapshot next to the JAR:
   `state: save to /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/multitouchdemo.jar.state -> 0`
   and the file appears (680,456 bytes for that 240x320 fixture).
5. The game card options show `恢复进度` *only when* a state file exists, and
   tapping it launches with a restore:
   `resumed MultitouchDemo from /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/multitouchdemo.jar.state`.

## Notes

- The panel's `退出游戏` row opens a confirmation dialog; `KEYCODE_BACK` in a
  confirmation closes the dialog, not the game.
- Cheat editor / cheat search flows were verified on the emulator in round 100
  (`docs/notes/2026-11-19-cheat-search.md`); this smoke test is about the
  release artifact plumbing (install, library, launch, state save/restore).
- The emulator is killed after the run (`adb emu kill`); `pgrep -f
  "qemu-system.*Medium_Phone"` self-matches the invoking shell, so use
  `ps -ef` when checking for leftovers.
