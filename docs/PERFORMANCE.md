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
| Release, after the three inlined checks below (2026-10-13) | 644.6 M/s | - | - |

549-563 million bytecodes per second originally; 644.6 M/s after the inlining
described in "Three checks the interpreter asks millions of times" below. At
5036 MHz the first figure is about **9 cycles per bytecode** and the second
about **7.8**, which is a normal range for a switch-dispatch interpreter with no
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
## ESP32-S3: measured on the real architecture

The estimate that used to be here has been replaced by a measurement.  The
runtime runs a MIDlet on **emulated ESP32-S3 hardware** (Xtensa LX7), inside
ESP-IDF, booted through the real ROM and second-stage bootloader.  Two
emulators are used, because they model different halves of the board.

### QEMU's esp32s3: the internal-RAM profile

QEMU cannot probe PSRAM (espressif/qemu#129), so this profile keeps the Java
heap in the 356 KiB of internal SRAM.  It is what first proved the interpreter
and the class library execute on Xtensa at all:

    I (85) nj2me:   after nj_vm_new_midlet classes=123 heap=2816/2816
    BENCH sum=-136042548 s=5
    I (9935) nj2me: frames=0 bytecodes=112117067 natives=1170 heap=88224/88224
    NJ2ME DONE rc=0 frames=0

112,117,067 bytecodes **at the time** - the count the desktop reported then.
The bytecode total stopped being usable as a comparand the moment a fused
push-constant+iand superinstruction started executing two instructions while
counting one (see "The instrument that DID matter" below), so the benchmark's
own `BENCH sum=-136042548 s=5` line is what holds behaviour across machines;
the count today is 105,717,067 everywhere.

Reproduce (the PSRAM profile is the default in `sdkconfig.defaults`, so QEMU
needs the override that turns PSRAM off):

    cd tests/embedded/idf
    idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.qemu" \
        set-target esp32s3 build
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

### Espressif's esp-emu: the 16 MB flash / 8 MB PSRAM board

esp-emu models the parts QEMU does not: the octal PSRAM die, the flash chip
size from the image header, and a much more detailed Xtensa core.  That is the
board this port targets (16 MB flash, 8 MB PSRAM), so it is the measurement
that matters:

    I (96) esp_psram: Found 8MB PSRAM device
    I (185) esp_psram: SPI SRAM memory test OK
    I (215) nj2me: nano_j2me on esp32s3, free: psram 8189 KiB, internal 349 KiB
    I (215) nj2me: pool 6144 KiB (psram)
    I (235) nj2me:   after nj_vm_create     classes=252 heap=1968/1968
    I (58195) nj2me: frames=0 bytecodes=105717067 natives=1170 heap=89536/89536
    NJ2ME DONE rc=0 frames=0

**105,717,067 bytecodes, exactly the desktop total**, and the whole class
library materialises: 252 classes, where the same image on internal RAM failed
to create about 120 built-in classes because the pool was only 308 KiB.  The
`BENCH sum=-136042548 s=5` line matches too.  This is the cross-architecture
claim the QEMU run could only make in the internal-RAM profile: on a board with
PSRAM, with every decoder, the M3G/MASCOT class libraries and the full MIDP
surface linked in, the interpreter does the same work as on x86-64.

Reproduce:

    tools/run_esp_emulator.sh

The script builds the image, merges the bootloader, partition table and
application into one flash image, boots it with `--psram-size 8M`, and passes
when the UART log has `NJ2ME DONE rc=0`, a bytecode total and a PSRAM pool.  The
guest clock reports 58.2 s for the run, about 1.8 M bytecodes per guest-second -
another emulator figure, and a slower core model than QEMU's (real PSRAM
latency, windowed-register stack effects).  The rate is the wrong thing to
compare across emulators; the count and the benchmark's own sum are the right
things.

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

## The whole table converted, and four fused pairs: about 5% minimum, 7% median

    baseline (frozen snapshot)   min 0.178 s   med 0.183 s
    this session's build         min 0.169 s   med 0.170 s
    gain                         ~5% min       ~7% median

Fourteen interleaved runs, pinned to one core, minimum and median of the same
runs.  The stages, each measured against the stage before it in the same
session:

    + full dispatch conversion, const+int-arith fusion    min 0.172   med 0.174
    + load+branch, store+load, aload+null-branch, locals  min 0.169   med 0.170

**The conversion is now the whole table, not a chosen batch.**  Every handler
whose body ends in a plain `break;` carries an `L_0xNN:` label and ends with
`DISPATCH()`; 198 of the 256 table slots point at a label and the rest at
`L_haveop`.  Two handlers stay on the switch on purpose: `athrow` (its body
ends in `goto do_unwind`) and `default` (it must keep throwing VerifyError for
unused opcodes).  A handler whose last `break;` is followed by live code is
never converted, which is the check that keeps this mechanical - the script
refuses the group rather than guessing which `break` was the terminator.

### The four fused pairs

Each one peeks at `code[pc]` inside the handler, executes both instructions and
dispatches once.  No bytecode is rewritten, so a branch onto either half still
does the right thing, and the fused path's peak stack depth never exceeds the
unfused one, so `stack_limit` stays valid.

| fused pair | why | dynamic share it removes |
|---|---|---|
| const-push + int-arith (`iconst_m1..5`, `bipush`, `sipush`, `ldc`/`ldc_w` of an Integer) | every mask, shift, multiply and compare-by-constant | 100% of the games' `ishl`/`iushr`/`iand` consumers, 96.8% `irem`, 94.5% `imul`, 77.3% `isub`, 20.8% `iadd` |
| `iload`/`iload_k` + `ifeq..ifle` | loop tests and guards load a local and branch on it | 8.3% of 1945's stream (measured on a real title) |
| `aload`/`aload_k` + `ifnull`/`ifnonnull` | null guards | small, but free once the int form exists |
| `istore_k` + `iload_k` (same slot) | `x = x op ...` reads the value back immediately | 4.28% of the fixture's stream |

The push+consumer fusion generalises the earlier `bipush/sipush + iand` pair
and replaces it; the old special case is gone.  Bytecode totals move with every
fusion and are therefore only a sanity counter now: 112,117,067 before any
fusion, 105,717,067 after push+iand, 94,516,867 after const+int-arith,
89,716,784 after the three branch/store fusions.  `BENCH sum=-136042548 s=5`
stayed identical at every step.

### Replicated dispatch, re-measured on the fully converted table

    fixture (bench.jar)        switch 0.171 / 0.174     threaded 0.198 / 0.201
    call/loop microbenchmark   switch 0.657 / 0.660     threaded 0.617 / 0.622

The fixture's inner loop is a few dozen instructions repeated millions of
times; the indirect branch predictor handles that better than a computed goto
per opcode, and the threaded body also duplicates the fetch.  A benchmark built
around calls, interface dispatch and straight-line arithmetic - where each
dispatch is a different target - prefers the replicated table by about 6%.
`NJ_THREADED_DISPATCH` therefore stays 0: the fixture is this project's metric,
and the embedded targets (xtensa, riscv, Cortex-M) have no computed goto at all.

Jump threading (`NJ_ENABLE_JUMP_THREADING`) follows `goto`-to-`goto` chains
without dispatching; the fixture has no chains, so it measures neutral there.
It is kept because the profile found real titles whose hot loop ends in
`goto; goto` sequences, and it costs one load and one compare per taken branch.

### Two bugs the benchmark could not see

**`DISPATCH()` is `continue` in the switch build, so a fusion must not be a
`do { ... } while (0)`.**  The first version wrapped the fused body in the usual
macro do-while; the `continue` then targeted the do-while, the handler fell
through to its normal path and pushed its operand a second time.  The benchmark
died with `VerifyError: operand stack overflow` at `bench/Bench.vcall(I)I pc=5`
- loud enough, but only because the fixture executes that method four million
times.

**Macro-internal names must not collide with the call site.**  The branch fusion
was `#define NJ_LOAD_BRANCH_INT(V) { ... nj_s4 v_ = (V); ... }`, called as
`NJ_LOAD_BRANCH_INT(v_)`.  In C the scope of `v_` begins at the end of its
declarator, so the initializer names the macro's OWN `v_`, not the caller's: the
branch tested an uninitialized value.  Every condition behaved as if the operand
were zero.  The benchmark still passed - its branches are on constants and
counters, never on a loaded value - and so did the whole ctest suite; only a
targeted digest test over branch/store/const/wide categories against a desktop
JVM caught it.  The macros now use `njlb_*` names and the digest test is the
reason to keep one around: `BENCH sum` proves the fixture, not the interpreter.
`tools/check_superinstr_digest.sh` (ctest `check_superinstr_digest`) compiles a
MIDlet that exercises each fused family, hashes every result into six
per-category digests and requires them to equal the same code on the desktop
JVM - values against a JVM, not counts against a constant, so it stays valid
with the fusions off or extended.  Reintroducing the shadowing bug in a scratch
worktree makes it fail (exit 1, three of seven digests differ) while the
benchmark still prints the expected `BENCH sum`.

### RAM and switches

Nothing here adds a writable static or a heap allocation: the tables are
`static const` (`.rodata`/`.data.rel.ro`), the fused paths use locals, and the
instruction counter plus the cached `stack_limit` live in the interpreter's own
frame and are flushed in `SAVE()`.  `tools/check_no_statics.sh` passes.

    NJ_ENABLE_SUPERINSTR      1   fused pairs; code size only
    NJ_ENABLE_JUMP_THREADING  1   goto-chain following; code size only
    NJ_THREADED_DISPATCH      0   replicated interpreter body; no RAM
    NJ_ENABLE_ENGINE          0   pluggable engine/JIT hook; one pointer in nj_vm

Verification on the frozen tree: 90/90 ctest (89 before the digest test landed),
and the 8-title real-game set
(pixels, stats, exceptions) reports 7 titles pixel-identical, 1 unstable by its
own same-binary control, 0 failures; bytecode totals in game stats drop by the
expected 5-15% and are INFO-only there.

An independent verifier then rebuilt the frozen commit in a fresh worktree and
re-ran the gates without trusting the numbers above: clean configure+build of
the four configurations, 89/89 ctest (the 90th test in the working tree belongs
to an unrelated workstream), interleaved A/B pinned to one core with 8 runs per
arm (baseline min 0.186 / med 0.187 s vs tip 0.173 / 0.175 s, faster in 8 of 8
rounds, +6.99% by min and +6.42% by median), byte-identical stats and `BENCH
sum`, both structural scripts passing, 8-title game regression 0 FAIL (pixels
identical where measurable), RAM unchanged (`.data` 16 B, `.bss` 6,688 B, bench
heap 92,432/92,432) with flash +42,152 B (+1.40%) for the replicated table, and
an engine-enabled build whose 54 checks pass while the default binary contains
zero engine symbols.  The threaded body is semantically identical to the switch
but ~8.8% slower on the fixture (0.173 -> 0.186 s), which is why it stays
opt-in; the call/loop microbenchmark above still prefers it.

A concurrent session's matrix run reported two titles breaking with both fast
paths on - 孤岛余生2 stuck at its splash with 1,350 caught throws and 3 colours
in frame 900, 都市赛车6 never painting a frame - and read it as the two switches
interacting.  That run sampled the intermediate tree between the two bugs above.
Re-running its exact commands on the frozen tip gives baseline behaviour for
every switch combination: 孤岛余生2 (900 frames, 240x320, `--network`, its key
script) gives 9 caught throws, 0 uncaught, 142 colours in frame 900 and a
2.59 MB heap for baseline, both-on, superinstr-off and jump-threading-off alike;
都市赛车6 (300 frames) reaches the same 54-colour scene on baseline, both-on and
superinstr-off, and that title's own run-to-run spread (17-79 M bytecodes and
5-12 k gfx calls on the SAME binary) is its timing dependence, not the switches.

## Three continuations and a cheaper return (round 4, 2026-10-22)

This round starts at `c78acb1`, after the previous fusion rounds and correctness
fixes. Execution is still interpreted: the engine seam is unchanged and no
machine code is generated. The retained changes are:

| Path | Work removed |
|---|---|
| Constant + integer arithmetic + `ireturn` | Reuse the ordinary return handler immediately after successful arithmetic; remove the return fetch/dispatch. |
| `lconst_0/1; lcmp[; if<cond>]` | Compare directly against 0 or 1, retaining the existing comparison/branch fold and avoiding the constant's two-slot materialisation. |
| `iinc; iload*; if<cond>` | Keep the unary-branch fold when the increment consumes the load; read the selected local after the increment, including different local indices. |
| Ordinary Java returns, including void/wide/reference | Flush the instruction counter, pop using the current frame's already-loaded `locals`, and reload the caller. Saving the dying frame's `pc/sp` and rechecking that it exists are unnecessary here. |

The new folds are bounded, peek without rewriting bytecode, and use the existing
`NJ_ENABLE_SUPERINSTR` switch. A branch can still enter any original instruction.
The long-constant and increment/load chains check the temporary stack capacity
that their unfused instructions would require, falling back when necessary.
Arithmetic exceptions are raised before looking at the following return. Return
simplification introduces no allocation or scheduler/engine callback between
popping the frame and restoring the caller (or returning at `stop_depth`).

### Same-session measurements

Both binaries were built in the existing Release build with identical options,
portable switch dispatch, and the existing `-O2` override for the interpreter.
Measurements pin one core, alternate AB/BA, discard one warm-up per arm, and
retain every later sample. CPU time includes user + system time for the whole
process; wall time includes process startup and teardown. Lower is better.

| Workload / metric | Baseline min / median (s) | Round 4 min / median (s) | Time reduction min / median |
|---|---|---|---|
| `bench.jar`, CPU, 24 runs/arm | 0.162274 / 0.168005 | 0.158164 / 0.163389 | 2.53% / 2.75% |
| `bench.jar`, wall, same runs | 0.163168 / 0.168868 | 0.159033 / 0.164222 | 2.53% / 2.75% |
| Call/arithmetic microbenchmark, CPU, 8 runs/arm | 0.536655 / 0.539574 | 0.521190 / 0.523765 | 2.88% / 2.93% |

An independent rerun of the same frozen binaries used the existing shell
harness (user CPU time, millisecond resolution, one warm-up, pinned CPU 2,
AB/BA). Over 16 runs/arm, the fixture's minimum/median fell from
0.156/0.161 s to 0.153/0.157 s (1.92%/2.48%), with the candidate faster in
14/16 pairs. Over 12 runs/arm, the microbenchmark fell from 0.531/0.535 s
to 0.517/0.520 s (2.64%/2.80%), faster in all 12 pairs. These independently
support a roughly 2–3% reduction on these workloads; the fixture's 4.5%/3.9%
within-arm spread makes its minimum alone a weak signal.

These are compute-workload measurements on this x86-64 host, not game FPS or
an embedded-target speed claim. The microbenchmark's integer-comparison phase
went from a 12 ms median to 13 ms, while its call and long-arithmetic phases
improved; individual short phases have millisecond clock resolution.

`BENCH sum=-136042548 s=5`, 1,170 native calls and the 92,432/92,432-byte heap
used/peak remain unchanged. Dispatches fall from 76,914,888 to 70,514,376
(8.32%): 4,800,000 removed return dispatches and 1,600,512 removed long-constant
comparison dispatches. The increment/load/branch continuation has no hits in
this fixture. Dispatch counts are not a speed measure or a raw JVM instruction
count. The existing quantum is also dispatch-based, so fusion changes its
bytecode granularity while retaining a bounded interval between polls.

### Rejected experiments and footprint

Inlining frame-push made the fixture 3.10% / 3.53% slower by CPU minimum/median.
Skipping an earlier save for already-initialised static-call owners was neutral
at the minimum and 0.79% slower at the median. A further constant/arithmetic/
bitwise continuation removed another 1,600,000 dispatches but was 2.38% / 2.26%
slower than the retained candidate in a 20-run/arm comparison. All three were
reverted; those timings are separate paired experiments, not additive deltas.

