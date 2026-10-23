# Baseline JIT performance: measurements and reproduction

This work responds to a second board report, against `fe974fc` — the commit
that carried the previous probe/dispatch optimisation set. It landed in two
steps: the probe map and field/array coverage first, then branch and loop
compilation with the budgeted block ABI described below. The reporting device
is an anonymous 32-bit embedded board with a 240×320 display. The harness runs
five fixed kernels for 50,000 iterations and averages two fresh-VM rounds with
superinstructions and threaded dispatch disabled in both modes, and JIT
statistics enabled.

The board measured (interpreter / probe-only / execute, milliseconds, and the
execute change against the interpreter):

| Kernel | Interpreter | Probe-only | JIT | JIT vs interpreter |
|---|---:|---:|---:|---:|
| Locals | 143.0 | 509.0 | 121.0 | −15.4% |
| Constants | 110.5 | 307.0 | 116.5 | +5.4% |
| Fields | 201.0 | 487.5 | 416.0 | +107.0% |
| Arrays | 205.5 | 588.0 | 409.5 | +99.3% |
| Branches | 204.5 | 644.5 | 256.0 | +25.2% |

On the fixed CPU workload the end-to-end duration rose from 9.8563635 s to
15.119776 s (+53.4%). The CPU fixture reported `compiled=11 executed=527957
bytecodes=2151590 code_bytes=442`: 4.08 Java bytecodes per compiled block and
only 18.9% of executed bytecodes running natively. The startup scenario reported
`compiled=4203 executed=469340 bytecodes=3055767 code_evictions=4140
site_evictions=1069968 peak_code=3376 metadata=5000 recompiled=18 short=40543`.

These are user-supplied board results, not a measurement made by this revision.
They identified two costs, not one: the probe path itself is expensive
(probe-only is two to four times slower than the interpreter), and coverage is
too narrow — fields and arrays always end the block, so most blocks are a few
bytecodes long and the probe is paid on every interpreted bytecode anyway.

## What the report asked for, and what changed

The report's P0 was to measure the probe/probe-exit cost and make non-benefiting
methods leave cheaply. This revision replaces the two-way 128-entry site table
with one **per-method probe map**: a 16-bit state cell per bytecode offset in a
fixed arena, plus a small method table.

- `0x0000..0x3FFF` is heat, `0x4000` refused, `0x6000|insns` analysed and waiting
  to compile, `0x8000|slot` compiled. The current method is cached, so the
  steady-state probe is one pointer comparison and one 16-bit array load per
  candidate bytecode: no hash, no way scan, no per-site record.
- Methods whose body cannot fit the arena are never mapped and never JIT'd;
  refused sites stay refused until their map is recycled; code eviction clears
  the cell so the site starts cold again. When the arena is full the least
  recently used method map is dropped and the arena compacted. Map recycling
  drops metadata only: compiled code stays in the code cache, and a later
  recompile of the same `(method, pc)` reuses the surviving entry instead of
  emitting a duplicate.
- Default metadata is now about 8 KiB per VM (a 4 KiB arena, 32 method slots and
  64 code entries) instead of about 5 KiB. The report's startup case recorded
  `metadata=5000` and 1,069,968 site-history evictions; the new scheme bounds map
  metadata and recycles whole methods, at the price of a larger fixed block.
  Every limit is configurable; see the table in [JIT.md](JIT.md).

The report's P1 was coverage first. This revision compiles natively:

- instance `getfield`/`putfield` for every primitive and reference type, resolved
  through the read-only constant-pool cache, with pointer-sized references,
  correct sign/zero extension and the interpreter's wide-value padding rules;
- `iaload`/`iastore` for integer arrays (validated null, kind, length and
  `aflags == NJ_ARRAY_T_INT`) and `arraylength`;
- integer `idiv`/`irem` inline, with a zero divisor as one guarded instruction;
- conditional branches (`if<cond>`, `if_icmp<cond>`, `if_acmpeq/ne`,
  `ifnull`/`ifnonnull`) and `goto`. A forward target inside the window becomes an
  internal label (paths may join only at equal operand depth, otherwise the block
  is declined); a forward target beyond the window ends the block through an
  exit that records the target PC and stack depth; a backward branch compiles
  only when it targets the block entry with an empty stack, i.e. a loop. Loop
  blocks keep a bytecode counter and check the remaining scheduling budget at
  the entry label before every iteration, so a back edge always returns through
  a budget exit and never starves other threads.

