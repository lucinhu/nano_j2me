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

---

# DONE: the framebuffer disconnect

Fixed in commit `290fe0f`.  `nj_midp_screen()` had two defects:

1. it called `port->display_init()` as if it were a way to *ask for the screen
   size*, then discarded the framebuffer the port had just published and
   allocated a private one;
2. its already-initialised guard tested `screen_pixels`, which is only non-NULL
   when the runtime owns the buffer - so with a port-owned framebuffer the guard
   never fired and the second caller allocated a private buffer *over* the
   first, leaving the runtime and the port looking at different memory.

Both are gone; the runtime now draws into the port's own framebuffer.

# RESOLVED: Tumbleweed (tests/games/jump.jar) runs at its supported sizes

The "Screen too small" Alert was the game's own size gate, not a runtime fault,
and the support range is real: the MIDlet rejects both a small and a large
handset.  Reproduced at four sizes under the headless port (240x320 default and
176x208 host snapshot; 176x220 and 128x160 additionally):

| size     | result                                                      |
|----------|-------------------------------------------------------------|
| 128x160  | game's own Alert: `java.lang.Exception:Screen too small`     |
| 176x208  | game runs: countdown ticks 0:57 -> 0:55, tumbleweeds animate |
| 176x220  | game runs: countdown 0:57, score -2, tumbleweeds animate     |
| 240x320  | game's own Alert: `java.lang.Exception:Screen too large`     |

The refusal text is drawn by the built-in LCDUI Alert renderer, wrapped inside
the panel (see the next section), so the "full-stack result" of the original
observation is confirmed - only the interpretation ("it refuses at every size
tried") was stale.  FreeJ2ME-Plus on the same host shows the identical split:
`Screen too large` at 240x320 and the running game at 176x208.  The earlier
"refuses at 176x208" reading predates the thread-lifecycle, paint and scheduler
fixes that made the game's own game thread run (see the closed jump.jar entry
below).

Evidence: `.local-tools/debug-runs/jump-sizes/{128x160,176x208,176x220,240x320}`
(headless, PNG frames at 120/240) and `.local-tools/debug-runs/jump-ref`
(FreeJ2ME-Plus reference shots at 240x320 and 176x208).

# RESOLVED: Alert text does not wrap

`ui_text()` in src/midp/nj_lib_lcdui.c wraps every built-in panel's body text
at the width it is given, breaking at the last space that fits and hard-breaking
inside a word longer than a line.  All four widgets (Alert, Form, List,
TextBox) go through it, so the fix covers the whole built-in UI rather than the
Alert branch alone.  The same renderer is what draws Tumbleweed's
`java.lang.Exception:Screen too large` message onto two lines today.

Regression: `tests/test_lcdui.c` renders a one-line Alert and a three-line
Alert (same constructor, same `AlertType`) and requires the wrapped body's
vertical text span to be more than twice the one-line span.  The check fails
(3 assertions, one per round) when the wrap is disabled, which is the old
"draw the string in one run" behaviour.

---

# Round 9: four bugs fixed, one narrowed; game corpus added

## Fixed

**`Canvas.repaint(x,y,w,h)` threw on a negative width or height.**  MIDP clips
the rectangle and does nothing when the result is empty; it does not reject it.
Games rely on that - a sprite that has left the left edge produces a negative x
- and throwing fails a game that is behaving correctly, in `startApp()`, where
the developer has no idea which call is at fault.  hello.jar and maze.jar both
died on this.

**`java.util.Random`'s no-argument constructor read below its own argument
array.**  `rnd_init` built a local `nj_value[1]` and passed it to
`rnd_setSeed`, which reads its receiver through `NJ_THIS(args)` == `args[-1]`.
A local array has nothing below it, so this is a stack-buffer-underflow - ASan
named it exactly, and it appeared in the wild as an intermittent SIGSEGV in the
-O3 build and nowhere else.  `rnd_init` now writes the seed itself, which is
also correct because a no-argument constructor has no `args[0]` to read a seed
from.

