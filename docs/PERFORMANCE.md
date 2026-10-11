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

## Measuring, before optimising

**The first version of this section was wrong, in the way that matters.** Two
optimisation attempts were judged on wall-clock time over a 0.19 s run, whose
run-to-run spread is 12-13%. Nothing under about 15% is visible in that, so both
"no measurable gain" verdicts were unfalsifiable - in either direction. "I could
not measure a gain" and "there is no gain" are different claims.

`tools/bench.sh` replaces it with something that can resolve a difference:

| | |
|---|---|
| **CPU time, not wall clock** | the interpreter bench is single-threaded and pure computation, so user time is exactly the work done |
| **`taskset` pinning** | a process that migrates cores pays for cache it no longer has, which is most of the 12% |
| **the minimum of N runs** | noise only ever ADDS time, so the fastest run is the closest to the machine's real speed; a mean reports how busy the machine was |

Spread falls from **12-13% to 2.2-2.6%**, so 5% is visible and 10% is
unambiguous. Two independent sessions on the same binary measured 0.186 s and
0.187 s, so it is reproducible and not just precise.

**Baseline: 112,117,067 bytecodes at min 0.186-0.187 s = 600-603 M bytecodes/s**
on this machine. (Higher than the 549-590 M/s quoted earlier, because CPU time
excludes the interference that wall clock was charging for.)

## Superinstruction: push-constant + iand, about 2.5-3%

     fused      0.165 / 0.167 / 0.166 s     (min, three interleaved rounds)
     unfused    0.169 / 0.172 / 0.170 s
     gain       ~2.5-3%, and A won every round on both statistics

The benchmark's inner loop masks four times per iteration - `sipush 255`,
`sipush 511`, `bipush 127`, `ldc 65535`, every one followed by `iand` - and that
is what a mask does in any game: clamp to a power of two minus one.  Fusing the
pair saves one dispatch for about 5.7% of executed instructions, which is
6,400,000 of the 112,117,067 in a run.

**FUSED AT DISPATCH TIME, NOT BY REWRITING THE BYTECODE.**  Rewriting is the
textbook version and it needs every branch target checked: a jump landing on the
second half of a fused pair would run it without the first.  That is a small
verifier's worth of work and a real correctness hazard.  Peeking at `code[pc]` in
the handler has neither problem - pc advances normally, a branch onto either
instruction still does the right thing, and the fused path's peak stack depth is
the same as the unfused one, so `stack_limit` stays valid.

**THE BYTECODE TOTAL IS NO LONGER A CORRECTNESS CHECK FOR THIS.**  It drops to
105,717,067, because a fused pair executes two instructions and counts one.
`BENCH sum=-136042548 s=5` is now the only instrument that proves behaviour on
this benchmark, which is exactly the instrument whose absence let the shifted
dispatch table survive several rounds.

## Replicated dispatch: measured again properly, about 8%

The earlier "~10%" was measured with a dispatch table that turned out to be
shifted by one and therefore mostly inert, so it did not describe the mechanism.
Re-measured after the table was rebuilt from the source labels, against a
**plain switch** rather than against the half-optimised intermediate:

    plain switch        0.184 / 0.186 s      (min, interleaved)
    replicated dispatch 0.170 / 0.170 s
    gain                ~8%

**The control matters more than the number.**  The first attempt at this
re-measurement compared against `NJ_THREADED_DISPATCH=0` - a build that turns
the TABLE off but keeps the restructured fetch, so it was already most of the
optimisation.  That comparison returned "no difference", and acting on it meant
reverting a real 8% win.  Reverting is one command, but the lesson is that
"which two things am I comparing" is the whole experiment; the benchmark's own
quality cannot save a wrong control.

## Replicated dispatch: 13 opcodes (superseded)

    converted    0.189 / 0.183 s      (interleaved A/B, same session)
    unconverted  0.204 / 0.205 s
    gain         ~10%

The baseline reads 0.204 s here and 0.185 s three rounds ago for the SAME code.
That drift is the reason the A/B is interleaved and the reason an old number is
not a baseline.

Converted so far: `bipush sipush iload iand ixor goto` (6) plus
`iconst_0..5 iload_0..3 lload_0..3 istore_0..3` (4 groups covering 7 more) and
`iadd imul lcmp`.  That is **55% of the instructions the benchmark executes**,
and the bytecode total has stayed exactly 112,117,067 after every one.

## Replicated dispatch: first batch converted, measured, kept

**3.8% faster from converting 6 of 201 opcodes.**  Interleaved A/B, same session:

    converted    0.176 / 0.180 s
    unconverted  0.185 / 0.185 s

The six are `bipush`, `sipush`, `iload`, `iand`, `ixor` and `goto` - chosen from
a measured DYNAMIC histogram, not from reading the source.  They are **25.7% of
the instructions the benchmark executes**:

    iload_3  9.99%   iadd     7.14%   iand     7.14%   sipush   5.71%
    istore_3 5.71%   bipush   4.28%   ireturn  4.28%   iload    4.28%
    iload_0  2.86%   getstatic 2.86%  iload_2  2.85%   iload_1  2.85%
    ixor     2.85%   lload_0  2.85%   iconst_1 2.85%   invokeinterface 2.85%
    imul     2.85%   iconst_3 2.85%   iinc     1.52%   goto     1.43%
    if_icmpge 1.43%  lcmp     1.43%

