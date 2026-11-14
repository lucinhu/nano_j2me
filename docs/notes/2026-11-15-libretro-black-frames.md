# Libretro black frames: three corpus root causes

The first full-corpus libretro visual probe
(`tools/check_libretro_corpus_visual.py`, 373 sweep-ok titles at 120 frames)
reported 14 failures. Six of them presented a completely black picture; this
note records the three distinct causes found and the fixes that removed five of
them.

## How the failures were found

```sh
NJ_LIBRETRO_HARNESS=/var/tmp/nj2me-lrstate-build/test_libretro_frontend \
NJ_LIBRETRO_CORE=build/libretro-dist/linux-x86_64/nano_j2me_libretro.so \
python3 tools/check_libretro_corpus_visual.py .local-tools/debug-runs/compat-20261115-r9 \
    --frames 120 --jobs 10
```

The mock frontend requires a non-black, animated frame (`--visual-required
--static-ok --min-nonblack 8`) and also runs the save-state round trips, so a
single invocation separates "renders" from "survives states".

## Cause 1: a stranded paint waiter inside `callSerially`

`都市赛车6火线追击_2230` halted with `all threads are waiting; assuming the app
is done`. The only VM thread was `NJ_THREAD_WAITING` with
`wkind=NJ_WAIT_NONE`, `wmon=NULL`, `wdeadline=0` parked in `h.run`, while the
VM reported `paint_active=1`, `paint_owner=NULL`, `npaint_waiters=1`.

`nj_midp_dispatch_serially()` (`src/midp/nj_lib_lcdui.c`) sets `paint_active`
for the callback batch but never sets `paint_owner` and never wakes paint
waiters on exit. The title runs its loop in a `callSerially` Runnable and calls
`Canvas.serviceRepaints()` from it; the guard in `c_serviceRepaints` sees
`paint_active && paint_owner != th` and parks the main thread for a paint that
never completes (a serial dispatch is not a paint), so the scheduler declares a
deadlock and the VM halts.

Fix (`src/midp/nj_lib_lcdui.c`):

* `nj_midp_paint()` wakes `nj_vm_paint_done()` for every outermost attempt,
  including a failed one - a failed paint must not strand waiters.
* `nj_midp_dispatch_serially()` sets `paint_owner` to the current thread for
  the duration of the batch and calls `nj_vm_paint_done()` when it was the
  outermost holder.

Result: `都市赛车6火线追击_2230` renders (non-black 54%) and `拳王争霸_11585`
renders every frame (85%).

## Cause 2: `Displayable.getWidth()`/`getHeight()` returned 0 before `show()`

`生化危机之恶化_8448` stayed black although its MIDlet ran. `javap` on
`c.class` showed `d.<init>` calling `Displayable.getWidth()` and passing the
result to `Image.createImage()`, while `l()` parsed `displayWidth : 0` from its
configuration. `d_getWidth()`/`d_getHeight()` returned the per-canvas fields,
which are zero until the canvas is shown; unlike `Canvas.getWidth()` there was
no fallback to the display size. The game therefore created 0x0 images and
unwound repeatedly.

Fix: new `displayable_width()`/`displayable_height()` helpers in
`src/midp/nj_lib_lcdui.c` return the canvas field when positive, otherwise fill
it from `nj_midp_screen(vm)` and return that (what FreeJ2ME and real phones
do). The CLI now prints `displayWidth : 240` / `displayHeight : 320`, and the
title renders non-black 25.8% by frame 300.

## Cause 3: the libretro displayable grace was shorter than a slow loader

`拳王争霸_11585` renders non-black when the libretro port's startup grace in
`lr_vm_thread()` is raised from 60 to 600 iterations: the title's background
loader needs more scheduler quanta than 60 before installing its first
Displayable. The grace loop runs unpaced (the frame hook drives frames), so 600
iterations cost little wall time. The desktop runner keeps its
`NJ_NO_DISPLAY_GRACE` value of 60 because `nj_vm_start_app()` there runs to
completion before the grace loop starts.

## Also bounded: clock assistance

While diagnosing a residual save failure the load clock assistance introduced
with the load-after-reset fix was found to be unbounded: a frame that spins on
its own loop could advance the virtual clock arbitrarily far while a request
was pending. `lr_idle()` and `lr_frame_hook()` now share the existing
`LR_STATE_CLOCK_MAX_FRAMES` (300) budget for both loads and saves, so a paused
frontend can never fast-forward the guest without limit. The load-after-reset
case still passes (`电子宠物蛋_11972`).

## Result after the fixes

Same probe at 300 frames (the tool's new default) against the fixed core:
**373 titles, 362 passed, 11 failed**. The five black screens caused by the
three bugs above render; the remaining failures are not visual:

* `火影忍者汉化版_10735`, `魂斗罗_5652`, `午夜台球圣诞中文版_8677`,
  `跑跑卡丁车-经典中秋版_2117` - `serialize_writes_state` refused: the
  paused-frontend save limitation (the title waits inside a nested native paint
  or its own clock loop). See
  `2026-11-15-libretro-save-while-paused.md`.
* `都市赛车6火线追击_2230` and `迈阿密之夜_9566` - only
  `serialize_after_refused_restore` (a save queued immediately after a
  refused-restore sequence); saving at a normal boundary is unaffected.
* `空战1944_10321`, `明星制造梦工厂_11048`, `毁灭战士3_10598` - harness
  watchdog; heavy/long titles need a longer harness timeout.
* `1945空战英雄_6105` and `bubblet.jar` - need
  `--set nano_j2me_device=nokia6230`; both render with that documented profile.

The same fixed core passed 20/20 libretro save-state round trips
(`tools/check_libretro_states.py`) and 183/183 CTest.