No VM/thread/frame fields, writable tables, allocations or per-method caches
were added. In the matching frozen executables, GNU `size` text (code plus
read-only sections) grows by 1,568 bytes, 2,727,950 to 2,729,518; its aggregate
data stays 160,432 bytes and BSS stays 6,688 bytes. Section-level inspection
confirms writable `.data` is 16 bytes and `.bss` is 6,688 bytes in both;
`.text` grows 1,472 bytes and `.rodata` grows 96 bytes. Matching x86-64 `-O2`
`-fstack-usage` builds report a 528-byte `nj_interp_run` C stack frame before
and after. The final executable's file length happens to stay unchanged because
of linker alignment. These figures apply to the measured host/compiler.
Fusion can be disabled with `-DNJ_ENABLE_SUPERINSTR=0`; the return simplification
needs no extra RAM switch.
`NJ_ENABLE_ENGINE=0` remains the default; enabling the pre-existing engine
interface still adds its documented per-VM pointer only.

### Correctness checks

The working-tree Release build passes all 92 CTest tests. The repository digest
script now checks 27 values (one aggregate plus 26 categories) against the same
classes on desktop HotSpot, including throwing arithmetic, live caller
operands, increment/read of different local slots, long-comparison loops and
integer wraparound. Both frozen binaries match all 27 values. The larger
standalone differential harness also matches all 6,505 labelled cases on the
candidate. Another 82 cases in a hand-assembled, HotSpot-verified class test
branches entering the middle of the new patterns, comparisons without a
following branch, and end-of-method peeks; all match.

Independent clean builds cover default switch dispatch, threaded dispatch,
superinstructions/jump-threading both disabled, and engine support enabled.
All four pass the 27-value digest and the benchmark result/heap checks. The
threaded-dispatch equivalence check and no-writable-statics check pass; the
engine build passes all 54 engine-interface checks. The independent default
CTest run needed five reruns after fixing fixture scripts' hard-coded build
path, plus an isolated rerun of the load-sensitive flush test; all six passed.
The main-tree 92/92 run above needed no reruns. An eight-pair comparison of the
independent rebuild with the frozen final binary also matches every checked
observable, including dispatch counts.

The eight-title game replay uses two runs per binary, isolated RMS directories,
fixed input scripts and 240/360-frame budgets. All eight titles' sampled frames
are pixel-identical across binaries, with no new exception signatures. The
harness reports **4 PASS, 4 WARN, 0 FAIL**: 1945空战英雄, 模拟人生2,
帝国时代3-亚洲王朝 and 暗黑启示录2 have paint/native/heap-counter differences at
the fixed frame budget (for example, 1945 paints 136 versus 137 times with
identical sampled frames). The other four are 3D版田园物语, 合金弹头4,
实况足球经理2009 and 仙魔飞行棋. These replays are compatibility checks;
the timing-dependent counters are not used to claim a game speedup.

The transient-stack-capacity guards are also reviewed statically; legal
javac-generated code does not exercise the malformed-stack fallback. This
work does not add a bytecode verifier or claim memory safety for invalid
class files.

Local reproducibility artifacts are under
`.local-tools/debug-runs/interp-round4-20261022/`: frozen baseline/final binaries,
`measure.py`, all A/B samples and a read-only candidate-profile report. They are
excluded from version control. Baseline binary SHA-256 is
`98c73cbb46c722a112499edc0e858b551d429e76b2c0a2cd0efefc42504be511`;
final is `8d0a2a06250bbcb1ad1b20cf6ed018ea1281112da84723c998b4849bcefb66df`.

## M3G flat fill, linear filtering and clipping (2026-10-22)

This round starts from the preceding interpreter result, with its 70,514,376
benchmark dispatches. Three further interpreter experiments were rejected:
field-address calculation before the type switch slowed benchmark/micro CPU
medians by 1.72%/2.19%; moving abstract-target validation off the virtual-cache
hit path gave +0.55%/-0.11%; and an extra dispatch entry that skipped a redundant
stack comparison slowed them by 18.89%/18.46%. Each comparison alternated A/B,
with 24 benchmark and 10 micro runs per arm. All candidates passed the 27-value
JVM digest, but none justified a source change. The JIT engine interface and
previous interpreter optimizations remain as before this round.

The retained changes are in `src/midp/nj_lib_m3g.c`:

- Opaque flat triangles select a short pixel loop outside the inner loop. It
  retains independent depth-test/depth-write controls, strict depth ties and
  the original floating-point depth expression and stepping order. Sprites,
  Gouraud colours, texture sampling and nontrivial compositing use the general
  loop.
- Linear filtering reads the four source pixels' channels directly, avoiding
  four ARGB packs followed by channel extraction. Coordinates, wrap rules,
  8-bit weights and final shifts are unchanged; RGB textures still have alpha
  255, and RGBA alpha is filtered with the same weights.
- Triangles inside both depth planes go directly to emission. The test uses
  the same projection-distance expressions and comparisons as the clipper;
  crossing triangles still use the original clipping and interpolation path.

All three are controlled by `NJ_M3G_FAST_PATHS`, default **1** in the config
header and CMake. Configure with `-DNJ_M3G_FAST_PATHS=0` to use the general
paths. There are no new persistent fields, caches, buffers or allocations.
This is exact rendering work reduction, with no lower-resolution framebuffer,
filter-quality change or reciprocal approximation.

### Paired full-call measurements

The old M3GDemo's 4,000 submissions repeat one triangle at the same depth;
after the first draw it mostly measures depth rejection. The new local Java
fixture separately measures six workloads through `Graphics3D.render`: 2,000
submissions into a 240x320 Image, sloped depth, 49,229 covered pixels, and a
64x64 RGBA texture where applicable. Except for the rejection case, depth
comparison and writing are disabled so every submission actually shades.
The alpha case changes 43,006 pixels because transparent fragments are tested.

Release GCC 16.2.1, `-O3` for M3G, CPU 4, alternating AB/BA, one warm-up pair
excluded, **10 measured runs per arm per workload**. The render timer includes
Java/native calls; process CPU additionally includes startup, fixture setup
and checksum work. Figures below are medians, with time reductions rather
than throughput increases:

| Workload | Render before → after (ms) | Render time reduction | Whole-process CPU before → after (ms) | CPU time reduction |
|---|---:|---:|---:|---:|
| Repeated depth rejection | 69 → 55 | 20.29% | 86.484 → 71.653 | 17.15% |
| Opaque flat fill | 96 → 57 | 40.63% | 112.929 → 73.769 | 34.68% |
| Nearest texture | 673 → 663 | 1.49% | 690.496 → 679.892 | 1.54% |
| Linear texture | 1,966 → 1,428 | 27.37% | 1,979.461 → 1,442.576 | 27.12% |
| Gouraud fill | 153 → 152 | 0.65% | 169.781 → 169.635 | 0.09% |
| Alpha texture | 1,286.5 → 1,273 | 1.05% | 1,302.955 → 1,288.369 | 1.12% |

The substantial gains are flat fill and linear filtering; the other shading
paths are effectively unchanged at this scale. These are controlled workloads,
not a claim of the same percentage improvement in game FPS. Early rejection
samples show warm-up/frequency variation, so the complete sample series is
retained; its render minima are 68 → 54 ms. Flat and linear render minima are
95 → 56 ms and 1,959 → 1,425 ms respectively.

### Rendering equivalence and footprint

All timed runs retain the same pixel digest and changed-pixel count; complete
PPM images also match between binaries for all six workloads. Dispatches,
native calls, drawing counters, cache counters and heap used/peak match in
each pair. Heap used/peak is 1,235,424 bytes for flat/reject, 1,235,712 for
Gouraud and 1,274,448 for textured cases, unchanged by the optimization.

A separate C differential probe compiles against the frozen old renderer,
the new fast paths, and the new code with the macro disabled. All three agree
on 120,000 sampler outputs, 12,000 randomized raster colour/depth digests and
10,000 clipping/rendering colour/depth/drawn-count results. Coverage includes
RGB/RGBA, both linear-filter aliases, 1-pixel texture dimensions, negative
repeat coordinates, clamp edges, alpha/compositing states, independent depth
controls, culling, sprites and perspective/parallel/generic depth planes.
This is regression equivalence to the existing renderer, not a claim that
every M3G feature is implemented. The persistent M3G unit test adds 65 checks:
56 exact RGB/RGBA linear-filter pixels, six flat colour/depth cases, two exact
clip-plane cases and one RGBA construction check. Both frozen libraries pass
all 386 checks. The full main build passes **92/92 CTest tests**, and the final
interpreter still matches the desktop JVM on all 27 digest values. Independent
clean builds with the M3G macro on and off each pass all seven M3G-related CTest
gates, M3GDemo's 146 checks and all six workload image comparisons. A separate
ASan/UBSan build passes the 386 M3G checks, 97 container checks and M3GDemo with
no sanitizer reports; this sanitizer run covers M3G, not the whole test suite.

Matching frozen executables keep `.data` at 16 bytes, `.bss` at 6,688 bytes,
`.data.rel.ro` at 158,560 bytes and `.rodata` at 1,157,344 bytes. `.text` grows
1,280 bytes; including unwind metadata, GNU `size` text grows 1,424 bytes.
File length grows 4,128 bytes because of section alignment. The macro allows
small-flash builds to omit the specialized paths.

Native C stack use changes with this compiler. `-fstack-usage` and disassembly
show that the old `jr_triangle_v` inlined emission into a 992-byte frame. The
new version uses a 576-byte frame and a separate 496-byte `jr_emit`: the
inside-plane path tail-calls the emitter, while the clipping path calls it
normally, for a conservative 1,072-byte combined peak (**+80 bytes** on that
path). All other 365 function frame sizes agree; the existing largest frame,
`m3g_render_scene`, remains 7,664 bytes. Persistent RAM is unchanged, but builds
with a tight native stack budget should account for this target-specific
80-byte increase or use `NJ_M3G_FAST_PATHS=0`. An independent macro-off build
restores the 992-byte inlined frame and has no separate `jr_emit` call.
Embedded stack margins were not measured on hardware in this round.

### Real 3D game replay

Rally Evolution (`3D拉力赛-进化汉化版_6366.jar`) runs for 3,650 frames at 30 fps
per arm, with the same scripted controls and fresh RMS directories. Both reach
the actual race, draw the car/track/HUD and respond to controls. Both report
141,002 triangles, 16,368 M3G calls and heap used/peak 3,203,136/5,459,824 bytes.
Both exit normally with the same four pre-existing thrown exceptions (RMS and
cancelled TimerTasks), with no new exception category or uncaught failure.

Of 36 sampled frames, 22 are byte-identical. The other 14 differ only in
25–89 pixels of the real-time "Total Time" HUD digits; the rendered 3D scene
matches. An earlier run of the same baseline binary exhibits the same timer
variation. Native/bytecode counts vary slightly with that clock too, so this
replay verifies behaviour and visuals, not game FPS improvement.

At this stage Micro3D remained a separate compatibility limitation: the deeper
Devil May Cry replay reached a missing `Figure(String)` constructor, and
`renderFigure` was unimplemented. The later model-rendering pass below addresses
that gap. The menu/intro results in this section are not a Micro3D throughput
measurement.

Local evidence is under `.local-tools/debug-runs/perf-round5-3d-20261022/`:
`final-ab/results.json` and its logs/images, `measure-renderer.py`,
`scout/M3GPerf.java`, `probes/results.json`, the probe sources,
`verify/REPORT-round5.md` with independent build/stack/sanitizer/game evidence,
and `interp/REPORT.md` for the rejected interpreter experiments. Frozen baseline
SHA-256 is `8d0a2a06250bbcb1ad1b20cf6ed018ea1281112da84723c998b4849bcefb66df`;
candidate `filter/nj2me` is
`d6d28bcdaba4ba240e678e26ba5e2dac5e23859f858a6d0d95d53915d96c95f7`.

## Micro3D field access and trigonometry (2026-10-22)

The next compatibility pass starts from `60a25d3`. Micro3D matrix and vector
natives used the receiver's class name to look up its class again for every
component. The receiver already points to that linked class. Calling the same
field walker directly preserves inherited/shadowed-field lookup and removes
repeated class-name hashing, without a cache or per-object storage.
`NJ_MASCOT_FAST_FIELDS=1` selects this path; CMake and the config header both
accept **0** for the general class-name path. The interpreter/JIT seam is
unchanged by this work.

There was also a correctness defect in rotations: the old sine approximation
returned **-6144 instead of -4096 at -90 degrees**, quantised angles to whole
degrees, and could overflow while forming cosine's phase at `INT_MAX`.
A 1,025-entry unsigned-short quarter-wave table now supplies all 4,096 phases,
with unsigned wrapping and reflection. It occupies **2,050 bytes of read-only
constants**, does no runtime initialization and requires no floating-point
runtime. The added `Util3D.sin(int)`/`cos(int)` APIs share it. A floating-point
oracle checks every phase; rotations also cover negative and extreme integer
arguments. The new math checks fail on the old implementation (8 failures) and
pass on the corrected implementation (52/52 at this stage).

### Isolated math measurements

Release GCC 16.2.1, native module `-O3`, CPU 4, 12 measured runs per variant,
forward/reverse order alternated, one warm-up excluded. The C harness exercises
the public VM call bridge: 600,000 rotations alternating 0 and 90 degrees, then
200,000 multiplications by a 90-degree rotation. The latter changes the matrix
on every iteration and checks its expected sine/cosine components each time;
rolling checksums also include the complete final matrix. These angles are
correct on the old code too. Both checksums match in every arm (rotation
6892164477519543683, multiplication 752192134875816851). This measures native
API work, not game FPS or Java interpreter throughput. The earlier draft used
an identity multiplier and a zero-valued checksum component; these replacement
measurements use the stronger changing-matrix oracle.

| Workload | Baseline CPU min / median (s) | Table + direct fields min / median (s) | Median time reduction |
|---|---:|---:|---:|
| Rotation | 0.233071 / 0.235290 | 0.119786 / 0.120692 | 48.71% |
| Matrix multiplication | 0.222692 / 0.224923 | 0.106163 / 0.106482 | 52.66% |

Attribution matters: the table **alone** slows this rotation workload by 8.18%
(median 0.254529 s), and speeds multiplication by 0.46% (noise). It is retained
for correct results, not claimed as a separate speed gain. The direct class
lookup removes enough overhead to more than compensate. Heap used/peak is
2,512/2,512 bytes in every measured arm; the optimization adds no writable
globals, object fields, buffers or allocations.

In isolated module objects, `.data` and `.bss` remain zero. Including the new
math APIs, `.text` grows 3,241 bytes, read-only constants/strings 2,082 bytes,
and relocation-read-only tables 96 bytes. GCC native frames for rotations and
identity initialization change from 128 to 144 bytes; multiplication falls
from 256 to 240 bytes. These are individual function frames, not a measurement
of the whole call-chain stack. Tight targets can disable the direct-field
optimization and should measure their own compiler's stack usage. API repairs
elsewhere in this compatibility pass have separate state costs; these math
figures do not claim the entire pass is RAM-neutral.

Local source snapshots, all raw measurements, the independent math oracle,
and section/stack comparisons are under
`.local-tools/debug-runs/compat-round6-20261022/lead/` (`measure-math-v2.py`,
`math-bench-v2.c`, `math-ab-v2.json`, `math-footprint.json`). These measurements
predate model/animation rendering support; they do not measure the subsequent
Devil May Cry gameplay path or its corrected matrix arithmetic.

### Integrated compatibility costs and checks

The complete pass also repairs the Micro3D primitive call ABI and projection,
adds the missing data-interface methods and Reader subclass behavior, and
persists RMS modification times. These are correctness changes; a faster old
renderer that reads the wrong arrays is not a valid rendering baseline.

The final x86-64 Release executable, including the mutable-texture follow-up,
compared with the frozen `60a25d3` build (host ELF sizes, not measured MCU flash):

| Resource | Before | After | Change |
|---|---:|---:|---:|
| `.data` | 16 B | 16 B | 0 |
| `.bss` | 6,688 B | 6,688 B | 0 |
| `.text` | 1,054,826 B | 1,066,922 B | +12,096 B |
| `.rodata` | 1,157,344 B | 1,160,384 B | +3,040 B |
| Relocation-read-only tables | 158,560 B | 158,912 B | +352 B |
| Executable file | 3,031,984 B | 3,048,952 B | +16,968 B |
| Host allocations after creating a 2 MiB VM | 2,636,352 B | 2,637,008 B | +656 B |
| Java heap used/peak at that point | 2,224 B | 2,224 B | 0 |

