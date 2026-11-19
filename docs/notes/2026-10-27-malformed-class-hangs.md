# Malformed class files: the hangs, the crashes, and what was actually wrong

2026-10-27. Task-8: a malformed MIDlet JAR must make the runtime throw, not
hang and not fault.

## The short version

- The claim "half of all malformed class files HANG" (`ce2ddf6`) did not
  survive its own retraction (`fe99a15`): the fuzzer had run without
  `--run-frames`, and the *unmutated control* "hung" the same way because a
  MIDlet runs until it calls `notifyDestroyed()`. On today's build, 300
  random single-locus mutations finish 300 clean, 0 crashes, 0 hangs
  (`.local-tools/debug-runs/triage-20261027/fuzz-baseline-300.log`).
- The retraction then over-corrected: "the runtime does NOT hang" was too
  strong. Bytecode-focused mutation found nine SIGSEGVs, and reasoning about
  the unwind loop found a genuine hang that the random mutator never built:
  an exception-table entry whose handler is **one past the last bytecode**
  makes `interp_unwind()` resume the frame on an instruction fetch that throws
  again, forever - no bytecode retires, no scheduler yield, 100% CPU.
- Three defects are fixed:
  1. a `Code` attribute's exception table is validated at class load and the
     interpreter refuses to resume at a handler that is not a real
     instruction (hang -> `NoClassDefFoundError`);
  2. `getfield` / `putfield` / `invokespecial` receivers are checked like
     `invokevirtual` already did (SIGSEGV -> `java/lang/VerifyError`);
  3. a class or interface that names itself in its super/interface chain is
     rejected instead of recursing the linker into a C-stack overflow
     (SIGSEGV -> `java/lang/VerifyError`).
- Regression: `tests/test_class_hardening.c` runs the checked-in fixture
  `tests/fixtures/hardening_malformed.jar` (regenerate with
  `tools/make_fuzz_fixture.sh`; sha256
  `a398b1b4948f5f030364f9a55fa2650b6d322aea6735e3b160af128a3d64dfaa`).
  Registered with ctest as `test_class_hardening`.

## Why the hang was lost the first time

`fe99a15` was right about the *measurement*: without `--run-frames` the
runtime is supposed to keep a MIDlet running, so a timeout proves nothing. It
was wrong to conclude that the mechanism did not exist. The old fuzzer's
mutation kinds mostly corrupt header bytes (and two of them - 'methodcount'
and 'fieldcount' - wrote at fixed offsets 6 and 4, which are the class-file
version numbers, not the member counts; that has been fixed too). None of them
ever built a Code attribute with a damaged exception table, which is where the
livelock lives.

The current tool walks the constant pool and the method attributes and offers
real structural mutations: real method/field-count offsets, `exc_handler`
(handler outside / one past the code array) and `super_self` (super_class =
this_class). 120 cases on the debug build: 120 clean, 0 crashed, 0 hung
(`.local-tools/debug-runs/triage-20261027/fuzz-postfix-kinds-120.log`).

## Mechanism 1: handler resumes past the end of the method (hang)

A `Code` attribute's exception table was read and stored with no bounds
check. Any `start_pc/end_pc/handler_pc/catch_type` combination loaded.

The interpreter's unwind loop (`src/core/nj_interp.c:68`) searches handler
tables and, on a match, sets `f->pc = h->handler_pc` and returns. The main
dispatch path re-enters the loop at `L_slow` -> `NJ_FETCH_OP`
(`src/core/nj_interp.c:1506`), whose only check is `pc < code_len` and which
throws `VerifyError: code runs past the end of the method` otherwise. The
throw path calls `interp_unwind()` with `throw_pc = pc - 1` and
`continue`s. With an entry of `[0, 2)` and `handler_pc == 2 == code_len`:

1. `athrow` at pc 1 throws; the entry matches (`throw_pc = 0` is in range);
2. the frame resumes at pc 2 == code_len;
3. the fetch throws `VerifyError` at pc 2; the entry matches again
   (`throw_pc = 1`);
4. goto 1.

