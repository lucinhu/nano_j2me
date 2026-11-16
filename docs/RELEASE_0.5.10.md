# nano_j2me 0.5.10 — Android vibration, 64-bit Android packages, full multi-platform release

Released 2026-11-16. Source commit for the artifacts: `8fa87ec` (the `v0.5.10`
tag adds only this release documentation). This is the first release after
0.5.9 that ships every artifact again: Android APKs, the legacy API-5 APK,
Linux, Windows and the eight-target libretro core package.

## Highlights

### libretro: vibration on Android frontends without a rumble interface

RetroArch for Android 1.19.1 answers `RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE`
with "unsupported", so the core's libretro rumble callback was never
installed and `Display.vibrate()` / Nokia `DeviceControl.startVibra()` did
nothing even though the log showed the requests. The core now falls back to
the Android `Vibrator` service through JNI when the frontend has no rumble
interface (resolve the JavaVM through `JNI_GetCreatedJavaVMs`, then
`ActivityThread.currentApplication()` → `getSystemService("vibrator")`, with
`VibratorManager.getDefaultVibrator()` unwrapped on Android 12+). The fallback
is Android-only and inactive when the frontend provides the normal rumble
interface. Verified on a Xiaomi 23049RAD8C (Android 15, Adreno 725) in
RetroArch 1.19.1 with `3D拉力赛-进化汉化版.jar`.

### Android packages: 64-bit slice by default

The universal APK now contains `arm64-v8a`, `armeabi-v7a` and `x86_64`, so a
64-bit phone runs the 64-bit interpreter instead of the 32-bit one. Measured on
the same phone with the same seed: the 64-bit generator is **2.29×** faster
than the 32-bit build in a headless benchmark, and an in-app Minecraft world
load (`LOAD read` + `prepare`) dropped from 51.6 s to 26.1 s (**1.98×**). A
separate arm64-only APK is provided for users who prefer the smaller download.

### libretro pacing, audio and fast-forward (accumulated since 0.5.8/0.5.9)

- The virtual clock follows the frontend's real time, so a 120 Hz frontend no
  longer runs games at 2× speed.
- The frame limit defaults to **30 fps** through the core option
  `nano_j2me_fps` (`15 / 20 / 30 / 60 / Unlimited`) and is enforced by the
  runtime's own frame pacer, the same code path the Android app uses.
- Audio is back-pressured with a 100 ms PCM ring: the decoder is throttled
  instead of silently dropping ~20 % of every buffer, which removed the
  "missing frames / not contiguous" artifact.
- Frontend fast-forward is honoured (one 60 Hz virtual frame per call, audio
  follows the sped-up clock); measured 12.6× on the phone.
- The MIDlet is constructed on the VM thread, so a loading constructor can no
  longer block `retro_load_game`. `毁灭战士3_10598.jar` runs its entire game
  loop inside its constructor (it joins its render thread), so that title
  loads and renders but cannot save state — recorded as a known limitation.
- Save states work for object-dense titles (0.5.9 buffer-bound fix included).

### core: CLDC 1.1 reflection subset

`java.lang.reflect.Method`, `Class.getMethod`/`getDeclaredMethod`,
`Method.invoke` (argument unboxing and return boxing), and the wrapper
`TYPE` statics (`Integer.TYPE`, `Long.TYPE`, …) are implemented, plus
`java/lang/NoSuchMethodException`. This makes the common vendor-API probe
idiom (`Class.forName(...).getMethod(...).invoke(...)`) work, e.g. Nokia
`DeviceControl.startVibra`. The constructor call watchdog is now tunable via
`nj_vm_set_call_block_ms()` (0 = wait until the VM stops).

## Artifacts

| File | Bytes | SHA-256 |
|---|---:|---|
| `nj2me-0.5.10-android-universal.apk` | 2611497 | `84a4b7a5a771cbaeb98e4e72619c57115f95a8dbdd2f2c8e16ac26a242c25513` |
| `nj2me-0.5.10-android-arm64-v8a.apk` | 993436 | `cd1a87ade3823138bec2339211398b634db11e4a0dae91a09f53ffe6b12f0a5b` |
| `nj2me-0.5.10-android-legacy-armeabi.apk` | 700570 | `f4ebf336ef9a9f9e3cc79af082e27145d69b055a0aff04b9264652260100deab` |
| `nj2me-0.5.10-linux-x86_64.tar.gz` | 1771180 | `01094dce567390e42b0b484335b5f09d47a240d501f7b073b992286c5cf8d805` |
| `nj2me-0.5.10-windows-x86_64.zip` | 2224805 | `00167610df951ad522e4368481407aa887d6db7d6ec1c40321d6814a995548f6` |
| `nj2me-0.5.10-libretro-cores.zip` | 11677845 | `16a0c00b9dcce83be5dea4d17fd635dd1c8118264314da08952631f42912b2e0` |

The libretro zip contains eight targets (`linux-x86_64`, `linux-aarch64`,
`linux-armv7`, `android-arm64`, `android-armv7`, `android-x86_64`,
`windows-x86_64`, `windows-i686`) with the core, `.info`, licenses and notes;
`SHA256SUMS-libretro-cores.txt` covers every top-level file.

Android notes: package `com.lucinhu.nj2me`, versionCode 510, versionName
`0.5.10` (`0.5.10-legacy` for the API-5 build), minSdk 21 for the modern APK
and 5 for the legacy APK. The legacy APK is software-only (no GLES2 path).

## Verification

- Clean release worktree at the source commit: `ctest` **160/160 passed**
  (fixtures regenerated with the tracked generators).
- libretro corpus visual sweep over 371 runnable titles: **370 passed, 1 known
  limitation, 0 failed** (the limitation is the Doom3 constructor above).
- On-device (Xiaomi 23049RAD8C, Android 15): arm64 core runs, produces audio
  and exits cleanly through a headless frontend; fast-forward 12.6×; frame
  limit options; vibration verified in RetroArch.
- Windows package: 60-frame smoke run under Wine 11, exit 0.
- Linux package: 120-frame headless smoke run, exit 0; dynamically linked
  against SDL2/libcurl/zlib, glibc 2.17+ symbol baseline.

## Known limits

- The GLES2 path stays optional: its startup calibration selects software when
  software is faster, which is the normal result on desktop CPUs for Minecraft.
- AAC decoding is Android-platform-only; other platforms keep AMR, MP3, WAV,
  ADPCM and MIDI.
- Containers that need presentation reordering (B-frames / non-monotonic CTTS)
  are rejected rather than rendered incorrectly.
- The legacy API-5 APK was verified by static checks only; no API-5 runtime was
  available here.
- Physical Windows hardware and non-x86 handhelds were not available for
  runtime testing.
