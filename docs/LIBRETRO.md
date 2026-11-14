# Running nano_j2me as a libretro core

nano_j2me provides a native C99 [libretro](https://www.libretro.com/) core for
J2ME MIDlets. The runtime is linked into the core; no external JVM or helper
process is required. Load a local `.jar` containing a `MIDlet-1` manifest entry.
JAD files and firmware/BIOS files are not required or read.

This is a locally built core. Preparation for upstream submission does not imply
acceptance into the RetroArch Core Downloader. See the
[submission kit](LIBRETRO_SUBMISSION.md) for the current evidence, license review
and remaining upstream steps, and the [library page draft](libretro/nano_j2me.md).

## Build and install

For a core without the CLI, SDL or networking dependencies:

```sh
cmake -S . -B build/libretro-local \
  -DCMAKE_BUILD_TYPE=Release \
  -DNJ_BUILD_LIBRETRO=ON -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_CLI=OFF \
  -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_EMBEDDED_PORTS=OFF \
  -DNJ_BUILD_POSIX_NETWORK=OFF -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
cmake --build build/libretro-local --target nano_j2me_libretro -j4
```

On Linux the output is `build/libretro-local/libretro/nano_j2me_libretro.so`.
Windows uses `.dll`, and a native macOS build is configured to use `.dylib`.
macOS has not been verified in the current matrix. The matching metadata source
is [nano_j2me_libretro.info](../src/port/libretro/nano_j2me_libretro.info).
`cmake --install build/libretro-local` installs the library and metadata under
`${CMAKE_INSTALL_LIBDIR}/libretro` (set an install prefix appropriate to your
system).

Copy the library to RetroArch's configured **Core** directory and the `.info`
file to its configured **Core Info** directory (`libretro_directory` and
`libretro_info_path`). Those directories may be different. Restart RetroArch
or refresh its core information cache after replacing metadata. The display name
is **Mobile - J2ME (nano_j2me)**. Choose *Load Core*, then *Load Content*, or run:

```sh
retroarch -L build/libretro-local/libretro/nano_j2me_libretro.so path/to/game.jar
```

The core requests `need_fullpath` and blocks frontend archive extraction: it
opens the JAR itself and reads resources while the MIDlet runs. Keep that JAR
available at its original path during the session.

The build above disables optional zlib discovery for a predictable dependency
set. Ordinary project builds can link zlib when detected. Always inspect the
actual artifact's dependencies rather than assuming every configuration is
self contained. Runtime code is MIT licensed; bundled AMR data retain Apache-2.0
and built-in font data retain SIL OFL 1.1. Include the complete applicable
[licenses and notices](LIBRETRO_SUBMISSION.md#license-review) with distributed artifacts.

## Platform builds

The shared library must match the frontend's operating system, CPU, bitness and
ABI. An Android arm64-v8a core needs a 64-bit frontend; an armeabi-v7a core needs
a 32-bit frontend. A Linux glibc artifact does not target musl-based firmware.

```sh
tools/build_libretro_cores.sh --list
tools/build_libretro_cores.sh --jobs 4 linux-x86_64 linux-aarch64 linux-armv7
tools/build_libretro_cores.sh --jobs 4 android-arm64 android-armv7 android-x86_64
tools/build_libretro_cores.sh --jobs 4 windows-x86_64 windows-i686
```

Outputs go to `build/libretro-dist/<target>/`, with a library and its metadata.
Use `--no-strip` to retain symbols. `all` selects the script's supported targets
and requires all of their toolchains.

The eight candidates currently in `build/libretro-dist/` were rebuilt on
2026-11-14 from the current tree, so they include the load-lifecycle fix, the
monitor and repaint-blocking fixes, the input-bitmask/rumble support and the
`nano_j2me_screen`/`nano_j2me_locale` options. `build/libretro-final-report-20261114.md`
records sizes, SHA-256 values and what was run per target: linux-x86_64 in the
mock frontend and real RetroArch, windows-x86_64 under Wine, and ABI/dependency
checks only for the Android and ARM-Linux cores (no device run in this rebuild).

| Target | Toolchain / artifact baseline | Verification scope |
| --- | --- | --- |
| `linux-x86_64` | host GCC/Clang, `.so`; current host artifact requires GLIBC_2.43 | native mock frontend and RetroArch checks; needs an older build environment for a general Linux release |
| `linux-aarch64` | Zig, glibc 2.17 by default | cross-build, dependency and ABI export checks; no ARM device run |
| `linux-armv7` | Zig, glibc 2.17 hard-float | cross-build and export checks; current artifact requires ARMv7 + NEON/VFPv3; no device run |
| `android-arm64` | Android NDK, API 21, arm64-v8a | cross-build and export checks; no device run |
| `android-armv7` | Android NDK, API 21, armeabi-v7a | cross-build and export checks; no device run |
| `android-x86_64` | Android NDK, API 21, x86_64 | core cross-build/export checks; separate native AMR service app exercised nine cases on API 21 x86_64; this is not a RetroArch core runtime test |
| `windows-x86_64` | MinGW-w64, PE32+ `.dll` | see submission evidence for Wine/native Windows distinction |
| `windows-i686` | MinGW-w64, PE32 `.dll` | see submission evidence for Wine/native Windows distinction |
| macOS | native CMake configuration, `.dylib` | complete core unverified and outside the cross-build/CI matrix; the separate AMR backend has API mocks and arm64/x86_64 compile checks only |

Android NDK discovery uses `ANDROID_NDK_HOME`, or an installed NDK under
`${ANDROID_HOME:-$HOME/Android/Sdk}/ndk`. ARM Linux cross-builds use Zig from
`PATH`; `NJ_ZIG_GLIBC` changes the configured glibc floor. These baselines are
build facts, not a promise of compatibility with every handheld or OS release.

## Controls

One RetroPad provides the handset keypad. The D-pad is fixed; the other eight
buttons have configurable bindings.

| Frontend input | Default J2ME input |
| --- | --- |
| D-pad | UP / DOWN / LEFT / RIGHT |
| A | FIRE |
| B | SOFT_RIGHT |
| X | SOFT_LEFT |
| Y | CLEAR |
| L | GAME_A |
| R | GAME_B |
| Select | 0 |
| Start | 5 |
| Pointer / mouse | canvas pointer events, transformed through rotation |

The keyboard callback accepts `0`–`9` and numeric keypad digits, `*` and `#`,
Enter/Space for FIRE, arrows, Backspace for SOFT_RIGHT, Tab for SOFT_LEFT and
Escape for CLEAR. RetroArch hotkeys or focus settings can intercept these keys.

The core negotiates `RETRO_ENVIRONMENT_GET_INPUT_BITMASKS` during
`retro_set_environment()`. When the frontend accepts, each poll reads
`RETRO_DEVICE_ID_JOYPAD_MASK` once and decodes the bits by RetroPad button id;
when it declines, the core keeps polling each bound button individually. Both
paths feed the same press/release edge detector, so a press reaches the MIDlet
exactly once and the two paths are never mixed within a session.

## Rumble

When the frontend exposes `RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE`, the Nokia
UI `DeviceControl.vibrate(ms)`, `startVibra(frequency, duration)` and
`stopVibra()`, and `Display.vibrate(ms)`, drive one
`RETRO_RUMBLE_WEAK` channel at full strength (`0xffff`) while active, then send
zero on `stopVibra()`, on `vibrate(0)`/`startVibra(..., 0)`, or when the
requested duration expires. Durations are milliseconds; the Nokia
`frequency` argument has no meaning for this one-motor mapping and is ignored.
State is serviced on the next `retro_run()` tick, and unload/deinit stops an
active motor. A negative duration is rejected as
`IllegalArgumentException`, matching the handset API.

If the frontend offers no rumble interface, or its `set_rumble_state` callback
declines, vibration is a silent no-op: the MIDlet keeps running, no error is
logged, and a callback rejection produces at most one warning for the session.
The interface is not required for any other feature.

## Core options

All options are read at content load. Reload the content after changing them.
Core Options v2 categories are provided, with a legacy variables fallback.

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `nano_j2me_device` | generic, nokia6230, sonyericsson-k750, motorola-e398, samsung-sgh-e700, siemens-s65 | generic | handset properties and keypad conventions |
| `nano_j2me_rotation` | 0, 90, 180, 270 | 0 | rotate the presented picture inside the core |
| `nano_j2me_screen` | 240x320, 176x208, 128x160, 320x240 | 240x320 | logical Canvas size the MIDlet sees |
| `nano_j2me_locale` | auto, en-US, zh-CN, zh-TW, ja-JP, ko-KR, de-DE, fr-FR, es-ES, ru-RU | auto | `microedition.locale`; auto keeps `en-US` |
| `nano_j2me_heap` | auto, 16, 32, 64, 128 MB | auto | Java heap budget; auto uses host RAM / 16, clamped to 32–128 MB in 8 MB steps; unknown RAM uses 32 MB |
| `nano_j2me_font` | auto, system, builtin | auto | external font selection; see Fonts below |
| `nano_j2me_sound` | enabled, disabled | enabled | sample and tone output |
| `nano_j2me_pointer` | enabled, disabled | enabled | canvas pointer events |
| `nano_j2me_btn_a` | key names below | FIRE | A binding |
| `nano_j2me_btn_b` | key names below | SOFT_RIGHT | B binding |
| `nano_j2me_btn_x` | key names below | SOFT_LEFT | X binding |
| `nano_j2me_btn_y` | key names below | CLEAR | Y binding |
| `nano_j2me_btn_l` | key names below | GAME_A | L binding |
| `nano_j2me_btn_r` | key names below | GAME_B | R binding |
| `nano_j2me_btn_select` | key names below | 0 | Select binding |
| `nano_j2me_btn_start` | key names below | 5 | Start binding |

Binding values are `NONE`, `FIRE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `0`–`9`,
`STAR`, `POUND`, `SOFT_LEFT`, `SOFT_RIGHT`, `CLEAR`, `GAME_A`–`GAME_D`, `VOLUP`,
`VOLDOWN` and `POWER`.

`nano_j2me_screen` sets the logical MIDlet canvas (240×320 by default). The
Canvas and the game's coordinate system use that size; rotation happens only
when the finished frame is copied. `retro_get_system_av_info()` reports the
canvas size for rotations 0/180 and the swapped size for 90/270, with the
matching aspect ratio — for example a 320×240 canvas with rotation 90 is
presented as 240×320. Do not apply the same rotation again in the frontend.
Pixels are RGB565. The core reports 60 Hz and 44.1 kHz stereo, producing 735
stereo sample frames per `retro_run()`; a game's own animation rate can be
lower.

`nano_j2me_locale` sets the `microedition.locale` system property at VM
creation. `auto` (the default) keeps the historic `en-US` value; the host
locale is not consulted. The value affects only MIDlets that read the property,
for example to choose message strings. It does not translate already-created
strings, change the handset profile, or select a font — Chinese text still
needs a font with that coverage. Reload content after changing it.

## Fonts

`nano_j2me_font` takes effect on content reload:

- **auto** first tries `<RetroArch system directory>/nano_j2me/font.ttf`, then
  `font.ttc`, then a short list of common operating-system font paths.
- **system** only tries those two files in the frontend's system directory.
  Use this mode to select a particular font consistently across machines.
- **builtin** uses the bundled Liberation Mono / Source Han bitmap faces.

For Chinese, supply a TrueType font with Chinese coverage. The built-in MIT
rasterizer supports TrueType `glyf` outlines and TTC collections, including a
Chinese-face preference when selecting from a collection. CFF/OTTO outlines
are unsupported; some Noto/PingFang collections therefore fall back to bitmap
fonts. OS lookup is a list of common Windows, Android, macOS and Linux paths,
not a complete native font discovery service. The core logs the accepted path
and CRC, or the fallback reason. No external font is downloaded or redistributed.
Missing characters still use built-in fallbacks. See the separate
[font license declaration](FONT_LICENSES.md) and retain both OFL notices when
packaging the built-in data.

Accepted font bytes are pinned for the loaded session and copied into restored
VMs. States check that font's CRC; use identical font bytes when loading a state
after content reload. State envelope version 2 adds this identity and rejects
earlier version-1 libretro states. Changing the file on disk does not change a
running game's font until content reload.

## In-game saves and save states

RecordStore and FileConnection are managed by the core, not by libretro SRAM:

```text
<save directory>/nano_j2me/<normalized JAR stem>/
<save directory>/nano_j2me/<normalized JAR stem>/files/
```

The save directory falls back to the frontend's system directory, then the
current directory if neither is supplied. The stem is lowercased, ends at the
first dot, and replaces runs of non-alphanumeric characters with underscores.
Different filenames can normalize to the same stem and share storage; use
distinct stems or separate save directories when needed. Back up in-game saves.

Basic manual save/load is implemented through `retro_serialize` and
`retro_unserialize`. The frontend posts a request to the VM worker; the worker
handles it at a safe execution boundary. Restore first validates into a fresh
candidate VM on that worker and swaps it in only after success. The snapshot
includes Java VM execution state, the front/back framebuffer and virtual clock.
The envelope checks content identity, relevant core options, version and payload
integrity. Invalid or incompatible states are refused without replacing the
running VM.

States are **architecture, ABI, endianness and build dependent**. Use the same
core build, JAR bytes, heap/device/rotation/screen/sound/pointer settings, and
platform. The envelope rejects a different framebuffer size, rotation, heap,
sound, pointer state, device profile or font, so a state saved for one
`nano_j2me_screen` value is refused under another. `nano_j2me_locale` is not
part of the state envelope; keep it unchanged when reloading content, or the
restored Java objects will coexist with a different current property value.
States are not a portable or stable archival format. Decoded heap PCM and its
media time/playback cursor are preserved and restored without opening a codec.
Native playback handles and device queues are cleared; encoded/native-only
media may stop or need the game to restart it. External RecordStore files,
FileConnection contents and other host resources are **not rewound**. Loading a state can therefore pair old Java state
with newer on-disk data. A busy VM that cannot reach a safe point can reject a
save/load request.

There is no determinism claim. Rewind, runahead and netplay are not advertised
or validated; use manual states and the game's own save system. Snapshot sizes
include the VM heap budget and can be large. The
[submission checklist](LIBRETRO_SUBMISSION.md#validation) records final test gates.

## Timing, media and limitations

The VM runs on its own worker. The virtual clock advances with frontend ticks,
so frontend pause, fast-forward and slow motion affect virtual deadlines.
Scheduling and external resources still prevent a deterministic execution
contract. Load-time clock advancement avoids waiting real seconds for some
splash screens; it is bounded and does not guarantee every MIDlet boots.

The default runtime includes WAV, MIDI, tone sequences, MP3 Layer III, AMR
narrowband and supported H.263/MPEG-4 Part 2 paths. This port routes decoded PCM
and video through its libretro callbacks; it does not universally support every
JSR-135 encoding, media profile or game. Features depend on compile-time codec
switches; the default features remain enabled. AMR first validates the whole
input, then tries the host audio hook and available providers for a complete
8 kHz mono PCM16 result. Errors, unsupported formats, partial output and stalled
providers are discarded before the original input is decoded by the built-in
fallback. Bare AMR and AMR tracks in supported 3GP/MP4 containers share this route.

CMake's `NJ_ENABLE_SYSTEM_AMR` accepts `AUTO` (default), `0` or `1`. `AUTO`
enables native services on hosted non-legacy Android, Apple and Windows;
`0` leaves explicit port/custom providers available. Android's existing Java
audio hook stays first; a VM-less Android CLI falls back immediately. Linux
uses the built-in decoder unless a port/custom provider is installed. System
and built-in decoders may produce different PCM. See [system AMR](SYSTEM_AMR.md)
for platform evidence and configuration, [media documentation](CODECS.md), and
the [license inventory](LIBRETRO_SUBMISSION.md#license-review).

JAR `MIDlet-1` is the entry point; JAD redirects and suite selection are not
implemented. Network hooks are absent in this core: online scores, HTTP/socket
services and downloadable content are unavailable. Bluetooth and sensors are
not provided by this adapter; vibration is forwarded to the frontend when a
rumble interface is available (see Rumble above). No cheats, disc control,
exposed libretro memory or achievements are implemented. The core uses one
process-wide instance and loads one game at a time. Compatibility and
performance vary by title, handset assumptions and host CPU.

## Feasibility limits

These are deliberate non-features, not untested paths. The libretro API can
advertise each of them, and claiming support the core cannot honour would be
worse than refusing it.

- **Rewind** — `retro_serialize()` is a request posted to the VM worker, not a
  synchronous frame snapshot: the frontend thread hands over the request and
  the worker copies the whole Java heap (32–128 MB), both framebuffers and VM
  state. Rewind needs one such snapshot per displayed frame with bounded
  latency; the size and the worker round-trip make that impractical. A restored
  VM also keeps newer RecordStore/FileConnection data and loses native media
  handles, so history would not be a faithful rewind anyway. Rewind is not
  advertised; manual save states are supported.
- **Runahead** — runahead requires running frames ahead of presentation and
  restoring a state every frame deterministically. The asynchronous worker,
  host wall-clock timers, external file side effects and Java scheduling
  nondeterminism break that contract, so the frame that is finally presented
  could differ from the one run at the same input. Not advertised.
- **Netplay** — rollback netplay needs cheap, deterministic snapshots and a
  stable input-to-frame mapping. The same nondeterminism, the multi-megabyte
  state and the single process-wide VM make lockstep/rollback synchronization
  infeasible in this design. Not advertised.
- **Cheats** — libretro cheat codes address a flat, stable memory region. A JVM
  heap has no stable addresses: objects move, the collector compacts, and a
  Java field offset is not a host address. `retro_cheat_reset()` and
  `retro_cheat_set()` are exported as no-ops and no cheat database is shipped.
  Game-specific patching would need per-title support.
- **Exposed memory** — `retro_get_memory_data()` returns NULL and
  `retro_get_memory_size()` returns 0. There is no SRAM-like backing store; the
  VM arena contains managed objects and native bookkeeping, and exposing it
  would be both meaningless to the frontend and unsafe for the VM.
- **Achievements** — rcheevos needs stable memory addresses, deterministic
  execution and content identification. None of the three holds here, so
  achievements are absent rather than silently broken. No achievements
  interface is advertised.

## Maintainer verification

The [adapter](../src/port/libretro/nj_port_libretro.c) exports the libretro ABI.
It owns one VM worker, synchronizes framebuffer transfer, and calls frontend
video/audio callbacks outside the VM mutex. Asynchronous MIDlet startup allows
long-running `startApp()` loops to yield to the Java scheduler. Save/load requests
are serviced on the VM worker rather than serializing concurrently from the
frontend thread.

Build tests separately from release artifacts:

```sh
cmake -S . -B build/libretro-check \
  -DNJ_BUILD_LIBRETRO=ON -DNJ_BUILD_TESTS=ON \
  -DNJ_BUILD_CLI=ON -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
cmake --build build/libretro-check -j4
ctest --test-dir build/libretro-check --output-on-failure
CORE="$PWD/build/libretro-check/libretro/nano_j2me_libretro.so" \
  tools/check_libretro_frontend.sh
RETROARCH=/path/to/retroarch tools/check_libretro_retroarch.sh \
  build/libretro-check/libretro/nano_j2me_libretro.so \
  tests/fixtures/alphademo.jar 600
tools/check_no_statics.sh
```

Use an existing redistributable fixture path for the final RetroArch command
(the script's default JAR can also be used by omitting the JAR argument). The
mock frontend checks real ABI loading, options/geometry, animated frames, audio,
input bitmasks (accepted and rejected), locale, the four screen sizes,
vibration, repeated load/unload and save/load cases. A real RetroArch smoke
check also tests frontend integration. A watchdog-bounded boot is weaker evidence
than a clean `run ended` and exit 0; record which occurred. Cross-build/export
checks alone do not establish device runtime compatibility.
