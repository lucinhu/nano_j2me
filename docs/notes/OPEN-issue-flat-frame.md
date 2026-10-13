# "flat frame" is two different failures, and one of them is not the game's fault

`tools/run_game_corpus.sh` reports

    FAIL (flat frame: the game is not drawing)

for 19 of the 238 collected titles.  That message is true and misleading at the
same time, because it is reached by two situations that need opposite fixes.

Instrumenting 宠物王国2紫_5550 (a 400-frame run with `--stats`, then the same
run with `--dump-frames`) gives:

    stats: frames=400 paints=400        <- paint() is called every single frame
    00800.ppm colours 1 [(0,0,0) x36608]  <- and the screen is pure black

So the frame loop, the display plumbing and the `paint()` dispatch are all
working.  Something inside the drawing calls, or between them, is losing the
picture.

## The picture is real, and it comes back when the game is poked

Dumping every frame of a fresh run shows the first ~14 frames are a correct
splash - 34 distinct colours, mostly near-white with a red title bar:

    frames: 400   first mostly-black frame: 0   last non-black: 27

(0 here counts the two surfaces of the double buffer, so "last non-black 27"
is about frame 13.)

Re-running with `--keys "10:fire,15:fire,20:fire,25:down,30:fire,..."` moves the
picture: the sampled frame at index 21 - that is, exactly the frame the first
`fire` arrives on - has the 34 colours back, and the frames either side of it
are black again.

That is the shape of a game that **repaints only when something happens** and
relies on the rest of the screen surviving between `paint()` calls.  On the
phones this title was written for it does survive: MIDP does not clear a Canvas
between paints, and 华娱无线's titles of that era are known to depend on it.

## Why this is written down instead of fixed

The obvious suspect - a per-frame clear before `paint()` - is not what is
happening.  The only whole-surface clear in this file is in
`displayable_render()` (`src/midp/nj_lib_lcdui.c:3958`), which runs for the
non-Canvas Displayables (Alert, List, TextBox, Form).  The Canvas path
(`canvas_call_paint_rooted()` -> `canvas_visible()` -> `canvas_call_paint()`)
does not clear anything.

So the black is coming from somewhere between "the game called
`drawImage`/`drawString`" and "the port presented the buffer", and the honest
state of this investigation is: **mechanism named, cause not yet located**.
Whoever picks this up next should start by watching a single frame's draw calls
land in the surface - the counters say the calls are made, so the question is
what happens to the pixels afterwards.

The lesson that does generalise: a corpus verdict of "flat frame" cannot be
read as "the game does not draw".  For this title it draws correctly for the
first 14 frames and again on every keypress.  Any fix aimed at the wrong one of
those two situations will look like it should work and will not.

## Update: the same title, one screen size later

The counters above were taken at 176x208 - the first entry in `SIZES`, and the
size the corpus settles on because 宠物王国2紫_5550 accepts it.  Running the
same binary at each size in turn, last frame of a 400-frame run:

    size        frames presented   colours in the last frame
    176x208     800                 1
    240x320     800                 28
    128x160     800                 1
    320x240     800                 3

No size is rejected; the title simply draws at 240x320 and does not at the
others.  So "the picture is real, and it comes back when the game is poked" and
"the screen is pure black" were both true observations of the same game at
different sizes, and the corpus's habit of stopping at the first size that
merely *starts* was manufacturing half of the failure list.

`tools/run_game_corpus.sh` now tries the next size whenever the verdict is
"flat frame" or "could not start", and takes the first size that actually
draws.  `tools/check_corpus_size_retry.sh` pins the behaviour down with a
fixture that draws two colours above 200 pixels wide and one below it.

What this does NOT establish: whether the game draws nothing at 176x208 because
it lays itself out for a screen it was built for, or because this runtime
mis-lays-out at that size.  The two look identical from the outside - one
paints, the other does not - and telling them apart needs a probe inside that
game's own paint path, at 176x208, watching where its draw calls land.  The
corpus fix stands either way: it no longer reports a size-dependent title as a
title that cannot draw.

