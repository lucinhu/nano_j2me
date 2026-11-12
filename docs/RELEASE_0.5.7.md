# nano_j2me 0.5.7 — Android legacy edition

This release adds a separate APK for **Android 2.0 and later (API 5)** on
**ARMv5/armeabi** phones, including Android 2.1 and 2.3. It shares the J2ME
runtime and launcher icon with the standard Android edition.

## Downloads

- **Android 2.0+ ARM:** `nj2me-0.5.7-android-legacy-armeabi.apk`.
- `SHA256SUMS` and `BUILD-INFO.json` record the download hashes, source revision,
  build tools, signing certificate and validation results.
- Standard Android 5.0+, desktop and libretro downloads remain available in
  [0.5.6](https://github.com/lucinhu/nano_j2me/releases/tag/v0.5.6).
  This release's binary download is the Android legacy edition.

## What is included

- A lightweight launcher, software Canvas/RGB565 display and AudioTrack sound.
  The native library uses ARMv5TE with software floating point and API 5 libc
  stubs, without dependencies on ANativeWindow, OpenSL ES or AAudio.
- An on-screen phone keypad, physical keys, screen-size and heap settings,
  per-game RMS saves and fast VM snapshots.
- JAR import from the built-in storage browser, manual paths or an installed
  system file picker. Imported files are copied into app-private storage.
- Opening a JAR from a file manager through `ACTION_VIEW` with `file://` or a
  readable `content://` URI, and receiving shared JARs through `ACTION_SEND`.
- Content-based deduplication reuses the existing game path and saves.
  Different contents with the same filename receive separate names. Archive,
  manifest and checksum validation reject invalid or incomplete imports.
  Limits are 64 MiB compressed, 256 MiB total expanded data and 65,536 entries.
- The same mdpi/hdpi launcher icon pixels as the standard edition.

## Installing and upgrading

The APK has versionName `0.5.7-legacy`, versionCode `57`, minimum/target SDK `5`,
a DEX 035 program and a v1 signature understood by Android 2.x. Its signing
certificate matches the preceding published Android packages and the earlier
local legacy builds.

The package name is `com.lucinhu.nj2me`, shared with the standard edition.
Installing it replaces that edition. Keep an existing installation and upgrade
in place to preserve app-private saves; uninstalling removes those saves.
Identical JARs reuse their existing library paths. Renaming or moving a JAR
changes the canonical-path-based save directory.

## Validation and limitations

The legacy frontend has been exercised on archived ARM system images for
Android 2.0, 2.1 and 2.3.3. Tests cover game input, snapshot saving and restoring,
normal exit, JAR import, external file/content intents, sharing, permission
errors and filename collisions. The Java sources compile against the actual
API 5 framework; the packaged raster icons match the standard edition.
`BUILD-INFO.json` records checks performed on the final release artifact.

These are emulator results. Physical-phone speed, memory capacity and audible
sound output remain to be checked. Some games need more memory than an old
phone can provide. This edition targets offline games and built-in codecs; it
does not include the standard frontend's editable floating keypads or all of
its Android media/network bridges. ARM64-only and x86 devices without 32-bit ARM
support cannot load the legacy library. Use the standard APK on Android 5.0+
where possible.

Build and usage details: [Android legacy guide](ANDROID_LEGACY.md).
