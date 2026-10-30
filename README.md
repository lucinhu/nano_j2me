# nano_j2me

**English** | [简体中文](README.zh-CN.md)

[![latest release](https://img.shields.io/github/v/release/lucinhu/nano_j2me?sort=semver&label=release&color=blue)](https://github.com/lucinhu/nano_j2me/releases/latest)
[![licence](https://img.shields.io/github/license/lucinhu/nano_j2me)](LICENSE)
[![tests](https://img.shields.io/badge/tests-131%20passing-brightgreen)](docs/VERIFICATION.md)
[![sanitized](https://img.shields.io/badge/sanitized-ASan%20%2B%20UBSan%20%2B%20LSan-brightgreen)](tools/run_sanitized_tests.sh)
[![C99, no dependencies](https://img.shields.io/badge/C99-no%20dependencies-informational)](#design-commitments)

A J2ME (CLDC 1.1 / MIDP 2.0) runtime written in plain C99, built to be embedded.

It runs MIDlet suites — the `.jar` games written for feature phones — on
anything with a framebuffer and a few hundred kilobytes of RAM: Linux, Android,
an ESP32-S3, or whatever you are holding.

```
                    ┌──────────────────────────────────────────┐
   game.jar  ──────►│  nj_app      ZIP reader, manifest,       │
                    │              class + resource source    │
                    └───────────────┬──────────────────────────┘
                                    │ .class bytes
                    ┌───────────────▼──────────────────────────┐
                    │  nj_class    parser, linker, constant-   │
                    │              pool cache (inline caches)  │
                    └───────────────┬──────────────────────────┘
                                    │
   ┌────────────────┐   ┌───────────▼───────────┐   ┌──────────────────┐
   │  nj_heap       │◄──┤  nj_interp            ├──►│  nj_thread       │
   │  arena + mark  │   │  CLDC 1.1 bytecodes   │   │  green threads,  │
   │  & sweep GC    │   │  exceptions, calls    │   │  scheduler       │
   └────────────────┘   └───────────┬───────────┘   └──────────────────┘
                                    │ natives
                    ┌───────────────▼──────────────────────────┐
                    │  the class library, written in C         │
                    │  java.lang · java.util · java.io ·       │
                    │  javax.microedition.lcdui · .game · .rms │
                    └───────────────┬──────────────────────────┘
                                    │ nj_gfx (software rasterizer)
                    ┌───────────────▼──────────────────────────┐
                    │  nj_port   ← the ONLY OS-facing surface  │
                    │  headless · SDL2 · Android · ESP32-S3    │
                    └──────────────────────────────────────────┘
```

## What's new in 0.4.0

**0.4.0 is the release that runs Minecraft PE, and the one that stops three
kinds of "the picture is wrong" from hiding in the rasteriser.** Since 0.3.0 the
runtime gained a UDP datagram connection, automatic heap sizing on Android, and
the JSR-75 and JSR-135 pieces the corpus actually names. Most of the release is
what one demanding title exposed - and the gates that keep it exposed.

* **Minecraft PE 0.6.1.** It boots, generates a world, draws it, and its LAN mode
  joins over the new UDP `DatagramConnection`. The picture needed three fixes in
  the JSR-184 sampler: texture coordinates follow the API's texel-centre
  convention (texel *i* covers `[i/W,(i+1)/W)`), so a UV on an atlas tile
  boundary no longer samples the previous tile; the level filter of
  `Texture2D.setFiltering()` is honoured, so `FILTER_BASE_LEVEL` - the class
  default, and what every title asks for - samples level 0 instead of a mip that
  averages neighbouring tiles; and JSR 184's paletted `Image2D` constructor
  expands its table, so `environment/clouds.png` comes out as clouds instead of
  a field of palette indices. Water is clean, clouds are back.
* **Frame pacing that lets a title's own threads run.** With a frame cap the
  pacer used to sleep, which parked the whole VM: a game that generates and
  renders on its own thread got 8 ms of every 33 ms at 30 fps. The wait is now
  spent in the scheduler (85x the application's work per presented frame while
  loading), and the cap is an absolute microsecond deadline, so `--fps 60` no
  longer runs at 62.5 and `--fps 30` lands on 30 (Minecraft's 27.4 -> 29.5 fps).
* **JSR-75 and vendor classes the corpus names.** `HttpsConnection` /
  `SecurityInfo` / `SecureConnection` / `Certificate`, with an `http://`
  connection that is deliberately *not* an `HttpsConnection`; the PIM contact
  list (opens, and is honestly empty - what a fresh phone answers); Samsung's
  `SM`/`SMS` (an `IOException` instead of a fabricated success); and JSR-135
  `TempoControl`/`RateControl`, which re-time a MIDI song for real.
* **Ports.** The ESP32-S3 image links again - its hand-written IDF source list
  had gone stale, and `check_port_sources` now compares it with the desktop list
  in both directions - and needs no resolver to format a datagram's address.
  Android sizes the Java heap to the title by default.
* **Gates.** Eight trimmed configurations build warning-free under `-Werror`
  (that build caught a real `AffineTrans.setRotation` bug: a 60-degree rotation
  shrank the model by half), the 400-title fast gate isolates each title's save
  directory and reaches fatal 0 / known 4, and an exception that escapes into
  the frame loop now prints where each thread was.
* **Numbers.** 131 tests in `ctest`, the sanitized suite 131/131 green, and
  **397 of 399** collected real games run - the two failures are the titles'
  own (a truncated JAR and a licence check).

Full changelog: [v0.4.0 release notes](https://github.com/lucinhu/nano_j2me/releases/tag/v0.4.0).

## Screenshots

**Desktop, SDL2 port.** The full 12-key phone keypad, drawn by the port because a
D-pad alone runs a platformer and nothing else.

<p>
<img src="docs/images/readme-sdl2-1942.png" width="240" height="320" alt="1942 Air Combat mid-mission over the English coast, with the on-screen keypad">
<img src="docs/images/readme-sdl2-xianjian.png" width="240" height="320" alt="Chinese Paladin: an isometric inn scene with a dialogue box">
<img src="docs/images/readme-sdl2-aoe.png" width="240" height="320" alt="Sango: the title screen with a fiery battlefield menu">
</p>

**Android.** A launcher that shows each game's own name and icon, and the game
running with one of the configurable floating keypads.

<p>
<img src="docs/images/readme-android-launcher.png" width="200" height="400" alt="Nano J2ME launcher listing four games">
<img src="docs/images/readme-android-game.png" width="200" height="400" alt="1942 Air Combat on Android with the floating keypad, fire key held">
</p>

**RetroArch.** The same runtime as a libretro core, loaded in the RetroArch
frontend.

<p>
<img src="docs/images/readme-retroarch.png" width="360" height="480" alt="RetroArch running the nano_j2me core with a game loaded">
</p>

**ESP32-S3.** A real MIDlet inside Espressif's `esp-emu`: the internal-RAM board
profile, the frame path streamed over UART and decoded to this PNG.

<p>
<img src="docs/images/esp-emu-1942-menu.png" width="180" height="240" alt="1942 Air Combat running on the emulated ESP32-S3">
</p>

**Minecraft PE 0.6.1, new in 0.4.0.** A world the game generated itself, with
the cloud layer textured out of the paletted `Image2D`, clean water, and the
title's own frame cap keeping up.

<p>
<img src="docs/images/readme-minecraft.png" width="240" height="320" alt="Minecraft PE world with clouds, water and grass">
<img src="docs/images/readme-android-minecraft.png" width="200" height="400" alt="Minecraft PE running on the Android build">
</p>

**Video, new in 0.3.0.** 1942's MPEG-4 intro, decoded frame by frame and drawn
as a direct-video overlay over the Canvas; the movie's own AMR-NB track plays
with it.

<p>
<img src="docs/images/readme-movie-1942.png" width="240" height="320" alt="1942 Air Combat's MPEG-4 intro movie playing in the emulator">
</p>

**Micro3D on real titles, new in 0.3.0.** 鬼泣3D's mission-1 combat and
浴火银河's flight scene, rendered through the Mascot Capsule Micro3D command
list and figures.

<p>
<img src="docs/images/dmc-micro3d-combat.png" width="240" height="320" alt="Devil May Cry 3D mission combat rendered by Micro3D">
<img src="docs/images/gof-micro3d-flight.png" width="240" height="320" alt="Galaxy on Fire flight scene rendered by Micro3D">
</p>

## Design commitments

These are not aspirations; each one is enforced by a test or a gate.

**Pure C99, no dependencies.** No C++, no libc requirement (freestanding builds
set `NJ_USE_LIBC=0`), no zlib, no libpng, no JPEG library. DEFLATE, PNG and the
rasterizer are all implemented here, because a microcontroller will not have
them.

**Zero mutable global state.** Every byte of runtime state belongs to an
`nj_vm`. `nj_vm_destroy()` releases all of it and *returns how much it
released*, so a caller can assert on it. A MIDlet can therefore be started,
stopped and restarted indefinitely on a device that never reboots, and several
VMs can coexist in one process. `tools/check_no_statics.sh` compiles the whole
runtime and fails if any object file defines a symbol in a writable section.

**One file to port.** `include/nj2me/nj_port.h` is the entire operating-system
surface: about ten callbacks. A new platform is a new `nj_port` implementation,
nothing else. See [docs/PORTING.md](docs/PORTING.md).

**Correctness first, then size.** The interpreter implements the full CLDC 1.1
instruction set including `long`, `double`, exceptions, `synchronized`,
`multianewarray` and the legacy `jsr`/`ret`. Those are the parts small JVMs
usually skip, and they are exactly the parts old games use.

## Status

| Component | State |
|---|---|
| ZIP/JAR reader, DEFLATE inflate | **done** — fuzzed, ASan/UBSan clean |
| Class-file parser and linker | **done** |
| Bytecode interpreter (CLDC 1.1) | **done** |
| Object model, arena heap, mark & sweep GC | **done** |
| Cooperative threads and scheduler | **done** |
| Exceptions, C-to-Java bridge | **done** |
| `java.lang` core, `Math`, `Thread`, wrappers | **done** |
| `javax.microedition.lcdui` | **done** |
| `javax.microedition.lcdui.game` | **done** |
| `java.util`, `java.io`, RMS | **done** |
| `javax.microedition.io` | **done** — `file://`, `http(s)://` (one optional `http_request` hook, the Android port wraps `HttpURLConnection`) and `socket://` (four optional `socket_*` hooks over TCP); any other scheme is refused by name |
| `javax.bluetooth` | **discovery done** — `LocalDevice`, `DiscoveryAgent`, `UUID`, `DeviceClass`, `DataElement`, `ServiceRecord` and `RemoteDevice` over nine optional `bt_*` hooks; RFCOMM/L2CAP servers are the port's, and a port with no radio gets `BluetoothStateException` |
| `javax.microedition.sensor` (JSR 256) | **done** — `findSensors`, connections, channels, ranges and data listeners, with the sensors themselves supplied by the port; conditions are refused rather than faked |
| Software rasterizer | **done** |
| `javax.microedition.m3g` (JSR 184) | **done** — scene graph, `.m3g` parser, keyframe tracks (TRS/color), skinned meshes, camera/light/fog, and a software rasterizer with depth, perspective-correct textures, mipmaps, two-sided lighting and texture transforms; the corpus audit closed every gap |
| `javax.microedition.media` (JSR 135) | **done** — tones, MIDI, WAV, AMR-NB, MP3, and H.263 / MPEG-4 Part 2 video Players with `VideoControl`; playback is paced by the movie's real-time clock and a movie's own AMR-NB track plays with it |
| Mascot Capsule Micro3D v3 | **done** — figures, affine/parallel/perspective projections, command lists (per-command and per-face colors), MTRA/MBAC; real titles render |
| Execution engines | interpreter, plus opt-in baseline RV32 and ARM64 JITs (`NJ_ENABLE_JIT=1`) sharing a bounded per-VM hot-block cache; see [docs/JIT.md](docs/JIT.md) |
| Decoders: PNG, JPEG, GIF, DEFLATE, WAV, AMR, MP3, MIDI, H.263, MPEG-4 Part 2, bitmap fonts | **done**, each behind its own switch (`NJ_ENABLE_*`) and all behind a pluggable registry, so a port can cut one or replace it with the platform's |
| Ports: headless (reference) | **done** |
| Ports: SDL2 | **done** |
| Ports: libretro / RetroArch | **done** — one native shared object that RetroArch loads as a core: RGB565 video, resampled audio, joypad/keyboard/touch input, core options for the handset profile, rotation, heap and button bindings, and per-game RecordStore/FileConnection saves. No JVM, no subprocess. See [docs/LIBRETRO.md](docs/LIBRETRO.md) |
| Ports: Android (NDK + JNI) | **done** — one APK for Android 5.0 (API 21) and up, 4 ABIs, a launcher that shows each game's own name and icon and opens `.jar` files from a file manager, three configurable floating keypad layouts, sound through AAudio or OpenSL ES with MIDI handed to the phone's own synth, multi-touch, and auto-detected screen size; 2.79 MiB for all four ABIs (858 KiB for arm64 alone), runs on an API 21 emulator and an API 36 emulator |
| Ports: ESP32-S3 (ESP-IDF, Xtensa) | **done** — real IDF build; boots the ROM, the full class library and a MIDlet on a 16 MB flash / 8 MB PSRAM board under Espressif's `esp-emu` (`tools/run_esp_emulator.sh`: `BENCH sum=-136042548`, 105,717,067 bytecodes — the desktop total), and runs the internal-RAM profile under QEMU. `tools/run_esp_emulator_ui.sh` also demonstrates the display, input and storage paths in the same emulator (240x320 frames decoded to PNG, keys injected over UART, JAR read from a mounted card). See [docs/PERFORMANCE.md](docs/PERFORMANCE.md#esp32-s3-measured-on-the-real-architecture) and [docs/FOOTPRINT.md](docs/FOOTPRINT.md#what-esp-emu-does-and-does-not-model-the-ui-demo) |
| Ports: ESP32-C3 (ESP-IDF, RISC-V) | builds, links and boots; needs PSRAM for the VM |
| Test suite | **131 tests** (`ctest`) |
| Sanitized suite | **131/131 passing** under ASan + UBSan + LSan (`tools/run_sanitized_tests.sh`) |

Verified end to end: **Tumbleweed** (`net.frog_parrot.jump.Jump`, Apress 2004)
plays on the headless port at 176x208 — scrolling ground, an animated sprite, a
live score and countdown. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).
The collected corpus - 399 real games - stands at **397 running / 2 title-side
failures** (a truncated JAR and a licence check); see
[docs/CORPUS.md](docs/CORPUS.md).

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build                    # compiles.  Two seconds.  Nothing else.
```

To run the tests, generate their data once first:

```sh
cmake --build build --target nj_fixtures   # needs a JDK; Pillow for PNG/JPEG
ctest --test-dir build --output-on-failure
```

**The fixture step is separate on purpose.**  It runs the PNG/JPEG/zip
generators, compiles a dozen MIDlet JARs with `javac`, and - only if you ask for
it - downloads two third-party games from GitHub.  Making that a dependency of
every build turned `cmake --build` into a multi-second job that had nothing to do
with compiling and would fail without a network.  It is now:

* **off by default** - `cmake --build build` is a compile, full stop;
* **explicit** - `--target nj_fixtures` when you want it;
* **network-free by default** - `-DNJ_FIXTURES_FETCH_GAMES=ON` is what allows the
  two downloads, because a build should not need GitHub to be reachable;
* **guarded** - `ctest` reports which command to run instead of failing 25 tests
  one at a time with "cannot open tests/fixtures/...".

Set `-DNJ_GENERATE_FIXTURES=ON` at configure time to get the old behaviour back.

The headless port is always available and is what the test suite uses. The SDL2
port is built automatically when `libsdl2-dev` (or the equivalent) is installed.

The libretro core is built by default as `build/libretro/nano_j2me_libretro.so`
(`-DNJ_BUILD_LIBRETRO=OFF` disables it). Copy it and its `.info` file into
RetroArch's `cores/` directory and load a `.jar`; controls, core options, save
locations and limitations are in [docs/LIBRETRO.md](docs/LIBRETRO.md).  A core
is architecture-specific, so `tools/build_libretro_cores.sh` also cross-builds
installable cores for Android (arm64-v8a, armeabi-v7a, x86_64) and ARM Linux
(ARM64/ARMv7) - run it with `--list`.

## In RetroArch

Every release ships the six cores as
`nano_j2me-0.4.0-libretro-cores.tar.gz`, so trying it does not need a
cross-compiler.  For the core on this machine:

```sh
cmake -S . -B build -G Ninja
cmake --build build --target nano_j2me_libretro -j
```

Copy `nano_j2me_libretro.so` and `nano_j2me_libretro.info` (they must stay
together) into RetroArch's `cores/` directory, pick **Mobile - J2ME
(nano_j2me)** under *Load Core*, then load the game's `.jar`.  Core options hold
the handset profile, rotation, heap size, sound and button bindings; saves go to
`<RetroArch save dir>/nano_j2me/<game folder>/`.  From a shell it is:

```sh
retroarch -L ~/.config/retroarch/cores/nano_j2me_libretro.so game.jar
```


## On-screen keys

The SDL2 window draws **every J2ME key**, because a D-pad alone runs a platformer
and nothing else:

![on-screen keys](docs/images/sdl2-keys.png)

* **soft-left / soft-right** top corners, where a phone had them
* **D-pad** bottom-left, with **A / B / C / D** under it
* **fire** in the bar between the D-pad and the game
* **the twelve-key phone pad** on the right, with **clear** under it and
  **volume / power** above it

Click or tap them. A press that lands on a key is a key press and NOT also a
pointer press, so a game with both a pointer UI and a keypad does not see two
events from one tap.

```sh
F12 or Tab          show / hide, at run time
NJ_SDL2_KEYS=0      start with them hidden
```

They are translucent and sit in the letterbox bars, so the game stays fully
visible at the scales the window normally opens at. The port computes that
letterbox itself rather than using `SDL_RenderSetLogicalSize`, precisely so the
overlay can be drawn in window pixels instead of inside the 176x208 the game
owns.

## Running a game

```sh
build/nj2me --port sdl2 game.jar
build/nj2me --port headless --dump-frames out/ game.jar
```

Text comes from six built-in bitmap faces. To draw it with a TrueType file
instead, name one — the em size of each face is fitted to the box the bitmap
face occupied, so no baseline moves:

```sh
build/nj2me --port sdl2 --font /usr/share/fonts/liberation/LiberationMono-Regular.ttf game.jar
```

An unreadable or malformed file is not fatal: the built-in faces stay in place
and a warning is logged. Compile the rasteriser out with `-DNJ_ENABLE_TTF=0`
to keep only the bitmaps. See `docs/PORTING.md` for the embedding details.

## Telling a title which phone it is on

A J2ME title asks `System.getProperty("microedition.platform")` to find out
which handset it is running on, and some of them refuse to start on an answer
they do not know — `1945空战英雄_6105` wants a platform that begins with
`"Nokia"`. Name a built-in handset profile and the runtime answers as that
phone, identity numbers and vendor keys included:

```sh
build/nj2me --list-devices                              # what is built in
build/nj2me --device nokia6230 game.jar                 # run as a Nokia 6230
build/nj2me --prop microedition.platform=Nokia6230/05.50 game.jar
```

`--prop key=value` may be repeated and wins over the profile; the default is
`generic`, which answers only the port's own identity. The IMEI/IMSI values in
the profiles are synthetic — a valid shape, nobody's number. See
[the device section of docs/COMPATIBILITY.md](docs/COMPATIBILITY.md#the-device-a-title-believes-it-is-on)
for what each profile answers and why.

## Verifying the memory contract

```sh
# no writable global state anywhere in the runtime
tools/check_no_statics.sh

# create -> run -> destroy three times, identical teardown, zero residue
build/test_vm tests/fixtures/hello.jar HelloWorld "hello from j2me"

# the same, with the process-level leak checker
gcc -std=c99 -g -O1 -fsanitize=address,undefined -Iinclude \
    -o /tmp/t tests/test_vm.c src/core/*.c src/zip/*.c src/midp/*.c \
    src/port/posix/nj_port_headless.c -lm
ASAN_OPTIONS=detect_leaks=1 /tmp/t tests/fixtures/hello.jar HelloWorld
```

## Repository layout

```
include/nj2me/     the public headers; nj_port.h is the porting contract
src/core/          VM: heap, class loader, interpreter, threads, strings, app
src/midp/          the class library (LCDUI, game, RMS, util, io, MIDlet)
src/gfx/           software rasterizer, bitmap fonts, PNG decoder
src/zip/           DEFLATE inflate and the ZIP/JAR reader
src/port/posix/    headless and SDL2 ports
src/port/android/  NDK port
src/port/esp32/    ESP-IDF component
src/port/libretro/ libretro core (RetroArch)
src/runner/        the runner that ties a port to a MIDlet
tests/             assertion-based test programs
tools/             fixture generators and the no-statics gate
docs/              architecture, porting, compatibility
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the VM works and why
- [docs/PORTING.md](docs/PORTING.md) — bringing it up on a new board
- [docs/LIBRETRO.md](docs/LIBRETRO.md) — running the core in RetroArch
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — what is implemented, what is not, and the failure modes that name themselves

## Licence

MIT — see [LICENSE](LICENSE).  The bundled test fixtures under `tests/` are
generated by this repository's own tools and carry the same licence; the game
JARs that `tools/fetch_test_games.sh` downloads are third-party and are not
redistributed here.