This needs an exact way to hand a bytecode back to the interpreter, so the block
ABI is now a **deoptimisation contract**: a block returns
`(completed << 4) | exit_index` — the number of completed Java bytecodes and
which exit to take (`exits[]` carries the target PC and stack delta; exit 0 is
the straight-line window end, further exits are leaving branches and the loop
budget exit) — or a fault word carrying the fault index and completion count.
Each fault records the bytecode offset and the stack delta from the incoming
`sp`, so the interpreter re-executes the faulting bytecode at the original PC
with the original frame, raising the usual `NullPointerException`,
`ArrayIndexOutOfBoundsException` or `ArithmeticException` (or field
resolution/class initialisation) itself. Guards branch off before the faulting
instruction writes any slot, so no partially visible state exists. A block
admits at most `NJ_JIT_MAX_FAULTS` guarded instructions and `NJ_JIT_MAX_EXITS`
exits; a further one ends the block in front of it. A fault before any bytecode
completes pins the site refused, so a failing site cannot thrash. Analyse and
compile share one decode loop, so the accepted prefix, fault list, labels and
exits are identical.

Retained from the previous revision: metadata-only probe, configurable minimum
block length, numeric `ldc`/`ldc_w`, the two-way code cache, RV32's optional T0/T1
write-through register cache, batched statistics and probe-only mode.

Still interpreted: static fields, non-int array kinds, long arithmetic,
conversions other than `i2b`/`i2c`/`i2s`, calls, allocation and monitors. Those
are the natural next coverage steps. The separate 3D-backend profiling the
report lists as P2 was not done either.

## Fixed-workload harness

[tests/jitperf/JitPerf.java](../tests/jitperf/JitPerf.java) now defines six
kernels: local integer arithmetic, large-constant multiply/add, fields, arrays,
a mixed field+int-array loop, and branches. The mixed kernel alternates a boxed
counter field with a 32-entry int array so one block spans both new fast paths.

[tests/test_jit_perf.c](../tests/test_jit_perf.c) runs each kernel in a fresh VM
in three modes: interpreter (JIT disabled), probe-only, and execute. A
zero-iteration interpreted call resolves classes and initialises fixture state
outside the timed region, so every mode starts from an empty JIT. An independent
unsigned C oracle checks Java wraparound results. The runner checks code-memory
allocation/free pairing and that probe-only performs no compilation, allocation
or commit.

Probe-only visits every interpreted bytecode, including sites skipped by an
executed native block; its timing measures the installed probe path, and
subtracting it from execute time is not an exact attribution. With the usual
interpreter fusions enabled, installing the JIT changes counting policy before a
site is hot; disable superinstructions for instruction-count comparisons.

The output line for execute mode includes `native_blocks`, `native_bytecodes`,
`compiled_blocks`, `declined_blocks`, evictions, `faults`, `metadata_bytes`,
`code_bytes`, and a `FALLBACK` line whose `fallback_ratio` is the fraction of
executed Java bytecodes that still ran in the interpreter. Cold-start and
steady-state numbers are obtained by choosing `--iterations`.

Generate the ignored fixture with a JDK that can emit Java 8 class files, then
build and run as before:

```sh
tools/check_jit_perf.sh --build-fixture-only
tools/check_jit_perf.sh --build-dir build-rv32-perf --no-build \
  --jar tests/fixtures/jitperf.jar --runner qemu-riscv32 \
  --iterations 20000 --require-execution
```

## QEMU observations, 2026-10-23

These are emulator wall times, not CPU cycles or board measurements. RV32 and
ARM64 Linux musl static binaries were built in Release mode with Zig/Clang
22.1.8 for `riscv32-linux-musl` / `aarch64-linux-musl` and run under
qemu-riscv32 and qemu-aarch64 7.2.0. Defaults held constant: 64 code entries in
two ways, 32 method maps, a 4096-byte arena, at most 12 faults and 4 exits, heat
threshold 8, minimum block 4, at most 64 bytecodes, default quantum, switch
dispatch, **superinstructions off in both modes** (matching the board report),
RV32 register cache on, statistics on, detailed statistics off. Each row is one
fresh VM; values are medians of three runs.

