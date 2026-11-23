# Delivering the first paint() of a newly current Displayable

## Problem

`Skater_by_BFLM` (83,417 B, MIDP 1.0, `b extends com.nokia.mid.ui.FullCanvas`)
was black in the round-223 expanded-corpus libretro probe.  Its `b.paint()`
has two modes selected by the loader thread `Skater$a`:

- `Q == 0`: load `/res/img/cover.png` into field `new`, draw it (the splash);
- `Q == 4`: draw the title screen using images the loader created.

`Skater.startApp()` does `new b()`, `System.gc()`,
`Display.setCurrent(b)`, then starts the loader thread.  The loader calls
`b.int()`, which loads the four sprite sheets, creates the static `B` image
with `Image.createImage(II)` and sets `Q = 4`.

The runtime painted the canvas once per frame boundary, i.e. at the end of the
frame in which `startApp()` had run.  By then the loader thread had already
advanced `Q` to 4, so the `Q == 0` splash branch never ran, field `new` stayed
null, and every `paint()` threw
`NullPointerException at Graphics.drawImage(...): image is null`.
FreeJ2ME-Plus shows the splash because its event thread paints before the game
thread gets a timeslice.

## Fix

`Display.setCurrent()` now records a pending *first-paint* request for the new
Displayable (`vm->fresh_paint` / `vm->fresh_paint_ref`).  The frame pump
delivers that paint right after input handling and before the first
background-thread timeslice of the frame:

    if (vm->fresh_paint && vm->in_call_pump == 0 && vm->in_paint == 0 &&
        !vm->paint_active) { ... nj_midp_paint(vm); ... }

Nested pumps (a `startApp()` parked in a blocking native) keep the request
pending so it is never delivered in the middle of application code.  An
explicit `nj_midp_paint()` of the newly current Displayable (a title's own
`serviceRepaints()` straight after `setCurrent()`) consumes the request, so
the adaptive paint cadence of `test_paint_idle_saturation` is unchanged.
Nothing is delivered twice: the request is one-shot and frame-local.

## Evidence

- CLI, `--port headless --size 240x320 --heap 16 --fps 30 --run-frames 300`:
  `paints=301 gfx=2406`, zero null-image `NullPointerException`s, and the
  dumped frames at 100/200/300 all contain **8,064 non-black pixels** (the
  splash) instead of an empty black canvas.
- A/B against the previous binary over a 150-title corpus sample (300 frames
  each, per-row screen/heap/device/key script): **0 verdict changes**; 85
  titles shifted by a few paint()/gfx calls (the extra first paint), e.g.
  `Super_Bomberman` 16852 -> 10891 gfx calls with **pixel-identical** frame
  dumps at 100/200/300.
- Full serial CTest: **210/210 passed in 368.98 s**.
- Libretro mock matrix **20/20** and real RetroArch 1.22.2 LoopDemo 120 frames
  PASS with the rebuilt core.

## Remaining limitation (libretro)

The libretro load path pumps frames while `startApp()` is still executing so
that titles which paint loading screens from `startApp()` keep their display
alive.  In that arrangement the loader thread runs inside the same scheduler
pass that installs the canvas, so by the next frame boundary `Q` is already 4;
`lr_load()`'s grace loop then paints the canvas with the loader state, and
`Skater_by_BFLM` stays black through the core.  Fixing that needs the
first-paint request delivered at a scheduler thread switch (between two Java
quanta) rather than at a frame boundary, which the cooperative scheduler does
not currently expose as a safe paint point.  Documented as an open core-side
limitation; the title renders through the CLI/headless runner and in
FreeJ2ME-Plus.
