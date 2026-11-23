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


## Port gate: synchronous-frame hosts only (0.5.37)

The whole-corpus libretro probe on the r56 build exposed a regression that the
CLI A/B sweep could not see: through the core, two Midnight Pool variants
(`午夜台球圣诞中文版_8677.jar`, `ChristmasMidnightPool_1.0.8_320x240.jar`)
turned black and refused serialization, while the r55 core passed and the CLI
rendered both.  The cause is the libretro load path itself: it pumps frames
while the MIDlet is still being constructed, so a first paint delivered at
frame entry can run before the title's own loader thread has initialized the
images its `paint()` uses (the same class of race the fix was meant to remove,
in the opposite direction).

The delivery is therefore gated on a new port capability,
`nj_port.first_paint_early`, set only by the headless/CLI port (a host that
runs `startApp()` to completion before it pumps frames).  Frontends with an
asynchronous loader leave it zero and keep the frame-boundary paint.  After the
gate the two Midnight Pool variants pass through the core again, `Skater_by_BFLM`
still renders in the CLI, and the core's Skater behaviour is unchanged (black;
the scheduler-level first-paint point remains open).

## Round 236: the libretro port gate is still required

The 0.5.39 clock work changed the failure modes enough to retest the gate:
temporarily setting `port.first_paint_early = 1` in `lr_port_init()` and
running the mock frontend on a 300-frame session gives:

- `Skater_by_BFLM`: still black (`non-black 0/76800`, one hash) and still logs
  `NullPointerException at Graphics.drawImage(...): image is null` on every
  paint.  The early paint does not land before the loader: by the time the
  core serves the first frame, the loader thread has already set `Q=4` and
  `b.paint()` takes the title-screen branch whose images are not ready.  The
  round-223 fix therefore cannot be enabled through the port flag alone; the
  core needs a paint point between Java quanta *during* the load sequence,
  which the async load pump does not provide.
- `午夜台球圣诞中文版` and `ChristmasMidnightPool`: render normally
  (`non-black` up to 7,682 px, 309/317 content frames, 20 hashes) and pass all
  45 checks - the round-229 regression was collateral damage from the
  load-clock epoch bug, not from the early paint itself.
- `MidnightPool1`: unchanged (`non-black` up to 8,348 px, 85 hashes, PASS).

The experiment was reverted (`git checkout -- src/port/libretro/nj_port_libretro.c`,
clean core rebuilt); the gate stays until a real scheduler-level paint point
exists.  Recorded as a negative result rather than enabling the flag, because
it buys nothing for `Skater_by_BFLM` and other asynchronous-load titles can
still depend on loader state before their first paint.

Why the obvious guard cannot simply be relaxed (round 237): in the core the
whole MIDlet construction (constructor plus `startApp()`) runs on the VM
thread inside the nested C-call pump, so `vm->in_call_pump != 0` for the entire
load sequence.  The frame-entry first-paint block requires
`in_call_pump == 0`, because painting from inside an active pump is not a safe
point (a synchronous paint there re-enters the frame machinery).  By the time
the pump unwinds and the guard would allow a paint, the title's loader thread
has already run - exactly the Skater case.  A correct fix therefore needs a
paint point *between Java quanta inside the pump* with the pump suspended, not
a relaxed frame-entry condition.

Round 238: frame sequence for Skater in the core (instrumented, reverted):
the core starts the MIDlet's `startApp()` as a scheduled VM thread inside the
first `nj_midp_frame()`, so `Display.setCurrent()` runs *after* that frame's
entry check but *before* the same frame's paint gate.  The scheduler then runs
the title's loader quantum in the same frame (the loader is a thread started by
`startApp()`), so by the time the frame paints, `Q` is already 4; the paint
takes the title-screen branch with `new == null`, throws, and the paint gate
consumes the pending first-paint request.  There is no moment at which the
frame-level delivery can run between `setCurrent()` and the loader quantum with
the current code: the request has to be served *inside* the scheduler, between
two Java quanta, before the loader thread first runs.  That is the scheduler
paint point this note asks for; the frame hook relaxation
(`in_call_pump == 0 || in_frame_hook`) and the early port flag were both tested
this round and changed nothing for Skater (still 0 content frames), while
MidnightPool1/ChristmasMidnightPool stayed correct.  Both experiments were
reverted (`git checkout -- src/midp/nj_lib_midlet.c src/midp/nj_lib_lcdui.c
src/port/libretro/nj_port_libretro.c`, clean core rebuilt).

A third variant was tried in round 238 and also rejected: paint the new
Displayable synchronously at the end of `Display.setCurrent()` when
`in_frame && in_paint == 0 && !paint_active && in_callback == 0 && in_call_pump == 0`,
gated by an env switch for the async ports.  It changes nothing for
Skater_by_BFLM (still 0 content frames) and leaves MidnightPool1 /
ChristmasMidnightPool correct, i.e. the guard conditions are never met while
`startApp()` is running - the construction path owns a call pump for its whole
duration, which is why "paint from the app thread" is not a safe substitute for
the scheduler-level point.  Reverted.