The **656 bytes per VM** are additional API/class metadata, with the same 878
live host blocks and 260 registered classes. This is a real RAM cost of the
compatibility additions. It is separate from the cache-free field-access
optimization. RecordStore (80 B), Reader/InputStreamReader (48 B), FigureLayout
(96 B) and AffineTrans (128 B) keep their previous instance payload sizes.
RecordStore packs its access flags into the existing field area; FigureLayout
packs its projection mode/angle and uses a null transform as identity. A call to
its newly supported `getAffineTrans()` creates a 144-byte heap allocation once
(including the allocator header), so Java can mutate that identity object.

Correct clipping also costs native stack: the primitive-rendering function's
GCC frame is 720 B, versus 352 B in the old incorrect renderer. Its measured
rendering helper chain reaches 760 B. These are not whole-VM stack peaks, and
no embedded hardware stack margin has been measured. `NJ_MASCOT_FAST_FIELDS=0`
only disables the field-access optimization; it does not undo the correctness
repairs. Existing `NJ_ENABLE_MASCOT` and `NJ_ENABLE_RMS` switches still remove
the corresponding optional libraries.

Regression covers all **93 ctests**, the **27-value desktop-JVM digest**, and
the benchmark checksum. The benchmark still executes **70,514,376 dispatches**
with heap used/peak **92,432/92,432 B**. The focused suites cover 144 Micro3D,
190 I/O, 365 RMS and 548 M3G checks. Tests include real `invokeinterface`
bytecode, 32-bit wrapping transform arithmetic, primitive and texture pixels,
malformed arguments, legacy RMS files, reopened timestamps and failed writes.
Baseline negative controls fail the newly added behaviors.

The final full run passed 92/93 immediately. The remaining demo assertion used
an invalid null-pixel constructor as a mutable image; it now uses the standard
three-int constructor. Both affected M3G tests pass after rebuilding the fixture.
The added texture tests also verify repeated subrectangles, copy ownership,
unchanged pixels, storage reuse, overflow rejection and mipmap invalidation.
Independent clean builds cover the default, fast-fields-off, float-off/M3G-off
and combined configurations. Targeted ASan/UBSan runs pass all four focused
suites with no findings; a separate 104-check Image2D probe passes too. The six
existing M3G rendering workloads retain identical full-frame pixel hashes.

The repaired racing-game loader first exposed a separate `Image2D.set` ABI bug:
only the race HUD drew because texture uploads read the wrong arguments. The
follow-up copies uploaded subrectangles into owned pixel storage, preserves
mutability across updates and invalidates cached mipmaps. It adds no Image2D
field. First upload allocates the required pixel array; further updates reuse it.
This is necessary texture storage, separate from the math optimization's costs.

The final 1,800-frame replay with fresh RMS, acceleration and steering now draws
the road, cars and scenery and responds to input, with no uncaught exception.
Heap used/peak is 10,731,168/10,734,224 B in that run. A black scene is not a valid
speed baseline, so no game FPS gain is inferred. The complete replay and a
screenshot are documented in `docs/COMPATIBILITY.md`; independent configuration,
sanitizer and final texture-delta results are retained in
`.local-tools/debug-runs/compat-round6-20261022/verify/`.

## Micro3D models, animation and deferred rendering (2026-10-22)

The next pass makes the model-rendering path usable. The original `7b12084`
executable could not construct DMC's resource-backed Figures and emitted no
model triangles. That is a compatibility baseline, not a valid speed baseline.
The clean verification control is `7a13dad`, which also contains the intervening
RV32/ARM64 JIT work with JIT disabled. Only the Micro3D overlay is compared here;
concurrent uncommitted JIT changes are excluded from the clean builds.

The implementation reduces repeated work and avoids expanding assets into a
large object graph:

* MBAC positions, normals and polygon tuples remain 16-bit values in one
  offset-based blob. Dante's 18,757-byte model becomes a 32,440-byte blob.
* MTRA keeps packed source curves plus small action/bone indices. It uses binary
  search for keys and evaluates the requested pose instead of retaining every
  frame's matrices. The largest DMC sample is 123,445 bytes encoded and 137,969
  bytes stored; the main Dante sample is 98,175 and 112,699 bytes respectively.
* A submitted model transforms each vertex once. One reusable scratch array per
  Graphics3D holds 32 bytes per vertex and 48 bytes per bone. Successive Figures
  share it rather than each retaining a transformed copy.
* The queue stores a 64-byte primitive record. Sorting uses 4-byte indices,
  with an iterative heapsort and submission order as the tie breaker. It moves
  indices rather than full records and requires no recursive sort stack.
* Texture decoding retains palette indices for model textures. Rasterization
  uses fixed-point coordinates/attributes without allocating per pixel or
  triangle. The normal model path has no depth buffer.

These are implementation properties, not a measured percentage speedup. The
60/150-second paced game replays check interaction and rendering; their frame
counts do not establish unthrottled throughput or MCU performance.

Buffers grow geometrically on demand and remain attached to Graphics3D for
reuse. `flush`/`release` clear queued items and texture references; they do not
shrink allocated scratch/queue buffers. With the default
`NJ_MASCOT_MAX_QUEUED_TRIANGLES=16384`, queue records can occupy 1 MiB and sort
indices 64 KiB at capacity, before array headers, scratch, textures and models.
This is an upper bound, not an allocation at startup. A smaller cap bounds that
part of the working set but can make complex frames throw `OutOfMemoryError`.
The field-lookup switch and `NJ_ENABLE_MASCOT=0` remain available.

The pure parsers, rasterizer and scene state introduce no writable globals.
Model/animation/texture blobs use offsets or raw values, with owning Java array
references for GC. Native pointers temporarily spanning buffer growth rely on
the current non-moving collector; a future moving collector must reacquire them
after allocation. No claim of whole-runtime RAM neutrality follows from these
choices: the newly supported APIs and rendering state have real storage costs.

### Measured storage and stack costs

Independent x86-64 Release builds use the same clean `7a13dad` base and an
18-file frozen overlay. JIT is disabled in both. These are host ELF sizes,
not an MCU flash measurement:

| Resource | Control | Model-rendering build | Change |
|---|---:|---:|---:|
| `.text` | 1,066,922 B | 1,144,554 B | +77,632 B |
| `.rodata` | 1,160,480 B | 1,164,032 B | +3,552 B |
| Relocation-read-only tables | 158,912 B | 161,056 B | +2,144 B |
| `.data` / `.bss` | 16 / 6,688 B | 16 / 6,688 B | 0 |
| Executable file | 3,048,952 B | 3,146,200 B | +97,248 B |
| Host allocations after creating a 2 MiB VM | 2,637,008 B | 2,639,840 B | +2,832 B |
| Java heap used/peak at that point | 2,224 / 2,224 B | 2,224 / 2,224 B | 0 |

The additional host allocation is API registration metadata: native bindings
increase from 1,760 to 1,795, with the same 260 classes and 878 live host blocks.
Graphics3D instance payload grows from 48 to 96 bytes; Figure grows from 96 to
112 bytes. ActionTable, Texture, Effect3D, Light, Vector3D, FigureLayout and
AffineTrans retain their previous payload sizes. Asset blobs and reusable
render buffers are separate, demand-driven Java heap allocations. The
4,500-frame Galaxy on Fire run uses/peaks at 2,598,144/2,602,064 bytes; the
1,800-frame DMC movement run uses/peaks at 2,295,184/3,002,928 bytes. Neither can
be compared as a memory optimization against the old non-rendering path.

Compiler stack-usage files give a largest single Micro3D function frame of
1,056 bytes, up from 720. The largest inspected Micro3D helper-chain subtotal
is 1,904 bytes, up from 768, along primitive submission and buffer growth.
Other subtotals include model submission at 1,440 bytes, `drawFigure` at 1,488,
and flush/rasterization at 1,104. There is no recursive sort or variable-length
stack allocation. These are native helper subtotals, not whole-VM stack peaks;
allocator/GC and interpreter callers need additional margin. Embedded hardware
stack margins have not been measured. The library-off switch removes the Java
Micro3D path; the field-access switch alone does not remove these new costs.

### Validation and practical limits

All 99 default ctests pass. The first run passed 94; five shell tests assumed
`<source>/build/nj2me` despite an out-of-tree build. Adding a scratch-only link
to the exact tested executable made those five pass, without a source change.
Clean builds also cover fast-fields-off, Micro3D-off, float-off/M3G-off, and the
combined float/fast-fields configuration. The focused default suites pass
233 API checks, 127 scene checks, 4,513 model checks, 71 raster checks and 36
command-list checks, plus the animation suite, 548 M3G checks and 146 demo
checks. Targeted ASan/UBSan runs report no findings. The no-writable-statics
check passes for 63 objects.

The 27-value desktop-JVM digest remains identical. The interpreter benchmark
still prints `BENCH sum=-136042548 s=5`, executes 70,514,376 dispatches and uses
92,432/92,432 heap bytes. This establishes interpreter regression invariants;
it does not establish a Micro3D throughput improvement.

Local evidence lives under `.local-tools/debug-runs/micro3d-round7-20261022/`:
`model/REPORT.md`, `anim/REPORT.md`, `raster/`, `api/REPORT-task31.md`,
`games/DMC-CANDIDATE2.md`, `games/GOF-CANDIDATE3.md` and
`verify/REPORT-round7.md`. Both original game JARs are unchanged. DMC reaches
3D combat, directional movement and attack, then death and retry. Galaxy on
Fire reaches rendered space flight after its introductory sequence. Mission
completion and save/continue are outside this evidence. See
`docs/COMPATIBILITY.md` for gameplay limits and the supported command-list subset.

## Micro3D triangle setup without the divides it never reads (round 8, 2026-10-22)

The Micro3D software rasteriser prepares five attribute ramps for every
triangle - texture `u`/`v`, smooth shading `s`, and the two environment-map
coordinates - and it did that unconditionally. A flat, untextured, non-env
primitive, which is what a large part of the model corpus submits for effects
and HUD geometry, never reads any of those ramps, so twenty divisions per
triangle (`src/midp/nj_mascot_raster.c`, `nj_mascot_raster_triangle`) were
computed and discarded.

Each derivative is now guarded by the same flag the row filler already tests
before reading it: `has_tex` for `u`/`v`, `smooth` for `s`, `env_smooth` for the
environment pair. Inactive ramps are written as zero. Every active-path
derivative, all rounding, the `dx_*` edge steps, and the flat vertex-0 attribute
values are unchanged, so the visible output is identical by construction, not by
tolerance.

### Independent measurement

An independent run re-derived the effect on the frozen Galaxy on Fire fighter
asset (original JAR, fixed posture and projection, 220 triangles) with an
interleaved ABBA of 12 pairs per case pinned to one core, comparing process CPU
time per triangle and the framebuffer hash of every arm. All hashes matched.
Reductions are min / median:

| Framebuffer / span | Reduction | Framebuffer / span | Reduction |
|---|---:|---|---:|
| ARGB8888, 80 px | 6.39% / 7.46% | ARGB8888, 180 px | 2.07% / 2.28% |
| RGB565, 80 px | 6.96% / 6.61% | RGB565, 180 px | 2.62% / 2.15% |
| RGB888, 80 px | 7.57% / 6.75% | RGB888, 180 px | 2.64% / 2.94% |

The first bounded scout measurement on the same asset reported 3.89-7.20%,
consistent in direction and magnitude. This is a bounded native-renderer
measurement of triangle setup and fill; it is not a whole-game FPS result, and
the titles in the corpus that submit textured, smoothly shaded models see less
than the flat path above.

### Correctness and cost

* 3,000/3,000 randomized differential cases produce identical framebuffer
  hashes (draw and low-level triangle entry points), return codes and blend
  results across RGB565/RGB888/ARGB8888, all material flags, blends 0-3, full/
  inset/offset/empty/zero-area clips, degenerate triangles and +/-32768 extreme
  vertices.
* The committed golden raster test is byte-identical: 71 checks, 0 failures.
* Focused ASan/UBSan/LSan runs, 600 cases each, report nothing.
* Linked default-build ELF: `.text` +576 B (isolated object +608 B), `.rodata`,
  `.data` and `.bss` unchanged, no symbols added or removed, executable size
  unchanged at 3,146,200 B. The single `nj_mascot_raster_triangle` frame grows
  928 -> 960 B (+32); `draw` is unchanged at 224 B. No persistent RAM, no new
  buffers, no new fields.
* The change is unconditional because it is semantics-preserving; there is no
  switch to turn off, and disabling it would only restore the wasted divides.

Evidence: `.local-tools/debug-runs/compat-round8-20261022/verify-raster/REPORT-raster-round8.md`
(independent verification, raw logs and objects), `scout-perf/REPORT.md`
(candidate discovery and first measurement), `scout-perf/reproduce.sh`.

## Three VM bugs the differential harness found, and none is about speed

A 6,505-case differential harness (`.local-tools/debug-runs/interp-diff-20261022/`)
runs the same classes on this VM and on a desktop HotSpot and compares every
labelled value.  It reported nine mismatches, in two families, and neither was
in a fused path:

    f2l / d2l of NaN, +-Inf and |x| >= 2^63     8 cases
    <clinit> that throws a non-Error            1 case

**`f2l` and `d2l` did not saturate.**  JLS 5.1.3: a float or double too large
for a long converts to `Long.MAX_VALUE`, too small to `Long.MIN_VALUE`, NaN to
0.  Both handlers did the C cast, which is undefined behaviour out of range and
on x86-64 produces `Long.MIN_VALUE` for every one of those inputs - so
`(long)1e30f` came out `-9223372036854775808` where the JVM gives
`9223372036854775807`, and `(long)Float.NaN` the same instead of 0.  Both now
test NaN and the two bounds before casting.  The fixture executes neither
opcode, so no benchmark number moved; only the harness could see it.

**A failed `<clinit>` must be wrapped, and must stay failed.**  JVMS 5.5: an
initialiser that completes abruptly with anything that is not an `Error` is
rethrown as `ExceptionInInitializerError`, and the class is left erroneous, so
every later attempt throws `NoClassDefFoundError`.  The VM returned the raw
exception instead, and because `nj_class_init()` sets `state = READY` before it
runs `<clinit>`, a second access handed out the class's default statics as if
the initialiser had run.  `nj_class_init()` now wraps a non-Error failure
(`nj_class_is_subclass_of()` against `java/lang/Error`), records the new
`NJ_CLASS_ERROR` state, and answers `NoClassDefFoundError` to later attempts;
`nj_class_init_lazy()` tests `== NJ_CLASS_READY` so the fast path cannot mistake
that state for a ready class.  The state reuses the existing byte and was
already serialised as a `u16`, so neither RAM nor the save-state format moved.

**A class literal was a `VerifyError`.**  `ldc` of a `CONSTANT_Class` is how
`Foo.class` reaches the operand stack.  The `ldc` handler asks `nj_cp_const()`
for the constant, that function knew INTEGER/FLOAT/LONG/DOUBLE/STRING only, and
the handler turned the refusal into `VerifyError: bad ldc operand` - so
`static Object o = Core.class;` and `synchronized (Core.class)` failed to load
at all.  The constant-pool resolver did not have to grow: `nj_class_object()`
already builds and caches the `java.lang.Class` mirror that `Object.getClass()`
returns, so the new case resolves the class *without* running its `<clinit>`
(a class literal is not an active use, JLS 12.4.1) and pushes that same mirror,
which is what `==` comparisons against `getClass()` expect.  The digest test
grew a class-literal category - identity, `getClass()`, `synchronized`, and a
literal of a class whose initialiser throws, which must stay lazy - and now
prints 20 identical values.

