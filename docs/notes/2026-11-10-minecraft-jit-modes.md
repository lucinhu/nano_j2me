# Current Minecraft GEN JIT modes (2026-11-10)

This diagnostic compares the current baseline JIT with the interpreter on the
original MinecraftJ2ME PE 0.6.1 V2.2 JAR. It adds explicit mode selection and
phase-scoped JIT statistics to [the fixed-work benchmark](../../tools/bench_minecraft_gen.c).
It changes neither runtime lowering nor the default `NJ_ENABLE_JIT=0` setting.

## What the three modes mean

The benchmark accepts:

```sh
bench_minecraft_gen original.jar 12345 --jit-mode interpreter
bench_minecraft_gen original.jar 12345 --jit-mode probe-only
bench_minecraft_gen original.jar 12345 --jit-mode execute
```

Use the same compiled executable and CPU affinity for all three. Without
`--jit-mode`, the existing two-argument invocation keeps the linked library's
default behavior. An enabled library attempts to install its JIT during VM
creation; an unsupported host or port retains its original interpreter fallback.
Explicit `probe-only` or `execute` instead fails when unavailable, including
with `NJ_ENABLE_JIT=0`, rather than reporting a successful JIT run.

Mode selection occurs immediately after VM creation, before library late
initialization, JAR opening and `Level`/`Gen` initialization. Interpreter mode
removes the owned JIT. Probe-only installs the JIT's heat/eligibility machinery
without native emission or execution. Execute additionally emits and runs hot,
supported blocks. Each measured mode starts in a fresh VM.

Both installed-JIT modes disable the interpreter's superinstructions. Therefore
probe-only minus interpreter includes the loss of fusion; it is not an isolated
measurement of probe overhead. Sparse probe entry and opcode filtering also mean
`queries` is not a count of all Java bytecodes or method calls.

## Measurement boundary and correctness

Only `Gen.generate(NULL)` is timed, with survival `gameType=0`, seed `12345`, a
fresh 64 MiB headless VM and the unmodified original JAR. Initialization, JIT
statistics retrieval and world hashing run outside the clocks. JIT event counters
are differences immediately around this call; code/peak/metadata bytes are
post-call gauges and may include initialization. This is neither the full
in-game world creation timer nor LOAD or FPS.

The JAR SHA-256 is
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
The experiment compares all 256 block/data chunk hashes and aggregate WORLD
hashes with the retained original-world oracle. Spawn is a separate fixed
expectation and cross-mode check. Native calls, GC and heap usage are also
checked; instruction counters are recorded without assuming equality.

## Observed runs

One isolated Release build used GCC 16.2.1 on Linux x86-64/glibc 2.44, `-O3`,
threaded dispatch and superinstructions enabled, `NJ_ENABLE_JIT=1`,
`NJ_ENABLE_ENGINE=1`, `NJ_JIT_STATS=1` and `NJ_JIT_DETAILED_STATS=1`. All other
interpreter compile definitions matched the existing main build. The three modes
ran in order on CPU 0 using the same executable, once each, with a 180-second
process timeout and no retries or tuning.

| Mode | GEN CPU ms | GEN wall ms | CPU change from interpreter |
| --- | ---: | ---: | ---: |
| Interpreter | 14,897.542 | 14,957.125 | baseline |
| Probe-only | 19,691.052 | 19,768.336 | +32.18% |
| Execute | 18,525.430 | 18,677.717 | +24.35% |

All processes exited 0 without timeout. All 256 block/data chunk hashes and WORLD
`a893a062/c77a1b93` matched the original-world oracle. Every run produced spawn
`161,64,111`, 1,616,679 native calls, zero GEN collections and 6,510,256 bytes of
heap/peak usage. This establishes output equality for these three runs; it does
not establish full-game JIT compatibility.

Selected execute-mode GEN counter deltas were:

| Event | Count |
| --- | ---: |
| Native entries | 320,596,379 |
| Completed native bytecodes | 3,435,617,542 |
| Compiled blocks | 991,373 |
| Code evictions | 991,322 |
| Method-map evictions | 299,599 |
| Reconnected live code entries (`recompiled_blocks`) | 462,376 |
| Admitted queries | 1,053,793,190 |
| Unsupported / short declines | 454,303 / 258,191 |
| Faults / allocation failures / commit failures | 0 / 0 / 0 |

Completed work averaged **10.716 bytecodes per native entry** in this run.
Native completed work was **43.566%** of this execute run's 7,885,973,861 charged
instructions. The interpreter's 5,175,713,674 charged handlers are a different
counting basis. Probe-only charged 7,885,973,861 instructions and performed no
compilation/native execution.

Execute-mode static-window bins contained 157,110,532 entries of length 4–7,
114,367,235 of length 8–15 and 49,118,612 of length at least 16. The first two
bins (1 and 2–3) were zero. Post-GEN requested code bytes were 13,388, peak
requested code bytes 18,792, and fixed JIT metadata 14,504 bytes. These are not
mapped-page RSS figures.

The current detailed-statistics JIT did not beat its interpreter mode in this
one-off comparison. Nearly as many code evictions as compilation events, along
with short completed work per entry, identify useful follow-up questions. They
do not separate compilation cost, cache replacement, lost fusion or native-call
overhead causally. There were no repeated pairs, stats-off measurements, cache
parameter variants, LOAD runs, Android measurements or FPS tests. These results
do not support changing the default to always-on JIT.

## Validation and retained evidence

The benchmark compiled with `-Wall -Wextra -Werror` for both the isolated JIT=1
library and the unchanged main JIT=0 library. Compile definitions must match the
linked library; the recorded builds reuse their configured target's compiler
and linker arguments. Ten bounded JIT=0 CLI checks covered usage, invalid modes
and flags, seed errors, explicit unsupported modes, legacy default invocation,
explicit interpreter selection and the minimum signed seed, without repeating
GEN. Runtime sources, original JAR and the main executable/archive hashes were
unchanged. The prior main suite result remains 158/158; the full suite and SDL
replay were not repeated for this benchmark/documentation-only change.

Local source/command manifests, three attempt/result/log records, the world
oracle and an independent verification record are retained under
`.local-tools/debug-runs/mc-jit-20261110-modes/` and
`.local-tools/debug-runs/mc-jit-20261110-main/`. Generated binaries, game bytes and
experiment records are excluded from the commit. The benchmark source and this
note provide the reusable mode/counter interface and the measured conclusions.

## Reading the counters

The legacy `GEN dispatch` label is `vm.insns`. With fusion it counts dispatched
handlers; with the owned JIT it includes unfused handlers plus completed native
bytecodes charged by the execution bridge. Its values across modes do not
measure equivalent opcode counts. Within execute mode, native bytecodes divided
by that run's charged instruction total is only a charged-instruction share.

`compiled_blocks`, evictions and declines count events, not unique methods or
bytecode locations. `recompiled_blocks` counts reconnecting an existing live code
entry after probe-map recycling, rather than freshly emitting it. The five
length bins count executions weighted by the static `block.insns` window length;
a loop entry can complete more bytecodes than that window before returning.
They do not determine average completed operations per native entry.

## Current architectural limits

The [x86-64 backend](../../src/core/nj_jit_x86_64.c) uses System V LP64 integer and
SSE2 lowering. It executes bounded leaf blocks, including int-array accesses,
resolved instance fields, local operations, arithmetic and supported branches.
Calls, returns, other array element types and unsupported operations return to
the interpreter. Only a backedge to the block entry at the same operand-stack
depth (zero net stack delta) can remain a native loop. Method invocation and Java frame entry remain outside
this baseline block compiler.

This differs from J2ME-Loader's JAR-to-DEX-to-Android-runtime path described in
[the source comparison](2026-11-10-j2me-loader-comparison.md). A Linux x86-64 block
JIT measurement does not establish ART performance or Android/ARM64 FPS. It also
does not change the previous finding that Minecraft's GEN and LOAD decode/mesh
phases spend their time in Java computation rather than the software rasterizer.
