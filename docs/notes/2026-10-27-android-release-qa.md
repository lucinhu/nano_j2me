# Android release APK 0.3.0, end-to-end on an emulator (2026-10-27)

The point of this run is that it uses the **shipped** universal APK, not a
debug build: `nj2me-0.3.0-android-universal.apk`, SHA-256
`92dacdf2a4666a0caea049867dbc9ded0f9ba52eaada7a26d1fa54a524d56307`,
byte-identical to the GitHub release asset (the release QC downloaded and
compared all four assets).  Package `com.lucinhu.nj2me`, `versionCode` 30,
`versionName` 0.3.0, `minSdkVersion` 21, `targetSdkVersion` 34.

## Environment

    AVD        NJ_API21 (Android 5.0.2, API 21, x86_64, 320x640, no Google APIs)
    emulator   emulator -avd NJ_API21 -no-window -no-boot-anim -no-snapshot
               -gpu swiftshader_indirect -memory 2048      (KVM available)
    title      tests/games/1942空战_10403.jar (594,069 B)

The AVD has no external volume mounted (`Vold: Failed to find mounted volume
for /storage/sdcard/...`), so a JAR left on `/sdcard` cannot be opened by the
app.  `adb push` into `/data/local/tmp` and a `file://` URI works, because the
path is world-traversable and the file is 0644.

## What was run and what it showed

1. `adb install -r nj2me-0.3.0-android-universal.apk` — `Success`.
2. `adb shell am start -a android.intent.action.VIEW -d
   file:///data/local/tmp/1942.jar -t application/java-archive` — the default
   handler is `LibraryActivity`; the library imports the file and shows the
   "导入成功 / 1942AirCombat / 1942.jar / 它在手机上应该占多大一块屏幕?" dialog.
3. Choosing "用 240x320 开始" starts `GameActivity`.  Logcat:

       I/nj2me: running com.lemonquest.aircombat.GameMIDlet (1942AirCombat supplied by negrohernan 1.0.17)
       I/nj2me: display_init: logical 240x320, window 320x640
       I/nj2me: window buffer 320x640 stride 320 format 1, 4 bytes/px

   The title's own caught `ArrayIndexOutOfBoundsException` in `j.<init>` is
   thrown here exactly as it is on the headless port.
4. Screens (evidence directory below): the ¿SONIDO? prompt with the default
   on-screen keypad; tapping the keypad's soft-left key answers it; the
   MPEG-4 intro of 1942 is shown; after it ends the main menu
   (NUEVO JUEGO / OPCIONES / RANKING / ACERCA DE / SALIR) and the OPTIONS
   screen (EFECTOS / MÚSICA / VIBRACIÓN / AYUDA / VOLVER) are reachable with
   keypad taps and the selected row follows.
5. Audio: while the menu is up, `dumpsys media.audio_flinger` reports five
   AudioTrack sessions for the app's uid and `3 Tracks of which 2 are active`,
   so the sampled/MIDI path is live.  The emulator has no host audio output,
   so this proves playback is *submitted*, not that the samples are correct.
6. `logcat`: no FATAL and no uncaught throw from the runtime.  The repeated
   `Character ﻿ not found` line is the title's own (it prints it for
   characters missing from its bitmap font).

## Limits of this evidence

- The emulator's `screencap` returned identical pixels for three captures two
  seconds apart while the intro movie was playing, so this run does not prove
  video *motion* on Android; it proves a decoded frame is displayed where the
  title put it.  Motion is covered by the headless and SDL2 m4v evidence.
- Menu navigation was driven by `adb input tap` on the default keypad layout.
  Held-key driving (car steering, movement) was not attempted.
- The image had a previous session's `com.example.nj2me` package installed; it
  was uninstalled first so the VIEW resolver listed one handler.  The AVD was
  shut down after the run; no APK/JAR was left installed as a permanent test
  fixture.

## Reproduce

    export PATH=$HOME/Android/Sdk/platform-tools:$PATH
    emulator -avd NJ_API21 -no-window -no-boot-anim -no-snapshot \
             -gpu swiftshader_indirect -memory 2048 &
    adb install -r nj2me-0.3.0-android-universal.apk
    adb push tests/games/1942空战_10403.jar /data/local/tmp/1942.jar
    adb shell chmod 644 /data/local/tmp/1942.jar
    adb shell am start -a android.intent.action.VIEW \
        -d file:///data/local/tmp/1942.jar -t application/java-archive
    # tap the library's "用 240x320 开始" row, then the keypad's soft-left key

Evidence: `.local-tools/debug-runs/android-release-0.3.0-qa/` — six
screenshots, `apk-badging.txt`, `apk-sha256.txt`, `device.txt`,
`logcat-filtered.txt`, `audio-flinger.txt`.
