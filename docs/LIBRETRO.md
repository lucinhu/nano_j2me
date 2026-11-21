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

The eight candidates currently in `build/libretro-dist/` are the r41 package
(the 0.5.22 release) built on 2026-11-21 from revision `294a2bf`.  r41 raises
the default static field area (`NJ_MAX_STATICS` 4096 -> 16384) so class-heavy
titles link instead of silently half-rendering (`Plants vs Zombies (240x320)`);
an exhausted area now throws an explicit `OutOfMemoryError` naming the knob,
not a bare `NoClassDefFoundError` for a class that is present.  r40 makes
the Android in-app 保存状态 work for frame-driven titles: a port that only gets
control from the frame hook (the launcher serves the save from its input poll)
can call `nj_vm_save_file_boundary()` and the runtime stores a thread parked in
`serviceRepaints()` as resumed-after-the-call (`docs/notes/2026-11-21-android-save-state-limitation.md`);
`LoopDemo` saves a 330,016-byte state and 恢复进度 restores it.  r40 also keeps a
bare pointer move on the single pointer (the multi-touch owner gate dropped
moves without a recorded down) and makes `test_pointer` test the built binary
instead of a shared tree.  The libretro core itself is feature-identical to r39
except for that pointer fix.  r39 adds the
`java.lang.Boolean.TRUE`/`FALSE` constants that J2SE-shaped ports read with
`getstatic` (`src/core/nj_lib_boxing.c`; `Alco Bench.jar` and other titles that
died with `NoSuchFieldError` now start).  r38 applies
full-screen mode changes requested on a visible `Canvas` instead of throwing
`IllegalStateException` (`docs/notes/2026-11-20-fullscreen-visible-toggle.md`);
titles such as `The Sims 3 Winter Edition`, which switch the mode from their
paint loop, painted pure black before and now render normally. r37 folded in
the blit-path field cache (Image width/height/pixels resolved once per VM
through `nj_hot_field`, byte-exact, -4.2 %/-5.1 % on the unpaced Minecraft
loader; `docs/PERFORMANCE.md`), so the libretro core gains the same runtime
speed-up as the CLI and APK. r36 added
live candidate values to the Android cheat search and kept one cheat line per
address (`docs/notes/2026-11-19-cheat-search.md`); that release left the
libretro core functionally unchanged from r35. r35 was the
Android in-game cheat-editor + value-search release: the game panel's 作弊码
row opens the title's cheat file, writes it and reloads the running VM's table
at the next frame boundary, and its 搜索作弊数值 row scans the arena for a
value the player can see, narrows the candidate list and appends the generated
code (`docs/notes/2026-11-19-android-cheat-editor.md`,
`docs/notes/2026-11-19-cheat-search.md`). The libretro core itself is
feature-identical to r33/r34 — the search helpers are a new runtime API used by
the Android panel, and libretro frontends keep using their own cheat search —
and r33 moved the cheat engine into the runtime so the libretro core, the
headless CLI (`--cheat`/`--cheat-file`) and the Android build share one table
(`docs/notes/2026-11-19-core-cheat-engine.md`). r32 added the arena-offset
cheat codes documented under [Cheats](#cheats) and the corrected
mock-frontend pointer-key parsing
(`docs/notes/2026-11-17-harness-pointer-keys.md`). r31 shipped the
multi-touch input path: the core follows `RETRO_DEVICE_ID_POINTER_COUNT`,
reports every finger the frontend sends with its own event id, and keeps the
MIDP `Canvas` pointer with the first finger
(`docs/notes/2026-11-16-multitouch-support.md`); the same release ships
multi-touch through the Android APKs. r30 added the pacer-owned idle nap, so a
paced run no longer burns a CPU core while it waits for the next frame
(`docs/PERFORMANCE.md`, "Paced-run CPU burn: the pacer owns the nap"); the
0.5.10 release in between added Android Vibrator-based rumble through JNI and
the 64-bit APK work. Before that, r27 fixes the
save-state buffer bound for object-dense titles
(`docs/notes/2026-11-16-state-buffer-bound.md`) and bumps the metadata to
0.5.9; before it, r26 added the version bump, the monitor acquisition for
synchronized JNI-bridge callbacks
(`docs/notes/2026-11-16-bridge-synchronized-callbacks.md`), and keeps
everything below. r25 added the malformed-bytecode
array-operand ownership guards and the GIF canvas pixel budget from
`a296ce8`, publishes the `SYSTEM_RAM` memory map and cheat-search window from
r24, and moves MIDlet construction onto the VM thread (`a2abdee`) with the
blocked-constructor watchdog tuning from `68bffb9`; on top of that it includes
the r22 real-time pacing and the M3G Gouraud-textured span fast path (see
`docs/PERFORMANCE.md`), so they include the load-lifecycle fix, the
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
| Multi-touch | every finger reported by the frontend, Nokia `MultipointTouch` listener |

The keyboard callback accepts `0`–`9` and numeric keypad digits, `*` and `#`,
Enter/Space for FIRE, arrows, Backspace for SOFT_RIGHT, Tab for SOFT_LEFT and
Escape for CLEAR. RetroArch hotkeys or focus settings can intercept these keys.

The core follows `RETRO_DEVICE_ID_POINTER_COUNT` and reports every finger the
frontend sends (up to eight). Finger id 0 stays the MIDP `Canvas` pointer; the
other ids are visible to titles that use the Nokia `MultipointTouch` listener
(see `docs/notes/2026-11-16-multitouch-support.md`). Frontends that only report
a mouse (count 0/1) behave exactly as before.

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

There is no determinism claim. Netplay and runahead are not advertised; use
manual states and the game's own save system. Frontend rewind works in a bounded
sense: RetroArch's rewind buffer is fed from the same `retro_serialize()` path,
so it only fits when the Java heap budget keeps snapshots small -- see Rewind
below and `tools/check_libretro_rewind.sh` for the measured setup. Snapshot
sizes include the VM heap budget and can be large, but the frontend's own
state compression keeps them small on disk: an auto-heap `loopdemo` state is
140,816,472 bytes raw and 170,087 bytes as the `.state.auto` file RetroArch
writes. `tools/check_libretro_retroarch_state.sh` proves the real frontend path
end to end -- first launch auto-saves on unload, second launch auto-loads and
the core logs `resumed from a save state`. The
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
rumble interface is available (see Rumble above). Cheats are supported in a
core-specific form over the exposed Java arena (see Cheats below); disc
control, netplay, runahead and achievements are not implemented. The arena is
also exposed as `RETRO_MEMORY_SYSTEM_RAM` so a frontend's own cheat search can
scan and patch it. The core uses one process-wide instance and loads one
game at a time. Compatibility and
performance vary by title, handset assumptions and host CPU.

## Feasibility limits

These are deliberate non-features, not untested paths. The libretro API can
advertise each of them, and claiming support the core cannot honour would be
worse than refusing it.

- **Rewind** — `retro_serialize()` is a request posted to the VM worker, not
  a synchronous frame snapshot: the frontend thread hands over the request and
  the worker copies the whole Java heap, both framebuffers and VM state. The
  snapshot therefore costs the heap budget: about **23 MB with
  `nano_j2me_heap=16`**, 40 MB at 32, and ~140 MB with the auto heap on a big
  host. RetroArch's rewind ring buffer must hold at least one snapshot
  (`rewind_buffer_size` in MB; the default 67 MB buffer at granularity 2 holds
  the 16 MB-heap snapshot). Measured headless on the released core with
  `nano_j2me_heap=16` and `rewind_granularity=2`: 600 loopdemo frames take
  **2730 ms with rewind versus 491 ms without**, i.e. the frontend pays about
  **7.4 ms per stored frame** (snapshot size 25,473,088 bytes, 300 snapshots).
  That is what decides whether rewind feels smooth: at granularity 2 it eats
  roughly half of a 60 Hz frame, so it works but competes with the emulation,
  and the ring holds only about two seconds of history in the default buffer.
  With the auto heap the snapshot is ~140 MB: the frontend reports "rewind
  buffer capacity insufficient" and cannot store history; a mock-frontend
  measurement of the bare request path (`tests/test_libretro_frontend
  --serialize-timing`) shows ~1 s per save/restore at that size, so the
  capacity error is honest. Recommended setup if rewind is wanted:
  `nano_j2me_heap=16` plus a rewind buffer of at least twice the snapshot
  size, and a coarser `rewind_granularity` to trade history resolution for
  buffer use and CPU -- `tools/check_libretro_rewind.sh` runs exactly that
  check and now also prints the measured per-stored-frame cost. History
  fidelity is still VM-only: a restored VM keeps newer
  RecordStore/FileConnection data and loses native media handles, so rewind is
  a convenience, not a validated guarantee. Manual save states remain the
  supported mechanism.
- **Runahead** — runahead requires running frames ahead of presentation and
  restoring a state every frame deterministically. The asynchronous worker,
  host wall-clock timers, external file side effects and Java scheduling
  nondeterminism break that contract, so the frame that is finally presented
  could differ from the one run at the same input. The cost side is measured
  too: even at `nano_j2me_heap=16` a snapshot is ~7.4 ms, and a two-frame
  runahead needs at least two of them per displayed frame, so it would spend
  the whole 60 Hz budget before any emulation runs. Not advertised.
- **Netplay** — rollback netplay needs cheap, deterministic snapshots and a
  stable input-to-frame mapping. The same nondeterminism, the multi-megabyte
  state and the single process-wide VM make lockstep/rollback synchronization
  infeasible in this design. Not advertised.
- **Third-party cheat formats** — Game Genie/GameShark/Action Replay style
  codes assume a flat address space with fixed addresses; the J2ME runtime's
  address space is a managed object heap, so none of those formats can be
  decoded. The core instead supports one documented form (`<arena offset>:<hex
  bytes>`, see Cheats) fed by an offset from a frontend cheat search, and it
  ships no cheat database. This does not stop a frontend's search-and-write
  workflow either.
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

## Cheats

The core exports `retro_cheat_set()`/`retro_cheat_reset()` and advertises
`cheats = "true"` in its metadata. A libretro cheat code is normally an
address/value pair over a flat address space, and a J2ME runtime has none:
values live in managed Java objects whose addresses a code format cannot know.
The cheat form is therefore core-specific and addresses the exposed arena
window directly:

```
<arena offset>:<hex bytes>
```

- `<arena offset>` is a hexadecimal byte offset into the
  `RETRO_MEMORY_SYSTEM_RAM` window (frontend cheat search returns such offsets,
  and the search itself works on the same window). `@` is accepted as a prefix
  and `<offset> = <bytes>` also works.
- `<hex bytes>` is one to 16 bytes written at that offset, e.g.
  `1A2B0:05` or `7ff000:0000FFFF`.
- The bytes are re-applied at the start of every `retro_run()`, so a game that
  writes the old value back does not cancel the cheat. Disabling the cheat
  (frontend passes `enabled = false`) or `retro_cheat_reset()` stops the
  writes.
- Malformed codes are ignored with a warning in the core log.

The parser and the per-frame re-apply live in the runtime
(`src/core/nj_cheat.c`, `nj_vm_cheat_add/load/disable/clear`), so the same
codes work outside the frontend too: the headless CLI accepts
`--cheat "<arena offset>:<hex bytes>"` (repeatable) and `--cheat-file <path>`
(one code per line, `#` comments allowed), and the libretro port, the runner
and the Android build share one table per VM.

A cheat patch can corrupt any object in the arena (the window is raw and
unsynchronised, see Exposed memory), so the offset must come from a deliberate
cheat search, never from a guessed address. Cheats are a local convenience
feature: the VM heap is not a save file, a restore replaces the arena, and a
cheat applies to whatever object currently occupies the offset. No cheat
database is shipped.

The runtime also has the engine side of an app-level cheat search
(`nj_vm_cheat_find`/`nj_vm_cheat_filter`/`nj_vm_cheat_read` in
`src/core/nj_cheat.c`): it finds a byte pattern in the arena, narrows a
candidate list and reads current values, returning the same arena offsets a
code names (`docs/notes/2026-11-19-cheat-search.md`). This is for shells such
as the Android in-game panel; the libretro core does not export it over the
libretro API because frontends already run their own search over the
`RETRO_MEMORY_SYSTEM_RAM` window.

The CTest gate `check_libretro_cheat` exercises the API end to end: it
enables a patch near the unused tail of the arena, verifies the write, verifies
it is re-applied after the game overwrites the byte, and verifies
disabling/reset stop it.

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
# Frontend rewind smoke check.  RetroArch snapshots the whole Java heap, so
# this only fits with a small heap budget; the script asserts the rewind buffer
# initializes and the snapshots fit, and prints the wall-time cost.
tools/check_libretro_rewind.sh \
  build/libretro-check/libretro/nano_j2me_libretro.so tests/fixtures/loopdemo.jar 600
# Real-frontend save-state round trip: launch 1 lets RetroArch auto-save on
# unload, launch 2 auto-loads the same file and must log
# "[nano_j2me] resumed from a save state".
tools/check_libretro_retroarch_state.sh \
  build/libretro-check/libretro/nano_j2me_libretro.so tests/fixtures/loopdemo.jar 150
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
vibration, repeated load/unload and save/load cases. The CTest gate
`check_libretro_multitouch` drives two independent fingers (pointer count 2,
per-index coordinates) through the real core with
`tests/fixtures/multitouchdemo.jar` and requires both ids to reach the Java
MultipointTouch listener while only the first finger owns the MIDP Canvas
pointer. `check_libretro_3d_keys` uses the harness `--keys` script to press the
corpus key sequence for `浴火银河2完整版_7689.jar` and requires an animated,
non-black M3G scene (917 frames, 183 distinct frames); it skips when the
gitignored `tests/games` corpus is absent. The corpus visual probe
(`tools/check_libretro_corpus_visual.py`) loads the title's
`tools/corpus_keys/<jar stem>.txt` when it exists and otherwise falls back to
the sweep's generic `keys.txt`, so every title receives input and menu-gated
titles are judged on their gameplay screen instead of a static menu; it records
the distinct-frame count and max non-black pixels per title so probe output can
distinguish "animated gameplay" from "a static screen that still passes with
`--static-ok`". `--keys` maps fire/soft1/soft2/arrows plus digits `0`/`5`
(core options Select/Start) and `pressed@X,Y` taps; the headless CLI keeps the
complete key set and stays authoritative. A real RetroArch smoke
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
cannot see. At the time of writing, with the curated-or-generic key scripts
above, it reports 385 titles -> 382 ok rows and 3 documented known limitations,
0 unexplained failures, and 380 animated / 5 static rows (the static screens
and the slow loader are classified in
`docs/notes/2026-11-17-static-title-classification.md`; the two load-timeout
limitations and the `毁灭战士3_10598` slow loader are listed in the probe's
`KNOWN_LIMITATIONS`). A longer 1,800-frame (30 s virtual) run of the same
probe on the 0.5.13 package reports 372 titles -> 370 ok rows and the same two
known limitations, 0 unexplained failures, 3 static rows (the carrier-SMS
dialog, the pool attract/menu screen and the music prompt) and a median of 76
distinct frames per title, so the long-run state and crash behaviour matches
the 600-frame numbers. The two titles that need the `nokia6230` profile load
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

MIDlet vibration reaches the frontend: `Display.vibrate()` and the Nokia
`DeviceControl.startVibra()`/`startVibra` paths call the port `vibrate` hook,
which records the request, and `retro_run()` forwards it through
`RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE` as `RETRO_RUMBLE_WEAK` (full strength
while active, 0 when the requested duration ends). The port logs
`MIDlet vibrate(N ms)`, `rumble set on/off`, and `frontend has no rumble
interface` (at most a few times) so a missing frontend implementation is
visible in the log instead of being silent.

### Android frontends without a rumble interface

RetroArch 1.19.1 on Android does not answer
`RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE` (its `enable_device_vibration`
setting only drives the frontend's own haptics), so the rumble callback is
`NULL` there and a core that relies on it never buzzes. Since 0.5.10 the core
falls back to the Android `Vibrator` service through JNI when the frontend has
no rumble interface: it resolves `JNI_GetCreatedJavaVMs`, attaches the calling
thread, calls `ActivityThread.currentApplication()`, obtains
`getSystemService("vibrator")` and unwraps the `VibratorManager` that Android
12+ returns into its default `Vibrator`, then calls `vibrate(ms)`/`cancel()`.
The fallback only engages when the libretro rumble callback is missing; a
frontend that provides the interface keeps the libretro path. Log lines:
`using the Android vibrator directly` on first use, and
`no rumble interface and no Android vibrator (step N)` when the fallback
itself is unavailable (N = 1 resolve the JavaVM, 2 create it, 3 find
`ActivityThread`, 4 obtain the Application, 5 obtain the vibrator service,
6 obtain `Vibrator.vibrate`). Verified in RetroArch 1.19.1 on a Xiaomi
23049RAD8C (Android 15): a game calling `Display.vibrate(500)` produces
`MIDlet vibrate(500 ms)` followed by `rumble set on (android vibrator)` and
the phone buzzes; `3D拉力赛-进化汉化版.jar` was the test title.

Verified end-to-end with the headless frontend (which provides a rumble
callback): a probe that calls `Display.vibrate(600)` four times produces
`MIDlet vibrate(600 ms)` followed by `rumble set on (weak, 0xFFFF)` and
`rumble set off` at the end of each request.

The Nokia path is reached through reflection (`Class.forName("com.nokia.mid.ui.DeviceControl")`
then `getMethod("startVibra", int.class, long.class).invoke(...)`), so the core
needs the CLDC 1.1 reflection subset: `Integer.TYPE`/`Long.TYPE`/... plus
`Class.getMethod()` and `java.lang.reflect.Method.invoke()`. Those were added
to the runtime on 2026-11-15 (see `tests/test_reflect.c`); before that the
call failed with `NoSuchFieldError: java/lang/Integer.TYPE` and titles that
only use the vendor API for vibration stayed silent. With the reflection
subset in place the probe logs `VIBE nokia startVibra 800` and the frontend
receives a fifth `RUMBLE effect=1 strength=65535` / `strength=0` pair.

Reminder for Android: RetroArch only forwards rumble when its own
`enable_device_vibration` setting is on (`retroarch.cfg`, default off), so a
correct core can still stay silent until that switch is enabled.

## Loading robustness and the async constructor (2026-11-15)

`retro_load_game()` used to construct the MIDlet synchronously on the frontend
thread. A title whose constructor runs its own loading screen (paint + sleep
loop, for example a 3D splash) therefore blocked the frontend inside
`retro_load_game()` until the constructor returned: `毁灭战士3_10598.jar`
painted 991 frames over 29 s there, and the mock harness (and any frontend with
a load watchdog) killed the core before it ever returned.

The constructor now runs on the VM thread. `retro_load_game()` only validates
the manifest class (`nj_class_lookup`) and waits up to 250 ms so a constructor
that throws is still reported as a load failure; the VM thread then switches
the phase to `RUN` itself before `startApp()`. A slow constructor keeps the
load clock and cannot make `retro_run()` wait for a frame it cannot produce.

The load clock was also made honest: while loading, the port's `millis()` is
now the fast-forwarded `load_ms` plus the wall time elapsed since the load
began, so a constructor that busy-waits on `System.currentTimeMillis()` makes
progress at real speed, and load sleeps still yield one real millisecond so a
loader thread gets scheduler time.

The blocked-call watchdog became tunable for this case: `nj_vm.call_block_ms`
(default 5000 ms) bounds how long a C caller waits for a parked Java call to
resume, and `nj_vm_set_call_block_ms(vm, 0)` waits until the VM is stopped.
The libretro port sets 0 while constructing the MIDlet, because some titles run
their **whole game loop inside the constructor**: `毁灭战士3_10598.jar` starts a
render thread and then `Thread.join()`s it from its constructor, so the
constructor only returns when the player quits. With the five second watchdog
such a title was killed while it was running normally; now it keeps running for
as long as the frontend keeps it loaded, and unloading ends the constructor
call as a normal teardown (no error is logged).

The consequence for that title is save states: the VM thread is inside a C call
for the whole game, so `nj_vm_can_save()` is never true at a frame boundary and
`retro_serialize` refuses (`save-state request refused (-8); the game
continues`). The game itself loads, renders and accepts input. This is recorded
as a known limitation in `tools/check_libretro_corpus_visual.py`
(`KNOWN_LIMITATIONS`), which reports it separately from pass/fail instead of
silently accepting a failure.

Corpus visual sweep after the change: **371 titles: 370 passed, 1 known
limitation, 0 failed** (`tools/check_libretro_corpus_visual.py` with
`--frames 300`, harness save-state round trips included). Regression checks
(仙剑奇侠传, 雷电4, 超级马里奥, test_vm/test_thread_lifecycle) stay PASS, and
`tests/test_vm.c` covers the watchdog setter contract.

### Known issue: early saves with the async load path

Building the MIDlet on the VM thread also means the frontend can start asking
for frames before the constructor's work (including font resolution) has
settled. A libretro state saved during the first seconds of
`快打旋风_10825.jar` is refused on restore with `save-state request refused
(-7)` even though the frontend re-signs it, and that refusal path currently
ends the session. The pre-async-load cores (r24, `a296ce8`) restore the same
state fine, and saves taken after a few seconds work on every core. Details,
reproduction commands and the working hypothesis are in
`docs/notes/2026-11-15-libretro-async-load-early-state.md`; the r25 release
package is held until this is resolved or accepted as a documented limitation.
