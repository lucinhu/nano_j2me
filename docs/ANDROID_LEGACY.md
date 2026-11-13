# Android 2.0+ legacy edition

The standard Android APK requires Android 5.0 (API 21). The separate legacy APK
is built for Android 2.0 (API 5) and later, including Android 2.1 and 2.3, on
ARM devices with the `armeabi` ABI. It uses ARMv5TE instructions and software
floating point; ARMv7, NEON and a hardware floating-point unit are not required.
The legacy APK is distributed separately from the standard Android package.
Android 1.x is outside this target.

Download the [0.5.7 legacy APK](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.7/nj2me-0.5.7-android-legacy-armeabi.apk),
or see the [release notes](RELEASE_0.5.7.md).

## Install and use

Build with `tools/build_android_legacy.sh`, then install
`build/android-legacy/nj2me-legacy.apk`. Enable installation from unknown sources
in the phone's settings, or install with ADB:

```sh
adb install -r build/android-legacy/nj2me-legacy.apk
```

The legacy and standard editions share the package name `com.lucinhu.nj2me`.
They replace each other; they cannot be installed side by side. The local build
uses the same development signing key as the standard local build. Android
will reject an update over an APK signed with a different key. Back up any game
saves before uninstalling an existing installation. The API 5 edition keeps the
previous API 7 edition's save paths and format.

Use Import in the launcher to browse storage or enter a JAR path. An optional
system file picker is also available when a compatible file manager is installed;
the built-in browser works without one. You can also choose Nano J2ME when opening
a `.jar` from a file manager (`ACTION_VIEW`, `file://` or granted `content://`),
or share the JAR to it (`ACTION_SEND` with `EXTRA_STREAM`). Imported games are
copied into private `files/jars`; the original file is kept. Identical content
reuses the existing library path and saves, while different games with the same
filename receive separate names. An existing running game must be exited before
starting another; receiving an import does not stop it.

Imports validate the archive, manifest and entry checksums before publishing a
file. The limits are 64 MiB of compressed input, 256 MiB of total uncompressed
entry data and 65,536 entries. Incomplete imports are removed. A file manager
must grant read access for a protected `content://` URI; otherwise the launcher
shows a Chinese error and keeps the library unchanged.

You can still put JARs in `/sdcard/nano_j2me` and tap Refresh. Choose the logical
screen size and VM heap in Settings. Start with 176×208 or 240×320 and a 4 or
8 MiB heap on a low-memory phone. The heap setting is only the J2ME VM heap;
images, decoded audio, the Android UI and native code need additional memory.
Some large games will need a larger heap than an early phone can provide.

There is an on-screen phone keypad and support for physical direction, number
and select keys. Back sends the game's right soft key. Menu opens the runtime
controls, including exit to the library. RMS data and snapshots are stored in
private, per-game directories. The JAR's canonical path identifies its save
directory, so moving a JAR changes which saves are selected.

## What changes for old phones

The J2ME runtime remains shared with the standard edition. The Android frontend
uses API 5 framework widgets, software `Canvas`/RGB565 display, and streaming
`AudioTrack`. It avoids `ANativeWindow`, OpenSL ES, AAudio and recent Android UI
APIs. Fonts and image decoding use API 5-compatible Android implementations.
The current multi-pointer input APIs are available from API 5; newer convenience
methods such as `MotionEvent.getActionIndex()` are not used.

The lightweight launcher does not include the standard edition's floating,
editable keypad layouts. Native Android platform media/network bridges that
are only in the standard frontend are not included; built-in J2ME codecs and
offline games are the main target. This edition does not make old Android's
network stack compatible with current web services.

An `armeabi` library also runs on many later 32-bit ARM phones. Use the standard
APK on Android 5.0+ when possible. ARM64-only devices without 32-bit ARM support,
and x86 devices without ARM translation, cannot load this legacy package.

## Reproduce the build

Build on Linux x86_64 with Bash, Python 3, a JDK, current Android SDK build-tools,
the Android 2.0/API 5 framework JAR, and NDK r10e. Modern NDKs no longer contain
the ARMv5 compiler or old Android libc stubs. Keep the old toolchain separate
from the modern SDK used by the standard Android build.

Official archived packages:

