# nano_j2me

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
| ZIP/JAR reader, DEFLATE inflate | **done**, 410 assertions, ASan/UBSan clean, fuzz-tested |
| Class-file parser and linker | **done** |
| Bytecode interpreter (CLDC 1.1) | **done** |
| Object model, arena heap, mark & sweep GC | **done** |
| Cooperative threads and scheduler | **done** |
| Exceptions, C-to-Java bridge | **done** |
| `java.lang` core, `Math`, `Thread`, wrappers | **done** |
| `javax.microedition.lcdui` | in progress |
| `javax.microedition.lcdui.game` | planned |
| `java.util`, `java.io`, RMS | in progress |
| Software rasterizer | in progress |
| PNG decoder, bitmap fonts | in progress |
| Ports: headless (reference) | **done** |
| Ports: SDL2, Android, ESP32-S3 | planned |

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The headless port is always available and is what the test suite uses. The SDL2
port is built automatically when `libsdl2-dev` (or the equivalent) is installed.

## Running a game

```sh
build/nj2me --port sdl2 game.jar
build/nj2me --port headless --dump-frames out/ game.jar
```

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

Not yet chosen.
