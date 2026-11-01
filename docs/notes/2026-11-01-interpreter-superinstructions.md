# Interpreter superinstructions: where the next speed win was

Status: implemented on `src/core/nj_interp.c` only; switch and replicated
(`NJ_THREADED_DISPATCH`) bodies share the same source, so both get the new
fusions.  Gate: `#if NJ_ENABLE_SUPERINSTR` plus the usual
`if (NJ_FUSION_ALLOWED)` check that disables fusions while the JIT is installed.

## How the candidates were chosen

A temporary pair/triple histogram (not committed) was run over the fresh-world
generation window of the Minecraft replay (frames 250..560, ~2.4G dispatches):
`iload` 13.2%, `fload` 7.7%, `bipush` 5.0%, `istore` 3.6%, `fadd` 2.5%,
`fmul` 2.1%, `fsub` 1.9% - the Perlin-noise loop of `Gen.java`, with no float
fusions at all at HEAD.  Ranked instruction pairs showed the fusion sites that
were still unfused:

| site | dispatches in window |
| --- | --- |
| `iload/iload_k; iconst/bipush/sipush; iarith` | 185.6M (7.7%) |
| `iconst/bipush/sipush; if_icmp<cond>` | 89.7M (3.7%) |
| `fadd/fsub/fmul/fdiv; fadd/fsub/fmul/fdiv` | 51.9M (2.2%) |
| `fload; fload; farith` | 38.7M (1.6%, saves 2 dispatches) |
| `fadd; fstore*` / `fadd; freturn` | 24.9M / 27.6M |
| `istore/istore_k/fstore/fstore_k; iload*` | 89.0M (3.7%) |

## What was added

1. `NJ_ILOAD_CONST_ARITH_FUSE`: `iload*; <const>; iadd/isub/imul/idiv/irem/
   ishl/ishr/iushr/iand/ior/ixor`, reusing `nj_interp_const_arith` so idiv/irem
   by zero still throws `ArithmeticException` with the unfused operand stack.
2. `NJ_CONST_BRANCH_FUSE`: `<const>; if_icmp<cond>` - the constant never reaches
   the operand stack, so only the deeper operand is popped.
3. `NJ_FARITH_TAIL`: chains `fadd/fsub/fmul/fdiv` after a fused float operation
   (the previous result is the chain op's top operand), then stores with
   `fstore`/`fstore_k`, returns with `freturn`, or pushes.
4. `NJ_FLOAD_FUSE`: `fload; fload; farith`, `fload; fconst_0/1/2; farith`,
   `fload; farith`.
5. Store+load fusion for generic `istore`, `istore_k`, `fstore`, `fstore_k`
   followed by `iload*`.

A fused pair counts as one executed bytecode in `--stats` (the count is
incremented in `NJ_FETCH_OP`), so the bytecode number drops; the semantics
oracle is `tests/check_superinstr_digest` (27 digest ints identical to the
desktop JVM) plus the full ctest suite.

## Measurements

Fixed-workload benchmark (`tools/bench.sh 5`, CPU time, pinned): 0.216 s ->
0.195 s per run (-9.7%); bytecode counter 70.5M -> 62.5M because of the fusions.
This is the clean number: same work, no world randomness.

Desktop fresh-world replay (960 frames, fps 30, alternating base/candidate):
creation phase (MCJ GEN + save + prepare) 20.80 s -> 19.05 s average of two
pairs (-8.4%); generation alone 14638 -> 13077 ms.  World size is random per
run, so individual pairs vary by +-10-20%.

Android x86_64 APK on emulator (NJ_API21, fresh world, 2 interleaved pairs):
generation 25.6 s -> 25.9 s raw, but the candidate worlds were 9% larger;
per 1000 world blocks 17.66 -> 16.14 ms/K (-8.6%), initial save 5.75 -> 5.43 s
(-5.6%), load-back prepare 7.56 -> 7.61 s (+0.6%).  Start -> first HUD frame
and in-world rendering are unchanged in behaviour; HUD3D appears in every run.

## Risks

* The fusions are hand-written fast paths over the operand stack; a mistake
  shows up as a wrong digest or a `VerifyError`, both covered by the digest
  test and the 135-test ctest suite (all green).
* `freturn` fusion jumps to the existing `do_fused_ireturn` label; any future
  change to the return path must keep that label reachable from the float
  handlers.
* The 89M store+load sites were measured before the other fusions; after them
  some sites are already covered by earlier peeks, so the remaining benefit is
  smaller than the raw counts suggest.
