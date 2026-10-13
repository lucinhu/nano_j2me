# nano_j2me

**English** | [简体中文](README.zh-CN.md)

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
| Software rasterizer | **done** |
| PNG and baseline JPEG decoders, bitmap fonts | **done**, behind a pluggable codec registry |
| Ports: headless (reference) | **done** |
| Ports: SDL2 | **done** |
| Ports: Android (NDK + JNI) | **done** — one APK for Android 5.0 (API 21) and up, 4 ABIs, a launcher that shows each game's own name and icon and opens `.jar` files from a file manager, three configurable floating keypad layouts, sound through AAudio or OpenSL ES with MIDI handed to the phone's own synth, multi-touch, and auto-detected screen size; 1.57 MiB for all four ABIs, runs on an API 21 emulator, an API 36 emulator and a phone |
| Ports: ESP32-S3 (ESP-IDF, Xtensa) | **done** — real IDF build, RUNS a MIDlet under QEMU |
| Ports: ESP32-C3 (ESP-IDF, RISC-V) | builds, links and boots; needs PSRAM for the VM |
| Test suite | **52/52 passing** (`ctest`) |
| Sanitized suite | **11/11 passing** under ASan + UBSan + LSan (`tools/run_sanitized_tests.sh`) |

Verified end to end: **Tumbleweed** (`net.frog_parrot.jump.Jump`, Apress 2004)
plays on the headless port at 176x208 — scrolling ground, an animated sprite, a
live score and countdown. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

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
src/runner/        the runner that ties a port to a MIDlet
tests/             assertion-based test programs
tools/             fixture generators and the no-statics gate
docs/              architecture, porting, compatibility
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the VM works and why
- [docs/PORTING.md](docs/PORTING.md) — bringing it up on a new board
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — what is implemented, what is not, and the failure modes that name themselves

## Licence

MIT — see [LICENSE](LICENSE).  The bundled test fixtures under `tests/` are
generated by this repository's own tools and carry the same licence; the game
JARs that `tools/fetch_test_games.sh` downloads are third-party and are not
redistributed here.