After the fixes the harness prints `ALL 6505 CASES MATCH` (9 mismatches before),
the digest test prints 20 identical values, `BENCH sum=-136042548 s=5` and
`bytecodes=76,914,888` are unchanged, ctest is 92/92, a 395-title smoke run has
the same five fatal titles as the arm built before the fixes, and on the
eight-title game set the two arms built from the same tree dump byte-identical
frames - including 暗黑启示录2, whose frame-60 md5 is identical across three runs
per arm.  The harness compares values rather than frame counts, which is why it
can see this class of bug at all.

## Five more folds, and a branch that read the wrong address

    round-2 tip (a6c062b)         min 0.171 s   med 0.171 s
    + five fused families         min 0.156 s   med 0.161 s
    call/loop microbenchmark      0.596 / 0.601  ->  0.529 / 0.531

Twelve interleaved runs per arm, pinned to one core, minimum and median; the
fixture drops from 84,915,400 to 76,914,888 bytecodes, 9.4% of its dispatches.
The five families, with the profile share that motivated each one:

    iinc i; iload j, any slot      3.54% 1945 - round 2 only folded i == j
    iload*; iload*; if_icmpXX      2.85% fixture, 4.77% 1942
    ldc2_w; ladd|lsub|lmul         2.86% fixture
    lload*; ladd|lsub|lmul         9.34% dark
    iadd; istore_k; iload_k        the read-back the round-2 fold had pre-empted

The last one is an accounting lesson rather than a new pattern: when the round-2
`iadd; istore_k` fold landed it swallowed the first half of the older
`istore_k; iload_k` fold, so 1.6 M dispatches that the histograms predicted never
appeared in the total.  The chain now folds both halves itself, which is part of
why this round's bytecode count falls by more than the four new patterns alone
would explain.

`iload*; iload*; if_icmpXX` decides the branch without materialising either
operand, and it is where the round went wrong first.  The handler handed the
fold the address of the second load instead of the branch, so the fold read the
branch opcode and its two offset bytes from the wrong place and computed a jump
target of 0xFFFFxxxx: `VerifyError ... code runs past the end of the method` at
`bench/Bench.work(I)I pc=4294943258` - on a fixture that the digest test still
passed, because no digest category had two loads feeding an if_icmp.  The fold
now takes the branch address as an explicit argument, and the digest test grew
five categories (two-load branches, long constants, loaded longs, a counter
read-back from a different slot, and the sum/read-back chain) to 18 categories
and 19 digest values.  That is the second bug the fixture could not see and the
digest could; the first is in the section below.

No bytecode is rewritten, so a branch onto either half of a fold still works, and
no fused path has a deeper peak stack than the unfused pair.  `nj_interp.o` grows
1,994 B of text (+5.6%) and `.bss` is unchanged at 6,688 B: every fold is
read-only code.  ctest 92/92; the eight-title game set has every pixel sequence
identical, 0 FAIL, three WARNs on stats fields that move with real-time pacing.

## Seven more fused pairs, and a peek that had to be merged

    round-1 tip (2a6292e)         min 0.168 s   med 0.170 s
    + seven fused pairs           min 0.169 s   med 0.171 s
    + one peek per load instead   min 0.165 s   med 0.167 s
    call/loop microbenchmark      0.612 / 0.615  ->  0.589 / 0.593

Fourteen interleaved runs per arm, pinned to one core, minimum and median.  The
middle line is why this section exists: the folds were correct, and the fixture
still got *slower*, because of how they were written rather than what they do.

### The folds

Each one peeks at the next opcode inside the host handler, executes both
instructions and dispatches once.  No bytecode is rewritten, so a branch onto
either half still does the right thing, and the fused path's peak stack depth
never exceeds the unfused one, so `stack_limit` stays valid.

| fused sequence | why | dynamic share (title) |
|---|---|---|
| `aload_0; getfield` | receiver loaded and consumed immediately | 9.93% dark, 6.29% 1942, 5.14% 1945 |
| `iload`/`iload_k; iaload..saload` | array element read | 4.37% 1942 (`iload; caload`), 3.50% 1945 (`iload_2; aaload`) |
| `iinc; iload_k` (same slot) | counter read back right after it is bumped | 3.54% 1945 |
| `iinc; goto` | loop tail | 1.43% fixture |
| `iadd; istore_k` | accumulate into a local | 2.85% fixture |
| `lcmp; if<cond>` | long compare and branch | 9.84% dark |
| `getfield(int); i2l; lcmp; if<cond>` | the whole compare chain in one dispatch | 9.84% dark |

The last two overlap on purpose.  The dark title's hot loop is
`aload_0; getfield; i2l; lcmp; ifge; goto`; when the `aload_0` fold runs first,
the chain fold still sees its `getfield` already consumed and folds the
remaining four, so the loop head becomes one dispatch instead of five.  That is
what makes the title's 600-frame bytecode total fall from 1.06-1.21 G to
0.68-0.75 G.

**A real-game bytecode total is not a measurement.**  The same binary on the
same title with the same frame count produces 8.3 M and 22.4 M bytecodes on two
30-frame runs, because the game branches on wall-clock timing; a pass over six
titles found none that reproduces its own bytecode count.  The totals above are
reported as what they are - evidence that the folds fire, not a speed claim.

### The peek is paid on every execution, not only when the fold fires

The first version of the load folds tested the branch case first and the array
case second, each with its own `pc < code_len` test and its own read of
`code[pc]`.  On the fixture that cost more than the folds saved: `iload_3` is
10% of the fixture's instructions and almost never feeds an array load or a
branch on a loaded value, so the peek was paid millions of times to fold
nothing.  One read of `code[pc]` and two range tests turned the same folds into
a win.

### Two more bugs, and the test that caught the second only after it grew a witness

**The constant-pool index was read twice.**  `NJ_GETFIELD_BODY(U2(), ...)`
evaluated its index expression twice, and `U2()` advances `pc`, so the second
read resolved a different constant-pool entry; the title that exercised the fold
died with `NoClassDefFoundError`.

**The chain fold left the operand stack two slots low.**  It decremented `sp`
once more after reading the pair it had just popped, so every later instruction
in the method worked against a stack pointer that pointed below the real top.
The title crashed with SIGSEGV inside `nj_interp_run` (exit 139).  The digest
test passed, because every method in it returned as soon as it had produced its
value, and the corrupted stack was never read again.  What gives this bug a
witness is a `ch_loop` category - a loop that keeps using the stack after the
chain - plus the negative control: re-adding the extra `sp -= 2` makes three of
the fourteen digests differ while `BENCH sum` still prints the expected value.
The digest test now covers thirteen categories (int branches, null branches,
store/load, const+arith, div/rem by zero, long/float, the round-2 families,
`ch_loop`, null receivers) and compares fourteen values against a desktop JVM.

### Switches, RAM, verification

    NJ_ENABLE_SUPERINSTR=0   still builds, still passes the digest test, and
                             prints the pre-fusion 112,117,067 bytecodes

No writable static and no heap allocation: `.bss` is byte-identical (6,688 B),
flash grows by 4,504 B of text and 1,056 B of read-only data, and
`tools/check_no_statics.sh` passes over the same 57 objects.  ctest is 92/92 in
this tree (90 before an unrelated workstream added two tests).  The eight-title
game set reports every title's pixel sequence identical and 0 FAIL; three titles
carry a WARN on stats fields (`paints`, `gfx`, `natives`) that move with
real-time pacing, and the same-binary controls show the same spread.

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
that label.  An unconverted handler has no label, its entry points at
`L_haveop` - the switch entry immediately AFTER the single fetch - and it runs
through the switch exactly as before.  **The two kinds coexist**, so the
conversion is one opcode at a time and after EVERY one the benchmark must still
report exactly 112,117,067 bytecodes - a mistake shows up as a wrong count, not
as a subtle wrong answer.

`NJ_THREADED_DISPATCH` is 0 in every build since 2026-10-21; it used to be
selected automatically on GCC/Clang, but the block that did so was nested inside
`#ifndef NJ_PIXFMT`, so `ctest`, the corpus runner and every number in this file
were measured on the switch while a hand build quietly ran an interpreter with a
double-fetch bug (`docs/notes/OPEN-issue-threaded-dispatch.md`).  It is now
explicitly opt-in, and `tools/check_threaded_dispatch.sh` compiles both bodies
under `ctest`.  `DISPATCH()` is then just `continue`; ESP32 (xtensa and riscv),
Cortex-M and three Android ABIs built by this project lose speed, not
correctness.

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

## The invoke family: measured, and it made no difference

`bipush`/`sipush` + `iand` fused into a superinstruction, `ireturn` was skipped
because its `break` follows `DO_RETURN`, and `invokeinterface` (0xb9) stayed
unconverted longer than anything else for a mundane reason: 0xb6, 0xb7, 0xb8 and
0xb9 have always shared ONE case body, so there was no single handler to convert
without splitting it.  They now share one replicated-dispatch label instead,
with four table rows pointing at it.

It was measured properly before it was kept, and it is NOT a speedup:

| instrument | A (switch) | B (converted) | verdict |
|---|---|---|---|
| synthetic, 40 samples | min 0.179 s, median 0.183 | min 0.180 s, median 0.183 | identical |
| vnes, 300 frames, 24 samples | min 0.112 s, mean 0.1130 | min 0.112 s, mean 0.1135 | identical |

Both arms were built ONCE into separate binaries and then run alternately, so no
round could measure the wrong build - the mistake that invalidated an earlier
A/B loop here.  The control was verified two ways: the two binaries differ
byte-for-byte, and re-applying the conversion from the unconverted source
reproduced `/tmp/nj_B` byte-identically.

The explanation is that neither workload ever becomes invoke-bound: the
synthetic benchmark is loop arithmetic, and vnes on this corpus renders its menu
rather than emulating a ROM.  So the honest summary is that the conversion
completes a technique the other seventeen handlers already use and costs
nothing - not that it bought anything.  A reported zero is a result.

The instrument that DID matter here is the benchmark's own output line,
`BENCH sum=-136042548 s=5`.  It is now the only thing that can tell a correct
dispatch conversion from a subtle one: the bytecode total stopped being usable
the moment a fused pair started executing two instructions while counting one,
which is why the total is 105717067 rather than 112117067.

## Three checks the interpreter asks millions of times: about 5.7%

A `gprof` run over the benchmark (built `-O2 -g -pg`, run on the fixture, flat
profile) says the loop itself is 68% of the samples and everything else is
per-call overhead spread thin - except that the call COUNTS are enormous:

| called from the loop | calls per bench run |
|---|---|
| `nj_heap_owns_object` (every reference operand about to be dereferenced) | 4,801,320 |
| `nj_class_init` (every `new`, static field access, static invoke) | 4,802,000 |
| `nj_frame_pop` (every return, every native call) | 4,801,413 |

All three are a handful of instructions behind an external function call.  They
are now `static inline` in `nj_internal.h` (`nj_heap_owns_object_fast`,
`nj_class_init_lazy`, `nj_frame_pop_fast`) with the out-of-line versions kept in
`nj_heap.c`/`nj_thread.c` for callers that want a symbol, and the interpreter
calls the inline ones.  `nj_class_init_lazy` is the one that changes shape
rather than just moving: a class that has reached `NJ_CLASS_READY` never needs
the call at all, so the common path is a load and a predictable branch.

Measured the way the rest of this file measures - CPU time, pinned to one core,
minimum of N, alternating arms, two binaries built once:

    A (before)   min 0.174 s    top3 0.174 0.174 0.175    607.6 M bytecodes/s
    B (after)    min 0.164 s    top3 0.164 0.164 0.166    644.6 M bytecodes/s

**5.7%**, with the bytecode total identical at 105,717,067 on both arms, so the
work done is the same and only the overhead moved.  ctest on the same tree is
58/59 (the one failure is the concurrent MIDI work's `test_consts`, unrelated).

The same profile was used to price two things NOT adopted:

  * **Link-time optimisation** (`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`, which
    would have inlined these and more automatically): min 0.174 s against a
    0.176 s baseline in ten interleaved runs - about 1%, inside the noise this
    instrument reports, for a build-system change that affects every port.  Not
    worth it; the three explicit inlines are what the profile actually asked for.
  * **Inlining `nj_frame_push`**, the other half of the 4.8M frame pairs: its
    body validates two overflow conditions and zeroes the locals the arguments
    did not fill, and it is called from two sites inside the one huge switch.
    Duplicating it there grows the hottest function in the runtime for a few
    percent; left as a function call on purpose.


## The rasteriser: per-pixel overhead a sprite title pays every frame

The bytecode-side work above was measured on `tests/fixtures/bench.jar`, which
is compute bound on purpose.  A profiled real title looks different: 3000 frames
of 合金弹头4(完美汉化版)_4053 under gprof (`-O2 -g -pg`, pinned to one core)
spent

    46.85%  nj_region_draw          3,217 calls
    15.32%  nj_plot           230,630,400 calls
    12.61%  nj_gfx_blend_pixel 230,630,400 calls

and the two 230-million-call functions are the interesting part: a run of pixels
of ONE colour was being drawn by calling `nj_plot()` per pixel, which called
`nj_gfx_blend_pixel()` per pixel, which re-asked the surface for its format and
re-checked its bounds per pixel - to discover, per pixel, that a fully opaque
colour replaces the destination.  `nj_span()` now does the obvious thing when
the colour is opaque (pack the colour once, then store 2/4 bytes or three
channel bytes per pixel), which is what the non-blending branch already did for
every other blend mode.

`nj_region_draw()` had the same shape one level up: the general loop asks
`nj_trans_inverse()`, tests the mask, scales the coverage and calls
`nj_src_pixel()`/`nj_raw_set()` once per pixel even when the caller asked for an
untransformed, unmasked, fully opaque blit.  Three fast paths were added in
front of it, all requiring `transform == NJ_TRANS_NONE`, no mask, no XOR and
`alpha == 255`:

  * an identical source and destination format copies whole rows with `memcpy`;
  * an opaque source in another format converts per pixel in a loop that has
    hoisted the destination format and the clip arithmetic out;
  * an ARGB8888 source under `rop PAINT` copies the opaque pixels and calls the
    blender only for the translucent ones (this is the case 合金弹头4 hits: a
    full screen of decoded ARGB8888, every pixel opaque).

Measured the usual way - CPU time, one core, alternating arms, both binaries
built once from the same revision, 600 frames uncapped:

    title                          before              after
    合金弹头4(完美汉化版)_4053      0.351 0.354 0.350   0.246 0.247 0.246   -30%
    仙魔飞行棋_417                 0.224               0.143               -36%
    疯狂企鹅中文版_10228           11.463              8.953               -22%
    模拟人生3中文版_6162           0.308               0.244               -21%

炸弹人_10311, 战争艺术2之全球联盟中文版_1918 and 实况足球经理2009_10609 are
unchanged to the millisecond: they do not blit like that, which is the expected
shape of the result.  The compute fixture is unchanged too - `bench.jar` min
0.167 s, 633.0 M bytecodes/s, inside the 2.4% spread of its own baseline - so
the interpreter itself was not touched.

One more per-frame cost came out of the same profile, and it is not in the
rasteriser at all: the runner's frame loop asked for the whole `nj_vm_stats`
snapshot once per frame to compare `frames` against `--run-frames`, and that
snapshot computes the bytes in use - `nj_heap_used_bytes()` walks every block in
the Java heap.  `nj_vm_frames()` now answers the one field the loop wants
(2% on 合金弹头4, and the walk grows with the number of live objects, so it is
worth more on a title with a fuller heap).

What makes this safe to believe is that the rendering is identical, not that the
tests pass: the same titles were run against both binaries with
`--dump-frames` and the PPMs are byte-for-byte equal (nine titles at 60 frames,
plus eighteen of twenty sampled corpus titles - the two that differ, book4 and
宇宙战机_10521, differ between two runs of the SAME binary as well, because they
animate on wall-clock time).  ctest is 61/61 on the same tree.

## The lookup a game makes millions of times, which the fixture never makes

The compute fixture measures the interpreter and the interpreter is not what a
game spends its time in.  Profiling 合金弹头4(完美汉化版)_4053 with `gprof` (1200
frames, uncapped) put `nj_field_lookup` at 16.8% of sampled self time with
1,498,980 calls, and the call graph says where they came from:

    1,346,898  bais_read -> nj_field_lookup
    1,498,980  nj_field_lookup -> nj_class_find
    1,498,980  nj_field_lookup -> nj_class_resolve
    1,498,980  nj_field_lookup -> nj_class_link

`bais_read` is `ByteArrayInputStream.read()` - the native every `DataInputStream`
read lands in - and it looked up its `buf`/`pos`/`count` fields BY NAME on every
single call.  Each of those lookups resolved the class from its name string
(hash, bucket walk, `strcmp`) and then walked the class's field array, then
called `nj_class_link()` to discover the class had been linked long ago.

Three changes, all of them plumbing rather than policy:

  * `nj_class_find()` keeps four slots keyed on the ADDRESS of the name (a
    literal in this library is the same address every time).  A hit is only
    taken when the class it names really carries that name, so a caller that
    reuses a stack buffer gets the slow path instead of a wrong answer, and a
    dotted name can never match, because class names use slashes.
  * `nj_class_lookup()` calls `nj_class_link()` only while `state < READY`.
  * `nj_string.c` asks `vm->cls_string` first - the class table has recorded it
    since it was created; the lookup stays as the answer for the window before
    that.

Measured the same way as everything else - CPU time from the runner's own stats
line, one core, two alternating runs per title per binary, both binaries built
once, 600 frames uncapped, smallest of the two:

    title                          before   after    change
    合金弹头4(完美汉化版)_4053      0.236    0.183    +22.5%
    模拟人生3中文版_6162           0.234    0.182    +22.2%
    实况足球经理2009_10609         1.308    1.131    +13.5%
    帝国时代3-亚洲王朝_6288        0.155    0.140     +9.7%
    疯狂企鹅中文版_10228           9.116    8.299     +9.0%
    炸弹人_10311                   0.110    0.104     +5.5%
    仙魔飞行棋_417                 0.143    0.136     +4.9%
    开心农场_240                   0.128    0.126     +1.6%
    total                         11.430   10.301     +9.9%

The compute fixture moves the other way, inside its own spread: 607.6 M
bytecodes/s before, 600.7 M after.  That is the honest and useful half of this
result - `bench.jar` allocates and dispatches, it does not call the library, so
it cannot see any of this.  A change that makes games faster while leaving the
fixture where it was is exactly why the corpus titles are the workload that
matters here.

## The 3D rasteriser: 5.5x slower than the thing it replaced, then 2.3x

The first version of `Graphics3D.render(VertexBuffer, ...)` drew every triangle
by calling the target `Graphics.fillTriangle` through the interpreter.  That is
cheap per triangle and flat-shaded, and it cannot do a depth buffer, a texture
or back-face culling at all, so it was replaced by a rasteriser that writes the
target's pixels itself.  The first honest measurement of the replacement was
bad, and worth recording: on one machine, 4,000 overlapping triangles into a
240x320 target took

| version | ms for 4,000 triangles |
| --- | --- |
| `Graphics.fillTriangle` path (flat, no depth, no texture) | 18 |
| software rasteriser, first version | 180 |
| + incremental edge functions instead of per-pixel multiplies | 99 |
| + solving each scanline's span instead of testing the bounding box | 41 |

The last two are the whole story of writing a filler that is worth having.
Evaluating the three edge functions per pixel costs three multiplies per pixel
per edge, and about half the pixels in a triangle's bounding box are outside
the triangle anyway; both go away when the edges are stepped by a constant and
each scanline's span is solved directly (each edge is linear in x, so "inside"
is one bound per edge).  The final version is 2.3x the cost of the path it
replaced and does three things that path could not do at all.

