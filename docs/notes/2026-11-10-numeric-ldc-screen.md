# Minecraft GEN: numeric ldc decoding screen

The guarded numeric-constant candidate was **rejected**. Its one original
Minecraft GEN pair reduced process CPU time by 1.42%, below the preselected
3% screening gate. No interpreter or default-setting change was applied.
This pair is a selection result, not evidence of a stable throughput gain.

## Candidate and reason

The baseline is commit `9457c05c1ac3b16c4fa3b1934b8920dffc726883`.
The shared `ldc`/`ldc_w` handler in
[the interpreter](../../src/core/nj_interp.c) calls
[`nj_cp_const`](../../src/core/nj_class.c) for every constant. Integer and
float entries only decode four big-endian bytes; they do not allocate or
resolve a reference. An earlier sampled GEN profile included time in this
helper. Those samples are historical evidence, not fresh call counts.

The sole candidate replaced that call's setup with a guarded read of the
existing packed CP word. It directly decoded INTEGER/FLOAT entries with a
nonzero offset, casting bytes to `nj_u4` before shifts. Missing owner/table,
invalid indices, zero offsets and all other tags retained the original helper.
The common integer-only fusion, exception, one-slot push and dispatch tail
remained byte-identical. `ldc2_w` was untouched. No cache, class-data rewrite,
layout change or new opcode was introduced.

Static review checked raw float bits, signed zero/NaN payloads, reference
constant GC paths, PC/stack/quantum and fallback behavior before compilation.
It did not establish malformed-CP hardening: both paths trust loaded numeric
payload bytes, and the existing operand-reading behavior was preserved.

## One-pair protocol and result

A private `git archive` of the baseline supplied a fresh Release library.
The existing main archive was not used as the baseline: it predates some
recent repairs. GCC 16.2.1 used `-O3 -DNDEBUG`, threaded dispatch and existing
superinstructions, with JIT and ENGINE both disabled. Only the candidate
interpreter object changed; both executables shared the same library and
unchanged [GEN benchmark](../../tools/bench_minecraft_gen.c).

The explicit build target was `test_vm`, without building the CLI. Both
variants first passed the existing `hello.jar` / `HelloWorld` / `wideargs ok`
integration test through three create/run/destroy cycles; their logs were
identical. `test_engine` would skip with ENGINE=0 and was not run. This
prescreen is general VM integration coverage, not an exhaustive ldc matrix.

Each executable then ran once, in baseline/candidate order, pinned to CPU 0
with seed 12345, type 0, a fresh 64 MiB VM and a 180-second process limit.
The original JAR SHA-256 was
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
The timer covers only `Gen.generate(NULL)`; setup and world hashing are outside
it. There was no retry, timeout, extra GEN pair, LOAD run or candidate variant.

| Metric | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| GEN process CPU | 9094.474 ms | 8965.661 ms | -1.42% |
| GEN wall time | 9125.534 ms | 9004.879 ms | -1.32% |
| Charged interpreter dispatches | 5,175,713,674 | 5,175,713,674 | equal |
| Interpreter object `.text` | 82,669 B | 82,795 B | +126 B |
| Benchmark executable `.text` | 1,528,614 B | 1,528,742 B | +128 B |

All 256 blocks/data chunk hashes and WORLD hashes matched the retained
original-world oracle. Spawn `161/64/111` matched a separate fixed expectation
and the paired run; the world oracle does not contain spawn. Both runs had
1,616,679 native calls, zero measured collections, and heap/peak 6,510,256 B.
All non-time GEN fields, including dispatch, matched.

The baseline interpreter object matched its fresh archive member byte for
byte. ELF section extraction was read-only, with input hashes checked before
and after. Commands, source identities, raw logs, objects, assembly, section
sizes and independent audits were frozen locally before native cleanup.
Original games, user saves, main-build files and runtime sources were unchanged.

## Decision and limits

The first pair failed the 3% gate, so no confirmation series, new ldc test
matrix, sanitizer campaign, SDL replay or Android/FPS measurement followed.
The prior 161-test heap-repair baseline was not rerun for this documentation-only
record. This experiment neither establishes an Android gain nor attributes the
small timing difference to instruction-cache or other hardware effects.
