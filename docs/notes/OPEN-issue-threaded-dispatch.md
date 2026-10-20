# The computed-goto interpreter: double fetch, and a default that hid it

Status: **resolved** (2026-10-21). Two defects were behind the `VerifyError`:

1. the computed-goto dispatch table sent unconverted opcodes to a label that
   fetched the opcode **again**, and
2. the dispatch default was nested inside the `#ifndef NJ_PIXFMT` guard, so the
   broken interpreter was selected by exactly the builds that no test covered
   while every CMake build ran the portable switch.

Both are fixed in the same commit: `src/core/nj_interp.c`,
`include/nj2me/nj_config.h`, and a new `tools/check_threaded_dispatch.sh` that
`ctest` runs.

## Symptom (historical)

`tests/fixtures/keysdemo.jar` threw

    java.lang.VerifyError at KeysDemo$Pad.<init>()V pc=14: receiver is not an object reference

The message comes from the interpreter's own receiver check in
`src/core/nj_interp.c` (`if (!nj_heap_owns(vm, recv))` before an
`invokevirtual`), so the operand stack held something that is not an object
where one is required.

## Root cause: the table entry refetched the opcode

`DISPATCH()` in `src/core/nj_interp.c` is the threaded body's branch:

```c
#define DISPATCH() do { NJ_FETCH_OP(); goto *nj_dispatch[op]; } while (0)
```

A handler that has not been converted to a label has no `L_` target, so its
table entry pointed at `L_slow`, and `L_slow` begins with its own `NJ_FETCH_OP()`
followed by `switch (op)`. The opcode had therefore already been fetched by the
*previous* handler's `DISPATCH()`, and this transition fetched the operand byte
as if it were the next opcode. The interpreter then resumed one byte late.

`keysdemo`'s `KeysDemo$Pad.<init>` is the shortest program that exposes it:

```
0 aload_0
1 invokespecial Canvas.<init>      <- converted handler, DISPATCH() fetches pc4
4 aload_0                          <- unconverted: L_slow fetched pc5 instead
5 iconst_0
...
11 invokevirtual setFullScreenMode <- runs with the stack of a different pc
```

`NJ_TRACE='*'` on debug builds of both interpreters showed the divergence
directly: switch ran pc=1 (sp=0, `aload_0`) → pc=2 (sp=1, receiver object);
threaded ran pc=2 with sp=0 and then jumped to pc=6, and by pc=11 the would-be
receiver was the integer `4`.

## Fix

* All 214 unconverted table entries now point at a new `L_haveop:` label placed
  **after** `L_slow`'s fetch and trace block, immediately before `switch (op)`.
  Converted handlers jump straight to their label (or to `L_haveop` for an
  unconverted opcode), and only the plain fall-through at the top of the loop
  ever executes `NJ_FETCH_OP()`.
* `include/nj2me/nj_config.h`: the dispatch block was moved out of the
  `#ifndef NJ_PIXFMT` guard and its default is now an explicit
  `#define NJ_THREADED_DISPATCH 0`, with a comment saying why it must stay
  independent of `NJ_PIXFMT`. The switch stays the default: every number in
  `docs/PERFORMANCE.md` and every `ctest` run is measured on it, and the second
  implementation of the 256 opcodes has to keep proving itself against that
  baseline. `-DNJ_THREADED_DISPATCH=1` opts in.

## Verification

* `tools/check_threaded_dispatch.sh` (registered in `ctest` as
  `check_threaded_dispatch`, TIMEOUT 300) builds the runtime twice from source -
  default and `-DNJ_THREADED_DISPATCH=1` - runs `keysdemo.jar` on both, and
  requires: no `VerifyError`, the full frame budget, and **equal bytecode
  counts** (1283 on the fixture). It also compiles the tiny header probe twice,
  with and without `-DNJ_PIXFMT`, and requires `NJ_THREADED_DISPATCH == 0` both
  times, so the de-nesting cannot regress.
* Real-game equivalence, `tests/games/1945空战英雄_6105.jar` with
  `--device nokia6230 --size 240x320 --fps 30 --run-frames 240 --stats
  --dump-frames`: the two interpreters produced identical statistics
  (`frames=240 paints=137 gfx=986 bytecodes=5370650 natives=714158
  heap=3383584/3762576 cpcache=546300/747`) and byte-identical PPM dumps at
  frames 60/120/180/240.
* Full `ctest` on a build configured with `-DNJ_THREADED_DISPATCH=1` passes, and
  so does the default build.

## Reproduction (now passes)

```
SRC="src/core/*.c src/midp/*.c src/gfx/*.c src/zip/*.c src/runner/nj_run.c \
     src/port/posix/nj_port_headless.c apps/nj2me_main.c"
gcc -std=c99 -Os -Iinclude -Isrc/core -Isrc/midp -Isrc/gfx -Isrc/zip \
    -o /tmp/td $SRC -lm -lz
/tmp/td --port headless --heap 8 --fps 0 --run-frames 20 --size 240x320 \
    tests/fixtures/keysdemo.jar           # runs to the frame budget
```

Add `-DNJ_THREADED_DISPATCH=1` to build the computed-goto interpreter; the same
run must also finish without a `VerifyError`.

## Lessons

* A second implementation that no default build compiles is untested code. The
  check script compiles both, so the next regression fails a test instead of
  corrupting one fixture's frame.
* A default that depends on an unrelated define is not a default. The two
  configure paths (CMake and hand builds) silently disagreed about which
  interpreter the project ships and tests.