The benchmark is not a fixture check - it would be a timing assertion, and
those rot.  It is a mode of the existing fixture, run by hand:
`nj2me --port headless --heap 16 --size 240x320 --fps 0 --run-frames 10
--prop nj2me.bench=4000 tests/fixtures/m3gdemo.jar` prints
`M3G BENCH triangles=4000 ms=41`.  The same jar on the older binary is the
other arm, which is how the table above was produced.

## The blit a 2D title spends its frame in: 1.58x across eight titles

`nj_region_draw` had three fast paths - same format, cross-format, and
ARGB8888 onto ARGB8888 - and every one of them was written from a profile of a
title that happened to hit it.  An ARGB8888 source blitted onto a **16-bit**
screen hit none of them: the general loop asked `nj_src_pixel()` and then either
`nj_raw_set()` or `nj_gfx_blend_pixel()` once per pixel, three function calls to
rediscover that an alpha of 255 means "replace".  A decoded PNG is an ARGB8888
buffer and most collected titles draw onto an RGB565 surface, so this was the
common case, not an exotic one.

The new branch is the ARGB-to-ARGB rule with the destination format left open: an
opaque pixel goes through `nj_dst_pixel()`, a translucent one through
`nj_gfx_blend_pixel()`, a transparent one is skipped, and `ROP_SET` copies all
three.  Same-tree binaries (one build with the branch, one without), CPU time
from the runner's `stats:` line, `taskset -c 3`, 400 frames uncapped, minimum of
three alternating runs:

| title | before | after | speed-up |
| --- | ---: | ---: | ---: |
| `帝国时代3-亚洲王朝_6288` | 0.095 s | 0.035 s | 2.71x |
| `开心农场_240` | 0.082 s | 0.031 s | 2.65x |
| `仙魔飞行棋_417` | 0.107 s | 0.044 s | 2.43x |
| `浴火银河2完整版_7689` | 0.121 s | 0.059 s | 2.05x |
| `炸弹人_10311` | 0.082 s | 0.041 s | 2.00x |
| `合金弹头4(完美汉化版)_4053` | 0.145 s | 0.073 s | 1.99x |
| `实况足球经理2009_10609` | 1.170 s | 0.831 s | 1.41x |
| `模拟人生2_10762` | 0.280 s | 0.203 s | 1.38x |
| **total** | **2.082 s** | **1.317 s** | **1.58x** |

How it was checked, because a blit is only "faster" until a pixel moves:

* `tests/test_gfx.c` gained the case (an ARGB8888 row onto an RGB565 surface:
  opaque copies, half-transparent blends to `0xFF840000`, transparent is
  skipped, `ROP_SET` writes it too) - 276 assertions, and the same 276 pass with
  the branch disabled, so the new loop and the general loop agree on the case.
* three titles dumped through `--dump-frames` are byte-for-byte identical
  between the two binaries (`合金弹头4(完美汉化版)_4053`, `炸弹人_10311`,
  `开心农场_240`), with the same binary run twice as the control.
* the corpus sweep is re-run with the new binary; every verdict and colour count
  is expected to match the previous sweep exactly.

## Two lookups the interpreter makes millions of times

A run asks the constant pool for the same reference tens of millions of times -
`实况足球经理2009_10609` asked 34.5 million times and missed 1,076 - and it reads
or writes a field about as often, so both are pure per-instruction overhead.

**The constant-pool cache is probed where it is asked for.**  `nj_cp_resolve()`
computed the entry's offset and read its tag *before* it consulted the cache, so
every hit paid for work only a miss needs, plus a call into another translation
unit.  The interpreter now probes the (class, index) slot itself
(`nj_cp_hit()` in `src/core/nj_internal.h`, hashing with the same function the
table uses) and calls the resolver only on a miss; the resolver reads the tag
and offset after the cache answers no.

**A field's type switch is inlined.**  Every `getfield`/`putfield`/`getstatic`/
`putstatic` called a function whose body is "work out the address, then switch on
the field's type": 1.78 million `nj_field_read_ref()` calls and 1.76 million
`nj_field_read_long()` calls in 400 frames of the same title.  The switch now
lives in `src/core/nj_internal.h` as `nj_field_load_int()` / `nj_field_store_int()`
and friends, the interpreter inlines them, and the out-of-line accessors in
`src/core/nj_vm.c` call the same bodies - one implementation, so an inlined read
and a called one cannot drift.

| title | before | after | speed-up |
| --- | ---: | ---: | ---: |
| `实况足球经理2009_10609` | 0.409 s | 0.325 s | 1.26x |
| `帝国时代3-亚洲王朝_6288` | 0.038 s | 0.037 s | 1.03x |
| `合金弹头4(完美汉化版)_4053` | 0.048 s | 0.047 s | 1.02x |
| `模拟人生2_10762` | 0.168 s | 0.167 s | 1.01x |
| `炸弹人_10311`, `开心农场_240`, `仙魔飞行棋_417`, `浴火银河2完整版_7689` | - | - | 1.00x |
| **total** | **0.840 s** | **0.753 s** | **1.12x** |

The constant-pool half of that, measured on its own (same tree, one build with
the inline probe and one without), is 1.04x-1.06x on the two invoke-heavy
titles and 1.00x on the rest: the win is real but it is per instruction, and a
title that spends its time in the rasteriser does not notice it.

The `--stats` line now ends with `cpcache=<hits>/<misses>`, because the numbers
above are what decided this change and they were not visible from the command
line before.  The full test suite passes with both changes, three titles dumped
with `--dump-frames` are byte-for-byte identical between the two binaries with
the same binary run twice as the control, and the corpus sweep still stands at
389 passed / 6 failed.

## The copy behind a transform

`合金弹头2013BT版_678` blits a 120x320 rotated column and a 21x19 mirrored
sprite every frame, and the profile of a 600-frame run put
`nj_region_draw` at 56% of its own CPU with 10.8 million calls to
`nj_src_pixel` - one per pixel of those two blits.  None of the existing fast
paths covered them: they all require `transform == NONE`, so every one of those
54,000 pixels a frame went through `nj_trans_inverse()` plus two round trips
through the format switches (`nj_src_pixel` to read, `nj_dst_pixel` to write)
to be copied unchanged.

The new branch keeps the inverse transform per pixel - it is a switch on a
value the caller fixed - and copies the pixel, because a source in the
destination's own format under full opacity and `ROP_SET` cannot be changed by
the round trip.  It sits after the untransformed copy, so the common case still
takes the row-wise `memcpy`, and it inherits the same conditions (no mask, no
xor, alpha 255, `ARGB8888` only under `ROP_SET`).

Correctness was checked the way the earlier rasteriser work was: four titles
dumped with `--dump-frames` and compared byte-for-byte between the two
binaries, with the same binary run twice as the control - `合金弹头4(完美汉化版)_4053`
identical, and the three that differ differ by exactly as much against
themselves (they animate on wall-clock time; the control shows it).

Measured the way the other rounds were (CPU time of the process, not wall
clock, `taskset -c 3`, 1500 frames at 30 fps with the same key script, best of
two runs), the whole change is worth what the profile said it would be - the
copy is a few percent of these titles' frames, and 27% of one of them:

| title | before | after | speed-up |
| --- | ---: | ---: | ---: |
| `合金弹头2013BT版_678` | 0.514 s | 0.503 s | 1.02x |
| `炸弹人_10311` | 0.625 s | 0.561 s | 1.11x |
| `合金弹头4(完美汉化版)_4053` | 0.121 s | 0.117 s | 1.03x |
| `仙魔飞行棋_417` | 2.418 s | 2.425 s | 1.00x |
| **total** | **3.677 s** | **3.607 s** | **1.02x** |

The number to read is the profile, not this table: a title only gains when its
frame is mostly that copy, and at 6000 frames `合金弹头2013BT版_678` (by then in
the level, not in its menu) went from 1.535 s to 1.202 s, 1.28x.  A 6000-frame
A/B of `仙魔飞行棋_417` first read as a 19% regression and turned out to be the
two runs reaching different scenes: repeated at 1500 frames it is 1.03x-1.05x,
which is why the table above uses one frame budget for both sides.

## The class-name cache that was never being hit (round 2, 2026-10-22)

`nj_class_find` has a pointer-keyed fast path: the hot callers pass string
literals ("java/io/ByteArrayInputStream" once per stream read), the same
address arrives again and again, and a hit costs one pointer compare and one
`strcmp` instead of a normalising copy, a hash and a bucket walk.  The cache
was there, correct, and *never used by the one path that needed it most*:
`class_resolve_from_source` normalised `internal_name` into a **stack**
buffer and looked that up.  Every resolution therefore arrived at the same
stack address, and one slot can only answer for one name at a time - an asset
loader alternating `ByteArrayInputStream` and `ByteArrayOutputStream` evicted
it on every call, so all of those lookups walked the class hash instead.

The fix is to ask `nj_class_find` with the caller's own pointer first.
`nj_class_find` normalises dotted names internally and validates every hit
with `strcmp`, so a dotted name or a reused stack buffer still takes the slow
path and gets the same answer; only the pointer identity of a literal is
trusted, and only for a name that `strcmp` agrees with.

What it is worth, on a workload built to be exactly reproducible
(`IoBench`, `2,000,000` `ByteArrayOutputStream.write(int)`, `500,000`
`write(byte[],0,4)` and three passes of `2,000,000` `ByteArrayInputStream.read()`
plus `500,000` `DataInputStream.readInt()`): 92,006,066 bytecodes and
16,001,501 natives, the same sink and the same bytecode count in both arms, so
the two sides do identical work.  Five interleaved runs per arm on one core,
CPU time of the process:

| arm | min | median | reduction |
| --- | ---: | ---: | ---: |
| before | 1.9299 s | 1.9320 s | - |
| after | 0.9918 s | 0.9972 s | 48.61% min, 48.39% median |

On a real title the share is smaller because it also interprets and renders:
`鬼泣3D中文版_3376` at 30 fps for 600 frames with the same key script went
from 0.7345-0.8812 s of CPU to 0.6390-0.7187 s, which is 103.6 -> 84.1 ms of
CPU per million bytecodes (about 19%).  That one is *indicative, not
controlled*: the two arms did not do identical work - the slower arm was still
loading when the faster one was fighting (`tris` 23,924-40,486 against a
consistent ~40,000) - so the reproducible fixture above is the number to
believe.

No measurable cost, and no new state: the cache slots are the same ones that
were already there, and `bench.jar` is unchanged (sum `-136042548`,
70,514,376 bytecodes, heap 92,432/92,432).  99/99 ctest and all
27 superinstruction-digest values pass.  The change is in
`src/core/nj_class.c` and is three lines plus a comment.

## The triangle fill loop that reloaded its own state (round 2, 2026-10-22)

With the class-name lookups fixed (previous section), the gprof profile of
`鬼泣3D中文版_3376` at 30 fps put the Micro3D rasteriser at about 22% of CPU:
`mr_fill_segment` 14.08% self over a 30.2 s, 900-frame replay, plus a helper
the compiler inlined into it that gprof then attributed to `mr_bmp_parse` with
43.7 million calls - a symbol-attribution artifact, not the BMP parser.  A
synthetic but deterministic benchmark for that file now exists:
`bench.c` in `.local-tools/debug-runs/perf-round9-20261022/raster-bench/`
draws 20,000 pseudo-random triangles per material configuration on a 240x320
ARGB8888 surface through the public `nj_mascot_raster_draw()`, and prints CPU
milliseconds plus an FNV hash of the framebuffer, so two builds can be
compared by linking the same object against each library.

Three changes to `src/midp/nj_mascot_raster.c`, each measured against the
previous stage with 8 interleaved ABBA rounds per arm on CPU 2:

1. the 15 per-pixel helpers (`mr_pixel_get/put`, palette fetch, texture shade,
   flat/smooth colour, toon pick, env add, ...) are `static inline`, and the
   blend switch moved into a `static inline mr_blend` that the public
   `nj_mascot_blend_pixel` now delegates to.  Total -7.09% (replace -7.29%,
   half -4.08%, colour -19.40%, flat -6.42%, env -5.59%).
2. the fill loop copies every state field it reads into locals.  The pixel
   stores go through an `unsigned char *`, which the compiler must assume can
   alias the state struct, so before this every `st->` read inside the loop was
   a fresh load.  The loop also walks a running `pp += bpp` pointer instead of
   recomputing `y * stride + x * bpp` per pixel, and calls address-taking raw
   get/put helpers.  Another -12.28% min on top.
3. the little-endian word helpers (`mr_u16`, `mr_u32`, `mr_put_u16`,
   `mr_put_u32`) use `memcpy` on a little-endian host and keep the portable
   byte-wise form for big-endian ports.

Cumulative, against the same library before the change (8 rounds per arm,
every framebuffer hash identical):

