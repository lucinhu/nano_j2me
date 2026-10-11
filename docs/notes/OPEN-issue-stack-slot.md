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

---

# RESOLVED: the extra slot was a void `return` pushing a value

Fixed in commit `c512b53`.  `case 0xb1` reused `DO_RETURN` with a zero
`nj_value`, and `DO_RETURN` always ends with `*sp++ = rv_`.  Every Java
constructor therefore left one slot on the caller's stack.  The entry above
correctly identified `nj_class_init` as the only asymmetry between the working
(`new Command`, native) and failing (`new DemoCanvas`, Java) sequences, but the
cause was one level down in the return path.

The fixture now contains a nested class with a constructor, a static array
initialiser, and two constructors in a row, so the bug cannot come back
unnoticed.

---

# RESOLVED: the "negative source region" was an RMS field-descriptor mismatch

Fixed.  Two separate things were wrong and both are now gone.

`RecordStore` looked up its `rsOpen` / `rsModified` fields with descriptor `"I"`
while the class declares them `"Z"`.  A field lookup keyed by a descriptor that
does not exist returns NULL, and the helper wrote nowhere - so
`openRecordStore` returned a store that was never marked open, and the first
`addRecord` raised RecordStoreNotOpenException.  The helpers now accept either
shape, and the fixture reports `status=[]`.

The `IllegalArgumentException: negative source region` came from the same run's
Sprite/TiledLayer paint path being reached with a buffer that had never been
sized, because the game thread was dying before the frame loop.  With the
thread fix and the RMS fix the fixture now completes:

    GAMEDEMO frames=90 checksum=e8cfe8b4 drawn=76800 refsum=460
             frame=2 transform=0 saved=-1 status=[]

`drawn=76800` is 240x320, i.e. every pixel of the off-screen image the game
rendered into is non-background.

# OPEN (next): the game's frame never reaches the framebuffer

Status: unfixed, and narrowed.  The MIDlet itself is now correct - it runs all
90 frames and reports `drawn=76800` (every pixel of its off-screen image
differs from the background) - but the frames the headless port dumps are
uniformly black, so the last hop into `nj_midp_screen(vm)` loses the picture.

## Ruled out

- **The two buffer accessors agree.**  `canvas_ensure_buffer()` in
  nj_lib_lcdui.c:2897 and `gc_buffer()` in nj_lib_game.c:2473 both read and
  write `NJ_F_C_BUFFER` on `NJ_C_CANVAS`, and both size the buffer from
  `nj_midp_screen(vm)` when the Canvas does not know its size yet.  They are
  looking at the same Image, so "the game draws into an orphaned buffer" is not
  the explanation.
- **`nj_midp_paint()` is not obviously at fault.**  Its Form/List/TextBox/Alert
  path is covered by tests/test_lcdui.c, which asserts the framebuffer ends up
  with >= 3 distinct colours and a drawn title bar.  Only the GameCanvas branch
  differs, and that branch does blit `buf -> scr`.
- **`Graphics.drawImage` is fixed.**  The "negative source region" that used to
  kill the game thread was `drawImage()` reading `drawRegion()`'s argument
  list; the LCDUI module now has a separate native for it.

## Where to look next

The frame order per tick is: runner calls `nj_midp_paint()` (blits cBuffer to the
screen), then the scheduler runs the game thread (which draws into cBuffer and
calls `flushGraphics()`).

1. Verify `gc_getGraphics()` really targets cBuffer: print the `nj_ref` returned
   by `gc_buffer()` and the value of the `cBuffer` field at the moment
   `nj_midp_paint()` runs.  If they differ, the game is drawing into a second
   Image and the flush is the only thing that ever writes the screen.
2. Check `flushGraphics()`'s `gc_flush_region()` destination.  If it copies into
   a *stale* `nj_surface` captured before the display was initialised, it would
   write somewhere harmless and the next `nj_midp_paint()` would overwrite the
   screen with an empty buffer - which matches the observation exactly, and is
   the single most likely cause.
3. Confirm the Canvas `cWidth`/`cHeight` seen by `gc_flushGraphics()` are the
   screen size (240x320) rather than 0, in which case the fallback path in
   `gc_flushGraphics` is what runs.

## Reproducer

    ./tools/make_game_jar.sh
    ./build/nj2me --port headless --heap 4 --dump-frames /tmp/f/ --dump-every 200 \
        tests/fixtures/gamedemo.jar
    # expect: GAMEDEMO frames=90 checksum=... drawn=76800 status=[]
    # actual: /tmp/f/*.ppm are all 000000

The fixture's report line is the discriminator: `drawn=76800` proves the game's
own rendering is complete, so anything wrong after that is the final blit.
