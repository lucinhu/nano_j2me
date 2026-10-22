# Running nano_j2me as a libretro core

nano_j2me builds a native [libretro](https://www.libretro.com/) core, so it can
run a J2ME MIDlet inside **RetroArch**, RetroDECK, Lakka or any other libretro
frontend.  The core is the whole runtime statically linked into one shared
object: there is no JVM, no subprocess and no helper program.

```
game.jar ──► nano_j2me_libretro.so ──► RetroArch
             (nj_vm + MIDP in C)        video / audio / input
```

The core lives in [`src/port/libretro/`](../src/port/libretro/):

| File | What it is |
| --- | --- |
| `src/port/libretro/nj_port_libretro.c` | the libretro entry points and an `nj_port` backed by the frontend |
| `src/port/libretro/nano_j2me_libretro.info` | RetroArch core metadata (name, extensions, options) |
| `src/port/libretro/libretro.h` | the vendored MIT-licensed libretro ABI header |

## Building

The core is built by default (`-DNJ_BUILD_LIBRETRO=OFF` turns it off):

```sh
cmake -S . -B build
cmake --build build --target nano_j2me_libretro -j
```

The two files a frontend needs are then:

```
build/libretro/nano_j2me_libretro.so
src/port/libretro/nano_j2me_libretro.info
```

The shared object links only the C runtime and the math library (the optional
SDL2 and libcurl ports are compiled into the archive but dropped again by
`--as-needed`, so a frontend does not inherit them).  `cmake --install build`
puts the pair in `${CMAKE_INSTALL_LIBDIR}/libretro`.

## Installing into RetroArch

Copy the `.so` **and** the `.info` beside it into RetroArch's core directory:

```sh
cp build/libretro/nano_j2me_libretro.so \
   src/port/libretro/nano_j2me_libretro.info \
   ~/.config/retroarch/cores/
```

RetroArch discovers the core from the `.info` file; it then appears as
**Mobile - J2ME (nano_j2me)** under *Load Core*.  Start a game with *Load
Content* and a `.jar`, or from the command line:

```sh
retroarch -L ~/.config/retroarch/cores/nano_j2me_libretro.so path/to/game.jar
```

The core uses `need_fullpath`: RetroArch passes the `.jar` path instead of
loading the file itself, because the MIDlet reads resources from the archive
while it runs.

## Other platforms and architectures

A libretro core is a shared library, so it is **not** portable between
frontends the way the C sources are: the frontend can only `dlopen()` a core
built for the same operating system and CPU, and on Android for the same ABI
(arm64-v8a / armeabi-v7a / x86_64) as the RetroArch app.  Desktop x86-64
Linux is the normal build; the other targets come from a small cross-build
driver around the same CMake project:

```sh
tools/build_libretro_cores.sh --list          # available targets
tools/build_libretro_cores.sh android-arm64   # one target
tools/build_libretro_cores.sh android-arm64 android-armv7 android-x86_64
tools/build_libretro_cores.sh linux-aarch64 linux-armv7 linux-x86_64
tools/build_libretro_cores.sh all             # everything this host can build
```

Each target gets its own CMake build directory and an installable copy in
`build/libretro-dist/<target>/` (the core plus its `.info` file):

| Target | Frontend it is for | Toolchain |
| --- | --- | --- |
| `linux-x86_64` | desktop Linux | host gcc/clang |
| `linux-aarch64` | Raspberry Pi OS, Armbian, ARM64 handheld firmware | `zig cc` |
| `linux-armv7` | older ARMv7 hard-float handhelds | `zig cc` |
| `android-arm64` | 64-bit RetroArch on Android (arm64-v8a) | Android NDK |
| `android-armv7` | 32-bit RetroArch on Android (armeabi-v7a) | Android NDK |
| `android-x86_64` | Android x86_64 emulators/Chromebooks | Android NDK |

Only the toolchain for the targets you ask for is needed:

* **Android**: the NDK found via `$ANDROID_NDK_HOME`, or the newest NDK under
  `~/Android/Sdk/ndk` (the same lookup `tools/build_android.sh` uses for the
  normal Android port).  The core is built for API 21+, links only bionic's
  `libc`/`libm`, and needs no `libcurl`/`libSDL2`.  Copy the `.so` and `.info`
  into RetroArch's core directory on the device, then *Load Core* →
  **Mobile - J2ME (nano_j2me)**.  Install the arm64-v8a core in a 64-bit
  RetroArch and the armeabi-v7a one in a 32-bit build; an ABI mismatch simply
  fails to load.
* **ARM Linux**: [zig](https://ziglang.org/) in `PATH` is enough - no
  `aarch64-linux-gnu-gcc` install and no sysroot juggling.  The core is linked
  against **glibc 2.17**, the oldest baseline for ARM64, so it loads on
  Raspberry Pi OS Buster and newer as well as the usual handheld firmware; set
  `NJ_ZIG_GLIBC=2.28` (or another version) if you want a different floor.
  Copy the core to `~/.config/retroarch/cores/` on the device.
* **macOS**: run the normal CMake build on the Mac; the core comes out as a
  `.dylib` and is installed the same way.  The script does not cross-compile
  from Linux because Apple's SDK is not redistributable.
* **Windows**: not a target yet.  The libretro adapter itself already has a
  Win32 thread/mutex shim, but the runtime's hosted-file layer uses POSIX
  `mkdir`/`opendir` for the RecordStore and FileConnection fallbacks, so a
  Windows core needs that port finished first; see *Limitations*.

The cross-builds pass `-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_CLI=OFF
-DNJ_BUILD_SDL2=OFF -DNJ_BUILD_EMBEDDED_PORTS=OFF -DNJ_BUILD_POSIX_NETWORK=OFF`
so a core contains only the runtime and the libretro adapter.  The installed
copies are stripped; `--no-strip` keeps symbols for a crash report.

## Controls

The D-pad is the phone's arrow keypad.  The face buttons map to the J2ME keys a
game actually reads; every binding is a core option, so a title that uses
`GAME_A`/`GAME_B` instead of soft keys can be rearranged without a recompile.

| RetroPad | J2ME key | Notes |
| --- | --- | --- |
| D-pad | UP / DOWN / LEFT / RIGHT | fixed |
| A | FIRE | |
| B | SOFT_RIGHT | the right soft key |
| X | SOFT_LEFT | the left soft key |
| Y | CLEAR | |
| L | GAME_A | |
| R | GAME_B | |
| Select | 0 | |
| Start | 5 | |
| Touch / mouse | pointer events | mapped back through the rotation |

A connected keyboard sends the phone keypad directly: `0`–`9` and the numeric
keypad *(0–9)*, `*` → star, `#` → pound, `Enter`/`Space` → fire, the arrow keys,
`Backspace` → right soft key, `Tab` → left soft key, `Esc` → clear.

## Core options

All options are read when a game is loaded; changing one takes effect on the
next load (RetroArch's *Quick Menu → Core Options*).

| Option | Values | Default | Meaning |
| --- | --- | --- | --- |
| `nano_j2me_device` | generic, nokia6230, sonyericsson-k750, motorola-e398, samsung-sgh-e700, siemens-s65 | generic | the handset profile a MIDlet believes it runs on (system properties, screen size hints, soft-key codes) |
| `nano_j2me_rotation` | 0, 90, 180, 270 | 0 | rotates the finished frame **inside the core**. The MIDlet canvas and its coordinate system stay 240x320; for 90/270 the reported geometry becomes 320x240, so a frontend that honours the reported geometry shows the game in landscape without a second rotation |
| `nano_j2me_heap` | 16, 32, 64, 128 (MB) | 64 | Java heap; the VM's own arena |
| `nano_j2me_sound` | enabled, disabled | enabled | disables both the PCM ring and tone synthesis |
| `nano_j2me_pointer` | enabled, disabled | enabled | delivers touch/mouse as pointer events |
| `nano_j2me_btn_a` … `nano_j2me_btn_start` | any `nj_key` name, or `NONE` | FIRE, SOFT_RIGHT, SOFT_LEFT, CLEAR, GAME_A, GAME_B, 0, 5 | button binding (A, B, X, Y, L, R, Select, Start) |

The canvas geometry reported to the frontend is 240×320, 60 fps, 44.1 kHz
stereo, RGB565.  `nano_j2me_rotation` does not change the reported geometry; a
frontend side rotation of the same value stacks with it.

## Saves

* **RecordStore** (`javax.microedition.rms`) works unmodified.  Files live in
  `<RetroArch save directory>/nano_j2me/<jar name>/`, so two games never share
  a store name.
* **FileConnection** (`javax.microedition.io.file`) is rooted at
  `<same directory>/files/`.
* **libretro save states are not supported.**  `retro_serialize_size()` returns
  0, which is how a core says "I cannot snapshot".  The reason is structural:
  a snapshot (`nj_vm_save`) must be taken while no Java thread is executing, and
  the VM here runs on its own thread so a MIDlet that never returns from
  `startApp()` cannot block the frontend.  RetroArch's save-state and rewind
  features are therefore unavailable; in-game saves are not affected.

## Timing and frontend features

The core presents exactly one frame per `retro_run()` call and produces exactly
735 stereo audio frames (44100/60) per call.  The VM's wall clock is driven by
the frontend's tick count, which makes the usual frontend features behave:

* **pause** — the frontend stops calling `retro_run`; the VM thread parks on its
  virtual deadline, so nothing advances;
* **fast-forward** — more `retro_run` calls per real second advance the virtual
  clock faster, and `Thread.sleep`/`Canvas.serviceRepaints` speed up with it;
* **slow motion** works the same way in the other direction;
* **rewind / run-ahead** need save states and are unavailable.

Games that sleep for long periods (`Thread.sleep(5000)` in a splash screen) are
fast-forwarded during load instead of stalling RetroArch's load screen, but a
MIDlet that never installs a `Displayable` is reported as such and the core
keeps presenting a black frame rather than hanging the frontend.

## Limitations

* **JAR only.**  The MIDlet class comes from the JAR manifest's `MIDlet-1`;
  `.jad` descriptors and `MIDlet-Jar-URL` redirects are not read.  A suite whose
  manifest has no `MIDlet-1` refuses to load.
* **No network.**  The core leaves the HTTP/socket hooks empty, so
  `HttpConnection` and `socket://` fail like a phone with no signal.  Offline
  games are unaffected; scores and downloadable content that need a server are
  not available.
* **No Bluetooth, sensors or vibration.**  Titles that require them get the
  runtime's "not supported" exceptions.
* **Media**: the built-in decoders are used (WAV, AMR, MIDI, tone generation);
  MP3/JSR-135 audio is not decoded by this port.
* **One game at a time** — the libretro ABI has no instance pointer, so the
  process has one core instance; loading another game tears the previous VM
  down first.
* **Windows**: the adapter already has a Win32 thread/condition shim, but the
  runtime's hosted-file layer is POSIX (`mkdir`/`opendir`), so no Windows core
  is produced yet; only the POSIX path is covered by the test suite.

## Architecture notes (for maintainers)

The libretro ABI calls fixed function names with no `userdata` argument, so the
core keeps one process-wide instance; that is the single documented exemption in
`tools/check_no_statics.sh` (which otherwise fails every writable static in
`src/`).  All MIDlet state still lives in the one `nj_vm`, and
`retro_unload_game()` destroys it — a second load starts from zero, which the
test suite checks by loading the same JAR twice in one process.

Internally the core is a small adapter:

* **VM thread** — `nj_vm_create`/`nj_app_open`/`nj_vm_new_midlet`/
  `nj_vm_start_app` and the `nj_midp_frame()` pump run on a dedicated thread,
  because `startApp()` may never return (many titles run their main loop inside
  it).  A frame hook is installed so a MIDlet sleeping inside `startApp()` still
  gets frames while it sleeps.
* **Virtual clock** — `nj_port.millis`/`sleep_ms` read and wait on a clock that
  advances once per `retro_run()` (60 ticks per second).  Load-time construction
  uses a fast-forwarding clock so a splash screen cannot stall the frontend;
  unload uses a real clock plus `nj_vm_set_halt_after_ms()` so a hanging
  `destroyApp()` cannot block RetroArch.
* **Video** — the runtime draws into a 240×320 RGB565 back buffer;
  `display_present` copies the dirty rectangle into a front buffer (applying the
  presentation rotation) under a mutex, and `retro_run` takes its own snapshot of
  the completed frame and hands that to the video callback **outside** the lock.
  Keeping frontend callbacks out of the critical section matters: the VM thread
  takes the same lock for its clock, sleep and present callbacks, and a slow
  frontend callback under the lock starves a MIDlet that runs its loop inside
  `startApp()` (that bug was caught by the loopdemo fixture).
* **Audio** — `pcm_write` resamples (linear interpolation) into a 44.1 kHz
  stereo ring; `tone_play()` synthesises square waves into a second ring; the
  two are mixed and clamped in `retro_run`.
* **Input** — frontend state is polled once per `retro_run`, diffed against the
  previous chord and pushed as `nj_event` key/pointer events; the keyboard
  callback does the same for the phone keypad.

## Verifying it

The core is tested without RetroArch by a mock frontend that `dlopen()`s it:

```sh
cmake --build build -j
ctest --test-dir build -R test_libretro_frontend --output-on-failure
# or, directly:
tools/check_libretro_frontend.sh
```

That harness checks the ABI version, system/AV information, pixel format,
options, one video frame per run (non-null, RGB565 pitch), animation,
non-zero audio for `audiodemo.jar`, joypad and keyboard delivery, load/unload
cycles in one process, clean refusal of invalid content, and the rotation
option.  When a real RetroArch is available,
`tools/check_libretro_retroarch.sh [core.so] [game.jar]` runs the same core
headless (`--video_driver null --audio_driver null`) and greps the core's own
log lines out of RetroArch's log.

The rest of the suite is unchanged: `ctest --test-dir build` and
`tools/check_no_statics.sh` ("OK: no writable statics across …") must stay
green, since the libretro adapter is the only file allowed to keep writable
process state.

## What was verified

On x86_64 Linux with GCC 16.2 and RetroArch 1.22.2 (portable buildbot binary,
`video_driver=null`, `audio_driver=null`, `input_driver=null`):

| Check | Result |
| --- | --- |
| `tools/check_libretro_frontend.sh` (real ABI via `dlopen`, 8 steps incl. loopdemo animation, audiodemo PCM, input, rotation, two load/unload cycles) | 8 passed, 0 failed |
| `tools/check_libretro_retroarch.sh` (alphademo, 600 frames) | PASS, exit 0, `running` + `run ended` markers |
| real titles: 波斯王子经典版, 1942空战, 3D城市桌球, loopdemo, audiodemo | PASS, exit 0, no crash or hang |
| core options reaching the core (`nano_j2me_device = "nokia6230"`, `nano_j2me_heap = "16"`) | logged as `running AlphaDemo (heap 16 MB, device nokia6230)` |
| `ctest --test-dir build` | 88/88 tests pass |
| `tools/check_no_statics.sh` | OK (56 objects; the core file is the documented exemption) |
| `tools/build_libretro_cores.sh all` | linux-x86_64, linux-aarch64, linux-armv7, android-arm64, android-armv7, android-x86_64 built |
| cross-built ELF checks | Android cores are API 21 aarch64/EABI5/x86-64 with only `libc`/`libm`; ARM Linux cores are glibc 2.17 and export all 25 `retro_*` symbols |
| cross-built x86_64 core re-run through `tools/check_libretro_frontend.sh` | 8/8 steps pass (the release/stripped artifact, not just the dev build) |
| Windows | adapter source compiles with the Win32 shim, but the runtime's POSIX file layer blocks a full Windows build (see *Other platforms*) |