**The top 20 opcodes are 80% of everything executed**, so the remaining work is
bounded and its payoff is predictable: 6 opcodes bought 3.8%, and the same rate
over the top 20 is worth roughly 10-12%.

### How it is done, and why it is safe

A converted handler carries an `L_0xNN:` label before its `case`, ends with
`DISPATCH()` instead of `break`, and its entry in `nj_dispatch[256]` points at
that label.  An unconverted handler has no label, its entry points at `L_slow`,
and it runs through the switch exactly as before.  **The two kinds coexist**, so
the conversion is one opcode at a time and after EVERY one the benchmark must
still report exactly 112,117,067 bytecodes - a mistake shows up as a wrong count,
not as a subtle wrong answer.

`NJ_THREADED_DISPATCH` is 0 on any compiler without the label-address extension
and `DISPATCH()` is then just `continue`.  ESP32 (xtensa and riscv), Cortex-M and
three Android ABIs are built by this project; they lose speed, not correctness.

## The measurement was still wrong in one way, and it produced a false positive

**Comparing against a baseline recorded EARLIER is not an A/B.**  Absolute speed
drifts between sessions on this machine by more than the effect being measured,
so a number from a previous round is not a control - it is a different
measurement of a different machine state.

That is exactly how a false positive got through:

                                 recorded then    re-measured in the same session
    original dispatch            0.186 s          0.182 / 0.184 s
    256-label dispatch           0.180 s          0.181 / 0.183 s
    apparent gain                3.3%             0.5%

3.3% looked like a real win.  0.5% is inside the noise.  Both pairs of the
interleaved A/B favour the new version by the same 0.001 s, which is suggestive
and not evidence, so the change was REVERTED - 54 lines of explicit opcode labels
in the hottest function in the program do not earn their place on a 0.5% that
cannot be distinguished from zero.

**The rule**: measure both variants in the same session, interleaved, and compare
their minima.  `tools/bench.sh` makes each measurement cheap enough that there is
no excuse for reusing an old number.

## Two attempts, now with falsifiable numbers

**Attempt 1: hoist the instruction counter into a register.** `vm->insns++` runs
on every bytecode and is a read-modify-write to memory that the compiler cannot
hoist, because it may alias the operand stack. The counter was moved into a local
and flushed at all eight exits of `nj_exec`.

    baseline   min 0.186 s   602.8 M/s
    hoisted    min 0.189 s   593.2 M/s     -1.6%

**It is slower.** Reverted. The bytecode count stayed exactly 112,117,067 across
the change, which is how the flush points were verified - a missed exit shows up
immediately as a wrong total rather than as a subtle statistic.

**Attempt 2: profile-guided optimisation.** Built `-fprofile-generate`, trained
on the benchmark, rebuilt `-fprofile-use -fprofile-correction`.

    baseline   min 0.210 s
    PGO        min 0.210 s     no change

Reverted, because the build system does not need a two-stage configuration to
buy nothing.

**Replicated dispatch: measured for feasibility, not attempted.**

The remaining classic win is to end every handler with its OWN indirect branch,
so the branch predictor keys on the handler rather than on one shared dispatch
site.  That needs each opcode's terminating `break` to become a dispatch to the
next opcode, and the numbers say it is not a mechanical edit:

    case labels in the dispatch switch   201
    lines in its body                    958
    `break` statements                   ~120

**`break` is overloaded.**  One inside an `if` still leaves the switch and must be
rewritten; one inside a `for` or a nested `switch` must not be.  Brace counting
cannot tell them apart - it reports 61 candidates one way and 35 the other,
because the `THROW`/`SAVE`/`CHECK_QUANTUM` macros carry braces of their own that
the counter cannot see.  A correct transformation needs a real C parser scoped to
this function, not a regular expression.

The verification would be strong (the benchmark must keep reproducing exactly
112,117,067 bytecodes, and 12 tests plus a 10-title corpus must pass), but the
EDIT is the fragile part, and a subtly wrong interpreter is the worst outcome
available to this project.  Recorded as a real, quantified next step rather than
attempted badly.

**Why the classic next step is expensive here.** `objdump` shows GCC already
turns the dispatch switch into a jump table (`cmp $0xc9` / `ja` / indirect jump),
so the textbook computed-goto win is mostly already taken. What remains is
REPLICATED dispatch - ending every one of the 170 opcode handlers with its own
`goto *dispatch[next]` so the indirect branch is predicted per site rather than
at one shared site. That is 170 edits in a function whose `break` means two
different things depending on nesting, verified by 12 tests and a bytecode count
that must keep reproducing exactly 112,117,067.

That is a real win of uncertain size bought with a real risk of a subtle
breakage. It is now at least measurable, which it was not before.

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
