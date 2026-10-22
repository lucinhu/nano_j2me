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