**The paint-target storage was on `Canvas`, so nothing that is not a Canvas
could be composed.**  The frame painter called the Canvas-scoped accessor for
Form, List, TextBox and Alert too.  On those the field lookup fails, the write
goes nowhere, and the painter allocates a fresh screen-sized image *every
frame* - 307 KB per frame at 240x320x4.  The heap thrashes, `img_alloc`
eventually fails, `nj_midp_paint` returns an error and the displayable is never
presented: a blank screen with no exception anywhere.  The three fields now live
on `Displayable`, which Canvas, Form, List and TextBox all inherit.  Every unit
test rendered once or twice and so passed throughout.

**A pending exception was never cleared between frames.**  Nothing outside the
interpreter resets `vm->exception`, so one throw in a paint path stayed pending
forever: the next frame's field lookups failed, `nj_midp_screen()` returned
NULL, paint bailed out before presenting, and the screen froze on its last image
with nothing printed.  `nj_midp_frame` now reports it once per frame and clears
it, so a game that throws in `paint()` is broken *visibly*.  This is what turned
maze.jar's silent freeze into a message.

## Added

`tools/run_game_corpus.sh` - a compatibility gate over a directory of JARs.  It
runs each headless, dumps frames and fails on: not a MIDlet suite, could not
start, an uncaught exception, nothing ever presented, or a single flat colour.
It *reports* whether the picture changes; that is not a failure on its own
because a title may be legitimately static (hello.jar draws "Hello World!" and
never changes a pixel), so `--strict-motion` makes it fatal when the corpus is
known to be animated.  Current result: hello PASS (static), jump PASS,
maze FAIL.

## OPEN: maze.jar throws AbstractMethodError from a virtual `paint` dispatch

`maze.jar` starts, and the first frame reaches its canvas, but
`MazeCanvas.paint` is then reported as abstract:

    exception escaped into the frame loop: java.lang.AbstractMethodError: paint

Narrowed already:

- `MazeCanvas` **does** declare `protected void paint(Graphics)`, and
  `nj_method_lookup(MazeCanvas, "paint", "(Ljavax/microedition/lcdui/Graphics;)V")`
  returns it correctly:
  `owner=net/frog_parrot/maze/MazeCanvas code=<non-NULL> flags=0004 nmeth=11`
  (flag 0004 is ACC_PROTECTED, not ACC_ABSTRACT).
- So the throw does not come from `canvas_call_abstract` (whose own check is
  satisfied); it comes from the interpreter's `invokevirtual`/`invokeinterface`
  path, which raises the same error when the *inline cache* hands it a method
  with no code and ACC_ABSTRACT set.

Next step: make the interpreter's abstract-method error carry the call site.
`exception_method`/`exception_pc` are only set in `do_unwind`, and this
exception is raised from the dispatch itself, so the message has no location.
Setting them there (`vm->exception_method = m; vm->exception_pc = op_pc;`) would
name the calling method and instruction, which is the missing half of the
diagnosis.  The likely cause is a stale `ic_target` on a call site that
dispatched `paint` against a different receiver class earlier.

---

# Round 4: the reference port had a no-op sleep and the wrong clock

Both were in `src/port/posix/nj_port_headless.c`, both invisible until
`--stats` and `--run-frames` made a run measurable.

**`hl_sleep_ms` did nothing.**  `(void)ms;` and return.  That silently
disabled the frame limiter on the reference port: `--fps 60` was parsed,
stored, passed to the VM, and then ignored, so a "60 fps" run presented 1739
frames a second.  The visible symptom was not the frame rate at all - it was a
game whose background thread never got scheduled.  `jump.jar` sleeps 50
emulated milliseconds between updates; the wall clock reached the end of a run
before the thread woke twice, so the picture never changed while every counter
said the game was running.  It now calls `nanosleep`.

**`hl_millis` used `clock()`, which is CPU time, not wall time.**  For an
emulated device that is the wrong clock in a way that breaks everything
downstream: a guest thread inside `Thread.sleep()` consumes no CPU time, so the
guest's clock barely advanced while it slept, and `System.currentTimeMillis()`,
the frame pacer and every timeout in the runtime were measured against a clock
that stops when the emulator is idle.  A game's own countdown ran at whatever
rate the host happened to be busy.  It now uses `CLOCK_MONOTONIC`, with
`_POSIX_C_SOURCE` defined unconditionally at the top of the file - guarding that
macro on `NJ_USE_LIBC` does not work, because `NJ_USE_LIBC` comes from a header
included further down.