## Update: the failure list splits in two, and `gfx=` does the splitting

`--stats` now reports `gfx=`, the number of `javax.microedition.lcdui.Graphics`
method calls (counted once per call in `gfx_load()`, not per pixel).  On the
three titles that were still flat at every screen size:

    title                        frames  paints  gfx
    都市摩天楼中文版_3219         400     400     0
    模拟人生3中文版_6162          400     400     0
    合金弹头4(完美汉化版)_4053     400     400     1200

Two situations, two opposite investigations:

  - **gfx=0**: `paint()` runs every frame and calls nothing on the Graphics.  No
    drawing is being lost because none is being asked for; the game is waiting
    for something (asset, timer, input) and this is not a rendering bug at all.
  - **gfx=1200**: three drawing calls per frame are made and the frame is still
    one colour, so the pixels do not survive to the port.  This is the case the
    old single verdict could not distinguish, and the only one where "the game
    is not drawing" is a lie.

Neither number was available before this round, and the corpus verdict alone
cannot separate them - which is exactly the T2 complaint about measuring pixels
instead of state.

## Round 69: two hypotheses eliminated by experiment

The titles that still fail with `flat frame ... N drawing calls reached nothing`
(合金弹头4(完美汉化版)_4053, 24000 calls; 疯狂企鹅中文版_10228, 31993 calls)
call the drawing API thousands of times and present one colour.  Two candidate
explanations were tested rather than argued about.

### 1. The game's clip is not the cause

`nj_gfx_set_clip()` and `nj_gfx_clip_rect()` were temporarily made no-ops, so
the clip stayed at the whole surface that `nj_gfx_init()` sets, and nothing the
game asked for could clip anything away.  Same binary otherwise, same flags:

    A  normal build      120 frames   colours in the last frame = 1
    B  clip bypassed     120 frames   colours in the last frame = 1

Identical.  The clip is excluded.  (The patch was reverted and the file
compared byte for byte.)

### 2. An invisible colour is not the cause

Both `Graphics.setColor(int)` and `setColor(int,int,int)` are checked, because
they are the two ways a game can make every pixel invisible without one error
message: the integer form already forces `| 0xFF000000` with a comment saying
exactly why, and the three argument form goes through `nj_gfx_set_color_rgb()`.
Neither leaves alpha at zero.

### What is left

The drawing calls therefore reach a surface and put pixels somewhere that is not
the surface which gets presented, or the copy that would carry them is never
made.  The next experiment should follow one `fillRect` from the call to the
bytes of the presented frame, instead of testing more hypotheses in bulk.

## The presentation pipeline is verified working (2026-10-12)

For a while the corpus verdict for these titles read "N drawing calls reached
nothing", which invited the conclusion that the runtime was losing the pixels.
It is not: the pipeline was instrumented at a fixed pixel and moves data
correctly.

`nj_midp_paint()` hands `paint()` a Graphics bound to an off-screen buffer
(`canvas_ensure_buffer()` + `gfx_new(vm, buf)`) and blits that buffer onto the
screen itself. That is deliberate, not a defect, and it is why a probe that
watches the Graphics methods only ever sees image-bound draws.

Printed from inside the GameCanvas branch, at pixel (10,10), 疯狂企鹅中文版_10228
at 320x240:

    [probe] GC-BLIT img_view=1 buf(10,10)=0xffffffff
            screen before=0xffffffff after=0xffffffff
            scr=320x240 buf=320x240

The buffer holds the colour, the blit runs, and the screen ends up holding it.
So a flat frame means the buffer itself is a single colour: the game's own
drawing produced that, and the runtime delivered it faithfully.

Two consequences:

  * "flat frame" is not one defect and not necessarily a defect at all. The
    instrument now distinguishes "paint() was never called" (paints=0), "paint()
    ran but made no drawing calls" (gfx=0) and "drawing calls ran and produced
    this picture" (both non-zero), which is the useful part.
  * The corpus note said "reached nothing", which was wrong; it now says the
    calls "did not change the presented picture".

