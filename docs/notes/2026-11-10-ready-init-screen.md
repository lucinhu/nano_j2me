# Minecraft GEN: guarded READY class-initialization saves

This candidate is **rejected**. One fixed-work screening pair reduced GEN CPU
time by 0.28%, below the predefined 3% gate. The interpreter remains unchanged.
This is a screening result, not evidence of a stable speedup or an Android FPS
improvement.

## Candidate and contract

The baseline is `89725e173a0e953a85dfd51cd39956e3e482c2ac`, including the
upstream drawRGB work and the 0.5.6 version metadata. The experiment adds exactly
three conditions in `src/core/nj_interp.c`: the getstatic helper, putstatic, and
the direct invokespecial/invokestatic path skip their class-initialization
`SAVE()` and `nj_class_init_lazy()` only when the resolved owner is non-null and
its state is exactly `NJ_CLASS_READY`.

That condition matches the existing lazy helper. READY is published before
`<clinit>` runs to support reentry; it does **not** mean that initialization has
finished. Cold, ERROR and null-owner paths retain their old saves and behavior.
The value remains on the stack across a cold putstatic initializer, including
reference and wide values. The final save before invoking a method remains in
place, as do monitor, native-call, exception, yield and JIT boundaries.

Independent static review reconstructed the exact candidate and reversed its
three edits back to the original bytes. It found no new allocation, callback,
ABI, cache state, fusion or instruction-counting change. The timing of saved
PC/SP and counter publication within a READY path can change; this does not
establish pointwise equality for asynchronous observers. The existing trace
hook receives the same local PC/SP.

## Screening protocol

A fresh private source tree came from the baseline commit. A Release build used
the existing GCC flags, `-O3`, threaded dispatch and superinstructions, with JIT
and engine disabled. The explicit targets were `test_vm` and `test_lib`; the
CLI, libretro and full test suite were not built or run for this screen.

The candidate interpreter object overrides only the original interpreter
member of the same fresh runtime archive. Both GEN executables use the same
benchmark object and archive. The rebuilt baseline interpreter object matches
the archive member byte for byte. ELF sections were extracted read-only, with
input hashes checked before and after extraction.

Before configuring, the ignored `hello.jar` fixture was copied from the existing
fixture set into the private tree. The `test_lib` variants used separate working
directories and RMS locations, with output directories prepared before the
tests ran. Both VM variants passed three lifecycle cycles with `HelloWorld`;
both `test_lib` variants passed 1790 checks with zero failures.
These controls are preliminary coverage, not an exhaustive initialization
matrix.

Exactly one baseline process and one candidate process then ran on CPU 0, with
seed 12345 and a 180-second per-process limit. Only `Gen.generate` is inside the
CPU and wall timers; world hashing is outside them. The original game JAR has
SHA-256 `fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.

| Metric | Baseline | Candidate |
| --- | ---: | ---: |
| GEN CPU time | 9435.078 ms | 9408.719 ms |
| GEN wall time | 9472.294 ms | 9445.676 ms |
| Charged dispatches | 5,175,713,674 | 5,175,713,674 |
| Interpreter `.text` | 82,669 B | 82,725 B |
| Benchmark ELF `.text` | 1,530,726 B | 1,530,790 B |

All 256 chunk block/data hashes and the WORLD hash match the original world
oracle. Spawn separately matches the fixed expectation `(161, 64, 111)` and the
paired result; the old world oracle contains no spawn field. Native calls
(1,616,679), GC runs (0), and heap/peak (6,510,256 bytes) also match. Independent
parsing of both raw logs confirmed these values and the failed CPU gate.

## Decision and limits

The small first-pair difference does not justify confirmation runs or retaining
the candidate. There were no additional GEN pairs, LOAD screens, variants,
Android measurements, focused initialization tests, full-suite runs, or game
replays for it. The previous regression evidence belongs to the earlier
changes, not to this rejected candidate. No PMU measurements establish a cache
or branch explanation for this result.

Source, exact diff, flags, commands, preliminary test logs, raw measurements,
assembly, section sizes and hashes remain under the ignored
`.local-tools/debug-runs/mc-ready-init-20261110-screen/` directory. The candidate
was never applied to the working interpreter. Games, user storage and the main
build were preserved.