After both fixes the limiter is honest:

    --fps 60   frames=60  time=0.988s  fps=60.7

# RESOLVED: jump.jar's game thread exists and animates under the headless port

The single-thread observation was a symptom of the pre-fix thread lifecycle (a
`Thread.start()` whose `run()` resolved to a native killed the thread instantly,
and a game thread that was never created logged nothing).  After those fixes
the headless port shows the game's own thread running: identical frame stepping
under `--run-frames` changes the countdown (0:57 -> 0:55) and moves the
tumbleweeds, with score changes when they are hit.  Evidence:
`.local-tools/debug-runs/jump-sizes/176x208/frames/{00120,00240}.png`.

What remains genuinely open from that observation is only the test-cost question
in the two follow-up notes at the end of this file; the game itself is fine.

# OPEN (next): the MIDP stub generator drops `throws`

Status: diagnosed, not fixed.  Found by building a real third-party game from
source for the first time (`tools/make_bubblet_jar.sh`).

`tools/midp_stubs.py` takes a method declaration as free text and emits a stub.
It parses out the modifiers, the return type, the name and the parameter list -
and then drops everything after the closing parenthesis, which is where a
`throws` clause lives:

    input   "public static void deleteRecordStore(String) throws javax.microedition.rms.RecordStoreException"
    output  public static void deleteRecordStore(String p0) { throw new RuntimeException("midp stub"); }

That is not cosmetic.  A stub without the throws clause makes a real game
uncompilable, because javac rejects a `catch` for an exception the try body
cannot throw:

    HighScoreManager.java:125: error: exception RecordStoreFullException is never
    thrown in the corresponding try block

and catching RecordStoreFullException is what every game that saves a high score
does.  The stub file is the contract a game is compiled against, so a stub that
is not faithful to the real API rejects working games - which is the opposite of
what a compatibility test is for.

The fix is in `emit()` in tools/midp_stubs.py, around line 695: keep the text
between the closing paren and the end of the declaration and emit it after the
parameter list.

Three other stub-fidelity bugs were found by the same build and ARE fixed:
MIDlet.startApp/pauseApp/destroyApp were missing their
`throws MIDletStateChangeException` (real MIDP has it, and a MIDlet that
declares it cannot then override them); `javax.microedition.midlet.
MIDletStateChangeException` was not in the stub list at all; and JSR 211's
members were emitted without `public`, so a game in another package could not
call them.

# What the Bubblet build is for

`tools/make_bubblet_jar.sh` fetches Summeli/Bubblet-Asha (GPL v2, ~900 lines, a
Nokia SDK 2.0 project) and builds it with these stubs as the compile classpath.

A prebuilt JAR only proves the runtime agrees with whatever javac emitted years
ago.  Compiling a game HERE proves something stronger: that the API surface this
runtime implements is the one javac resolves against, so a missing or
differently-described method is a compile error rather than a
NoSuchMethodError found later by a player.

The game also needs `com.nokia.mid.ui.VirtualKeyboard`, which is now stubbed,
and `javax.microedition.content` (JSR 211), for which `src/midp/nj_lib_content.c`
is a deliberate, documented stub: `getRegistry()` returns a real Registry so a
game's field is not null, `invoke()` throws ContentException naming the reason,
and a game that wraps the share in try/catch - which every real one does - keeps
playing.

---

# RESOLVED, and it was never a bug: the tan is RGB565 quantisation

**The picture was correct.  I was wrong, and the way I was wrong is worth
keeping.**

## What the colour actually is

Slime Volleyball's `drawScore()` paints a wooden scoreboard across the whole
lower half:

    graphics.setColor(0xBDA27E);
    graphics.fillRect(xStart, getHeight() / 2, getWidth() / 2, getHeight() / 2);

    // BUG in the original: width and height are both getWidth()/2 and yStart,
    // but the effect is the whole lower half.

`0xBDA27E` is (189, 162, 126).  The screen is RGB565, which TRUNCATES:

    189 >> 3 = 23, 23 << 3 = 184
    162 >> 2 = 40, 40 << 2 = 160
    126 >> 3 = 15, 15 << 3 = 120

