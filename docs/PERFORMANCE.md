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
## ESP32-S3: measured on the real architecture under QEMU

The estimate that used to be here has been replaced by a measurement.  The
runtime runs a MIDlet on **emulated ESP32-S3 hardware** (Xtensa LX7), inside
ESP-IDF, booted through the real ROM and second-stage bootloader:

    I (85) nj2me:   after nj_vm_new_midlet classes=123 heap=2816/2816
    BENCH sum=-136042548 s=5
    I (9935) nj2me: frames=0 bytecodes=112117067 natives=1170 heap=88224/88224
    NJ2ME DONE rc=0 frames=0

**112,117,067 bytecodes - the same count the desktop reports for the same
fixture.**  That is the result worth having: the interpreter executes the same
work on Xtensa as on x86-64, which says much more than a frame rate does.

Reproduce:

    cd tests/embedded/idf
    idf.py set-target esp32s3 build
    idf.py merge-bin -o flash.bin && truncate -s 4M flash.bin
    qemu-system-xtensa -machine esp32s3 -nographic \
        -drive file=flash.bin,if=mtd,format=raw \
        -global driver=timer.esp32s3.timg,property=wdt_disable,value=true

The guest clock reports 9.85 s for the run, about 11.4 M bytecodes per
guest-second.  **Read that as an emulator figure, not a hardware one**: QEMU does
not model timing, so it does not pay for cache misses, flash wait states or PSRAM
latency, and it does not account for them either.  The claim this supports is
that the bytecode COUNT is exact and identical to the desktop, and that the rate
is the right order of magnitude - which is still worth having, because the
estimate it replaces (3.3-6.7 M/s, from 36-72 cycles per bytecode) was 2-3x too
pessimistic.  The part of that estimate that was wrong is the cycles-per-bytecode
penalty assumed for an in-order core, not the arithmetic around it.

What the two real titles need per frame, measured at 176x208 and 60 fps over 600
frames: `jump.jar` ~235 bytecodes, `maze.jar` ~378.  At 11 M/s that is 30,000 and
47,000 frames per second.  The interpreter is not the constraint at these
resolutions; the panel is.

## A measurement mistake worth recording

An earlier revision reported `jump.jar` at 147 bytecodes over 600 frames against
`maze.jar` at 441,870, and left it as an open question about the scheduler.  It
was not a scheduler bug: it was measured at 240x320, and `jump.jar` refuses any
screen larger than 250 in either axis -

    if (DISP_WIDTH > 250 || DISP_HEIGHT > 250) throw new Exception("Screen too large");

so it threw, drew its own error Alert and never started its game thread.  The
counters were right; the measurement was at a resolution the title does not
support.  Two things follow, both now in the tools: `tools/run_game_corpus.sh`
tries a list of sizes per title and records which worked, and a per-frame figure
is only quoted next to the resolution it was taken at.

## Notes on the ESP32 bring-up that cost the most time

Three of them, all now fixed and all in the runtime rather than the port, because
each produced a message that pointed somewhere other than the cause:

* `nj_heap_init()` rejects an arena that is not 16-byte aligned and can only
  reject, so a port whose allocator aligns to 8 gets "nj_vm_create: heap failed"
  and nothing else.  The alignment requirement is part of the allocator contract,
  it is written down in `nj_port.h`, and the error now says which of the two
  possible causes it is.
* The pool handed to `nj_vm_create()` covers the VM instance, the Java arena AND
  the statics plus native registry - about 32 KiB each for the last two.  Sizing
  it as "sizeof(nj_vm) + java heap" fails with "statics failed".
* `NJ_MAX_STACK_SLOTS` defaults to 64 KiB of slots per Java thread, a desktop
  number; the symptom of leaving it is "main thread failed" with nothing to
  suggest memory.
