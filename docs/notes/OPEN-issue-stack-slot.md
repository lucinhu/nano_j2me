# OPEN: one extra operand slot after an `invokespecial` of a Java constructor

Status: **unfixed**, evidence collected, reproduction one command away.

## Symptom

`tests/fixtures/gamedemo.jar` (built by `tools/make_game_jar.sh`) reaches
`DemoGame.startApp` and throws

    java.lang.NullPointerException @ DemoGame.startApp+41

at offset 39, `invokevirtual DemoCanvas.addCommand(Command)V`, because the
receiver loaded by `getfield canvas` at offset 32 is `null`: the preceding
`putfield canvas` at offset 28 stored the value into the wrong object.

## Evidence

Instrumented `NJ_TRACE=startApp` (see `nj_trace_step`), reading `pc`, `sp-stack`
and the top slots:

```
pc=24  sp=2      dup
pc=25  sp=3      invokespecial DemoCanvas.<init>()V     <- caller sp = 3
pc=28  sp=3      putfield canvas                        <- EXPECTED sp = 2
       S0=this  S1=<new DemoCanvas>  S2=0
pc=31  sp=1      aload_0
pc=32  sp=2      getfield canvas
pc=35  sp=2      S1=0                                   <- canvas really is null
pc=39  sp=3      invokevirtual addCommand -> NPE
```

`putfield` pops the value first, so with the extra slot it pops `S2 = 0` as the
value and `S1 = <the new DemoCanvas>` as the object: it writes `0` into a field
of the freshly allocated canvas instead of storing the canvas into
`DemoGame.canvas`. That is exactly the observed `null`.

Instrumenting the invoke path shows the callee is entered correctly:

```
INV DemoCanvas.<init>()V total=1 sp_off=3 frame_sp_off=3 nframes=1
  -> st=0 pushed=1 sp_off=3 nframes=2
```

and instrumenting `DO_RETURN` shows the rewind target is right:

```
RET DemoCanvas.<init> callee_locals_off=-5 sp_off=0
```

`callee_locals_off = -5` means `argbase = stack_callee - 5`, i.e. the callee's
`max_locals` is 5 and `argbase` is where the caller's `sp` must go. Everything
in `DO_RETURN` (nj_interp.c:813) reads correctly, `RELOAD()` runs before
`sp = rsp_`, and there is exactly one `case 0xb1`.

## Where to look next

The difference between the working case and the failing one in the same method
is that `Command.<init>` is a **native** and `DemoCanvas.<init>` is **Java**.
The `new Command` sequence at offsets 5..17 is stack-correct (sp 6 -> 2 -> 0).

The prime suspect is the interaction with `nj_class_init()`:

* `DemoCanvas` has a `<clinit>` (it initialises `private static final int[]
  TRANSFORMS`), so the `new` opcode at offset 21 runs Java code *before*
  `sp->r = o; sp++`.
* `nj_class_init` calls `nj_call_static`, which calls `vm_invoke`, which takes
  its frame base from `th->frames[nframes-1].sp` - the value written by the last
  `SAVE()`, which at that moment is stale (`stack+6`, saved by the
  `invokespecial` at offset 14). The interpreter's live `sp` is `stack+1`.
* The `<clinit>` frame is therefore placed above the live operand stack, and
  `nj_frame_pop` leaves `th->top` at that point. The interpreter's C-local `sp`
  is untouched, so this *looks* harmless - but it is the only asymmetry between
  the two sequences in this method.

A second, related suspect: `vm_invoke` on the success path deliberately does not
restore `th->frames[saved_frames-1].sp`, relying on `nj_frame_pop`. That is
correct for a frame pushed at a *saved* `sp`, and wrong for one pushed at a
stale `sp` - which is precisely the `nj_class_init` case above.

The clean fix is probably to make the interpreter publish `sp` into the frame
before any call that can run Java code (a `SAVE()` before `nj_class_init` in the
`new`, `invokestatic` and `invokespecial` paths), rather than to change
`DO_RETURN`.

## Reproduction

    ./tools/make_game_jar.sh
    ./tools/midp_stubs.py >/dev/null          # only needed to rebuild the JAR
    gcc -std=c99 -g -O1 -Iinclude -Isrc/core -o /tmp/nj2me \
      apps/nj2me_main.c src/runner/nj_run.c src/core/*.c src/zip/*.c \
      src/gfx/*.c src/midp/*.c src/port/posix/nj_port_headless.c \
      src/port/posix/nj_port_sdl2.c src/port/android/nj_port_android.c \
      src/port/esp32/nj_port_esp32.c -lm $(pkg-config --cflags --libs sdl2)
    /tmp/nj2me --port headless --heap 4 tests/fixtures/gamedemo.jar

Rebuild with `-DNJ_ENABLE_DEBUG=1` and run with `NJ_TRACE=startApp` to get the
instruction trace back.
