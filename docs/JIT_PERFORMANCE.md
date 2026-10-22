# Baseline JIT performance: measurements and reproduction

This stage responds to a board report against `fed8d0c`: 320 MHz CPU,
250 MHz PSRAM, 240×320 display, volume 40, fresh RMS, identical JAR and fixed
120×4000 work, superinstructions disabled in both modes. The reported end-to-end
average increased from 9.7966 s to 14.7888 s with JIT enabled (+51.0%), with equal
checksums and final image CRC. Generated blocks averaged only 1.6476 bytecodes per
call. Display submission time also increased, so the full difference cannot be
attributed to one JIT function. These are user-supplied board results, not a new
measurement of this revision. Scheduler-frame FPS is not a speedup metric.

The changes below reduce identifiable costs and add measurements. They do not
establish a board speedup. Fields, arrays and Java control flow still execute in
the interpreter; workloads dominated by those boundaries can still slow down.

## What changed

- A conservative inline opcode filter precedes a metadata-only probe. Cold,
  unsupported, short and guarded-out hits return without publishing the frame.
  Compilation, executable-memory callbacks and native execution still follow
  an exact `SAVE()` of PC, SP and instruction counts.
- `NJ_JIT_MIN_BLOCK_INSNS` defaults to 4. Decode-only analysis rejects short
  prefixes before emission/allocation. Emission rechecks the threshold if the
  code-buffer capacity shortened the block. Values 1 and 8 are comparison points,
  not established board optima.
- Both backends load Integer/Float `ldc` and `ldc_w` from checked, immutable raw
  constant-pool bytes. They do not resolve or allocate String/Class constants.
- RV32's optional `NJ_JIT_RV32_REG_CACHE=1` reuses temporary register values and
  propagates integer constants inside a leaf block. Stores remain write-through,
  preserving all slot bytes. Partial overlap, float/wide operations and failed
  emission invalidate or roll back the corresponding register state.
- The code cache defaults to 64 entries in two-way sets. A separate 128-entry,
  two-way table holds heat and refusals. Configurable 1/2/4 code-cache ways allow
  comparison without automatically increasing executable memory.
- Hot execution statistics use 32-bit batches folded at engine returns.
  `NJ_JIT_STATS=0` removes hot updates; slow compilation/memory accounting stays.
  `NJ_JIT_DETAILED_STATS=1` adds candidate/hit/skip counts and length histograms.
  See [JIT.md](JIT.md) and [nj_jit.h](../include/nj2me/nj_jit.h) for exact semantics.

A structural encoder comparison repeats `local0 = local0 * 1664525 + 1013904223`
three times in one 18-bytecode block:

| RV32 configuration | Code bytes | Integer loads | Integer stores |
|---|---:|---:|---:|
| Compressed instructions enabled, register cache off | 218 | 18 | 18 |
| Compressed instructions enabled, register cache on | 150 | 1 | 18 |
| Compressed instructions disabled, register cache off | 220 | 18 | 18 |
| Compressed instructions disabled, register cache on | 152 | 1 | 18 |

The compressed example has 31.2% fewer code bytes and 94.4% fewer loads. All stores
remain. These instruction counts do not imply the same percentage speedup.
ARM64 received numeric constants and analysis, but retains its per-operation
loads; the corresponding example is 224 bytes, 18 loads and 18 stores.

## Fixed-workload harness

[tests/jitperf/JitPerf.java](../tests/jitperf/JitPerf.java) defines five kernels:
local integer arithmetic, large-constant multiply/add, fields, arrays and branches.
[tests/test_jit_perf.c](../tests/test_jit_perf.c) runs each in a fresh VM in three
modes:

1. **interpreter**: disable the automatically installed JIT.
2. **probe-only**: install it and select `NJ_JIT_MODE_PROBE_ONLY`.
3. **execute**: install it and select `NJ_JIT_MODE_EXECUTE`.

An interpreted zero-iteration call resolves classes outside the timed region.
The measured call starts with an empty JIT cache, so its time includes heat,
analysis, compilation and execution. It excludes VM creation/JAR loading and
teardown. There is no display, audio or RMS in these kernels. An independent
unsigned C oracle checks Java wraparound results. The runner checks code-memory
allocation/free pairing and zero compilation/allocation/commit in probe-only.

Probe-only visits every interpreted bytecode, including sites skipped by an
executed native block. Its timing measures the overhead of that installed mode;
subtracting it from execute time is not an exact attribution of compile/probe
costs. Disable superinstructions for instruction-count comparisons. With the
usual interpreter fusions enabled, installing JIT changes counting policy even
before a site becomes hot.

The output includes checksum, wall time, Java/native instruction counts,
compilation/refusal/eviction/recompilation counts, length bins, metadata and code
bytes, and code callback counts. Counters disabled by build flags are explicitly
reported as `unavailable`. With statistics off, `--require-execution` checks
successful compilation and commit; per-call native execution counts are
unavailable. With statistics on it also requires observed native execution.

Generate the ignored fixture with a JDK supporting Java 8 output:

```sh
tools/check_jit_perf.sh --build-fixture-only
```

Configure for the deployment CPU using its normal toolchain. This example fixes
superinstructions and statistics for comparison; add the appropriate ISA macros
and compiler `-march`/`-mabi` flags for the board:

```sh
cmake -S . -B build-rv32-perf \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/rv32-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DNJ_ENABLE_JIT=1 \
  -DNJ_ENABLE_SUPERINSTR=0 -DNJ_JIT_STATS=1 -DNJ_JIT_DETAILED_STATS=0 \
  -DNJ_JIT_MIN_BLOCK_INSNS=4 -DNJ_JIT_CACHE_WAYS=2 \
  -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_LIBRETRO=OFF \
  -DNJ_BUILD_EMBEDDED_PORTS=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
cmake --build build-rv32-perf --target test_jit_perf
tools/check_jit_perf.sh --build-dir build-rv32-perf --no-build \
  --jar tests/fixtures/jitperf.jar --runner qemu-riscv32 \
  --iterations 500000 --require-execution
```

On a supported Linux CPU omit `--runner`. Use an AArch64 toolchain and
`qemu-aarch64` for ARM64. A bare-metal port can reuse the Java kernels and public
API instrumentation in its own runner. The supplied runner uses POSIX host
facilities. CTest registers it when the fixture exists at configure time;
unsupported hosts return skip code 77. `--require-execution` turns a missing JIT
into failure. Explicit `--jar` avoids requiring a JDK on the execution machine.

## QEMU observations, 2026-10-22

These are emulator wall times, not CPU cycles or board measurements. RV32 Linux
musl static binaries were built in Release mode with Zig/Clang 22.1.8 and run
under qemu-riscv32 7.2.0. All settings were held constant except the minimum block
length: 64 code entries, two ways, 128 history entries, heat threshold 8,
32-bytecode maximum, default 20000 quantum, switch dispatch, superinstructions
off, register cache on, statistics on and detailed statistics off. Each case
used 500000 iterations and three sequential fresh-VM runs; times below are the
medians. They compare this implementation's modes, not old-versus-new binaries.

Default minimum 4:

| Kernel | Interpreter ms | Probe-only ms | Execute ms | Execute / interpreter |
|---|---:|---:|---:|---:|
| Locals | 341.633 | 461.817 | 86.187 | 0.252 |
| Constants | 124.701 | 217.340 | 85.063 | 0.682 |
| Fields | 250.271 | 376.249 | 326.645 | 1.305 |
| Arrays | 231.520 | 416.206 | 292.160 | 1.262 |
| Branches | 233.107 | 433.612 | 181.142 | 0.777 |

Threshold comparison (execute/interpreter, lower is faster in this environment):

| Kernel | Minimum 1 | Minimum 4 | Minimum 8 |
|---|---:|---:|---:|
| Locals | 0.212 | 0.252 | 0.255 |
| Constants | 0.579 | 0.682 | 1.819 |
| Fields | 1.102 | 1.305 | 1.540 |
| Arrays | 0.878 | 1.262 | 1.237 |
| Branches | 0.651 | 0.777 | 1.108 |

A higher threshold is not automatically better: minimum 8 rejects the useful
seven-bytecode constant loop segment. Minimum 4 still slows fields and arrays in
this emulator. This is why the threshold remains configurable and the default
is not presented as an optimal board setting.

Separate diagnostic runs used 200000 iterations and detailed statistics. All
five kernels had identical checksums and Java instruction counts in all three
modes on RV32 and ARM64, and on RV32 with statistics disabled. The default RV32
metadata allocation was 5000 bytes (4936 with statistics off). In kernel order,
execute mode compiled 12/4/1/6/12 blocks and requested peak live code bytes
922/216/36/304/608. Allocation, commit and final free counts matched. OS page
rounding is additional. These small cases had no code evictions, so they do not
establish a game-cache improvement; synthetic collision tests cover replacement
correctness separately.

## Correctness coverage and board follow-up

The extended encoder matrices executed 27547 RV32 and 32209 ARM64 generated
calls through executable memory, across nine and seven configurations. Complete
slot bytes, raw constant-pool edge cases, analysis metadata, rollback, aliasing,
integer extremes and FP/ABI behavior matched independent references. Host
ASan/UBSan checks passed with RV32 register caching both on and off and ARM64 FP
on. Full VM regressions passed on RV32, ARM64 and the host; additional RV32 runs
covered minimum 1/direct mapping, minimum 8/four ways/threaded dispatch/quantum
64, and statistics off. Tests exercise division exceptions, budget exhaustion,
two-thread fairness, callback publication, ownership and code-memory failures.

The original game JAR/board setup and its display/audio workload have not been
rerun here. Reuse the reported CPU/PSRAM clocks, firmware placement, screen size,
volume, input, fresh RMS and fixed Java work on the board. First collect the three
modes with detailed counters; then repeat timing with details off and statistics
off. Compare minimum 1/4/8 and cache ways 1/2/4 independently. If increasing code
entries to 128/256, record both metadata and actual executable allocator usage.

Record fixed-workload end-to-end duration separately from compute time, compile
cycles, native execution cycles, display submission time and actual LCD updates.
A board cycle counter can wrap on RV32; sample/accumulate it appropriately in
profiling code. Keep checksum and final-image CRC checks. For concurrent WAV/MIDI,
record completion, total source samples accepted and underruns. The desktop
compute harness supplies none of those board-specific display/audio results.

Resolved fields and compiled loops remain future work. Fields need precise null
exception PCs, resolution/class-init handling, reference-store and GC contracts.
Backedges need explicit quantum debit and exits that preserve thread/input
responsiveness. Extending those contracts is necessary before claiming that
field-heavy game loops are accelerated.
