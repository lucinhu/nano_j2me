# FLOAD/FNEG fusion: fixed-work Minecraft screen

The proposed float-load/negation fusion was **not accepted**. The first GEN
screen did not meet the predeclared 3% CPU-time reduction gate, so the candidate
was stopped without changing the runtime. Reduced dispatch counts alone were
not a reason to merge it.

## Why this candidate

The existing [Minecraft profile](2026-11-10-j2me-loader-comparison.md) identified
`Gen$INoise.add` and `grad` as substantial generation work. In the original JAR,
`grad(IFFF)F` contains `fload 5; fneg` at bytecode offsets 63/65 and
`fload 6; fneg; fadd; freturn` at 78/80/81/82. The interpreter already fuses
several float-load/arithmetic patterns, but did not match a following `fneg`.
This was a generic interpreter experiment motivated by that code, with no
class-name specialization or game modification.

The source copy added exactly four lines at the end of `NJ_FLOAD_FUSE`, inside
its existing bytecode-length guard:

```c
if (sp < stack_limit && njff_nx_ == 0x76u) {
    pc++;
    NJ_FARITH_TAIL(-(V));
}
```

All old arms and their order remained unchanged. The strict `sp < stack_limit`
guard matters: the original FLOAD pushes one slot, and the next FETCH rejects
`sp > stack_limit`. Using `<=` here could bypass that existing error for a
malformed full-stack method. Unary float negation and the existing arithmetic,
store and return tail were reused. Static review covered signed zero, NaN,
infinity, operand order, PC bounds, branches entering the standalone FNEG,
JIT gating and portable dispatch. This review was not an executed differential
regression suite.

## First screen

Baseline was commit `74ae026f2fbd30068747719ad33fbe82b1a0c31e`. Both variants used
the same freshly frozen library, original JAR, benchmark and header, with the
same GCC 16.2.1 Release/O3 settings, threaded dispatch, superinstructions enabled
and JIT disabled. The rebuilt baseline interpreter `.text` matched the frozen
library member byte for byte. Neither variant changed floating-point flags.

The original Minecraft JAR ran headless with seed `12345`, world type `0`,
64 MiB heap and CPU affinity `0`. Only `Gen.generate(NULL)` was timed;
construction, world export and hashing were outside the timer.

| Measurement | Baseline | Candidate | Observed change |
| --- | ---: | ---: | ---: |
| GEN CPU time, ms | 9198.963 | 9232.013 | +0.3593% |
| GEN wall time, ms | 9231.681 | 9265.652 | +0.3680% |
| Fused dispatches | 5,175,713,674 | 5,130,673,587 | −45,040,087 (−0.8702%) |
| Interpreter `.text`, bytes | 82,669 | 84,358 | +1,689 (+2.0431%) |

Each of the 256 chunks' block/data hashes and the total WORLD hash matched
both the other variant and the frozen original-world oracle. Both variants
produced the expected spawn `(161, 64, 111)`, exited successfully, made
1,616,679 native calls, performed zero collections and ended
with heap/peak usage of 6,510,256 bytes.

There was one valid baseline run and one candidate run. The wrapper initially
stopped after the baseline because it still expected the older dispatch count
`5,175,712,766`. The valid baseline log was retained and checked offline; only
the obsolete wrapper expectation was corrected before running the candidate
once. Source, binary and compiler settings did not change. The recovery left a
gap between the two runs, so this is a screening observation, not an alternating
confirmation experiment or a stable estimate of regression magnitude. There
was no PMU measurement or evidence attributing the result to instruction-cache
behavior.

No additional pairs, LOAD run, variant tuning, Android measurement or new
regression matrix followed. This batch changes only this note. The previously
validated runtime remains unchanged; its 158-test baseline was not rerun for
this documentation-only result. The result makes no Android FPS or J2ME-Loader
performance claim. Temporary native artifacts were hashed before cleanup;
source copies, commands, logs, manifests and the static review remain locally.