A fourth variant (round 238) put the paint between Java quanta: a
`nj_midp_serve_fresh_paint(vm)` exported from the MIDP layer and called from
`nj_scheduler_frame()` before each `nj_scheduler_step`, env-gated
(`NJ_FP_SCHED`).  It also leaves Skater_by_BFLM black (0/317 content frames,
same null-image NPE) while MidnightPool1 and ChristmasMidnightPool stay
correct.  Combined with the earlier traces this shows the request is not
pending at any scheduler-frame boundary the current load path exposes: the
`setCurrent()` that installs the canvas happens inside the asynchronous
`startApp()` execution, and by the time control returns to the frame loop the
title's loader has already advanced its state.  Fixing this needs the load
path itself to expose a frame boundary *inside* `startApp()` (or an event-thread
model), which is a larger change than a local paint hook.  Reverted; the
core-side limitation stands documented.

## Round 241: where the pending request actually goes

A fifth approach (post-`startApp` paint from the libretro port) exposed why
none of the local hooks can work: `nj_vm_start_app_async()` only *prepares*
the `startApp()` frame - it does not run it.  By the time the port's load
path continues, no Displayable exists yet (`fresh_paint = 0`).  `startApp()`
itself runs later, inside the first frame's scheduler quantum.

Tracing `fresh_paint` writes inside that quantum showed the whole sequence in
one step: `Display.setCurrent()` sets `fresh_paint = 1`; its synchronous
`sizeChanged()` delivery makes the title call `repaint(); serviceRepaints();`,
and that in-quantum paint consumes the request *before* the scheduler quantum
ends, so neither a frame-entry hook nor a `nj_scheduler_frame()`-entry hook
ever observes it pending.

That in-quantum paint is also the splash paint: it runs before the loader
thread's first quantum, when Skater's `Q` is still 0, which is exactly the
moment the title's `/res/img/cover.png` branch expects.  Instrumenting
`Image.createImage(String)` showed the cover is never requested, i.e. the
paint returns without running the Canvas `paint()` body (the canvas is not
considered shown yet).  Every later paint is the `Q == 4` title branch drawing
an uninitialised image -> the NPE.  So the remaining gap is visibility
semantics for the synchronous first paint, not request delivery; forcing that
paint to run is a wider change than the paint hooks tried so far and was left
open.  Experiment reverted; no code change kept this round.

## Round 248: the show request is serviced between bytecode quanta (async-load ports opt in)

The libretro-only gap is closed without a port flag.  Instrumentation of the
core load path showed the real ordering: `startApp()` calls
`Display.setCurrent()`, which posts the first-paint request, and the **same
scheduler pass** then runs the loader thread that `startApp()` started.  The
loader advanced `Q` to its title-screen mode before the frame paint gate ran,
so `Skater_by_BFLM` never created its splash cover image and every paint threw
`NullPointerException` on a null `Image`.  Every earlier attempt (frame-entry
delivery, `setCurrent()`-time synchronous paint, scheduler-entry hook, an
early flag for the libretro port) looked at a point either before the request
existed or after the loader had already run.

The fix is a port capability, because only hosts that load MIDlets inside
their own frame pump (the libretro core) need this ordering; the CLI/headless
runner keeps its historical interleaving:

* `include/nj2me/nj_port.h`: new `show_before_background` flag; the libretro
  port sets it in `lr_port_init()` (`src/port/libretro/nj_port_libretro.c`),
  the headless runner leaves it zero.
* `src/core/nj_thread.c`, `scheduler_step()`: when the flag is set, a pass
  stops after the current thread's quantum if `vm->fresh_paint` is pending, no
  nested C pump is active and no paint is in flight, so the splash `paint()`
  runs before the loader thread's next quantum.
* `src/core/nj_thread.c`, `nj_scheduler_frame()`: the frame loop also leaves
  when the request is pending, so the frame paint gate runs in the same frame.
* The gate keeps the CLI contract unchanged: without it, the timer-fairness
  test and the Street Fighter loader fixture observed the new interleaving and
  failed, which is why the capability is opt-in rather than global.

Evidence:

| Check | Result |
|---|---|
| `Skater_by_BFLM` through the mock libretro frontend (300 frames) | **PASS**, 47/47 checks, 7,480-8,004 non-black px, 47/47 content frames (was 0/76,800 and `FAIL visual_nonblack`) |
| targeted A/B (10 titles, old dist core vs fixed core) | same PASS verdicts (TheMummy still the documented black/handshake case); NeonRunner shows a transient black first frame (313/317 content vs 317/317) and otherwise identical |
| 150-title CLI A/B (ungated experiment, `tools/sweep_compat.py`, 600 frames, 18 jobs) | 0 verdict changes, identical frame counts; the shipped gated build leaves CLI scheduling unchanged, so the CLI A/B is baseline-identical by construction |
| full serial CTest | **210/210 passed** |
| libretro mock matrix | **20 passed, 0 failed** |
| whole-corpus visual probe (1,664 titles) | **1,659 passed / 1 known limitation / 4 failed** (`TheMummy_K700F`, `PAC-MAN (176x220)`, `assault`, `Asphalt4EliteRacing`); `Skater_by_BFLM` is in the passing set |

`TheMummy_K700F` is not fixed by this: it needs the opposite observation
(`isShown()` must still be false after `setCurrent()` until an event-thread
show), which is a real event loop rather than an ordering boundary.
