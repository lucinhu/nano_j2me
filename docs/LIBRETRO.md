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

The eight candidates currently in `build/libretro-dist/` are the r23 package
rebuilt on 2026-11-15 from revision `d2ed8d8` (which adds the `SYSTEM_RAM`
cheat-search window on top of the r22 real-time pacing and the M3G
Gouraud-textured span fast path; see `docs/PERFORMANCE.md`), so they include the load-lifecycle fix, the
monitor and repaint-blocking fixes, the CLI/manual-state boundary work, the
input-bitmask/rumble support and the `nano_j2me_screen`/`nano_j2me_locale`
options, bounded load/save state clock assistance including the paused
`System.currentTimeMillis()` poll assist (paint-aware and stalled-frame gated),
the level-3 Player-start diagnostics, and three corpus rendering fixes (stray
paint waiters, pre-show `Displayable` sizes, longer startup grace), plus the
device-property answers and the generic-handset load retry described above, and
the M3G software-raster fast path for the Gouraud + nearest-textured +
MODULATE span class (about -4.6% CPU on the profiled 3D title; the opening
deterministic frames of that title are byte-identical to the previous build).
The report also records the whole-corpus rendering probe, which is what found
those rendering bugs. Windows cores are linked with a zero COFF timestamp and no
build-id, and are stripped at link time, so the whole package is byte-reproducible
across clean rebuilds (verified twice for both Windows targets).
`build/libretro-final-report-20261115.md` records sizes, SHA-256 values
and what was run per target: linux-x86_64 in the mock frontend and real
RetroArch, windows-x86_64 under Wine, and ABI/dependency checks only for the
Android and ARM-Linux cores (no device run in this rebuild). That report also
records the offline simulations of the libretro GitLab CI template commands for
Linux and Android, which are part of the upstream submission preparation.

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

### Device properties and the handset retry

When no device profile is selected, the core answers the machine properties a
title may probe before its MIDlet starts, exactly the way the headless, SDL2,
Android and web ports do: `com.nokia.keyboard.type` is `None` (the frontend has
no numeric keypad under the screen, so the game's own on-screen keypad is the
right answer), `com.nokia.mid.ui.version` is `1.1`, and
`microedition.platform` is `nano_j2me/libretro`. A title that calls
`.equals()` on one of these without checking for `NULL` would otherwise die in
its constructor (`bubblet.jar` in the corpus does exactly that); on a handset
the property always exists, so answering it is the compatible behaviour. A
device profile's own properties take precedence when one is selected.

Some titles branch on `microedition.platform` and refuse the generic handset
before their MIDlet starts (the corpus's `1945空战英雄_6105.jar` throws
`IllegalAccessException` from `Class.newInstance` on its own MIDlet class).
When a load fails while the option is still `generic`, the core retries the
load once with the `nokia6230` profile, logs both attempts, and keeps that
profile for the rest of the session (including `retro_reset`). Selecting a
device profile explicitly disables the retry. The default itself stays
`generic`: a full-corpus experiment with `nokia6230` as the default lost four
titles and gained none (`docs/notes/2026-11-15-device-default-experiment.md`),
so the retry is deliberately limited to content that failed to load.

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
with newer on-disk data. A busy VM that cannot reach a safe point can still
reject a save request. A load request made while the VM is inside a blocking
native call is completed by fast-forwarding the virtual clock so the VM reaches
the next frame boundary (the load replaces the VM, so this cannot alter the
restored state); this also makes loading a state immediately after a reset work
for titles whose startup sleeps in a splash screen. A save or load requested while the
frontend is paused advances the virtual clock by up to about twenty seconds of
game time (1200 virtual frames) so that a frame which busy-waits on the clock
-- including one that polls `System.currentTimeMillis()` in a loop -- can reach
the boundary where the request is served; a save is then taken at that boundary
and includes the additional progress, while a load replaces the VM and restores
the pre-request clock if it is refused. The assist only runs when the frame
cannot finish on its own: during a paint callback, or after the frame has
stopped advancing presented frames for 100 ms. A frame that keeps advancing
normally is never accelerated, which keeps titles whose logic is timer-driven
(e.g. the football managers) intact. If the frame still cannot finish within
the budget, the request is refused after the four-second deadline. Saving or
loading during normal play, at a frame boundary, is unaffected. The blob itself
is validated on the worker; any refusal is a serving-time limit, not a format
change.

