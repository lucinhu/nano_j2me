# Minecraft JIT lazy-filter screen

The single candidate was **dropped**. Skipping normal-path maintenance of the
allocation-backoff filter did not meet the predefined 3% CPU improvement gate.
No runtime, cache configuration, fusion gate or default JIT setting changed.

## Hypothesis and exact scope

The [JIT runtime](../../src/core/nj_jit.c) maintains `code_methods`, a conservative
method/PC-bit filter. Its only reader uses the filter while executable-memory
allocation is in backoff. The current removal helper nevertheless scans all
64 code entries to rebuild an affected bucket during ordinary code eviction.
The [earlier mode diagnostic](2026-11-10-minecraft-jit-modes.md) motivated checking
this repeated work; an eviction count alone does not establish its CPU cost.

The isolated candidate made three changes:

1. Return from `jit_method_remove` immediately when allocation backoff is zero.
2. Before publishing a new allocation backoff, clear all filter buckets and
   rebuild their union from live entries whose `code_refused` flag is clear.
3. During positive backoff, retain the original precise removal logic so that
   removing one entry does not hide another entry with the same bucket/PC bit.

A failed allocation occurs after an eviction victim has been cleared. Rebuilding
at this point excludes that victim and any fault-refused code. The candidate
changed no fields, capacities, hash function, allocation callbacks, bytecode
lowering, scheduling, mode API or statistics counters. Source SHA256:

- Baseline: `1550ea06e9c464def10845c786763bcdf37a5a48cadd529c7d69253b6a47f3bb`.
- Candidate: `4ef1d6a2a5c983b242b8283240bec594344f0d183375398df204477cd3e24ae5`.

Static review approved this invariant for screening. This was an optimization
experiment, not evidence of a pre-existing Java behavior bug.

## Fixed-work result

One fresh GCC 16.2.1 x86-64 Release library used the main build's feature settings
with JIT, engine hooks and detailed JIT statistics enabled. The candidate replaced
only the JIT runtime object before the same frozen archive. Both benchmark links
used the same benchmark object. The actual build target was `test_jit`, which
built the library and existing JIT test; it did not build the CLI.

Each process ran the [generation benchmark](../../tools/bench_minecraft_gen.c)
once with seed 12345, `--jit-mode execute`, CPU 0 affinity, a 64 MiB heap and a
180-second timeout. CPU/wall clocks surround only `Gen.generate(NULL)`;
initialization, statistics queries and world hashing are outside the clocks.
The unmodified JAR SHA256 was
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.

| Metric | Baseline | Candidate |
| --- | ---: | ---: |
| GEN process CPU | 15,588.330 ms | 15,591.611 ms |
| GEN wall time | 15,661.688 ms | 15,663.345 ms |
| Charged instruction count | 7,885,973,861 | 7,885,973,861 |
| JIT runtime object `.text` | 7,581 B | 7,789 B |

The observed CPU change was +0.02105%, so the candidate failed the 3% reduction
gate. This single pair does not establish a stable regression magnitude.
There were no confirmation pairs, LOAD runs, cache/hash variants or Android
measurements after the failed gate.

All 256 chunk block/data hashes and the WORLD hashes matched both each other and
the original world oracle: `a893a062` / `c77a1b93`. Spawn matched the separately
fixed expectation `(161, 64, 111)`. Both runs made 1,616,679 native calls, performed
zero collections and reported 6,510,256 bytes for both used and peak Java heap.
Both exited successfully without a timeout or retry.

All 19 JIT event deltas, five executed-window length bins and seven post-run
state/resource fields were identical between the two variants. Selected deltas:

| Event in the timed GEN interval | Both variants |
| --- | ---: |
| Compiled blocks | 336,563 |
| Code evictions | 336,512 |
| Method-map evictions | 299,599 |
| Live-code reconnections | 515,006 |
| Native block entries | 322,916,612 |
| Native-completed bytecodes | 3,467,256,948 |
| Allocation failures / commit failures / native faults | 0 / 0 / 0 |

These are this pair's counters. They differ from the earlier three-mode
experiment and should not be substituted for its measurements. There was no
address-layout or PMU experiment to attribute the cross-run difference. In this
pair, 336,512 evictions correspond to 21,536,768 source-level slot checks in the
original 64-entry removal loop; that arithmetic is not a measured CPU cost.

## Validation and disposition

The existing [JIT runtime test](../../tests/test_jit.c) passed against both
variants before generation. It covers modes, saved frames, scheduling, guards,
method maps, callbacks, caches, ownership and failures. Independent retained-log
verification checked the complete world oracle, fixed spawn, non-time GEN
fields and all reported JIT fields. Original sources, JAR, main archive and
main executable remained unchanged.

An archive command initially rewrote the two measured ELF files while extracting
`.text`. Relinking once per variant from the unchanged objects and archive
reproduced both original measurement SHA256 values exactly, without executing
the reconstructed binaries. Records of the rewritten files, recovery commands
and hashes remain in the audit. Extraction now reads ELF sections without
modifying its inputs. This did not add a generation or test run.

Existing tests are not exhaustive proof of the new lazy-state invariant.
Focused stale-union, repeated-OOM and refusal-transition fixtures would be
required before retaining this change. They were not added after the failed
performance gate. The prior 158-test suite was not rerun because this batch
changes only this note. Temporary native artifacts are removed after their
hashes, commands, source, assembly and raw logs are captured.

The default remains JIT off. This screen supports no Android FPS, world-loading
or production statistics-off speed claim. A global fusion-gate change was
separately rejected in static review because it would alter current instruction
accounting, scheduling and probe behavior; it was not compiled or measured here.