**Block-length and fault-slot defaults matter more than any single opcode.**
The previous defaults (32 bytecodes, 4 fault slots) split the benchmark loops:
the branch kernel's 38-bytecode loop body and the field/array kernel's body
(two `getfield`s, one `putfield`, two `iaload`s, one `iastore`, nine guarded
operations in all) could not fit in one block, so every iteration left native
code and re-entered the probe. Raising the defaults to 64 bytecodes and 12
fault slots makes both loop bodies compile as a single loop block: execute-mode
`fallback_ratio` for those kernels drops from 0.1450 / 0.0743 to 0.0022 / 0.0034,
and the RV32 execute/interpreter ratios fall from 0.308 / 0.271 to 0.038 / 0.034.
The cost is metadata: every cache entry carries `NJ_JIT_MAX_FAULTS` four-byte
fault records, so `metadata_bytes` grows from 9,432 to 11,480 on RV32 and from
10,600 to 12,648 on LP64. Both macros remain configurable for RAM-constrained
builds; lowering them is a fallback-ratio trade, not a correctness trade.

Steady state, 20,000 iterations:

| Kernel | RV32 interpreter / probe / execute ms | RV32 exec/interp | RV32 probe/interp | ARM64 interpreter / probe / execute ms | ARM64 exec/interp | ARM64 probe/interp |
|---|---:|---:|---:|---:|---:|---:|
| Locals | 35.79 / 18.37 / 1.53 | 0.043 | 0.513 | 28.28 / 14.33 / 1.39 | 0.049 | 0.507 |
| Constants | 5.84 / 11.20 / 0.26 | 0.044 | 1.919 | 4.25 / 8.29 / 0.24 | 0.056 | 1.949 |
| Fields | 12.23 / 21.05 / 0.59 | 0.048 | 1.721 | 9.41 / 19.05 / 0.65 | 0.069 | 2.025 |
| Arrays | 12.22 / 21.57 / 0.80 | 0.065 | 1.766 | 11.36 / 19.49 / 1.06 | 0.093 | 1.716 |
| Mixed | 26.54 / 44.72 / 0.90 | 0.034 | 1.685 | 19.13 / 33.77 / 1.14 | 0.060 | 1.765 |
| Branches | 12.01 / 21.94 / 0.46 | 0.038 | 1.826 | 8.91 / 17.18 / 0.57 | 0.064 | 1.928 |

The first measured row in each process (locals) is distorted by page-in and host
noise, which is why its probe/interpreter column shows less than one even though
probe-only mode does strictly more work; treat the ratio columns as directional.
Do not subtract probe-only time from execute time for attribution either:
probe-only also visits bytecodes skipped by a native block.

Execute-mode compiled footprint, RV32 / ARM64: locals 1 block and 238 / 364 code
bytes; constants 1 and 150 / 220; fields 1 and 324 / 448; arrays 2 and 570 / 740;
mixed 1 and 910 / 1200; branches 1 and 414 / 632. Every code allocation is freed,
and `metadata_bytes` is 11,480 on RV32 and 12,648 on LP64 as noted above.

Cold start, 500 iterations (interpreter / execute ms, execute/interpreter,
compiled code bytes):

| Kernel | RV32 int / exec | RV32 ratio | RV32 bytes | ARM64 int / exec | ARM64 ratio | ARM64 bytes |
|---|---:|---:|---:|---:|---:|---:|
| Locals | 1.05 / 2.86 | 2.715 | 238 | 0.83 / 2.83 | 3.410 | 364 |
| Constants | 0.57 / 0.30 | 0.530 | 150 | 0.45 / 0.25 | 0.548 | 220 |
| Fields | 0.61 / 0.62 | 1.005 | 324 | 0.44 / 0.73 | 1.652 | 448 |
| Arrays | 0.68 / 0.74 | 1.081 | 570 | 0.48 / 0.99 | 2.056 | 740 |
| Mixed | 1.32 / 0.65 | 0.491 | 910 | 0.95 / 0.71 | 0.746 | 1200 |
| Branches | 0.64 / 0.22 | 0.349 | 414 | 0.50 / 0.28 | 0.557 | 632 |