A very slow loader keeps the frontend inside `retro_load_game()` until the
MIDlet constructor returns, and a constructor that joins its own loader
threads cannot return earlier. `毁灭战士3_10598.jar` does exactly that: its
constructor starts two loader threads and `Thread.join()`s them while the
loader renders a 3D loading animation (190 software-M3G passes). 400 s was not
enough in the sampling run, so a harness watchdog shorter than several
minutes kills a load that is progressing. Give slow 3D titles a long load
timeout rather than treating this as a hang.

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

Each `retro_run()` advances that clock by one 60 Hz frame, while the VM worker
runs on the host CPU at its own pace. A frontend that calls `retro_run()` far
faster than real time (an unthrottled loop, or a fast-forward mode) can consume
frames faster than the VM finishes their paint work, so titles that animate on
wall-time timers may present blank or stale frames until the frontend returns
to its normal cadence. Keep the frontend at 60 Hz for display-accurate
playback, and treat unthrottled playback as a speed mode rather than a
rendering-correctness mode. The mock harness shows the effect directly:
`3D激爆拳击_8805.jar` presents only black frames with `--pace-us 0`, and draws
its logo with `--pace-us 16666`.

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
rumble interface is available (see Rumble above). Code-based cheats, disc
control and achievements are not implemented; the Java arena is exposed as
`RETRO_MEMORY_SYSTEM_RAM` so a frontend's own cheat search can scan and patch
it (see Cheats below). The core uses one process-wide instance and loads one
game at a time. Compatibility and
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
- **Code-based cheats** — `retro_cheat_set()`/`retro_cheat_reset()` stay
  no-ops: a libretro cheat code is a (address, value) pair over a flat memory
  map, and a managed heap has no object or field addresses a code format could
  describe. No cheat database is shipped. This does not stop a frontend's
  search-and-write workflow, which operates on the exposed arena instead.
- **Exposed memory** — the Java arena is a single non-moving allocation
  (objects are never relocated), so `retro_get_memory_data(SYSTEM_RAM)`
  returns its base and `retro_get_memory_size(SYSTEM_RAM)` the heap budget
  (16-128 MB depending on `nano_j2me_heap`). The pointer is stable for the
  VM's lifetime and the window is readable and writable, which is what a
  frontend cheat search needs: scan for a value, then patch it. The window is
  raw and unsynchronised: a write bypasses the Java memory model, lands
  without a write barrier and can corrupt any object, so it is a debugging
  tool for deliberate user actions, not a save-data channel. Only SYSTEM_RAM
  is exposed; `VIDEO_RAM`/`SAVE_RAM`/`RTC` report NULL/0 because there is no
  SRAM-like backing store. The same arena is also published as one
  `RETRO_MEMDESC_SYSTEM_RAM` descriptor through
  `RETRO_ENVIRONMENT_SET_MEMORY_MAPS` (`.info` `memory_descriptors = "true"`),
  which is what frontends prefer for cheat search and memory viewers; it is
  refreshed after every state load because a load replaces the VM and its
  arena address.
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
# The same end-to-end contract over a set of real corpus titles (serial, one
# RetroArch instance per title; logs per title under
# .local-tools/debug-runs/retroarch-corpus/).  Runs are unpaced because the
# null video driver does not throttle, so this is an integration check rather
# than a frame-rate measurement.
tools/check_libretro_retroarch_corpus.sh \
  build/libretro-check/libretro/nano_j2me_libretro.so 600
tools/check_no_statics.sh
# Offline validation of the prepared upstream submission bundle.  Without the
# clone arguments it checks the .info identity, both patches and the release
# tree; with them it resets the scratch clones, applies the patches and (with
# --mkdocs) builds the documentation site.
tools/check_libretro_submission_bundle.sh
tools/check_libretro_submission_bundle.sh \
  --super-clone /path/to/libretro-super --docs-clone /path/to/libretro-docs \
  --apply-clones --mkdocs /path/to/mkdocs