Still not established: whether a title that draws a single colour is doing that
by design at that screen size, or because our layout differs from the device it
was written for. Telling those apart needs a probe inside the game's own paint
path, not more counting at the boundary.

## The verdict itself was the next wrong thing

Two of the three "drawing calls reached nothing" titles turned out not to be
rendering defects at all, and the corpus was the reason they looked like ones.
合金弹头4(完美汉化版)_4053, measured with a probe that was removed afterwards
(`git diff --quiet src/midp/nj_lib_lcdui.c` against HEAD):

    at                       <- the current Displayable, 20 times (once per frame)
    stats: frames=20 paints=20 gfx=60

`at` is declared `extends javax.microedition.lcdui.game.GameCanvas`, so the
GameCanvas path ran, the runtime called the game's own `paint()` every frame,
and `paint()` made three Graphics calls per frame.  Decompiled, `at.paint` is a
`tableswitch` on a state code whose branches mostly fall straight through to an
exit that draws nothing — which is what three calls per frame looks like.  The
GameCanvas buffer is blitted to the screen and that blit had already been
verified end to end (`buf(10,10)=0xffffffff` before and after).

So the picture does not change because the game is sitting in a state where it
paints nothing, and the phrase "the game is not drawing" was a claim the corpus
had never measured.  The verdict now reports counts and stops interpreting:

    FAIL (flat frame even after the whole key set was sent; 40 Graphics calls
          over 20 paint() calls left the picture unchanged)

`tools/check_flat_wording.sh` (ctest, with the `tests/flatdemo` fixture) holds
it to that: a flat frame must carry both counts, and the old sentence fails the
check.

## 仙魔飞行棋_417 — measured, and it is not a performance problem

This title was on the failure list as `flat frame` with a large drawing-call
count.  Two measurements pin it down.

**It is not slow.**  240x320, `--heap 16`, `--stats`:

| frames | time | fps | gfx | bytecodes | heap (used/peak) |
|---|---|---|---|---|---|
| 1 | 0.036 s | 27.8 | 6 | 79,649 | 351,280 |
| 5 | 0.168 s | 29.8 | 30 | 325,436 | 356,112 |
| 10 | 0.334 s | 29.9 | 50 | 330,973 | 634,288 |
| 20 | 0.666 s | 30.0 | 90 | 333,263 | 635,888 |
| 40 | 1.330 s | 30.1 | 170 | 337,849 | 639,088 |

Forty frames in 1.33 s at exactly the 30 fps pace, a flat per-frame bytecode
count after frame 10, and a heap that stops growing: a fixed-length loop, no
accumulating structure.  (An earlier note here said this title "took longer
than expected" -- that reading came from a command line that omitted `--stats`,
so the line being looked for was never printed.  Withdrawn.)

**It draws about four times per frame and the result is one colour.**
`--dump-frames` over 40 frames writes 80 images: the first and the last have
exactly **1 distinct colour**, and only **2 of the 80** differ from each other
-- the picture changes once and then holds.  No exception is thrown in the run.

So the game calls `Graphics` about four times a frame, forever, and produces a
uniform surface.  That is consistent with its own state machine (or with a
resource it needs but never gets) and is **not** evidence of a runtime defect;
what is still unmeasured is *which surface* those four calls land on.  The
probe that answers that question is the one used for the other titles: print
the bound target's class in `gfx_load()`.

### Where those four calls land (probe run)

`gfx_load()` in `src/midp/nj_lib_lcdui.c` is the single place every `Graphics`
method passes through, so a one-line probe there (`nj_logf(vm, 0, "PROBE
bound-to=%s", nj_ref_class_name(target))`) answers the question for good.  Over
three frames of `仙魔飞行棋_417`:

    18 PROBE bound-to=javax/microedition/lcdui/Image

**Every call is bound to an Image, not to the screen surface** -- and that is
the design, not a bug: `nj_midp_paint()` hands `paint()` the Canvas buffer's
`Graphics` and then blits that buffer to the display itself.  Combined with the
dump above (one colour in the presented frames), the chain is closed:

* the game draws ~6 uniform operations per frame **into its own buffer**,
* the runtime blits the buffer faithfully,
* so the uniform picture is the game's own output.

No runtime defect is demonstrated for this title, and the probe was removed and
the worktree discarded after the run.

## 合金弹头4(完美汉化版)_4053 — the game's own table index, and byte-exact resources

This one is flat *and* draws: `--run-frames 1500 --stats` reports
`paints=1502 gfx=4506`, exactly **three Graphics calls per paint**, and the
dumps show a real nine-colour picture (white 248,252,248 and pink 232,48,104)
at 500 and 1000 and pure black at 1500 and after.  So the game paints a splash
and then paints black forever.

The corpus line names why:

    302 thrown: java/lang/ArrayIndexOutOfBoundsException
        at ad.a(Ljavax/microedition/lcdui/Graphics;IIIII)I pc=33

once per frame.  `javap -c -p ad.class` shows the failing instruction:

     27: aload         7        // the short[] from c()
     29: iload_2
     30: iconst_4
     31: iadd
     32: saload                 // index = local2 + 4
     33: istore        8

with `local2` rewritten at 20..26 as `3 + local2 * 7`, so the index is
`7 * (local2 + 1)` — a multiple of seven into a table the game holds in its own
`Object[] b` field (`c()` is `(short[]) b[0]`).  A short table is a game-side
problem *unless* we handed it short data, so that was measured next: a probe on
`Class.getResourceAsStream` printed every name and the length we returned, and
every one matches the JAR entry byte for byte —

    PROBE got 2.pak len=40908          (unzip -l: 40908)
    PROBE got loadingbar.uspac len=97  (unzip -l: 97)
    PROBE got menu.png len=6548        (and the other four PNGs)

— and no `EOFException` is thrown anywhere in the run, so the reads complete.
The resource layer is therefore not the cause: the game builds that table from
complete bytes and then indexes past its end.  What is still unverified is only
how our `DataInputStream` *presents* `2.pak` (the game parses it itself, so a
value read slightly differently would give the same symptom), and that is where
a future round should look next.

### The pak path, read instruction by instruction

That lead was followed and closed.  The game's reader is

    public static java.io.DataInputStream a(java.lang.String);
      aload_0; invokevirtual Object.getClass()Ljava/lang/Class;
      invokevirtual Class.getResourceAsStream(Ljava/lang/String;)Ljava/io/InputStream;
      new java/io/DataInputStream; dup_x1; swap; invokespecial <init>(Ljava/io/InputStream;)V

and the entry walk is `readInt()` for the length, `skip((long)len)` to step
over an unwanted entry, and `readFully(byte[])` for the wanted one.  Every one
of those was checked against the file itself:

* `2.pak` is `[int32 big-endian length][data]` repeated -- parsed independently
  in Python, entry lengths 2827, 6034, 5928, 3126, 836, 626, 700, 521 ... tiling
  the whole 40908 bytes.
* `dis_read_int` (`src/midp/nj_lib_io.c:904`) shifts in big-endian order.
* `is_skip` (`:120`) and `bais_skip` (`:299`) both read `NJ_ARG_LONG(args, 0)`
  and return `ret->j` -- the long is not mistaken for two ints, which is the
  trap this project's `tools/check_arg_slots.py` exists for.

So the runtime reads that file exactly as the format says.  The index the game
computes is its own, and it is out of range in **every** frame, not only during
the splash: `--dump-every 250` over a 3000-frame run shows nine real colours at
250/500/750/1000/1250 and one colour (black) from 1500 on, with the
`ad.a(...) pc=33` throw appearing throughout.

## 灌篮高手-爆发!樱木_7947 -- the game divides by the literal zero

The corpus line is `nothing was ever presented` with
`first throw: java/lang/ArithmeticException at w.paint(...) pc=91: / by zero`.
`javap -c -p w.class` shows what is divided:

    85: aload_0
    86: invokevirtual Canvas.getWidth:()I
    89: iconst_0
    90: idiv
    91: ...

