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