- [NDK r10e, Linux x86_64](https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip)
- [Android 2.0/API 5 platform](https://dl.google.com/android/repository/android-2.0_r01-linux.zip)

```sh
export ANDROID_NDK_LEGACY_HOME=/path/to/android-ndk-r10e
export ANDROID_LEGACY_JAR=/path/to/android-2.0_r01-linux/android.jar
export ANDROID_HOME="$HOME/Android/Sdk"
tools/build_android_legacy.sh
```

For the native library alone:

```sh
tools/build_android_legacy.sh --native-only --out build/android-legacy-native
```

The default toolchain paths are `.local-tools/android-legacy/android-ndk-r10e`
and either `$ANDROID_HOME/platforms/android-5/android.jar` or
`.local-tools/android-legacy/sdk5/android-2.0_r01-linux/android.jar`.

Java compilation uses the API 5 framework as both the boot class path and the
class path. Native code uses NDK r10e's API 5 ARM sysroot,
`-march=armv5te -mfloat-abi=soft`, and SysV ELF hashes. The link rejects unresolved
symbols and checks that no API 9 window/audio library is required. The legacy
port explicitly aligns VM allocations because old bionic's default `malloc`
alignment is insufficient for the shared core. Java resources are packaged with
`aapt --utf16`; `aapt2` output caused Eclair's package parser to fail. DEX and
manifest minimum API are 5. The build stages the standard edition's mdpi/hdpi
raster launcher icons as legacy drawable resources without adaptive icon APIs.
Signing explicitly includes the JAR/v1 signature
understood by Android 2.x. Build intermediates, including ELF dependency and
architecture reports, stay in the output's `work.*` directory for inspection.

## Validation (2026-11-12)

The API 5 APK was installed on the archived Android 2.0/API 5 ARM system image,
running on the ARM926EJ-S/ARMv5 emulator. These are emulator results, not
physical-phone performance measurements.

The import/Intent/icon update was also exercised on a fresh API 5 image:

- The built-in SD-card browser imports `game.v1.JAR`, starts Maze and preserves
  the source. Manual path entry and `ACTION_GET_CONTENT` results both reuse the
  existing imported file and its snapshot.
- Implicit `ACTION_VIEW` works with a multi-dot, uppercase `file://` filename
  without a MIME type, and with a granted `content://` URI. `ACTION_SEND` with
  `EXTRA_STREAM` also imports the same game without duplicating its save path.
- A protected test provider denies reads without a URI grant and allows the same
  URI with a grant. The denied import displays a readable error and leaves no
  partial JAR. Invalid archives are rejected. Different contents sharing
  a filename remain separate; repeating the original content selects its saved
  game. The final APK restores the saved game region pixel for pixel, accepts
  left input from x=68–72 back to x=59–63, and exits normally.
- All six Java sources compile strictly against API 5. Host import checks pass
  52 normal assertions and 25 scaled-limit assertions. The latter lower the
  three limits only in a temporary source copy; they do not perform an actual
  256 MiB extraction. Both packaged launcher icons have identical pixels to the
  standard edition's mdpi/hdpi icons.

These import logs, screenshots, helper-provider sources and host checks are in
`.local-tools/debug-runs/android-legacy-import-20261112/`. This helper uses an
explicit provider read permission; Android 2.0's `exported=false` alone was not
sufficient for a reliable permission-denial test after provider caching.

Earlier API 5 runtime validation:

- `maze.jar` runs at 240×320 with the default 8 MiB VM heap. The on-screen right
  key moves the red player from screen x=59–63 to x=68–72.
- Saving produces an 8,408,654-byte snapshot. After exiting to the library and
  resuming, the saved and restored game regions match pixel for pixel. Pressing
  left after restoring returns the player to x=59–63. Both exits complete with
  exit code 0.
- `1942空战_10403.jar` responds to the sound prompt, reaches its main menu and
  writes a 138-byte `aircombatdata.rms` on normal exit. This does not establish
  full game completion or compatibility with every JAR.
- On Android 2.3.3/API 10, the API 5 APK installs over the earlier API 7 build.
  Its existing Maze snapshot still resumes with an identical game region;
  direction input and normal exit work after upgrading.
- All six Java sources compile against the actual API 5 framework only. The
  APK contains DEX 035 and an `armeabi` library, with minimum/target SDK 5 and a
  verified v1 signature. The native library links against the API 5 sysroot
  without API 9 window/audio dependencies.

The previous API 7 build was also tested on Android 2.1/API 7 and Android
2.3.3/API 10: Maze input and snapshot round trips passed on both, and 1942
created RMS data. The standard API 21 `armeabi-v7a` native build passed its
writable-static-state gate, and ten focused host checks for heap, lifecycle,
filesystem, save, audio, pointer and key behavior passed. The first full host
run passed 160 of 161 checks; the unrelated existing `check_device_props`
fixture check failed (`emptykeyIAE`, then `DEV FAILED java.lang.IllegalArgumentException`
after rebuilding). Property handling and that fixture were not changed by this
port or the API 5 adjustment.

API 5 logs, screenshots and frame comparison results are retained under
`.local-tools/debug-runs/android-api5-20261112/`. Initial API 7/API 10 evidence is
under `.local-tools/debug-runs/android-legacy-20261112/`. Audio synthesis/pull
and shutdown were checked in a temporary native sanitizer harness during the
initial port. Emulators were launched with host audio disabled, so physical
sound output remains to be checked on the target phone.