`iconst_0` pushes the divisor, so this instruction sequence raises
ArithmeticException unconditionally on the first paint -- no device, ours or
anyone's, can present a frame from it.  **Not a runtime defect**: the bytecode
says what it says, and the same title is one of several in the corpus carrying
a "cracked" name.

## 模拟人生3中文版_6162 -- the NPE is a null receiver, and it is downstream

`--run-frames 40 --stats` reports `paints=40 gfx=0`: `paint()` runs forty times
and makes **no Graphics call at all**, so the frame is necessarily flat.

The corpus names `q.c(I)V pc=18`.  With this project's known pc off-by-one
(`fr->pc = pc` runs after the fetch), the failing instruction is pc=13:

     7: invokevirtual a:(I)Ljava/lang/Object;
    10: checkcast     // class javax/microedition/media/Player
    13: invokeinterface javax/microedition/media/Player.realize:()V
    18: return

`checkcast` on null **succeeds** (JVM spec, and this runtime implements it that
way), so "checkcast passed and invokeinterface threw NPE" has exactly one
explanation: the receiver was null, i.e. the game's own `a(int)` returned null
from a slot that was never filled.  Both runtime sides were read and neither
can throw NPE here:

* `player_realize` (`src/midp/nj_lib_media.c:1332-1379`) throws only
  `IllegalStateException` (state 0, closed) and `MediaException` (malformed tone
  sequence);
* `Manager.createPlayer` (`:1287-1303`) never returns null -- an unknown
  protocol is refused by name with `MediaException`.

The game catches that NPE itself and logs "SDKSound An error occured while
trying to prefecth the sound", so the flat frame is downstream of an earlier
init failure.  The next thing to find is that title's **first** throw: the
40-frame run raises none at all, and the corpus run (whole key set, 8000
frames) is the one that reaches it.

## The sweep was running the titles faster than any phone (2026-10-13)

A sweep runs uncapped (`--fps 0`): 600 frames of a light title cost a tenth of a
second of wall clock. That is what makes 395 titles finish in minutes, and it is
also a way to record a blank screen for a title that was never given time to
draw one.

`开心农场_240` (`com.auer.title.GameMidlet`, `happy_farm 1.30.04`) is the case
that proves it, because the two runs differ in nothing but the pace:

| run | bytecodes | gfx | colours |
|---|---|---|---|
| 600 frames, uncapped (0.075 s) | 1750 | 4 | 0 |
| 600 frames, `--fps 30` (19.9 s) | 48262 | 918 | 61 |

`--where` names the reason: the game's own thread is parked in

    af.a()V pc=30
    ab.b()V pc=60
    ab.run()V pc=11

and `af.a()` is

    while (!this.a) { d(); f(); a(this.a_Graphics); Thread.sleep(this.a_J); }

so the loop's pace is the game's `Thread.sleep`, and an uncapped host never lets
it come round. Nothing was wrong with the runtime, the pixels or the pipeline;
the harness was measuring a game that had run for 75 ms.

The same one-variable test on the rest of the flat-frame list separates the
titles that only needed time from the titles that really do not paint:

| title | paced 300 frames @30 fps | bytecodes |
|---|---|---|
| 开心农场_240 | 61 colours | 24088 |
| 纯爱物语3之梦想之翼_5112 | 69 colours | 856836 |
| 新三国志_10225 | 10 colours | 21150 |
| PC正版-使命召唤5_7199 | 61 colours | 7975847 |
| 反恐精英2正式版_5836 | 1 colour | 11132085 |
| 疯狂企鹅中文版_10228 | 1 colour | 3477701 |
| 疯狂农场2_2375 | 1 colour, **gfx=0** | 16557054 |
| 模拟人生3中文版_6162 | 1 colour, **gfx=0** | 2039905 |
| 仙魔飞行棋_417 | 1 colour | 397392 |

The bottom half is the same evidence as before, now with the harness
explanation removed: `gfx=0` means `paint()` asked for nothing (the game is
waiting for something of its own), `gfx>0` with one colour means it drew into a
surface that stayed uniform. The `Thread.sleep`-paced explanation is gone for
all of them, which is what made the earlier rows worth re-reading.

