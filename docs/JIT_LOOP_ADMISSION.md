# RV32 loop admission checks

This follow-up starts from `4b08bbe`. The earlier completed-bytecode batching
is described in [JIT_LOOP_ACCOUNTING.md](JIT_LOOP_ACCOUNTING.md).

A native loop must return before exhausting the Java scheduling budget. The
old loop head computed `budget - completed`, loaded the decoded window size,
and compared the two on every iteration. The emitter now checks
`budget >= window_size` once, sets `limit = budget - window_size`, and uses one
unsigned comparison, `completed <= limit`, at the loop head. The existing
conservative whole-window bound, exact completed counts, exits and fault
replay stay intact. The body never uses the budget argument register, so the
limit survives back edges and array/field guards without a new register or
stack slot.

The entry check matters even though the VM normally checks admission before
calling a block: direct backend calls with a smaller budget must return zero
completed bytecodes without modifying the frame. Subtraction only occurs
after that check, avoiding unsigned wraparound. Large unsigned budgets retain
the same behaviour. No CPU-specific extension or new configuration is needed.

This saves two executed native instructions per loop-head visit at the cost
of a three-instruction setup once per invocation. For the eight-bytecode
sum-loop fixture, the complete block grows from 88 to 90 bytes with compressed
instructions enabled; code size is a small tradeoff, not a claimed reduction.
The ABI, cache limits and ordinary non-loop blocks remain unchanged.

Validation covers all 14 RV32 configurations in `tools/check_jit_rv32.sh`,
including extension switches, register cache off, metadata limits and both
statistics settings. There are 22,292,876 host assertions, 22,675,438 RV32
assertions and 42,170 actual generated-code invocations. Budget tests include
all values 0–40 around eight-bytecode iteration boundaries, unchanged whole
frames when no iteration fits, and `0x7fffffff`, `0x80000000`, `0xffffffff`.
Existing tests compare loop frames and guarded side effects against an oracle.

The host JIT regressions also pass with address and undefined-behaviour
sanitizers. Eight fixed Java kernels match independent C checksums in
interpreter, probe-only and execute modes (24 results). Maximum interpreted
fallback in execute mode is 0.2854%; executable allocations are released.
Emulator elapsed time is not used as target performance evidence.

Target measurements and their limitations are recorded in
[jit-loop-admission-results.json](jit-loop-admission-results.json).

An anonymous RV32 target was measured at 240×320 with the FPS overlay disabled,
fresh temporary RMS, ordinary JIT statistics enabled and detailed statistics
disabled. Both images share the frontend/audio objects, compiler flags and
private integration fixes; only the RV32 emitter object changes. The original
five-kernel JAR is the same one used in the earlier cache report. Its 50,000
iteration runs are too short to resolve the small differences reliably.

For the longer comparison, only the wrapper's iteration constant changes from
50,000 to 2,000,000. All kernel class bytes remain identical and the checksums
are cross-checked with a desktop JVM. Keeping the ZIP length unchanged also
keeps each image's executable sections and memory layout unchanged. The image
checksum and digest are regenerated and verified. Three fresh-VM candidate
runs are followed by three baseline runs, reversing the order of the short
comparison. Means, in milliseconds:

| Kernel, 2,000,000 iterations | Baseline | Patched | Less time |
| --- | ---: | ---: | ---: |
| Locals | 290.00 | 278.00 | 4.14% |
| Constants | 190.00 | 178.00 | 6.32% |
| Fields | 379.00 | 366.00 | 3.43% |
| Arrays | 719.00 | 705.67 | 1.85% |
| Branches | 394.33 | 382.00 | 3.13% |

Every kernel checksum, bytecode count and final pixel CRC agrees. Each run
completes normally with host and executable memory reclaimed, no JIT faults
and no executable allocation failures. Combined end-to-end time averages
2.204034 s before and 2.140589 s after (2.88% less), including setup, blank
presentation and teardown. Generated code for the five kernels grows from
1,490 to 1,502 bytes.

The CPU fixture with 120 complete rendered frames averages 3.149719 s before
and 3.155774 s after, with the same checksum and final pixel CRCs. Its reported
VM rate is about 38.8 FPS in both cases. The one 300-frame bouncing-ball startup
sample takes 7.425084 s before and 7.520024 s after. These measurements establish
no presentation or gameplay FPS gain. Startup is not a gameplay benchmark;
final scene CRCs and asynchronous audio progress differ. The startup also
records 10,746 method-map evictions in either version and 602/765 failed code
allocations, pointing to remaining cache and memory pressure to investigate.
No audio improvement is claimed.

The earlier approximately 1,682 ms branch-loop anomaly no longer reproduces
at the current baseline: the identical original JAR takes 10 ms with JIT and
202 ms interpreted. That observation predates this patch and is not attributed
to it; the historical builds differ in additional code and layout. JIT remains
opt-in for applications whose measured workloads benefit.