The quantum/yield check lives in the converted-handler path, not on this slow
loop, so the process spins forever with one log line per iteration until the
ring buffer stops recording (`hang-badhandler-repro.log`; 1999+ repetitions of
the same exception, then "log limit reached", rc=124 under `timeout 8`). The
unmutated fixture under the same flags exits 0, so this is a real hang, not a
budget artifact.

Fix, in two places (load is the attribution point, the interpreter is the
defence in depth):

- `src/core/nj_class.c:555-600`, in the `Code` attribute parser: reject an
  entry whose `start_pc >= end_pc`, whose `end_pc` or `handler_pc` is not
  inside `[0, code_len)`, or whose non-zero `catch_type` is not a class
  constant. A valid JVM class file never violates these.
- `src/core/nj_interp.c:99-114`, in `interp_unwind()`: skip an entry that is
  not self-consistent with `mm->code_len` before resuming at it, so a table
  somehow built anyway can cost a catch but never a spin.

## Mechanism 2: untyped receivers (crash)

The operand stack is untyped and the runtime has no verifier, so a slot used
as an object receiver can hold an int or float bit pattern. `invokevirtual`
already checked this (its comment cites corpus crash `0x87fff8008`); the field
opcodes and `invokespecial` did not:

- `getfield`: `nj_field_addr_fast()` did `(nj_u1 *)obj + f->offset` with no
  validity check, so a mutated `aload_0` -> `fconst_2` made the field read
  copy from address `0x56e4` (gdb: `nj_field_read_ref_fast` ->
  `nj_interp_run` at `L_0xb2`; mutant `interesting-crash-39`). Same on the
  write side (`putfield`).
- `invokespecial`: only the virtual path checked the receiver. A mutated
  `new` -> `ior` in `KeysDemo.<init>` put `0x7fff00000004` into `this` and the
  emulator died inside a MIDP native (`nj_field_write_ref`; mutant
  `interesting-crash-301`).

Fix: every `getfield`/`putfield` (`src/core/nj_interp.c:857-885`,
`:2290-2320`) and `invokespecial` (`:2437-2468`) path now requires
`nj_heap_owns_object_fast(vm, recv)` and throws
`java/lang/VerifyError: ... receiver is not an object reference`.

## Mechanism 3: circular class hierarchy (crash)

`class_link_inner()` resolves a superclass and recursively links it while the
class being linked is still in state `LOADED`. A class whose `super_class`
index points at itself therefore re-enters its own link and recurses until the
C stack dies: SIGSEGV, no exception. The same holds for an interface that
lists itself in its `interfaces` array (that path used to ignore the recursive
failure result entirely).

Fix (`src/core/nj_class.c:957-1010`, `nj_internal.h`): a per-class `linking`
mark and a per-VM `link_cycle` flag; a re-entrant link is a cycle and fails,
the exception is `java/lang/VerifyError: <class>: circular superclass or
interface chain`, and a failed interface link now fails the implementing
class. `java/lang/ClassCircularityError` is not registered in this runtime's
builtin exception tables (`src/core/nj_lib_object.c`, outside this task's
scope), so `VerifyError` - a `LinkageError` sibling - carries the message.

## Before / after

`timeout 10` on a small driver that opens the fixture JAR and calls one class's
`main()`, linked against a debug build of `git archive HEAD` (pre-fix,
`/tmp/nj-prefix-build`) and against the fixed build. `rc` is the process exit
status: 0 = a Java exception was reported (the correct outcome for malformed
input), 124 = `timeout` killed a spinning process, 139 = SIGSEGV.

| class                 | defect                                    | before        | after |
|-----------------------|-------------------------------------------|---------------|-------|
| HardeningControl      | none (control)                            | rc 0 accepted | rc 0 accepted |
| HardeningBadHandler   | handler resumes at `code_len`             | rc 124 HANG, 2000+ repeated `VerifyError` log lines | rc 0 `NoClassDefFoundError` (class rejected at load) |
| HardeningBadField     | `getfield` on an int                      | rc 139 SIGSEGV | rc 0 `VerifyError: getfield receiver is not an object reference` |
| HardeningBadPutfield  | `putfield` on an int                      | rc 139 SIGSEGV | rc 0 `VerifyError: putfield receiver is not an object reference` |
| HardeningCircularSuper| `super_class` = this class                | rc 139 SIGSEGV | rc 0 `VerifyError: HardeningCircularSuper: circular superclass or interface chain` |
| HardeningIfaceLoop    | interface lists itself as super-interface | rc 139 SIGSEGV | rc 0 `VerifyError: HardeningIfaceLoop: circular superclass or interface chain` |