`tools/run_game_corpus.sh` carries the fix: any verdict that still has no colour
on screen after the long re-run is run once more at `NJ_PACED_FPS` (default 30)
for `NJ_PACED_FRAMES` (default 300 - ten seconds of the game's own time), and
the row records which pace it came from ("the picture appeared only when the run
was paced at 30 fps" / "still flat when paced at 30 fps"). In a twelve-title
check set that turned three FAILs into PASSes (纯爱物语3之梦想之翼_5112,
新三国志_10225, PC正版-使命召唤5_7199) and left the genuinely flat ones flat.

## 疯狂企鹅中文版_10228: the runtime's half is verified, the game is stuck (2026-10-13)

The row has read "flat frame ... 19200 Graphics calls over 300 paint() calls left
the picture unchanged" since the paced retry went in, and the first throw is
always `java/lang/NullPointerException at DCTuxedo.i()V pc=27`. What the runtime
supplies is not in question:

- A probe MIDlet injected into a copy of the JAR read every data resource the
  title ships (`p`, `l0_0`, `r2`, `r10`, `icon.png`) through
  `getClass().getResourceAsStream()` and reported the same length and the same
  FNV-1a hash as python's `zipfile` reads from the JAR - byte for byte, so the
  ZIP layer, the stream and the entry decompression all deliver what the file
  holds.
- Those blobs are the game's own format rather than PNG, JPEG or zlib (`l0_0`
  starts `00 00 13 3d`, which is the file's own length, and there is no format
  magic anywhere in them), so the decode is the game's Java code and nothing in
  the run throws from it.
- The title's current Displayable is its own fallback canvas: the whole of
  `f.paint(Graphics)` is `System.out.println("aaaaa")`, which is where the 1439
  `aaaaa` lines in the log come from.

What the game does instead of drawing: its loop thread runs `k.run()` into
`DCTuxedo.b(I)V pc=2017`, and `DCTuxedo.i()` throws at pc=27 on `a[C].a()` - `a`
is a `[Ld;` of 26 slots and `DCTuxedo.v()` deliberately nulls every slot except
4, 24, 25 and 21, expecting the state it is about to enter to have been built by
`DCTuxedo.a(Z)`, `n()`, `w()`, `y()` or `b()`. The NPE is caught inside `b(I)`
itself, which calls `printStackTrace()` every iteration (400 times before the
run's log limit), so nothing is ever reported as uncaught.

Three thousand frames paced at 30 fps - a hundred seconds of the game's own time
- leave the same single colour, so this is not the starved-sweep case either.
Either the game's flow never reaches the state factory it needs (its manifest
advertises `Media-Price: 单次下载12元`, and this family is where the
never-completing billing request lives), or the runtime breaks something in its
data decode that the game swallows. The next step is to read the state index `B`
holds when `i()` throws: it is a plain static field, so a debugger settles it in
one run.

### Resolved: it was the runtime after all (same day)

The "reads the state index `B`" experiment never happened, because the cause
turned out to be reachable from a different title with a field access to
compare against. 疯狂农场2_2375 lost a `static Vector b` whose superclass
declared an *instance* `Vector b`: resolving a Fieldref by asking for the
instance variant first linked its `getstatic`/`putstatic` to the instance
offset, and both accesses then did nothing at all - a static read off a NULL
receiver, a static write dropped on the floor - silently. `5919ad0` resolves a
field by name and descriptor through the class chain (JVMS 5.4.3.2) and makes
the interpreter refuse a static/instance mismatch by name instead of accessing
nothing.

That fix took this title, 模拟人生3中文版_6162 and 疯狂农场2_2375 off the flat
list in one sweep (384 passed / 11 failed, from 381/14). It is worth keeping the
two lessons: a field that goes missing is invisible in the game's own code, and
"the game is stuck in its own flow" was the wrong conclusion here twice - both
titles had a runtime bug that their state machines then reported as a null
somewhere else.