The first few hundred iterations pay for analysis and emission (locals — a
one-block kernel whose whole hot path is compiled immediately — and, on ARM64,
fields and arrays are still slower there), while the loop kernels are already
ahead. A JIT-enabled launch therefore only pays off if the hot loops run long
enough — the board's startup scenario is exactly that case, which is why it must
be re-measured rather than inferred.

For comparison with the previous revision, its QEMU table (500,000 iterations,
medians of three runs, five kernels) gave RV32 execute/interpreter 0.252
(locals), 0.682 (constants), 1.305 (fields), 1.262 (arrays), 0.777 (branches).
The iteration count, block-length defaults and aggregation differ, so treat the
change as directional: fields, arrays and branches went from clearly slower than
the interpreter to clearly faster, which is attributable to the new coverage,
loop compilation, and the larger block window; locals and constants improved as
well.

## Correctness coverage

Standalone encoder matrices execute real generated code through the port's
executable-memory hooks in QEMU:

- RV32: 14 build configurations, 38,362 native block invocations and 22,428,156
  assertions (22,078,150 on the host build).
- ARM64: 12 configurations, 50,534 invocations and 24,965,155 assertions
  (24,357,840 on the host build). These totals are insensitive to the
  block-length and fault-slot defaults: the encoder matrices generate their own
  synthetic methods and the runtime tests adapt their fixtures to the macros.

The same matrices were repeated under ASan/UBSan with identical assertion
counts. Each configuration checks complete frame slots byte for byte against the
interpreter, analysis/compile metadata prefix equality, capacity rollback,
a guard page after the analysed prefix (a block must never read or execute past
it), integer and IEEE edge values, fixed round-to-nearest-even with garbage host
rounding modes (ARM64), pointer-sized references with high-half preservation,
null/wrong-kind/out-of-bounds/zero-divisor fault points, exact fault PC and SP,
refused-site pinning, eviction/refusal interaction, and per-call preservation of
callee-saved registers, SP and FPCR.

On RV32 and ARM64 QEMU, the full VM suite passes: `test_jit` (probe/execute
modes, `SAVE()`, quanta, guards, cache, ownership, failure paths), `test_jit_rv32`
(1,755,790 assertions, 2,981 compiled blocks, 2,987 native calls on RV32;
encoding-only 934,949 on ARM64), `test_jit_arm64` (2,315,309 assertions, 4,402
compiled, 4,456 native on ARM64; encoding-only 1,524,664 on RV32),
`test_jit_perf` (six kernels × three modes, checksums match the oracle),
`test_vm` (`hello.jar`), `test_engine` (`bench.jar`, 54 checks), `test_lifecycle`,
`test_blocked_init` (12 checks), `test_thread_lifecycle` and `test_natives`. The runtime
tests force minimum block length 1 so guarded operations really execute natively,
and they require at least one real zero-divisor fault, which exercises the exact
deoptimisation path end to end. The runtime test also drives one deoptimisation
through the public API: a two-field block returns `completed=2`, restores the
PC to the second `getfield` and the SP to entry+2, leaves the full frame
unchanged and reports exactly one fault in the statistics. Code-memory
callbacks are checked to receive the port's embedder pointer (`port->ud`), not
the log-only `port->user`.

Host CTest passed 88 of 97 tests in this minimal configure. The nine failures
are environment fixtures, not JIT behaviour: missing generated expected files
(`test_consts`), SDL2 assets that this configure does not build (`test_font_png`,
`test_jpeg`, `test_lcdui`, `test_input_effect`, `test_pointer`, `test_key_codes`),
and two corpus wording scripts whose tooling is not built here. The same
categories fail on the pre-change baseline; no JIT test fails.

The structural sample is unchanged in spirit: an 18-bytecode LCG block on RV32 is
154 bytes with 1 load and 18 stores with compression and register caching on,
versus 222 bytes and 18 loads with the cache off; ARM64 emits 228 bytes, 18 loads
and 18 stores (the previous 224 plus the BTI entry). This is a code-shape metric,
not a board speedup.

