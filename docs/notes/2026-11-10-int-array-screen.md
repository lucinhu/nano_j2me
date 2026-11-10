# Exact-int array access screen: candidate dropped

One interpreter candidate was screened against the original MinecraftJ2ME
world generator. Its first CPU pair improved by **0.1373%**, below the predefined
3% gate for further confirmation. The candidate was dropped. The production
interpreter remains unchanged.

## Candidate and isolation

The game's `Gen$Rnd` uses Java integer arrays. The candidate preserved the
existing null, array-kind, unsigned-index, heap-ownership, PC and stack checks,
then avoided a second generic helper call for exact `int[]` values at three
sites in [nj_interp.c](../../src/core/nj_interp.c): fused `iaload`, standalone
`iaload` and standalone `iastore`.

The added read branch was equivalent to:

```c
array->aflags == NJ_ARRAY_T_INT
    ? ((nj_s4 *)array->u.bytes)[index]
    : nj_array_get_int(array, (nj_u4)index)
```

The store branch similarly wrote directly only for `NJ_ARRAY_T_INT`, falling
back to `nj_array_set_int` otherwise. The original generic primitive-conversion
behavior in [nj_vm.c](../../src/core/nj_vm.c#L868) stayed available. There were
no new helpers, forced inlining, header changes, fusion changes or dispatch
reductions. Reversing exactly the three substitutions recovered the complete
baseline source byte for byte.

The source baseline was `4afa9e2260cb2237dd364a1bfcb4219e3903c4f1`; its runtime
matches `78114ba5493dce28776e6fd0beae26d8a96ac63a`. Both binaries linked the same
frozen archive, SHA-256
`49712268c52cbfe0a79894de8344c4cfd60861aebc7adc4a8a298e46d7acfced`.
Only their interpreter objects differed. The baseline interpreter `.text`
matched the object extracted from that archive byte for byte. Original
compiler and linker arguments were reused with experiment paths substituted.
The parallel Random fix and updated main archive did not enter either binary.

## First fixed-work pair

Host: Intel Core Ultra 9 275HX, Linux 7.2.11, glibc 2.44, GCC 16.2.1. The
interpreter used the existing Release `-O3` configuration and `-Werror`, with
no added LTO, fast-math or force-inline options. Runs were pinned to CPU0,
baseline first, then the sole candidate, once each.

| Metric | Baseline | Candidate | Change |
|---|---:|---:|---:|
| GEN CPU | 9218.867 ms | 9206.213 ms | -0.1373% |
| GEN wall | 9252.034 ms | 9243.594 ms | -8.440 ms |
| Fused dispatches | 5,175,712,766 | 5,175,712,766 | Equal |
| Native calls | 1,616,679 | 1,616,679 | Equal |
| GEN GC | 0 | 0 | Equal |
| Heap / peak | 6,510,256 / 6,510,256 B | Same | Equal |
| Interpreter `.text` | 82,669 B | 82,816 B | +147 B |
| Interpreter `.rodata` | 1,657 B | 1,657 B | Equal |

[bench_minecraft_gen.c](../../tools/bench_minecraft_gen.c) supplies the fixed
work: original game JAR, seed `12345`, survival type `0`, fresh 64 MiB headless
VM and the original `Gen.generate(NULL)`. Only `generate` is timed; constructor,
UI, player creation, rendering, saves and checksums are outside the interval.

All 256 chunks' block and packed-data hashes matched the independent original
world oracle individually, as did aggregate WORLD `a893a062/c77a1b93`, spawn
`161/64/111` and every non-time counter, including dispatches. The original JAR
SHA-256 remained
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
No game or user save was changed.

The first pair missed the gate, so there were no confirmation pairs, LOAD
screen, variants or production application. This single pair establishes no
stable speedup, Android performance result, whole-game FPS result or hardware
cache explanation. GEN equivalence also does not replace a full bytecode,
exception and malformed-array regression matrix for a future retained change.

The local frozen evidence contains 51 artifacts: exact candidate diff,
reversible substitutions, build/run commands, binary and object hashes,
section sizes, disassembly, raw logs, all chunk identities and isolation
provenance. Report SHA-256:
`419c9b852637c472dba6457f9066ff7ce42bcbc4e01c8cdfe31015322fe662d6`.