Independently, the same hang was observed through the CLI before the fix:

```
timeout 8 /tmp/nj-core-build/nj2me --port headless --heap 8 --fps 0 \
         --run-frames 30 /tmp/badhandler.jar     # rc 124
timeout 8 /tmp/nj-core-build/nj2me --port headless --heap 8 --fps 0 \
         --run-frames 30 tests/fixtures/keysdemo.jar   # rc 0 (control)
```

Evidence: `.local-tools/debug-runs/triage-20261027/before-after-driver.log`
and `hang-badhandler-repro.log`.

## Reproduction

```sh
# fixture (deterministic, byte-for-byte)
tools/make_fuzz_fixture.sh
sha256sum tests/fixtures/hardening_malformed.jar

# the regression test
cmake -S . -B /tmp/nj-check -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNJ_ENABLE_DEBUG=1
ninja -C /tmp/nj-check test_class_hardening
ctest --test-dir /tmp/nj-check -R test_class_hardening --output-on-failure

# the fuzzer (now with real structural mutation kinds)
python3 tools/fuzz_classfile.py /tmp/nj-check/nj2me 120
```

`tests/fixtures/` is generated and gitignored, so the fixture is force-added
(`git add -f tests/fixtures/hardening_malformed.jar`). It is committed data on
purpose: the suite must be able to reproduce these failures on a machine
without a JDK and without running the python fuzzer.

## Still open / honest limits

- The historical "18 of 36 hang" figure cannot be reproduced and its mechanism
  is unknown; the control-run bug is the only verified cause of that report.
  Nothing here should be read as confirming it.
- The default fuzz mode is single-locus: one mutation per case, applied to the
  MIDlet's own class only. An optional third argument selects several
  mutations per case (the new `fuzz_classfile_multi` gate uses two); multi-class
  corruption is still not covered.
- There is no bytecode verifier. Malformed Code that is structurally valid
  still executes, and the runtime relies on per-instruction checks
  (receiver type, operand-stack bounds, handler bounds) to fail cleanly
  instead of faulting. More checks of that kind may still be missing; the
  fuzzer finds them one signal at a time.
- Class bytes from a JAR are trusted for memory layout arithmetic beyond the
  checked bounds; the fixture and fuzzer only cover what the loader and
  interpreter walk today.

## Update, 2026-11-15 (round 44): multi-locus coverage

`tools/fuzz_classfile.py` now takes an optional third argument: the number of
mutations applied to the same class per case. The mutation kinds combine the
same mix as before; a later structural mutation that no longer lands because an
earlier one truncated or moved bytes falls back to a shorter buffer, and the
result is still a malformed class from the runtime's point of view.

Evidence and gate:

- Two-locus run against the current release CLI:
  `python3 tools/fuzz_classfile.py /var/tmp/nj2me-goal13-build/nj2me 300 2` ->
  **clean: 300, crashed: 0, HUNG: 0** (control run first, so the per-case
  budget is derived from the same machine; `.local-tools` log kept).
- New ctest gate `fuzz_classfile_multi`
  (`python3 tools/fuzz_classfile.py <cli> 60 2`, TIMEOUT 300) runs next to the
  single-locus `fuzz_classfile` smoke test; both pass. The suite is now
  185 tests.
- Remaining gaps: bytecode that is structurally valid but semantically
  malformed (there is no verifier; the runtime relies on per-instruction
  checks), and corruption spread across *several* classes in the same run (each
  case mutates one class, chosen at random).

### All-class targets (`all`)

A fourth argument `all` aims the mutations at a random class from the JAR
instead of the MIDlet: a damaged inner class or canvas is as realistic as a
damaged MIDlet.  The kinds are chosen per case from the target class's own
layout (structural kinds only when that class can be walked), and the `base`
buffer is re-pointed to the chosen class before each case.