| configuration | before min | after min | reduction |
| --- | ---: | ---: | ---: |
| textured, smooth, replace | 147.2 ms | 115.6 ms | 21.44% |
| textured, smooth, HALF | 186.5 ms | 140.8 ms | 24.52% |
| flat colour, replace | 73.2 ms | 33.5 ms | 54.21% |
| textured, flat, replace | 139.8 ms | 112.1 ms | 19.83% |
| textured, smooth, env map | 186.9 ms | 162.9 ms | 12.86% |
| total | 733.6 ms | 564.9 ms | 23.00% |

Correctness, not just hashes: the 71-check `test_mascot_raster` golden suite is
byte-identical, the differential harness from the round-8 raster work (random
formats, flags, blends, clips, degenerate and out-of-range triangles) gives
identical framebuffer, return-code and blend-pixel digests for 20,000 cases
against the previous rasteriser, and 3,000 cases run clean under ASan+UBSan.
`test_mascot`, `test_mascot_scene` and `test_mascot_commands` still pass
(233 / 127 / 36 checks), and a 900-frame paced replay of `鬼泣3D中文版_3376`
behaves as before (only the known caught boot `EOFException`, no uncaught
exception, 104,032 triangles drawn).

RAM: no new state or allocation; `.data` and `.bss` are 0 in the object file
and the stack frames are unchanged (`mr_fill_segment` 176 B, bounded
`nj_mascot_raster_triangle` 960 B, `nj_mascot_raster_draw` 224 B).  The host
code grows: object `.text` 15,195 -> 17,379 B, most of it the inlined pixel
paths.  This is a host-side raster measurement, not a claim about game FPS;
the game share above says how much of one title's CPU the rasteriser owns.

## Eight field pointers the hot natives kept re-resolving (round 2, 2026-10-22)

The class-name cache fix above removed the class lookup from every
`nj_field_lookup(vm, "java/io/ByteArrayInputStream", …)`, but the walk that
follows it — find the pkg/class, walk the field table, compare name and
descriptor — still ran on every element of bulk I/O and every line of text.
The 900-frame paced DMC replay attributed 6.17% of its CPU to
`field_lookup_walk` alone, on top of `nj_field_lookup` 3.70%, with about 8
million lookups: `bais_read` 2.58 M, `baos_write` 1.56 M, `font_for` 1.44 M,
`baos_ensure` 1.04 M.  They only ever ask for eight fields:
`ByteArrayInputStream.buf/pos/count/mark`,
`ByteArrayOutputStream.buf/count`, `Font.style/size`.

`nj_hot_field(vm, slot, cls, name, desc)` (`include/nj2me/nj_config.h`,
`NJ_ENABLE_HOT_FIELDS`, default 1) memoises exactly those eight `nj_field *`
per VM and leaves `nj_field_lookup()` untouched for everything else, so the
resolved pointers — and therefore the semantics, including subclass field
hiding — are identical to the uncached path.  The slots are cleared in
`clear_host_caches()` when a save state is restored, because the restore
rebuilds the class structures the pointers point into.  No allocation, no
writable globals; `NJ_ENABLE_HOT_FIELDS=0` restores the per-call walk and the
exact pre-change binary layout.

Measured on the deterministic I/O fixture (`io-bench.jar`: 2 M
`ByteArrayOutputStream.write(int)`, 0.5 M range writes, then three passes of 2 M
`ByteArrayInputStream.read()` and 0.5 M `DataInputStream.readInt()`, one fixed
sink), interleaved A/B, 6 runs per arm, CPU 2, child CPU time from
`getrusage`:

| arm | min CPU | median CPU |
| --- | --- | --- |
| `NJ_ENABLE_HOT_FIELDS=0` | 0.9686 s | 0.9742 s |
| default (memo) | 0.4049 s | 0.4110 s |

That is a 58.20% / 57.81% reduction with identical work — bytecodes
92,006,066, natives 16,001,501, heap 9,344,752/9,344,752 and the printed sink
are the same in all 24 runs.  (The first measurement of this change quoted
64.28%; its macro-off arm had been built before the change landed and was not
source-identical.  The table above is an independent rerun from two clean
builds of the same commit, 12 interleaved runs per arm.)

A paced real game agrees in direction but is only indicative, because its
timing-dependent state means the two arms do slightly different work: a
600-frame `鬼泣3D中文版_3376` replay cost 0.5859/0.6035 s CPU (min/median) with
the memo off and 0.5085/0.5114 s with it on, i.e. 13.21% / 15.25% less, while
the memo arm drew 1.5% *more* bytecodes and triangles.

RAM: 64 bytes per VM (8 pointers).  `nj_vm_sizeof()` is 10,808 -> 10,872 B on
x86-64, `.data`/`.bss` are unchanged and the macro-off build keeps 10,808.
`bench.jar` is byte-identical (`BENCH sum=-136042548`, 70,514,376 bytecodes,
heap 92,432/92,432), the 27-value digest matches the desktop VM and the full
`ctest` suite passes 99/99.

Three more slots followed the same evidence one commit later: the 900-frame
profile still showed 2.7 million class resolutions, now almost all from
`DataInputStream.in`, `DataOutputStream.out` and
`DataOutputStream.written`, which the data-stream natives read on every
element.  With those three memoised (`NJ_HF_DIS_IN`, `NJ_HF_DOS_OUT`,
`NJ_HF_DOS_WRITTEN`) the deterministic I/O fixture falls a further 11.73% /
11.85% (min / median CPU, 4 runs per arm, previous commit as the control)
from 0.4135/0.4144 s to 0.3650/0.3654 s, bytecodes and sink again identical;
together the two changes put the fixture roughly 63% below the
`NJ_ENABLE_HOT_FIELDS=0` arm.  The eleven slots cost 88 B
per VM (`nj_vm_sizeof()` 10,896 on x86-64), still with no allocation and no
writable globals, and `NJ_ENABLE_HOT_FIELDS=0` still restores 10,808 B.

## The Graphics state that was re-resolved around every primitive (round 2, 2026-10-22)

Every `Graphics` native in this runtime is the same three steps: serialise the
Java object's fields into an `nj_gfx` on the C stack, run one primitive, write
the mutated state back. The serialise step reads thirteen fields (`target`,
`translateX/Y`, `clipX/Y/W/H`, `color`, `blend`, `stroke`, `font`, `xorMode`,
`xorColor`) and the write-back touches eleven of them, each through
`nj_field_lookup()`. That resolver first resolves the *class name* and then
walks the class chain comparing field names, so a title drawing a few thousand
primitives per second paid a name walk around every single one of them.

The fix extends the per-VM hot-field slots (the earlier `ByteArray*` / `Font` /
`Data*` work) with thirteen `Graphics` slots. The field pointers are resolved
once per VM; the per-call path is one array index and a field read/write at a
known offset.

Deterministic measurement, no game timing involved: a fixture draws 100,000
iterations of `fillRect`/`drawRect`/`drawLine`/`drawString`/`drawImage` with
translate and clip churn after every iteration — 1,100,002 `Graphics` calls,
10,400,066 bytecodes, sink `42382025` (identical in every run):

| arm | CPU s (min / median, 8 runs each, interleaved ABBA) |
| --- | --- |
| clean HEAD 68a33f9 | 0.8362 / 0.8384 |
| hot Graphics slots | 0.3595 / 0.3623 |

That is **57.01% / 56.79%** less CPU on this drawing-bound workload. Unlike a
game replay it proves only the drawing-call overhead, so treat it as a ceiling
for paint-heavy 2D titles rather than a frame-rate claim.

Correctness: `ctest` 99/99, `bench.jar` invariants and the 27-value digest
unchanged, and 900-frame paced real-game replays of 午夜台球圣诞中文版_8677
(444 paints) and 鬼泣3D中文版_3376 (682 paints, 149,462 triangles) exit 0 with
no uncaught exception.

RAM: one pointer per slot, so **+104 B per VM** (`nj_vm_sizeof()` 10,896 →
11,000; the whole hot-field set is +192 B over the 10,808 baseline). Building
with `-DNJ_ENABLE_HOT_FIELDS=0` restores 10,808 B and the per-call lookups.
No writable statics, no allocation, no eBPF-style caches.

## The little store every opaque glyph pixel made (round 2, 2026-10-22)

After the Graphics slots took the name walk out of the drawing natives, the
next profile of the same 2D fixture (`-pg` build, one frame set) put
`nj_gfx_blend_pixel` at the top with 29.6 M calls, and `nj_plot` accounted for
12.0 M of them. Every glyph interior and every short opaque fill handed
`nj_plot` a colour with alpha 255 and `blend == SRC_OVER`, and `nj_plot` called
the general blend routine anyway. That routine does its job correctly: for
`a >= 255` it just stores the converted colour. But it reaches that store
through a call, a surface validity check, two bounds checks, a per-pixel
`(a * alpha + 127) / 255` division and a coverage clamp.

`nj_plot` now takes the store directly when `blend == SRC_OVER` and the colour
is opaque — exactly the bytes the `a >= 255` branch would write — and leaves
the blended and XOR cases on their existing paths. `nj_plot`'s contract already
requires the point to be inside the clip, which is what those bounds checks
would confirm.

Deterministic fixture (the same 1,100,002-call drawing workload as above,
bytecodes 10,400,066 and sink `42382025` identical in every run, 8 interleaved
runs per arm):

| arm | CPU s (min / median) |
| --- | --- |
| clean HEAD ed6c170 | 0.3605 / 0.3626 |
| opaque `nj_plot` store | 0.3467 / 0.3496 |

**3.83% / 3.59%** less CPU. A paced real-game comparison on
新西游记－格斗天王_1211 could not be used as evidence: the two arms diverged in
workload (15.7–18.0 M vs 17.7–21.7 M bytecodes, because wall-clock-paced replay
state differs run to run), so the honest claim is the deterministic one — it
helps opaque glyph/fill pixels, and nothing else about the drawing path
changed.

Correctness: `test_gfx` 324, `test_clip` 15, `test_directgraphics` 34,
`test_lcdui` pass, `ctest` 99/99, `bench.jar` invariants and the 27-value
digest unchanged. No new state: RAM, stack and the public surface are
untouched.

## Round-2 summary: what the round adds up to (fixtures, not FPS)

The individual sections above each measure one change against its parent. This
one measures the round as a whole: the round-8 binary
(sha256 `0858a1f0…`, before any of this work) against the round-2 tip built
from HEAD, on the two deterministic fixtures, four interleaved runs per arm on
a pinned core with child CPU from `RUSAGE_CHILDREN`. Every run of every arm
reported the same bytecode count, native calls, heap figures and `PHASE`/sink
output, so the two arms did identical work.

| fixture (fixed work) | round-8 binary CPU s (min) | round-2 tip CPU s (min) | reduction |
| --- | --- | --- | --- |
| `io-bench.jar` (2 M `ByteArrayOutputStream.write(int)`, 0.5 M `write(byte[],int,int)`, 3x 2 M `ByteArrayInputStream.read()` + 0.5 M `DataInputStream.readInt()`) | 1.853 | 0.3595 | **80.6%** |
| `gfx-bench.jar` (100 k iterations of a 2D drawing mix: fill/draw rect, line, string, image, clip and translate) | 1.283 | 0.3551 | **72.3%** |

The Mascot triangle-fill fixture (`.local-tools/debug-runs/perf-round9-20261022/raster-bench`) measured **23.0%** for the fill-loop
rewrite alone, and the interpreter benchmark `bench.jar` is unchanged as
always (`BENCH sum=-136042548`, 70,514,376 bytecodes, heap 92,432/92,432), which
is the point: these changes remove per-call bookkeeping from native-heavy
paths, not bytecodes from the interpreter.

Correctness work landed in the same round and is written up in
`docs/COMPATIBILITY.md` rather than here: the 512-literal intern table that
let a cached constant-pool string be collected (and the two StringBuilder
temporaries that were never rooted), the 64-bone cap that hung a boxing
title on its loading screen, PNG stale-CRC parity, and the keypad
`TextBox`/`TextField` editor.  None of them is a speed claim.

What this is not: an FPS claim. Paced real-game replays are wall-clock
dependent, so two runs of the same binary can differ in bytecodes and
triangles; the round-2 real-game evidence is functional (a title that used to
stall now plays, a save that used to truncate now round-trips) and the speed
numbers are fixture numbers with identical work on both sides.

Costs: 24 hot-field slots are **+192 B per VM** (`NJ_ENABLE_HOT_FIELDS=0`
restores the old lookups exactly); the growable intern table is host memory
that only grows for VMs with more than 512 literals; the raster fill rewrite
adds no RAM and `mr_fill_segment`'s frame is 176 B; the bone roster growth is a
Java `Object[]` whose capacity is the array length (rigs of 64 or fewer links
allocate exactly what they did before). No writable static data was added
(the no-statics check still passes over 63 objects).

## The rasteriser branch tree, resolved once per row (round 14, 2026-10-23)

A Micro3D triangle has the same material flags for all of its pixels, yet the
pixel loop was re-evaluating them for every pixel: textured or not, flat or
Gouraud shaded, toon or not, environment map or not, and which blend mode.

The rasteriser now writes the combinations that dominate real scenes as
separate span loops - textured flat and smooth shading, each with replace and
half blend, untextured smooth shading with both blends, and a constant-colour
fill - and keeps the generic loop as the fallback for everything else (toon,
environment maps, ADD/SUB, the other colour-key orders).  A macro generates
each loop from one body so the arithmetic stays the one in the generic loop,
and the mode is chosen once per row, which is cheap because spans are wide.

Measurement: the deterministic raster bench (`tools/`-style harness in
`.local-tools/debug-runs/perf-round9-20261022/raster-bench/`, 20,000 random
triangles per configuration through `nj_mascot_raster_draw` on a 240x320
ARGB8888 surface, 8 interleaved ABBA pairs pinned to one CPU):

| configuration | base min | cand min | min | median | framebuffer |
|---|---:|---:|---:|---:|---|
| textured, smooth, replace | 116.571 ms | 98.813 ms | -15.23% | -15.04% | identical |
| textured, smooth, half | 141.146 ms | 121.888 ms | -13.64% | -12.66% | identical |
| flat colour, smooth, replace | 33.769 ms | 24.366 ms | -27.85% | -28.09% | identical |
| textured, flat, replace | 113.039 ms | 96.924 ms | -14.26% | -14.03% | identical |
| textured, smooth, environment | 154.965 ms | 160.576 ms | +3.62% | -1.73% | identical |
| total | 559.490 ms | 502.567 ms | -10.17% | | |

The environment row is the unchanged generic path and its spread is the
measurement noise; it is not a regression.  Correctness: the span loops were
checked against the previous implementation with five bench seeds (different
random geometry, 5 configurations x 20,000 triangles each, 500,000 triangles in
total) and every framebuffer hash matched, `tests/test_mascot_raster.c` stays
byte-identical at 71 checks, and an ASan+UBSan build of that test reports
nothing.  Full ctest 101/101, bench.jar and the 27-value digest unchanged.

Cost: the rasteriser object grows from 17,379 to 27,823 bytes of text at the
shipped `-O3` (+10.4 KB, no RAM, no new state), and `mr_fill_segment`'s frame
grows by 120 bytes to 296 (still below the unchanged 960-byte triangle frame).
`-DNJ_MASCOT_RASTER_SPANS=0` compiles the single generic loop instead (object
17,627 bytes) and draws the same picture.

This is a rasteriser measurement, not a game frame rate: a paced 鬼泣3D replay
was run as a check and its bytecode/triangle counts vary by several percent
between identical runs, so no game-level percentage is claimed.

## drawRGB after the source range has already been checked (2026-10-23)

`Graphics.drawRGB` already validates the source row endpoints with 64-bit
arithmetic before staging pixels. Nevertheless, every staged pixel went
through `nj_array_get_int`, repeating the array kind, bounds and element-type
checks. The new path checks once that the source is an actual `int[]` and
reads its payload directly. Other array kinds passed by native C callers
retain the generic accessor. The row structure, alpha expression, exception
order, staging allocation and drawing order are unchanged. Staging remains
necessary when the source is the destination image's own pixel array.