so it is displayed as (184, 160, 120) - exactly the value I spent a round
calling unexplained.

## The mistake, precisely

I searched the game's source for `0xB8A078`, the colour I OBSERVED.  The source
says `0xBDA27E`.  **Searching for a display-space colour in source-space finds
nothing, and "it is not in the source" is then a false premise** - one I built a
chain of ruled-out hypotheses on top of, and reported to the user twice as "the
picture is wrong".

The step that should have caught it came earlier: I counted the game's seven
`fillRect` calls and never once looked at what COLOUR each paints.  The seventh
is the answer, and it was in output I had already printed.

## What was still worth doing

The probe that settled it: a MIDlet extending GameCanvas that takes
`getGraphics()`, draws ONE rectangle, and never calls `flushGraphics()`.
Everything else in the frame is then whatever the runtime supplies for the
unpainted area:

    red rectangle drawn:      64 pixels
    everything else:          (248, 252, 248), 36544 pixels

Near-white, as MIDP requires of a GameCanvas buffer.  That is a real result:
it proves the runtime's unpainted background is correct AND that drawing through
`getGraphics()` without `flushGraphics()` is presented, which is the unusual path
this title exercises and nothing else in the corpus does.

## The rule this leaves behind

**Convert an observed colour back through the screen's pixel format before
looking for it in the source.**  On an RGB565 screen a source colour is not the
colour you see, and the difference is not small: 162 became 160, 189 became 184.

---

# Reference: jelatine's threaded interpreter (and what else is worth reading)

Studied <https://github.com/tekknolagi/jelatine> after the replicated-dispatch
work stalled.  **It is GPL v3**, and this project's licence is not settled, so
nothing may be COPIED from it - but the design is readable and the design is
what was needed.

## The one thing that unblocks the stalled optimisation

jelatine runs the same handler source in both modes through two macros
(`src/jelatine/interpreter.h`):

    #if JEL_THREADED_INTERPRETER
    #   define OPCODE(x)  x##_label:
    #   define DISPATCH    goto *dispatch[*pc]
    #else
    #   define OPCODE(x)  case x:
    #   define DISPATCH    goto dispatch
    #endif

    OPCODE(NOP)
        pc++;
        DISPATCH;

**Every handler ends with `DISPATCH;`.  None of them ends with `break`.**

That is the whole reason their transformation is clean and mine is not.  This
project's handlers end with `break`, and `break` is overloaded: one inside an
`if` still leaves the switch, one inside a `for` or a nested `switch` does not.
Brace counting cannot separate them (it reported 61 candidates one way and 35
the other, because the THROW/SAVE/CHECK_QUANTUM macros carry braces the counter
cannot see).

So the tractable path is NOT a 200-site big-bang rewrite.  It is:

  1. introduce `OPCODE(x)` and `DISPATCH` macros over the existing switch, with
     `DISPATCH` expanding to the current shared dispatch;
  2. replace the terminating `break` of ONE case at a time with `DISPATCH;`,
     compiling and checking that the benchmark still reports exactly
     112,117,067 bytecodes after each;
  3. flip the macro to the threaded form once every case is converted.

Step 2 is incremental and verifiable at every point, which is what the big-bang
version was not.  A case whose `break` was wrongly rewritten fails to compile
(no enclosing loop) or changes the bytecode count, and both are caught
immediately.

## Also worth knowing

* **`DISPATCH` has no bounds check** - `goto *dispatch[*pc]` over a 256-entry
  table.  Measured here at 0.5%, so the bounds check was never the win; the
  replication is.

* **`JEL_FP_SUPPORT`** is a real compile-time floating-point switch, which is
  exactly what `NJ_ENABLE_FLOAT` claims to be and is not (see
  docs/FOOTPRINT.md).  Worth reading for how the natives are guarded rather
  than for its code.

* **`src/preverifier/` is 13,194 lines** (`check_code.c` alone is 4,009).  This
  is the principled answer to a weakness this project has: with no verifier,
  every array reference read from an untyped operand stack has to be
  range-checked at run time, and the interpreter carries a per-instruction
  bounds check.  Adopting a verifier is far too large to be a next step, but it
  is the reason the defensive checks are there, and it is honest to say so.

