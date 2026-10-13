# OPEN: the computed-goto interpreter is on in every build that does not happen to define NJ_PIXFMT

Status: **unfixed**. The build that exposed it is worked around (it now defines
`NJ_PIXFMT` like CMake does), but the two real fixes are still to be made: the
one-line de-nesting in `nj_config.h`, and whatever the threaded interpreter is
doing wrong.

## Symptom

`tests/fixtures/keysdemo.jar` throws

    java.lang.VerifyError at KeysDemo$Pad.<init>()V pc=14: receiver is not an object reference

The message comes from the interpreter's own receiver check in
`src/core/nj_interp.c` (`if (!nj_heap_owns(vm, recv))` before an
`invokevirtual`), so the operand stack holds something that is not an object
where an object is required - the same shape of bug as
`docs/notes/OPEN-issue-stack-slot.md`, one instruction earlier.

It is not the MIDlet. `cadence`/`aoe`/the corpus run fine, because the bug needs
a particular stack shape at a particular pc; `keysdemo`'s inner-class
constructor is simply the fixture that hits it.

## Why only some builds see it

`include/nj2me/nj_config.h`:

```
268: #ifndef NJ_PIXFMT
269: #  define NJ_PIXFMT NJ_PIXFMT_RGB565
270:
271: /*
272:  * Threaded (computed-goto) interpreter dispatch.
...
279: #if !defined(NJ_THREADED_DISPATCH)
280: #  if defined(__GNUC__) || defined(__clang__)
281: #    define NJ_THREADED_DISPATCH 1
282: #  else
283: #    define NJ_THREADED_DISPATCH 0
284: #  endif
285: #endif
286: #endif          <- this closes `#ifndef NJ_PIXFMT`, not the dispatch block
```

The dispatch default is nested **inside** the pixel-format guard, so the switch
is inverted from what the comment intends:

| how the build is configured          | NJ_THREADED_DISPATCH | interpreter |
|--------------------------------------|----------------------|-------------|
| `-DNJ_PIXFMT=NJ_PIXFMT_RGB565`       | never defined -> 0   | switch      |
| `-DNJ_PIXFMT=NJ_PIXFMT_RGB565` `-DNJ_THREADED_DISPATCH=1` | 1 | computed goto |
| *(nothing)*                          | 1 (gcc/clang)        | computed goto |
| `-DNJ_THREADED_DISPATCH=0`           | 0                    | switch      |

Measured, three runs of the same fixture each, byte-identical outcome every
time (not flaky):

```
none                         -> failures/3 = 3
-DNJ_THREADED_DISPATCH=0     -> failures/3 = 0
-DNJ_PIXFMT=... -DNJ_THREADED_DISPATCH=1 -> failures/3 = 3
-DNJ_PIXFMT=...              -> failures/3 = 0
```

`NJ_MAX_HEAP` and `NJ_ENABLE_*` were ruled out first: they do not move the
result. The value of `NJ_PIXFMT` is not what matters - *definedness* is, because
definedness is what decides whether the dispatch default is ever seen.

## Consequences

* **CMake has always defined `NJ_PIXFMT`** (`CMakeLists.txt`), so the desktop
  runner, `ctest`, `tools/run_game_corpus.sh` and every number in
  `docs/PERFORMANCE.md` were measured on the **switch** interpreter.
  The computed-goto path has never been exercised by this project's own tests.
* Any build that compiles the sources by hand - `tools/build_android.sh` did,
  before this session - silently turns it on and gets a different interpreter
  from the one everything is tested against, including this miscompile.
* `-DNJ_THREADED_DISPATCH=1` on top of a CMake build is the way to reproduce on
  the desktop; expect the same `VerifyError`.

## Where

* `include/nj2me/nj_config.h:268-286` - the nesting. Moving `#endif` up to after
  line 269 (and deleting line 286) makes both builds agree. That is the whole
  fix for the *config* half, and it is deliberately **not** done yet: it would
  turn the threaded interpreter on for every build, and the interpreter is not
  ready for that.
* `src/core/nj_interp.c:415-423` and `:722` - `#if NJ_THREADED_DISPATCH` selects
  the threaded body. The bug is in there, not in the portable switch.

## Reproduction

```
SRC="src/core/*.c src/midp/*.c src/gfx/*.c src/zip/*.c src/runner/nj_run.c \
     src/port/posix/nj_port_headless.c apps/nj2me_main.c"
gcc -std=c99 -Os -Iinclude -Isrc/core -Isrc/midp -Isrc/gfx -Isrc/zip \
    -o /tmp/td $SRC -lm -lz
/tmp/td --port headless --heap 8 --fps 0 --run-frames 20 --size 240x320 \
    tests/fixtures/keysdemo.jar           # -> VerifyError
```

Add `-DNJ_THREADED_DISPATCH=0` and it passes. `tools/build_android.sh` carries
the same warning next to its `FEATURE_DEFS`, because that is the build that
tripped over it.