# Wider sampling probes; both take the output directory of a sweep run.
NJ_BIN="$PWD/build/libretro-check/nj2me" \
  python3 tools/check_corpus_deep.py .local-tools/debug-runs/<sweep> \
  --count 24 --frames 1800 --jobs 8
NJ_LIBRETRO_HARNESS="$PWD/build/libretro-check/test_libretro_frontend" \
NJ_LIBRETRO_CORE="$PWD/build/libretro-check/libretro/nano_j2me_libretro.so" \
  python3 tools/check_libretro_option_matrix.py .local-tools/debug-runs/<sweep> \
  --count 8
# Whole-corpus rendering probe (non-black, animated, saved/restored per title).
NJ_LIBRETRO_HARNESS="$PWD/build/libretro-check/test_libretro_frontend" \
NJ_LIBRETRO_CORE="$PWD/build/libretro-check/libretro/nano_j2me_libretro.so" \
  python3 tools/check_libretro_corpus_visual.py .local-tools/debug-runs/<sweep> \
  --frames 300 --jobs 10
```

Use an existing redistributable fixture path for the final RetroArch command
(the script's default JAR can also be used by omitting the JAR argument). The
mock frontend checks real ABI loading, options/geometry, animated frames, audio,
input bitmasks (accepted and rejected), locale, the four screen sizes,
vibration, repeated load/unload and save/load cases. A real RetroArch smoke
check also tests frontend integration. A watchdog-bounded boot is weaker evidence
than a clean `run ended` and exit 0; record which occurred. Cross-build/export
checks alone do not establish device runtime compatibility.

The long-run probe (`check_corpus_deep.py`) samples sweep-ok titles at a
real-time cadence for much longer than the sweep window (1800 frames at 30 fps
is a minute of emulated time each) and reports late crashes, hangs, early
key-script exits and new uncaught exceptions. The option matrix
(`check_libretro_option_matrix.py`) runs real titles through the mock frontend
with every non-default core option (screen sizes, rotation, heap, builtin font,
sound and pointer off, device, locale); profiles that change the reported
geometry pass the matching frontend expectation. Both are sampling tools, not
CI gates. The corpus visual probe (`check_libretro_corpus_visual.py`) runs every
sweep-ok title through the mock frontend and requires a non-black animated
frame, so it catches "loads but paints nothing" regressions that the CLI sweep
cannot see. At the time of writing it reports 372/373 at both the default
300-frame (5 s) horizon and a 600-frame (10 s) re-run. The single remaining
failure is one very slow 3D loader (see below) that cannot finish within the
sampling watchdog; the two titles that need the `nokia6230` profile now load
through the device-property answers and the handset retry.

The probe tools copy the core to a private file before the run. The core is
memory-mapped from disk, so a concurrent rebuild that rewrites
`build/libretro-dist` on top of a running probe can fault the mapping: an
8-job 600-frame run that reported eight `core raised a fatal signal` failures
early on reproduced 372/373 with zero crashes once the core came from an
immutable copy, and every "crashed" title also passed alone and under an
8-way stress rerun. Treat "core crashed" from a shared-tree probe as suspect
until it reproduces from a private core copy.

## Real-time pacing (2026-11-15)

`retro_run` used to advance the virtual clock by one frame per call, so a
frontend running at the panel rate (adaptive 60/90/120 Hz Android displays)
made `System.currentTimeMillis()` and `Thread.sleep()` run at that rate: a
120 Hz frontend ran games 2x fast and produced audio twice as fast as it was
consumed. The port now advances `vclock_us` by the real monotonic delta between
`retro_run` calls (clamped to 16.67 ms after a pause or a stall longer than
100 ms) and derives the audio batch from the same delta, so game time and audio
follow real time regardless of the frontend call rate.

The frame rate itself is limited by the runtime's own `frame_pace()` through
the port `fps_cap`, exactly like the Android APK: the core option
`nano_j2me_fps` ("Frame rate limit", default **30**) sets it to 15, 20, 30, 60
or Unlimited. Old MIDP handsets ran titles at 12-30 fps (60 fps is essentially
absent), so 30 fps is the default that keeps frame-loop games at their intended
speed; titles that pace themselves with `Thread.sleep()` are not slowed by it.
Measured with a self-driven loop probe: cap 30 -> 33.4 ms/frame, cap 20 -> 50 ms,
cap 60 -> 16.7 ms, cap 0 -> 8.5 ms.

Verified on the desktop: a probe MIDlet that sleeps 16 ms per frame reports the
same in-game elapsed time under 60 Hz and 120 Hz frontend pacing (5068 ms and
5119 ms for 300 frames), audio output stays at ~44100 samples/s at both rates,
AudioDemo keeps `state=400` for WAV/AMR/ADPCM, and the same probe inside real
RetroArch reports real-time game time.

## Audio backpressure and latency (2026-11-15)

Decoded audio was pushed into a two-second ring that accepted every frame and
silently dropped whatever did not fit. Titles that decode ahead of real time
(one measured title produced ~1.24x real time) filled the ring in under two
seconds and then lost roughly a fifth of every buffer, which is heard as
chopped/stuttering sound with the pitch intact ("缺帧", sound not glued
together) while the video is fine.

The port now reports real backpressure: `pcm_write()` accepts only the frames
that fit in a short 100 ms ring and returns how many source frames it took, so
the runtime keeps the rest and offers it again next frame (this is the port
contract the other backends already follow). The resampler state is rolled back
when it cannot finish an input frame, so no sample is duplicated or lost.
`pcm_queued()` is implemented as well, so `Player.getMediaTime()` reports the
audible position instead of a position one ring ahead.

Verified by capturing the raw stereo output of the headless frontend: after the
startup silence, 3D拉力赛 at 60 Hz and 120 Hz frontend pacing is continuous
(only the game's own short engine-loop gaps remain), a 400 ms-per-note
`Manager.playTone()` probe yields exact 400 ms tone segments, and the captured
duration matches the wall clock at both rates.

Cores with these changes log
`nano_j2me 0.5.7 libretro core (pacing rev 5: realtime clock + frame limit + audio backpressure)`
at startup so the installed binary can be identified from the frontend log.

## Fast-forward (2026-11-15)

The frontend's fast-forward key (RetroArch: the FF toggle / hold) is honoured
through `RETRO_ENVIRONMENT_GET_FASTFORWARDING`. While the frontend reports
fast-forwarding, one 60 Hz virtual frame is advanced per `retro_run()` call
instead of the measured real-time delta, so the frontend's unthrottled call
rate becomes game speed; audio is generated from the same delta, so it speeds
up (or is muted by the frontend) instead of drifting behind the picture.

The run loop also yields 1 ms per call while fast-forwarding: without it the
frontend can reacquire the audio/clock lock before the VM thread it just woke
is scheduled, and the game does not actually speed up. 1 ms still allows about
16x, which is well above the 2x-10x users normally want.

Verified with the headless frontend at 0 Hz (back-to-back `retro_run`, like the
frontend in fast-forward): a 300-frame self-paced probe reports its normal
5000 ms of game time in ~0.5 s of wall time, while the same run without
fast-forward does not advance at all. A second probe driven by
`repaint()`/`serviceRepaints()` speeds up the same way.

## Vibration (2026-11-15)

MIDlet vibration already reaches the frontend: `Display.vibrate()` and the
Nokia `DeviceComtrol.startVibra()`/`startVibra` paths call the port `vibrate`
hook, which records the request, and `retro_run()` forwards it through
`RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE` as `RETRO_RUMBLE_WEAK` (full strength
while active, 0 when the requested duration ends). RetroArch's Android build
implements the interface with the phone's `Vibrator`, so a game that vibrates
on a handset vibrates in RetroArch too. The port logs `MIDlet vibrate(N ms)`,
`rumble set on/off`, and `frontend has no rumble interface` (at most a few
times) so a missing frontend implementation is visible in the log instead of
being silent.

Verified end-to-end with the headless frontend (which provides a rumble
callback): a probe that calls `Display.vibrate(600)` four times produces
`MIDlet vibrate(600 ms)` followed by `rumble set on (weak, 0xFFFF)` and
`rumble set off` at the end of each request.
