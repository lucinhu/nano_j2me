# Baseline JIT performance: measurements and reproduction

This revision responds to a second board report, against `fe974fc` — the commit
that carried the previous probe/dispatch optimisation set. The reporting device
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
- integer `idiv`/`irem` inline, with a zero divisor as one guarded instruction.

This needs an exact way to hand a bytecode back to the interpreter, so the block
ABI is now a **deoptimisation contract**: a block returns the number of completed
Java bytecodes, or a fault word carrying the fault index. Each fault records the
bytecode offset and the stack delta from the incoming `sp`, so the interpreter
re-executes the faulting bytecode at the original PC with the original frame,
raising the usual `NullPointerException`, `ArrayIndexOutOfBoundsException` or
`ArithmeticException` (or field resolution/class initialisation) itself. Guards
branch off before the faulting instruction writes any slot, so no partially
visible state exists. A block admits at most `NJ_JIT_MAX_FAULTS` guarded
instructions; a further fault point ends the block in front of it. A fault before
any bytecode completes pins the site refused, so a failing site cannot thrash.
Analyse and compile share one decode loop, so the accepted prefix and fault list
are identical.

Retained from the previous revision: metadata-only probe, configurable minimum
block length, numeric `ldc`/`ldc_w`, the two-way code cache, RV32's optional T0/T1
write-through register cache, batched statistics and probe-only mode.

Still interpreted: conditional branches and backedges, static fields, non-int
array kinds, long arithmetic, conversions other than `i2b`/`i2c`/`i2s`, calls,
allocation and monitors. Branch compilation is the natural next step, but it
needs quantum-safe backedges and exact branch-fault PCs; it is not claimed here.
The separate 3D-backend profiling the report lists as P2 was not done either.

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
22.1.8, built for `riscv32-linux-musl` / `aarch64-linux-musl`, and run under
qemu-riscv32 and qemu-aarch64 7.2.0. Defaults held constant: 64 code entries in
two ways, 32 method maps, a 4096-byte arena, at most 4 faults, heat threshold 8,
minimum block 4, at most 32 bytecodes, default quantum, switch dispatch,
superinstructions off, RV32 register cache on, statistics on, detailed
statistics off. Each row is one fresh VM; the numbers are single runs.

Steady state, 20,000 iterations:

| Kernel | RV32 interpreter / probe / execute ms | RV32 exec/interp | ARM64 interpreter / probe / execute ms | ARM64 exec/interp |
|---|---:|---:|---:|---:|
| Locals | 29.064 / 45.475 / 8.782 | 0.302 | 24.637 / 34.594 / 8.807 | 0.357 |
| Constants | 10.680 / 17.074 / 6.995 | 0.655 | 7.711 / 17.097 / 6.918 | 0.897 |
| Fields | 21.922 / 34.920 / 7.773 | 0.355 | 17.261 / 30.625 / 8.275 | 0.479 |
| Arrays | 20.835 / 37.585 / 8.550 | 0.410 | 16.693 / 33.886 / 9.954 | 0.596 |
| Mixed | 48.626 / 76.831 / 16.543 | 0.340 | 38.956 / 68.402 / 18.071 | 0.464 |
| Branches | 20.574 / 34.792 / 14.857 | 0.722 | 16.127 / 32.279 / 14.731 | 0.913 |

Fault follow-ups and fallback ratios are small for the compiled kernels: on RV32
the execute-mode `fallback_ratio` was 0.211 (locals), 0.364 (constants), 0.211
(fields), 0.175 (arrays), 0.147 (mixed) and 0.360 (branches); compiled blocks
were 12/4/12/17/35/11 and requested code bytes 946/224/1554/3970/7572/564.
`metadata_bytes` was 8408 on RV32 (9576 on LP64, where method pointers are 8
bytes) and every code allocation was freed.
Probe-only stays above the interpreter by design (1.56–1.80× on RV32,
1.40–2.22× on ARM64): it is a diagnostic mode, not a performance mode.

Cold start, RV32, 500 iterations:

| Kernel | Interpreter ms | Execute ms | Execute / interpreter | Compiled blocks / code bytes |
|---|---:|---:|---:|---:|
| Locals | 1.157 | 2.413 | 2.086 | 1 / 118 |
| Constants | 0.555 | 0.505 | 0.909 | 1 / 66 |
| Fields | 0.972 | 1.090 | 1.121 | 1 / 200 |
| Arrays | 1.152 | 0.470 | 0.408 | 2 / 472 |
| Mixed | 1.339 | 0.411 | 0.307 | 4 / 936 |
| Branches | 0.427 | 0.280 | 0.656 | 4 / 224 |

The first few hundred iterations pay for compilation and emission (locals and
fields are still slower than the interpreter there), while every kernel is
clearly faster once warm. A JIT-enabled launch therefore only pays off if the
hot loops run long enough — the board's startup scenario is exactly that case,
which is why it must be re-measured rather than inferred.

For comparison with the previous revision, its QEMU table (500,000 iterations,
medians of three runs, five kernels) gave RV32 execute/interpreter 0.252
(locals), 0.682 (constants), 1.305 (fields), 1.262 (arrays), 0.777 (branches).
The iteration count and run aggregation differ, so treat the change as
directional: fields and arrays went from clearly slower than the interpreter to
clearly faster, which is attributable to the new coverage; locals, constants and
branches did not regress materially.

## Correctness coverage

Standalone encoder matrices execute real generated code through the port's
executable-memory hooks in QEMU:

- RV32: 14 build configurations, 45,379 native block invocations and 22,476,585
  assertions (22,245,302 on the host build).
- ARM64: 12 configurations, 58,614 invocations and 25,074,430 assertions
  (24,610,643 on the host build).

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
(1,756,246 assertions, 2,940 compiled blocks, 3,542 native calls on RV32;
encoding-only 950,186 on ARM64), `test_jit_arm64` (2,321,161 assertions, 4,361
compiled, 5,211 native on ARM64; encoding-only 1,555,523 on RV32),
`test_jit_perf` (six kernels × three modes, checksums match the oracle),
`test_vm` (`hello.jar`), `test_engine` (`bench.jar`, 54 checks), `test_lifecycle`,
`test_blocked_init`, `test_thread_lifecycle` and `test_natives`. The runtime
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
152 bytes with 1 load and 18 stores with compression and register caching on,
versus 220 bytes and 18 loads with the cache off; ARM64 emits 228 bytes, 18 loads
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
- **Memory.** Default metadata is 8408 bytes per VM on RV32 (9576 on LP64), up
from about 5000. On
  RAM-constrained parts reduce `NJ_JIT_CACHE_ENTRIES`,
  `NJ_JIT_METHOD_ENTRIES` and `NJ_JIT_METHOD_ARENA_BYTES`; `metadata_bytes`
  reports the resulting size.
- **Cold start.** Emission and analysis dominate the first few hundred
  iterations; short-lived launches may not amortise them.
- **No board run.** This revision was measured only under QEMU. ARM64 emits BTI
  as an entry hint, but a kernel enforcing `PROT_BTI` has not been exercised.

## Board follow-up

Re-run the same 50,000-iteration harness and the startup scenario with
`--stats` on, keeping the reported clock/PSRAM placement, screen, volume, input,
fresh RMS and fixed Java work. Fields and arrays now compile, so they should be
the first place to look for improvement; branches are still interpreted and
should stay close to their current numbers, while the 1,069,968 startup
site-history evictions should disappear from the counters. Record cold and
steady separately, together with `code_bytes`, `metadata_bytes`,
`method_map_evictions`, `recompiled_blocks`, `faults` and `fallback_ratio`.

Compare `NJ_JIT_METHOD_ENTRIES` 16/32, `NJ_JIT_METHOD_ARENA_BYTES` 2048/4096 and
`NJ_JIT_MIN_BLOCK_INSNS` 2/4/8 independently, and only then decide whether to
raise the minimum block length. Conditional branches and backedges are the next
coverage step; they require exact branch-fault PCs and quantum-debited backedges,
and 3D-backend profiling remains separate work.

Keep checksum and final-image CRC comparisons: emulator wall time is not a board
measurement, display submission time must be recorded separately, scheduler FPS
is not a speedup metric, and an RV32 cycle counter may wrap.
