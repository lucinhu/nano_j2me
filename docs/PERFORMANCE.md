# Performance

Every number here is produced by a command in this file on the machine named
below. Nothing is estimated unless it says so, and the one estimate is labelled
and given with its assumption spelled out.

    CPU     Intel Core Ultra 9 275HX, 5036 MHz
    Build   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   (-O3 -DNDEBUG)
    Kernel  Linux x86-64

## How to measure

    tools/make_bench_jar.sh
    build/nj2me --port headless --heap 16 --fps 0 --stats tests/fixtures/bench.jar

    build/nj2me --port headless --heap 8 --size 240x320 --fps 0 \
                --run-frames 3000 --stats tests/fixtures/gamedemo.jar

`--stats` prints the counters from `nj_vm_stats` (declared in
`include/nj2me/nj_vm.h`, so an embedder can measure its own target with the same
definitions). `--run-frames N` stops after N presented frames, which is what
makes a benchmark of a MIDlet that never exits reproducible - without it the run
has to be killed from outside, the counters are lost with it, and the frame
count depends on how loaded the machine was.

## Bytecode throughput

Bytecode rate has to be measured on a compute-bound workload. A game thread
spends most of its life in `Thread.sleep`, so counting bytecodes while a game
runs measures the scheduler, not the interpreter - a game can present 4500
frames per second while executing almost nothing.

`tests/fixtures/bench.jar` (built by `tools/make_bench_jar.sh`) exists for this:
a fixed mix of static calls, interface calls with two implementations, int and
long arithmetic, array loads and stores, and string building, for a fixed
iteration count. It runs 112,117,067 bytecodes and prints a checksum that is
identical on every run, so the work is the same everywhere.

| Build | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| Release (-O3 -DNDEBUG) | 563.4 M/s | 549.6 M/s | 549.6 M/s |

549-563 million bytecodes per second. At 5036 MHz that is about **9 cycles per
bytecode**, which is a normal figure for a switch-dispatch interpreter with no
register allocation and no superinstructions.

## Frame rate

Uncapped, 240x320, headless (no presentation cost, which is the point - this is
the emulator's own ceiling, not the display's):

| Title | Frames | Time | FPS |
|---|---|---|---|
| `gamedemo.jar` (the game-API fixture) | 3000 | 0.683 s | 4392 |
| `maze.jar` | 3000 | 0.636 s | 4717 |
| `hello.jar` | 3000 | 1.054 s | 2846 |

The goal was >200 fps at 240x320. It is 14-23x that, so the headroom is not in
question on the desktop.

The SDL2 port adds the real presentation cost and a 30 fps cap by default;
`--fps 0` removes the cap there too.

## Heap

`nj_vm_get_stats()` reports `heap_used` and `heap_peak`. For the fixtures these
converge, which is what a non-leaking runtime looks like:

| Title (3000 frames) | heap used / peak |
|---|---|
| `gamedemo.jar` | 1,142,176 / 1,142,176 |
| `maze.jar` | 828,128 / 828,368 |
| `hello.jar` | 789,168 / 789,168 |
| `bench.jar` | 83,952 / 83,952 |

Peak equal to used means the collector is not being forced to grow: the arena is
allocated once at `nj_vm_create()` and the high-water mark is the live set, not
a sequence of expansions. `tests/test_lifecycle.c` is what holds this to
account across repeated create/run/destroy cycles.

## ESP32-S3: an estimate, and its assumption

This is an **estimate**, not a measurement. There is no ESP32-S3 here and the
toolchain is not installed, so nothing below has been run on the target.

The arithmetic, so it can be checked and corrected:

    bytecodes_per_second(target) = clock_hz / cycles_per_bytecode(target)
    frames_per_second(target)   ~= bytecodes_per_second(target) / bytecodes_per_frame

`cycles_per_bytecode` on this x86-64 host is 5036 MHz / 550 M/s ~= 9 cycles.
The Xtensa LX7 in an ESP32-S3 is a dual-core 240 MHz in-order core with a much
shallower pipeline and no out-of-order execution. A switch-dispatch interpreter
is branch-heavy and cache-resident, which is the worst case for an in-order
core, so a slowdown of 4-8x per bytecode over this host is a defensible range:
**36-72 cycles per bytecode**, i.e.

    bytecodes_per_second(ESP32-S3) ~= 240e6 / (36..72) ~= 3.3 .. 6.7 M/s

What that buys depends entirely on the game. The two real titles above, measured
at 240x320 and 60 fps over 600 presented frames:

* `maze.jar` executes ~736 bytecodes per presented frame.
* `jump.jar` reports far fewer, and that figure is not trustworthy yet - see the
  open question below.

At 736 bytecodes per frame:

    6.7e6 / 736 ~= 9100 fps      (fast end of the range)
    3.3e6 / 736 ~= 4500 fps      (slow end)

So the interpreter is not the constraint at 240x320 on an ESP32-S3: even the
pessimistic end leaves two orders of magnitude over 30 fps. The constraint on
that part is the panel and the SPI/DMA bandwidth for a 240x320 frame, not the
bytecode rate.

Two obvious caveats, stated rather than buried:

1. The host has a 2 MB L2 and megabytes of L3; the ESP32-S3 has 512 KB of SRAM
   with the Java heap and the framebuffer competing for it. A game whose working
   set does not fit will be far slower than this arithmetic suggests, and
   `nj_port_esp32.c`'s PSRAM path is what that is for.
2. `maze.jar` is not a heavy game. A title that does per-pixel work in Java
   every frame would be limited by the interpreter; the number to watch is
   bytecodes per frame, and `--stats` reports it.

To replace this estimate with a measurement, on the hardware:

    idf.py build flash monitor
    # the ESP-IDF example already feeds --stats-equivalent counters through
    # nj_vm_get_stats(); run bench.jar and read the rate off the console.

## Open question

`jump.jar` reports 147 bytecodes over 600 presented frames while `maze.jar`
reports 441,870 over the same. Both render correctly - `jump.jar` animates, keeps
score and countdown - so the game logic is running somewhere the counter is not
seeing, or the thread is being scheduled far less often than the picture
suggests. Compare with `maze.jar`, whose thread is scheduled normally, before
trusting any bytecodes-per-frame figure for a threaded title.