The frozen parent is `3c3070767f235bc59a0331bb90d9374eb8923e8e`.
A native harness calls the registered Java drawing native, including staging
and the complete draw operation. Each timed process performs 199,000 calls
and consumes 740,800,000 source pixels across six cases: opaque large images,
alpha-enabled opaque images, mixed alpha, negative scan with clipping and
translation, small images, and source/destination overlap. Both arms use the
same Release library with only the LCDUI object replaced. Measurement uses
`CLOCK_PROCESS_CPUTIME_ID`, CPU 23, and two ABBA groups (four runs per arm).

| Combined CPU time | Parent | Candidate | Reduction |
|---|---:|---:|---:|
| Minimum | 1.298835 s | 0.849530 s | 34.59% |
| Median | 1.299760 s | 0.850132 s | 34.59% |

The first parent sample was slower (1.583321 s); the minimum and median above
avoid presenting that outlier as additional benefit. Every pixel hash,
call count, heap-use value, host allocation counter and teardown count agrees
across all eight runs. This is a measured native drawing improvement, not an
emulator-wide speedup or a game FPS percentage. The real-game replays were
capped at 30 FPS and their work varies with time, so they cannot support such
a percentage. RV32 and ARM targets were not benchmarked in this round.

Semantic comparison includes 97 image/screen cases with positive, negative,
zero and overlapping scan lengths, clipping, translation, source/destination
overlap, alpha boundary values and the non-int-array fallback, plus six
invalid/empty argument cases. The aggregate image hash is
`7556dac9ba7ed1a1` in both arms. Existing LCDUI and graphics suites pass
3,108 and 324 assertions respectively. Four original-game parent/fixed
900-frame replays finish without uncaught exceptions; their stage limits and
sample comparisons are documented in
[COMPATIBILITY.md](COMPATIBILITY.md#stream-reads-checked-against-the-official-cldc-contracts-2026-10-23).
The full integrated tree passes CTest 102/102 and the LCDUI suite also passes
ASan/UBSan with leak checking.

RAM is unchanged on the measured x86_64 build: `g_drawRGB` stack usage is
320→320 B, the LCDUI object data is 28,960→28,960 B, BSS is 0→0, and the
existing temporary buffer has the same size, count and lifetime. Object text
increases 120 B. A preliminary copy-loop variant used 16 B more stack and
was rejected in favour of this candidate. No configuration switch is needed
for the retained path's measured RAM footprint.

Reproduction artifacts are under
`.local-tools/debug-runs/official-round46-20261023/`: the scout's
`perf-scout/REPORT.md`, harness, assembly and stack reports; the Lead's
`lead/abba-summary.json`, eight raw ABBA logs, game commands/hashes and
sanitizer/full-suite logs. These local artifacts are not checked into Git.

## The mip chain that copied level 0 twice (round 47, 2026-10-23)

`m3g_image_mips()` builds the downsampled levels a textured triangle needs once
it gets small on screen. It used to allocate the whole chain including a full
RGBA copy of level 0, then `memcpy` the image's pixels into it - a duplicate of
a buffer that already existed, and a copy that had to be made before the first
small level could be produced.

The chain now holds levels 1..N and the sampler takes level 0 from the image's
own pixels, so both the duplicate buffer and the `memcpy` are gone. Measured
with the two builders (parent and current) compiled side by side against the
same deterministic `Image2D` fixtures, interleaved pairs, `taskset -c 5`,
`-O2` Release flags:

| Texture | Baseline (median) | Current (median) | Ratio | Pairs | Spread |
|---|---|---|---|---|---|
| 128x128, 21,552 builds/arm | 12,895.0 ns | 10,961.4 ns | **0.851** | 10 | 0.35% |
| 256x256, 5,408 builds/arm | 54,331.6 ns | 44,045.1 ns | **0.811** | 10 | 0.32% |

Both arms rebuild the same chain from a cold cache each iteration; the current
arm does fewer bytes and one less pass. Every cached level was compared byte
for byte against the parent chain for 1x1, 2x1, 3x5, 128x128 and 256x256, so
the saving is not bought with different pixels. Level 0 after a smaller level
was also checked to be the original pixels again.

This is a per-texture-upload win, not a frame-rate number: a title uploads a
palette when it changes character or stage, so the CPU saved is a shorter stall
at that moment, plus the RAM described in
[FOOTPRINT.md](FOOTPRINT.md#the-second-copy-of-every-3d-texture-round-47-2026-10-23).
The 4-title replay in COMPATIBILITY.md shows identical frames before and after,
so nothing here changes what is drawn.

Reproduction: `.local-tools/debug-runs/compact3d-round47-20261023/mip-measure/`
(snapshotted parent/current sources, `probe.c`, `run_measure.py`). Those local
artifacts are not checked into Git.

## Three divides per shaded pixel (round 48, 2026-10-23)

`Texture.generateShadedPalette()` in the reference builds 32 shaded copies of a
texture's palette, one per shade level, as `channel * level / 31` truncating.
The runtime keeps only the 256 unshaded entries in its texture blob and applies
that same formula per sampled pixel instead - which put three integer divides
on the hottest path the Micro3D renderer has, because a shaded textured span
shades every pixel it writes.

`mr_tex_shade()` now multiplies instead of dividing. For `c <= 255` and
`level <= 31` the numerator stays below 8192, and multiplying by 8457 and
shifting 18 is exactly `x / 31` over that whole range - verified exhaustively
for every numerator, not sampled. The level's scaled reciprocal is computed
once and shared by the three channels. RGB565 targets go one step further: the
surface conversion keeps the top bits, so shading straight into 565 is the same
colour (`floor(x / 31) >> 3` is `floor(x / 248)`, `floor(x / 31) >> 2` is
`floor(x / 124)`, and both are again exact multiply-shifts). A 565 surface
without blending therefore shades and converts in one pass instead of building
an ARGB word and unpacking it again.

### How it was measured

No game in the corpus is CPU-bound in the renderer, so a controlled workload is
needed. `.local-tools/debug-runs/raster-round48-20261023/raster-bench.c` drives
`nj_mascot_raster_triangle()` directly on synthetic model and pattern blobs with
eight passes: full-screen textured fills with smooth and flat shading, an
environment-mapped fill, a toon fill, a flat-colour fill, and 40,000 random
small textured triangles for the setup-heavy case. 400 full-screen triangles and
40,000 small ones per run, 240x320 RGB565, `taskset -c 5`, seven interleaved
pairs. An A/A run of the same binary moves 0.1%, which is the resolution the
ratios below are read against.

| Step | CPU (min of 7) | vs previous | vs parent |
|---|---|---|---|
| parent (`9887375`) | 1.2428 s | - | - |
| `x / 31` -> exact multiply-shift | 0.8842 s | 0.718 | 0.712 |
| + one scale per pixel for the three channels | 0.8718 s | 0.986 | 0.701 |
| + direct RGB565 shading (no ARGB round trip) | 0.7220 s | 0.828 | **0.581** |

Per pass, parent -> this build, from the same run:

| Pass | Parent | This build | Ratio |
|---|---|---|---|
| fill-textured-shaded | 0.0503 s | 0.0226 s | 0.449 |
| fill-textured-smooth | 0.0718 s | 0.0534 s | 0.744 |
| fill-envmap | 0.1048 s | 0.0850 s | 0.811 |
| fill-toon | 0.0581 s | 0.0410 s | 0.706 |
| fill-colour | 0.0042 s | 0.0041 s | 0.976 |
| setup-textured (40,000 small triangles) | 0.7866 s | 0.3678 s | 0.468 |
| setup-colour | 0.0926 s | 0.0899 s | 0.971 |
| fill-8888 | 0.0797 s | 0.0605 s | 0.759 |
| framebuffer sums | `95cd0b35` / `1507cd61` | same | - |

The framebuffer sums are identical, so every pass writes byte-identical pixels:
`sum565` covers the RGB565 passes and `sum888` the ARGB8888 one. On top of that,
`test_mascot_raster` (71 checks, exact pixel comparisons against the reference
behaviour), `test_mascot` (233), `test_mascot_scene` (127) and
`test_mascot_commands` (36) all pass, including under ASan/UBSan/LSan, and the
whole CTest suite is green.

This is rasteriser cost, not a frame-rate number. The only title in the corpus
that calls the Micro3D API (`鬼泣3D中文版_3376`) renders through this path every
frame; 900 frames on both builds exit 0 with the same screen progression, but
its animation is keyed to the wall clock, so frame hashes are not an oracle for
it - even two runs of one binary differ. The titles whose rendering is
frame-count driven (`生化危机3D(CAPCOM经典)_3671`, `真幻想三国志完整版_12469`,
`3D激爆拳击_8805`) replay byte-identically on both builds at 400 frames. The
interpreter is untouched: `BENCH sum=-136042548 s=5`, 70,514,376 bytecodes, and
the 27-value superinstruction digest still match.

Reproduction: `.local-tools/debug-runs/raster-round48-20261023/`
(`raster-bench.c`, `ab.sh`, the two arms' logs and the game replays). Those
local artifacts are not checked into Git.

## The textured span of the M3G rasteriser (round 49, 2026-10-24)

JSR-184 draws through two different paths in this runtime. `Graphics3D.render()`
with a vertex and index buffer hands each triangle to the Java-side
`Graphics.fillTriangle`; a `World` render goes through the C software rasteriser,
`sw_tri()`, which owns the depth buffer, the texture sampler and the blending.
Only the second path can be optimised here.

`sw_tri()` used to run every pixel through one general loop: solve `key`, test
the z-buffer, then call `sw_texel()`, which walks the wrap-mode and filter
branches to reach NEAREST sampling, returns an `0xAARRGGBB` word, and hands it
to `m3g_pixel_write()`, which converts it back to the surface format. In RGB565
with a flat colour and no blending, every one of those steps is predictable
from the triangle's state, so the loop was paying for a branch tree, an ARGB
round trip and a function call per pixel.

### Where the pixels are

An API scan over `tests/games/` finds 26 titles that reference
`javax/microedition/m3g` (and 3 more that use Mascot Capsule instead). Replaying
them showed that most draw their scenes late or fall back to 2D sprites; within
the first 600 frames of a replay only `毁灭战士3_10598` really rasterises
triangles: 486,472 of them, 23,189,487 textured samples, all of them
flat-coloured, NEAREST-filtered, REPEAT-wrapped, MODULATE, RGB565 with
ALPHA blending and a fully opaque texture. The other 25 titles report `tris=0`
over the same replay window, so a title replay can prove the change is neutral
for them but not that it is fast.

### What changed

`src/midp/nj_lib_m3g.c` gains `sw_near_texel()`, an inline that reproduces
`sw_texel()`'s REPEAT + NEAREST arithmetic exactly (fractional fixups, the
`0.999f` corner case, the round and the clamps) without the branch tree, and
two specialised span loops in front of the general one. A span takes a loop when
it is textured, flat-shaded, not a sprite, REPEAT on both axes, MODULATE,
NEAREST, `athresh <= 0`, and either REPLACE or ALPHA blending; the loop is then
picked by surface format (RGB565 or ARGB8888). Each pixel still solves the same
`key`, still tests and writes the z-buffer at the same point, and still blends
through `sw_blend()` when a texel's alpha is below 255 (with a fully opaque
texel ALPHA blending replaces, which is the same bytes).
RGB565 writes the three channels straight into the destination word.

### How it was measured

`.local-tools/debug-runs/m3g-raster-round49-20261023/m3g-bench.c` includes
`nj_lib_m3g.c` to reach the file-static `sw_tri()`, then renders 200
full-screen 24x16 textured quads (400 triangles, a 128x128 RGBA texture,
240x320 RGB565, `taskset -c 5`) in eleven configurations. Each arm ran seven
times, interleaved; a second copy of the parent binary ran as an A/A control.

| Configuration | Parent (min) | This build (min) | Ratio | A/A noise |
|---|---|---|---|---|
| 565 NEAREST flat texture, REPLACE | 0.2767 s | 0.1863 s | **0.673** | 1.000 |
| ARGB8888 NEAREST flat texture, REPLACE | 0.2613 s | 0.1978 s | **0.757** | 1.009 |
| 565 NEAREST textured, ALPHA blend | 0.4787 s | 0.4335 s | **0.906** | 1.000 |
| ARGB8888 NEAREST textured, ALPHA blend | 0.4334 s | 0.3900 s | **0.900** | 1.003 |
| 565 NEAREST gouraud | 0.2902 s | 0.2917 s | 1.005 | 1.000 |
| 565 LINEAR flat | 0.5317 s | 0.5276 s | 0.992 | 1.013 |
| 565 NEAREST `athresh` | 0.2631 s | 0.2613 s | 0.993 | 1.011 |
| 565 NEAREST CLAMP | 0.2382 s | 0.2406 s | 1.010 | 1.004 |
| 888 NEAREST flat | 0.2667 s | 0.2643 s | 0.991 | 1.006 |
| 565 untextured flat | 0.0465 s | 0.0467 s | 1.004 | 1.000 |

The four configurations the fast path covers lose a third (REPLACE) or a tenth
(ALPHA blending, which still runs `sw_blend()` per pixel) of their CPU; the
seven it does not cover sit inside the A/A resolution. The eleven framebuffer
checksums are byte-identical between the arms, so every covered pixel writes the
same bytes and every uncovered pixel is untouched.

The real title agrees. `毁灭战士3_10598`, 600 frames at 30 fps, `taskset -c 5`,
six interleaved pairs, user CPU: parent 0.547 / 0.513 / 0.520 / 0.526 / 0.516 /
0.532 s, this build 0.461 / 0.460 / 0.468 / 0.454 / 0.464 / 0.466 s - min
0.513 -> 0.454 s and median 0.523 -> 0.463 s, **11.5% less CPU**. Six frames
dumped either side (`--dump-every 100`) are byte-identical.

### Cost

The fast path is code, not state: no field, no scratch buffer and no static
table was added, so the RAM rule holds and `.data`/`.bss` are untouched in both
builds. It costs flash - see
[FOOTPRINT.md](FOOTPRINT.md#the-textured-span-of-the-m3g-rasteriser-round-49-2026-10-24):
4,328 bytes of `.text` in `nj_lib_m3g.c.o` in the hosted `-O2` build and 1,495
bytes in the freestanding `-Os` one.

Gates: `ctest` 103/103, `test_m3g` 581 checks and `test_m3g_references` 389
checks under ASan+UBSan+LSan with no reports, `tools/check_no_statics.sh` clean,
the 27-value superinstruction digest and `BENCH sum=-136042548 s=5` unchanged.

Limits: this is rasteriser time, not a frame rate - the title is paced at 30 fps
and spends its remaining CPU elsewhere. LINEAR filtering, mip-mapped sampling,
gouraud shading, sprites, `athresh`, RGB888 surfaces and non-REPEAT wrapping
still take the general loop.

### The test that keeps the span honest (round 50, 2026-10-24)

The round above could only say "looks right on a bench kept outside Git, and on
one title's frames". `tests/test_m3g.c` now has `test_textured_span_variants()`,
which renders the same 2x2-textured quad twice through
`Graphics3D.render(VertexBuffer, IndexBuffer, Appearance, Transform, int)` - the
pipeline a World's meshes use, so the rasteriser and not a Java fallback:

1. once with `WRAP_CLAMP`, which the specialised loops deliberately do not take,
   so the pixels come from the general loop;
2. once with `WRAP_REPEAT`, which they do take.

The texture coordinates cover exactly one repetition, so a correct sampler must
produce the same image either way, and the check compares all 76,800 pixels of
the 240x320 target byte for byte. It then asserts that every pixel in the
interior is one of the four modulated texel colours and that all four appear, and
repeats the comparison with an RGB (three-byte texel) texture to cover the
`bpp == 3` branch, where a texel's alpha is 255 and the loop replaces.

Rendering into an `Image2D`-backed target means the exercised loop is the
ARGB8888 one: an `Image` used as a `Graphics3D` target is ARGB8888 here even
though the display is RGB565, and `Image2D` has no RGB565 format. The RGB565 half
is still covered only by the bench's identical checksums above.

The test is not vacuous: changing the specialised loop's blue channel by one
(`tb ^ 1u`) fails four of its assertions, and restoring the source brings the
suite back to `test_m3g` 592 checks, 0 failures (11 more than before).

## A strip's vertices are transformed three times, not once (round 51, 2026-10-24)

`jr_strip()` walks a triangle strip and hands each triangle to
`jr_triangle()`, which fetches all three of its vertices: `jr_fetch()`
transforms a vertex by the model-view-projection matrix, lights it and fogs it.
Two of those three vertices were fetched for the previous triangle already.

A profile of `毁灭战士3_10598` (6,000 frames at `--fps 240`, heap 32) with a
`noinline` `sw_tri`/`jr_emit` build puts the cost where the strip cache would
help:

| Symbol | Share | Calls |
|---|---|---|
| `sw_tri` | 60.81% | 6,360,928 |
| `nj_region_draw` | 22.80% | 21,735 |
| `jr_fetch` | 7.84% | 19,082,784 |
| `nj_run` | 2.38% | - |

All 1,674 `jr_draw()` calls in that title are strips (16 strips of ~1,054
indices each, 559 vertices), so every triangle paid three fetches where one
would do.

### What changed

`jr_strip()` keeps the transformed vertices of the two indices the next
triangle starts with and reuses them, which is the post-transform vertex cache
of an ordinary strip rasteriser and costs no memory beyond two `jr_vtx`
records on the stack:

- the degenerate test (`a == b`, `b == c`, `a == c`) runs on the indices before
  any fetch, so exactly the triangles `jr_triangle()` used to skip are skipped;
- a fetch failure still skips just that triangle, as before;
- the winding swap of odd triangles is preserved by swapping the first two
  vertices of the call, not their contents;
- the kept records are the values `jr_fetch()` would return again, so no pixel
  can move.

### How it was measured

Two sessions of interleaved runs on `毁灭战士3_10598`, `--fps 240`,
`--run-frames 6000`, `taskset -c 5`, user CPU from `RUSAGE_CHILDREN`:

| Build | Session A min / med | Session B min / med |
|---|---|---|
| parent (1e49791) | 4.205 s / 4.982 s | 4.904 s / 5.377 s |
| strip cache + blit groups | **3.925 s** / **4.736 s** | **4.526 s** / **4.883 s** |

That is -6.7% min / -4.9% med in the first session and -7.7% min / -9.2% med in
the second. The CPU frequency of the measurement machine drifts by up to 8%
within a session, which is why the arms are interleaved and why two sessions
are reported instead of one.

Twelve frames dumped at `--fps 30` (`--dump-every 100`) are byte-identical
between the two builds.

The same binary also carries the blit change below; a blit-only arm measured
4.178 s against the parent's 4.205 s, so the title-level gain is the strip
cache's. The blit change's own evidence is its loop benchmark.

### Cost

`.text` +1,600 B in `nj_lib_m3g.c.o`; no field, no static, no buffer;
`.data`/`.bss` unchanged. 240 B more C stack inside `jr_strip()` (the two kept
vertices and the three temporary vertices of a triangle).

## The blit that tested what it was about to overwrite (round 51, 2026-10-24)

`nj_region_draw()`'s ARGB8888-to-RGB565 branch exists because 浴火银河2 paints
a screenful of sprites every frame. Its per-pixel loop reads the source word,
decides from the alpha byte whether the pixel is opaque, replaces it, blends it
or skips it, and packs the result to RGB565.

A replay of `毁灭战士3_10598` (1,200 frames) issues 5,560 `nj_region_draw()`
calls: 3,526 ARGB8888-to-ARGB8888, 1,674 ARGB8888-to-RGB565 with
`NJ_ROP_SET`, 360 ARGB8888-to-RGB565 with `NJ_ROP_PAINT`. For a whole frame of
opaque pixels the alpha test never fires, and yet it is paid per pixel.

### What changed

The RGB565 row loop converts up to eight pixels at a time
(`NJ_GFX_BLIT_GROUP`): it reads the group, checks the alpha bytes, and when
every pixel is opaque - or the operation is `NJ_ROP_SET`, which copies whatever
the source holds - it converts the group with no tests. A group that contains
translucency falls back to the original per-pixel code, so a translucent or
PAINT frame is emitted by exactly the code that emitted it before. The pack
expression and the two-byte store are unchanged.

### How it was measured

The controlled bench (`.local-tools/debug-runs/blit-round51-20261024/`,
outside Git) draws a 240x320 ARGB8888 image onto a 240x320 RGB565 surface
through the public `nj_gfx_draw_image()` and checksums the destination:

| Case | Before | After | Delta |
|---|---|---|---|
| opaque, per 240x320 frame | 40.5 us | **38.4 us** | -5.2% |
| every 64th pixel alpha 128 | 46.7 us | **40.5 us** | -13.4% |

The destination FNV hashes are identical in every run (`25fa0c9a` opaque,
`c55a50fd` mixed). A 1,500-frame menu-phase replay of the same title (which
paints 1,496 full screens and draws almost no 3D) measured min 0.2178 s to
0.1930 s and med 0.3152 s to 0.2603 s, but that phase's whole CPU is a fifth of
a second, so the percentage is noisier than the benchmark's.

### Cost

`.text` +1,112 B in `nj_gfx.c.o`; `grp[8]` is 32 B of C stack in the row loop;
no field, no static, no buffer; `.data`/`.bss` unchanged.

## The blits whose pixels are mostly nothing (round 52, 2026-10-24)

`毁灭战士3_10598` blits more than it rasterises. A gprof build of the tree
after round 51 (6,000 paced frames at 30 fps, `--fps 240`) puts `jr_emit()` at
62.76% - `jr_fetch()`, `jr_vertex()`, `jr_lit()` and `jr_fog()` are inlined into
it, `sw_tri()` into those - and `nj_region_draw()` next at 27.26%.

A census of those blits (temporary instrumentation at the top of
`nj_region_draw()`, 1,200 frames at 30 fps) says why:

| Call | Count | Pixels |
|---|---|---|
| ARGB8888 -> ARGB8888, `NJ_ROP_PAINT` | 3,530 | 155.1M (1,857 full frames of 249x334, 1,673 sprites of 20x21) |
| ARGB8888 -> RGB565, `NJ_ROP_SET` | 1,676 | 128.7M (every one a full frame) |
| ARGB8888 -> RGB565, `NJ_ROP_PAINT` | 359 | 27.6M |

The ARGB8888-to-ARGB8888 branch - the one round 51 did not touch - is the
biggest pixel mover, and 85.2M of its 155.1M pixels have alpha 0, 42.4M have
alpha 255 and only 15.5M are actually translucent. In groups of eight that is
10.28M all-transparent groups, 4.74M all-opaque groups and 2.91M mixed ones:
two thirds of the loop's work writes nothing or copies 32 bytes.

### What changed

The ARGB8888-to-ARGB8888 `NJ_ROP_PAINT` loop decides per row, once, which of
two loops runs:

- the first eight pixels are read into `probe[]` and their alpha bytes are
  ORed and ANDed together. If every alpha byte is zero the row starts
  transparent, if every one is 255 it starts opaque, and either way the group
  loop is used;
- the group loop reads eight pixels, ORs and ANDs their alpha bytes, and then
  skips the group (all transparent), `memcpy`s it (all opaque) or runs the
  original per-pixel code over it. A row that keeps producing mixed groups -
  a soft gradient rather than a sprite - drops back to the plain loop after
  four of them, so an image of soft edges pays the group machinery only for
  its first four groups (and eight probe reads);
- a row whose probe group is mixed uses the plain loop for the whole row.

`nj_paint_argb_pixel()` (opaque replaces, translucent composites, transparent
leaves the destination alone) is the single implementation both loops call, so
the pixels cannot drift apart; the group path only decides *which* pixels to
hand it.

### How it was measured

The round-51 bench (`.local-tools/debug-runs/blit-round51-20261024/`, outside
Git) gained three ARGB8888-to-ARGB8888 cases - an opaque image, an image where
every 64th pixel has alpha 128, and an image where every pixel does. Per
240x320 frame, min of interleaved runs:

| Case | Before | After | Delta |
|---|---|---|---|
| opaque ARGB image | 18.67 us | **15.90 us** | -15% |
| every 64th pixel translucent | 28.07 us | **20.33 us** | -28% |
| every pixel transparent | 20.01 us | **12.20 us** | -39% |
| every pixel alpha 128 (worst case) | 157.0 us | **160.0 us** | +1.9% |
| the two RGB565 cases | unchanged | unchanged | noise |

The destination hashes are identical in every run (`9bee3eda`, `e1e89aeb`,
`0df4c0da`, `06f55852`), as are the RGB565 ones from round 51.

At title level the effect is small enough to argue about: over 6,000 paced
frames of `毁灭战士3_10598`, two independent interleaved sessions reported
paired medians of -1.3% and -1.5% (one session's minimum was +0.8%), and the
2D title `合金弹头4` moved by +4.6%/-0.6% and +2.4%/-0.6% between sessions -
i.e. inside this machine's ±2% drift. The census above is the honest summary:
`nj_region_draw()` is 27% of the 3D title and roughly 45% of its blitted pixels
go through this branch, and a 15-39% loop win on that share predicts 1-3% of
the whole, which is the size of the medians that were measured.

### Cost

`.text` +1,600 B in `nj_gfx.c.o`, +1,792 B in the `nj2me` executable;
`.data` and `.bss` unchanged. The row loop holds `probe[8]` and `grp[8]` (up to
64 B of C stack); no field, static or buffer is added, so RAM is unchanged.

## The span solve that ran every scanline (round 53, 2026-10-24)

`sw_tri()` fills a triangle by walking its bounding box row by row. For each
scanline it solves the three edges for the x where they cross that row, keeps
the widest inside interval, and then walks the pixels between them. The solve
needs one division per edge, so it cannot be removed without changing the
pixels, but it was doing a great deal of work *around* those divisions on every
single row:

```c
for (i = 0; i < 3; i++) {
    float w = sgn * (i == 0 ? e0 : (i == 1 ? e1 : e2));
    float d = sgn * (i == 0 ? dxe0 : (i == 1 ? dxe1 : dxe2));
    if (d > 0.0f) { ... } else if (d < 0.0f) { ... } else if (w < 0.0f) { ... }
}
```

The sign `sgn` is a triangle constant, the derivatives `dxe*` are triangle
constants, and only the edge values `e*` change from row to row. So the loop
became three straight-line blocks that use pre-scaled accumulators:

```c
float sgn = (area > 0.0f) ? 1.0f : -1.0f;
float sd0 = sgn * dxe0, ...;          /* pre-scaled derivatives */
float w0 = sgn * e0, ...;             /* pre-scaled edge values */
int dir0 = (sd0 > 0.0f) ? 1 : ((sd0 < 0.0f) ? -1 : 0);   /* per edge, once */
...
if (dir0 > 0) { x = fminx + (-w0) / sd0; if (x > lo) lo = x; }
else if (dir0 < 0) { x = fminx + w0 / (-sd0); if (x < hi) hi = x; }
else if (w0 < 0.0f) { empty = 1; }
...
w0 += sw0; w1 += sw1; w2 += sw2;      /* the row step, pre-scaled too */
```

The arithmetic is bit-identical: `sgn * x` is exact for `sgn = ±1`, and
`(-a) + (-b)` rounds exactly like `-(a + b)`, so the pre-scaled accumulators
reach exactly the same floats as `sgn * e` did. The dividing expression
`fminx + (-w0) / sd0` has the same operands as before, only hoisted. The four
per-pixel loops also use locals for `sw->dtest`, `sw->dwrite`, `sw->z` and the
depth scale `ia * 65536.0f` instead of reloading them from `sw` on every pixel.

### How it was measured

Two harnesses, both outside Git. The round-49 bench
(`.local-tools/debug-runs/m3g-raster-round49-20261023/`) renders the same
76,800 pixels per iteration and its eleven configurations keep their previous
framebuffer hashes - they are checked on every run, and all eleven were
`SAME` after this change. The new triangle-size bench
(`.local-tools/debug-runs/m3g-sweep-round54-20261024/m3g-trisize.c`) keeps the
pixel count per iteration constant while varying the triangle size, which is
what a setup change trades against:

| Triangles | Pixels per triangle | Before | After | Delta |
|---|---|---|---|---|
| 24x16 quads | 192 | 0.488 ms | **0.461 ms** | -5.5% |
| 8x8 quads | 32 | 0.591 ms | **0.489 ms** | -17.3% |
| 4x4 quads | 8 | 1.175 ms | **0.800 ms** | -31.9% |

(min of five interleaved rounds each, framebuffer hashes identical in all
three; on the round-49 bench the texture-dominated cases move by -1.5% to
-2.9% because their time is per-pixel, and the untextured flat case, which
spends nearly everything in the setup, moves by -12.4%.)

Real titles: `毁灭战士3_10598` over 4,000 paced frames, eight interleaved
rounds against the round-52 binary gave paired deltas of +2.02, -0.72, -3.05,
+1.01, -2.99, -1.23, -4.71 and -3.23 percent - median -2.11%, mean -1.61%,
six of eight rounds faster. The machine's load average was 3-5 during the run,
so the honest reading is "about 1-2% of the title, direction consistent with
the 48 pixels per triangle it averages". Its dumped frames are byte-identical
to the previous binary, as are those of `合金弹头4(完美汉化版)_4053`.

### Cost

`nj_lib_m3g.c.o` `.text` 275,900 -> **274,488** B (-1,412 B, the hoisted code
is smaller than the per-row loop it replaces); the `nj2me` executable `.text`
2,905,966 -> **2,905,166** B (-800 B); `.data`/`.bss` unchanged. Only C stack
locals are added, no field, static or buffer, so RAM is unchanged.

## Two pixels per word in the rgb565 conversion (round 56, 2026-10-24)

`nj_region_draw` has copied an opaque ARGB8888 source onto an RGB565 surface in
groups of eight since round 51: read eight pixels, prove all eight opaque, and
convert them with no alpha test.  The conversion itself still ran one pixel at
a time - three shifts and three masks per pixel to build `r5 | g6 << 5 | b5 <<
11`.  Two of those pixels fit in one 64-bit word, so each channel extraction
can serve both of them: three shifts, three masks and an `or` for the pair,
then one shift and one `or` to fold the halves back into the four bytes that go
to the destination.  The bytes written are the same ones: the pair is built
`(nj_u8)lo | ((nj_u8)hi << 32)` and the folded word stores `lo | hi << 16`,
which is exactly two little-endian rgb565 pixels.  A host whose byte order is
not little-endian keeps the per-pixel loop that was there before.

Numbers are from the scratch blit bench (`nj_gfx_draw_image` of a 240x320
ARGB8888 image onto a 240x320 RGB565 surface, six interleaved rounds, min of
each arm, `taskset -c 5`):

| Mode | Before | After | Delta |
|---|---|---|---|
| opaque paint (`run`) | 37.98 us | **35.60 us** | -6.25% |
| `drawImage` with `NJ_ROP_SET` | 37.81 us | **36.16 us** | -4.36% |
| opaque paint, second size | 37.64 us | **35.95 us** | -4.47% |
| textured/alpha ARGB modes (untouched) | - | - | within +-2% / noise |

Every framebuffer hash is identical to the parent binary in all eight bench
modes, so this is a faster way to compute the same bytes, not a different
picture.  On the heaviest M3G title the effect is below what this machine can
resolve: `毁灭战士3_10598`, 2,400 paced frames, six interleaved rounds of user
CPU - parent min 2.355 s / med 2.417 s, this binary min 2.332 s / med 2.442 s
(min -1.0%, median +1.0%, noise band about +-2%); its dumped frames are
byte-identical to the parent's.  The title spends most of its CPU in the M3G
rasteriser, and the conversion is a small part of the blits it does.

### Cost

`nj_gfx.c.o` `.text` 35,960 -> **35,688** B (-272 B: the pair loop replaces more
code than it adds); the `nj2me` executable `.text` 2,910,230 -> **2,909,942** B
(-288 B); `.data` 162,736 B and `.bss` 6,688 B unchanged.  RAM is unchanged: no
field, static, heap allocation or buffer is added.