---

# RETRACTED: "half of all malformed class files hang" was a broken measurement

**The real result is 36 clean, 0 crashed, 0 hung.**  The section below is kept
because the two mistakes in it are worth more than the finding was.

The tool ran the runtime WITHOUT `--run-frames`.  `nj_midp_run` loops until the
MIDlet exits, so a MIDlet that does not call `notifyDestroyed()` runs forever
**by design** - that is what a game does.  Unmutated `hello.jar` "hangs" under
exactly the same conditions, which is the control I should have run first and
did not:

    timeout 5 ./build/nj2me --port headless tests/games/hello.jar   -> rc=124

With `--run-frames 30` the same 36 mutations give 36 clean, 0 crashed, 0 hung.
A gdb backtrace (which needed ptrace to work - see below) showed the "hang"
sitting in `nj_midp_paint` -> `nj_region_draw`, i.e. a MIDlet painting frames,
which is the frame loop working correctly.

**This is the third measurement this round that did not measure what it
claimed**, and the pattern is the same every time: the instrument was never
checked against a case whose answer was already known.  The first fuzzer mutated
a class the runtime never loaded and reported "300 clean"; the constant check
compared the stubs against themselves and reported "ok"; this one had no frame
bound and reported a hang for normal behaviour.  All three agreed with
themselves.

**What survives from the work below**: the segfault is real.  A malformed Code
attribute let a method push past its own `max_stack`, the operand stack walked
into the NEXT frame's slots, and `putfield` dereferenced class bytes -
`obj=0x4e4a484800010000`, the ASCII "NJHH".  `nj_frame.stack_limit` and the
per-instruction check fix that, and the check costs less than the measurement
can resolve.

## The original (wrong) write-up, kept for the two mistakes in it

Status: one mechanism fixed, several remain.  Found by
`tools/fuzz_classfile.py`, which mutates the class the manifest names and
watches what happens.

## What the fuzzer does, and the first version that lied

It mutates the MIDlet's OWN class - the one the runtime is guaranteed to load -
and classifies each run as clean, crashed, or hung.  Timing out at six seconds
counts as hung, because a malformed JAR that spins forever is a denial of
service and "reports an error rather than crashing" also means "does not hang".

The first version mutated a class the runtime never loaded: it wrote a JAR
containing only the mutated file, so the MIDlet itself was missing and every run
failed before reaching the mutation.  It reported **300 clean, 0 crashes** and
was worth nothing.  The same mistake as the constant check that compared the
stubs against themselves: a test that never reaches its subject agrees with
itself.

## The result

    36 mutations:  18 clean,  0 crashed,  18 HUNG

Half.  Every mutation kind is represented in the hangs - byte flips, method
counts, attribute lengths, field counts, trailing bytes.

## The one mechanism that is fixed

Reproducing a hang as a SEGFAULT under a debug build gave a precise stack:

    #0  nj_field_write_ref (obj=0x4e4a484800010000, ...)  nj_vm.c:785
    #1  nj_exec ... nj_interp.c:1295      (a putfield)

`0x4e4a484800010000` is ASCII "NJHH" - class-file bytes read as an object
reference and dereferenced.

The cause: a frame's operand stack was bounded only by the thread's `stack_end`,
never by its own `max_stack`.  A method that pushed past its own limit walked
into the NEXT frame's slots, and a corrupted Code attribute makes that happen
easily.  `nj_frame` now carries `stack_limit = stack + max_stack` and the
interpreter checks `sp` against it once per instruction, next to the `pc` bound
that was already there.  The malformed class now raises VerifyError.

Without a verifier this check IS the boundary between a malformed JAR and memory
corruption.  Measured cost, interleaved A/B: **below the noise floor** - the two
rounds disagreed about the sign, so it is free.

## What is still open

The other hangs.  They are not the operand stack - that one is fixed and the
hang count did not move - so they are somewhere else in class loading or in the
interpreter's handling of a Code attribute whose length disagrees with the
bytecode it contains.

Next step: take one hanging case, reproduce it under a debug build, and see
whether it faults or genuinely loops.  A hang that is a loop and a hang that is
a fault are different bugs and the backtrace tells them apart immediately.