Evidence:

- `keysdemo.jar` (inner pad class + MIDlet), two-locus, 300 cases:
  **clean 300, crashed 0, HUNG 0**.
- `gamedemo.jar` (four classes including two anonymous inner classes),
  two-locus, 300 cases: **clean 300, crashed 0, HUNG 0**.
- New ctest gate `fuzz_classfile_allclass` (`python3 tools/fuzz_classfile.py
  <cli> 60 2 all`, TIMEOUT 300); the three fuzz gates run in ~0.5 s together.
  A longer 1000-case two-locus all-class run (seed 5) is also **clean 1000,
  crashed 0, HUNG 0** (round 49).
  The suite is now 186 tests and the full serial run is **186/186 passed in
  338.68 s** (`/tmp/r45-ctest.log`).
- Remaining gap after this: one class per case (a JAR with two independently
  damaged classes in one run is not simulated).

### Array operand guard (round 52)

The 3000-case long campaign found one crash the small runs had missed
(`tools/fuzz_jar.py ... 3000 2 102`, `saved=jar-crash-2451.jar`): a JAR whose
`DemoCanvas$2.class` had five bytes rewritten inside its `compare([B[B)I` code.
The resulting instruction stream (`...; lstore_1; iconst_0; fdiv; ...;
sipush 11011; baload`) is type-invalid - `lstore_1` consumes an int as a long
and the later `baload` takes the int 11011 as its array reference - but the
class-file loader has no type verifier and accepted it.  `ARRAY_HEAD()` checked
`arr_` for NULL and `->kind != NJ_OBJ_ARRAY`, so the wild pointer was
dereferenced: SIGSEGV in `baload` inside the interpreter.

Fix: `ARRAY_HEAD()` and the fused array-load body (`NJ_ARRAY_LOAD_BODY`, which
repeats the checks instead of sharing the macro) now call the existing
`nj_heap_owns_object_fast(vm, ref)` before touching the pointer and throw
`java.lang.VerifyError` for a slot that is not a live object; array stores,
`getfield`/`putfield` and invokes already had the same guard.

Regression coverage:

- `tools/make_fuzz_fixture.sh` gained two classes, `HardeningBadArray`
  (`sipush 11011; iconst_0; baload`) and `HardeningBadArrayFused`
  (`iconst_0; istore_0; sipush 11011; iload_0; baload` for the fused path);
  `tests/fixtures/hardening_malformed.jar` regenerated (SHA-256
  `eb86bee7dbe5da0d14bfd78761443356754ca609f7d5efd41658ac1ef10a3122`) and
  force-added.
- `tests/test_class_hardening.c` lists both; both are rejected with
  `VerifyError`, twice in a row, and the control class still runs (2 rounds,
  no leak).
- Re-run of the failing campaign: `tools/fuzz_jar.py <cli> 3000 2 102` ->
  **clean: 3000, crashed: 0, HUNG: 0**.
- `bench.jar` dispatch throughput is unchanged (62,513,694 bytecodes at
  419.55 M/s, same as the pre-guard binary on the same machine); the guard is
  two predictable loads per array access.

## Round 95: extended campaigns on the 0.5.15 tree (2026-11-19)

Re-ran the class-file campaigns with larger budgets against the release CLI
(`/var/tmp/nj2me-goal13-build/nj2me`, `f18ed5c` + docs):

```
python3 tools/fuzz_classfile.py <cli> 3000 2 91          # MIDlet class only
python3 tools/fuzz_classfile.py <cli> 3000 2 92 all      # any class in the JAR
```

Both finished **clean: 3000 cases, 0 crashed, 0 hung** (two mutations per
case; two-locus coverage plus the optional all-class target mode).  Together
with the round-44/45 runs this keeps the two known crash classes covered:
multi-locus bytecode (the `ARRAY_HEAD` ownership guard) and damaged inner
classes.  Still not covered: semantically invalid but structurally valid
bytecode, and several damaged classes in one run (the all-class mode damages
one selected class per case).