## Known limits and risks

- **Trusted references.** The native array fast paths validate null, array kind,
  length and element type, but not that the reference points into the VM heap;
  the interpreter's `nj_heap_owns_object_fast` check is not replicated. For
  instance fields the interpreter only null-checks, so the exposure is the same
  trust boundary. References produced by the VM are always heap objects; an
  embedder planting a raw value in a frame slot could be dereferenced natively.
- **RV32 branch reach.** Fault stubs sit at the end of the block and are reached
  with encoded B-type branches, whose range is ±4 KiB. The default
  `NJ_JIT_MAX_CODE_BYTES=2048` is well inside; configuring 8192 or 16384 without
  branch relaxation can wrap an offset. Keep the limit at or below 4096 unless
  relaxation is added.
- **Map arena size.** A method whose body needs more than
  `NJ_JIT_METHOD_ARENA_BYTES/2` bytecode cells is never JIT'd (2048 bytecodes at
  defaults). That is a deliberate cheap exit; raise the arena for larger hot
  methods.
- **Memory.** Default metadata is 11,480 bytes per VM on RV32 and 12,648 on
  LP64, up from about 5,000 in the first revision. Most of the growth is the
  larger `faults[]` array per cache entry. On RAM-constrained parts reduce
  `NJ_JIT_MAX_FAULTS`, `NJ_JIT_CACHE_ENTRIES`, `NJ_JIT_METHOD_ENTRIES` and
  `NJ_JIT_METHOD_ARENA_BYTES`; `metadata_bytes` reports the resulting size, and
  the block-length/fault defaults section above gives the fallback-ratio trade.
- **Cold start.** Emission and analysis dominate the first few hundred
  iterations; short-lived launches may not amortise them.
- **Loop budget.** A compiled loop counts every completed bytecode and checks
  `budget - completed` at its entry label, so a back edge always leaves through
  a budget exit and a thread cannot starve. The check costs two instructions per
  iteration; a loop whose trip count is smaller than the block is still compiled
  when requested, which is why loop blocks are exempt from the minimum block
  length.
- **No board run.** This revision was measured only under QEMU. ARM64 emits BTI
  as an entry hint, but a kernel enforcing `PROT_BTI` has not been exercised.

## Board follow-up

Re-run the same 50,000-iteration harness and the startup scenario with
`--stats` on, keeping the reported clock/PSRAM placement, screen, volume, input,
fresh RMS and fixed Java work. Fields and arrays now compile, and conditional
branches and empty-stack loops compile too, so fields, arrays, branches and the
scanline/geometry loops that dominate a J2ME game should all move. Record cold
and steady state separately, together with `code_bytes`, `metadata_bytes`,
`method_map_evictions`, `recompiled_blocks`, `faults`, `fallback_ratio` and
`peak_code_bytes`.

The startup scenario is the one that must be re-measured: the previous revision
reported 1,069,968 site-history evictions and 5,000 bytes of metadata there, and
this revision replaces the site table with per-method maps (11480 bytes on RV32,
12648 on LP64 at defaults) so those evictions should be gone and the per-bytecode
probe should be a pointer compare plus one cell load. If the board shows the
stepping cost still dominating, tune `NJ_JIT_METHOD_ARENA_BYTES` and
`NJ_JIT_METHOD_ENTRIES` against `method_map_evictions` rather than raising the
minimum block length globally.

Compare `NJ_JIT_METHOD_ENTRIES` 16/32, `NJ_JIT_METHOD_ARENA_BYTES` 2048/4096,
`NJ_JIT_MIN_BLOCK_INSNS` 2/4/8 and `NJ_JIT_MAX_EXITS` 2/4 independently. Verify
that a compiled loop still yields to other threads within the scheduling
quantum and that a fault inside a loop rewinds to the exact PC and stack depth
on real hardware, then decide whether branch/loop coverage should expand to
`tableswitch`/`lookupswitch` and static fields. The 3D-backend profiling the
report lists as P2 remains separate work, as does the x86_64 backend.

Keep checksum and final-image CRC comparisons: emulator wall time is not a board
measurement, display submission time must be recorded separately, scheduler FPS
is not a speedup metric, and an RV32 cycle counter may wrap.
