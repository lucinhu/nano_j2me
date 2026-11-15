# libretro: saving a state while the frontend is paused

Date: 2026-11-15

## Symptom

A 30-title visual probe through the mock frontend
(`tests/test_libretro_frontend.c`, `--frames 240 --pace-us 16667
--visual-required --static-ok --min-nonblack 8`) passed 29 titles and failed
one on the serialize round trip:

```
僵尸危机2_4349.jar   FAIL   serialize_writes_state: retro_serialize(140816448) returned 0
```

The core logged

```
[core][warn] [nano_j2me] save-state request refused (-8); the game continues
```

`-8` is `NJ_ERR_STATE` (`include/nj2me/nj_types.h`), the value
`lr_request_state()` returns when the request was never served. Repeating the
run at `--frames 300` and `--frames 600` moved the failure to
`unserialize_restores_state`, with the same `-8` refusal: the VM worker could
not reach a save point while the frontend was blocked inside
`retro_serialize()`.

## Root cause

Instrumented traces showed the request never reached the between-frames serve
point in `lr_run_frames()`: `lr_serve_state_request()` was called for every
frame (`kind=0`) and then not even once after the save was queued. A gdb
`SIGSTOP` capture (temporary `raise(SIGSTOP)` in `lr_idle()` while a request
was pending) produced this worker stack:

```
lr_cond_wait
lr_idle                      src/port/libretro/nj_port_libretro.c:2274
lr_idle                      src/port/libretro/nj_port_libretro.c:2249
port_idle                    src/core/nj_thread.c:209
scheduler_step               src/core/nj_thread.c:1171
nj_scheduler_step            src/core/nj_thread.c:1188
vm_invoke                    src/core/nj_vm.c:1514
nj_call                      src/core/nj_vm.c:1688
nj_midp_dispatch_serially    src/midp/nj_lib_lcdui.c:6382
nj_midp_frame                src/midp/nj_lib_midlet.c:658
lr_run_frames                src/port/libretro/nj_port_libretro.c:2716
```

The VM had a single thread: `state=RUNNING nframes=1 in_c_call=1`, frame
`run()V`. That title runs a `callSerially` Runnable at the frame boundary which
busy-waits on `System.currentTimeMillis()`; its loop therefore only ends when
the virtual clock advances. The virtual clock is advanced by `retro_run()`, and
the frontend cannot call `retro_run()` while it is blocked inside
`retro_serialize()`. The worker spun inside one `nj_midp_frame()` (about 300,000
`port_idle` calls in 30 s) with `vclock` frozen, so the snapshot request timed
out after `LR_STATE_TIMEOUT_MS` (4 s).

This is the same class of problem as loading a state inside a blocking native
frame (`docs/notes/2026-11-15-libretro-load-state-in-blocking-frame.md`), but a
load discards the VM and a save must not silently change the snapshot's game
time in an unbounded way.

## Fix

`src/port/libretro/nj_port_libretro.c`:

* `LR_STATE_CLOCK_MAX_FRAMES` (300, about 5 s of virtual time at 60 Hz) is a new
  budget for clock assistance while a save is pending.
* `lr_request_state()` resets `s->clock_budget` when it queues a save
  (`0` for other request kinds).
* `lr_idle()` (RUN phase) and `lr_frame_hook()` advance `s->vclock_frames` and
  decrement that budget while a save request is pending, so a frame that
  busy-waits on the virtual clock can finish; the worker then reaches the
  boundary where `lr_run_frames()` serves the request.
* Loads keep their unbounded clock advance (the loaded snapshot replaces the
  VM, so no extra game time is retained).

The snapshot is taken at the first frame boundary after the request, so it can
include up to that much additional game time. If the budget or the 4 s timeout
is exhausted, the save is still refused and the game continues.

## Validation

* `僵尸危机2_4349.jar` through the mock frontend: all serialize/unserialize
  checks pass (`35 checks, 0 failed, 1 skipped` because `--static-ok` was used;
  the run reaches `非-black` content in 125/256 frames).
* `电子宠物蛋_11972.jar` (load-after-reset case from the earlier fix): still
  passes (`34 checks, 0 failed`).
* `tools/check_libretro_states.py` 20-title probe: 20 round trips, 0 failed.
* The 30-title visual probe: 30 passed, 0 failed (was 29/30 before the fix).
* Mock frontend matrix, real RetroArch (loopdemo/textdemo), Windows Wine
  fixtures and the full CTest suite are re-run with the rebuilt r13 artifacts;
  numbers are recorded in `build/libretro-final-report-20261115.md`.

## Limits

* Only a save waiting on the virtual clock benefits; an untimed C/native park or
  a VM that is genuinely busy still cannot be snapshotted and is refused.
* Saving while paused can include up to about 5 s of extra game time; the
  state's stored clock matches the frame boundary that was actually captured.
* The 4 s `LR_STATE_TIMEOUT_MS` deadline is unchanged.

## Known remaining case: a paint that waits on the virtual clock

A wider probe (50 titles, `--frames 240 --pace-us 16667`, seed 21) still
found one refusal: `火影忍者汉化版_10735.jar` fails `serialize_writes_state`
with the same `-8` log at 240, 300 and 600 frames. A gdb `SIGSTOP` capture
while the request was pending shows a different shape from 僵尸危机2:

```
k.paint(Ljavax/microedition/lcdui/Graphics;)V  pc=625   (main thread, state=SLEEPING,
                                                         in_c_call=1, wake_at_ms set)
k.run()V / Canvas.serviceRepaints()V                    (the worker thread is already DEAD)
```

The worker is inside a nested paint dispatched from the game thread's
`serviceRepaints()`; the paint's Java code sleeps on the virtual clock, and the
pump cannot reach a frame boundary where the save is legal
(`nj_vm_can_save()` refuses `in_paint`/`in_c_call` frames). Clock assistance
cannot help because the frame is a nested C call, not the frame loop that
`lr_run_frames()` can serve between. Increasing the clock budget and advancing
several frames per hook was tried and only made the nested pump slower, so the
production behaviour keeps the simple assist and this title is documented as a
limitation in `docs/LIBRETRO.md`: saving while paused can be refused for such
a title, while saving during normal play (frontend at a frame boundary) works.
The 50-title visual probe therefore reports 49 passed, 1 refused, 0 visual
failures.

## Update, 2026-11-15 (round 18): the busy-poll class

The "nested paint" shape above was only half of the story. A second gdb
`SIGSTOP` capture, taken 1.5 s after a load request was queued, showed the
worker inside

```
nj_invoke_native -> sys_currentTimeMillis (src/core/nj_system.c:23)
                 -> nj_now_ms -> lr_port_millis
```

i.e. the title was not waiting in the frame loop or in a native sleep: it was
polling `System.currentTimeMillis()` in a busy `while` loop inside its own
frame. That poll never calls the port `idle` entry point, so the two clock
assist paths (`lr_idle`, `lr_frame_hook`) never ran, the virtual clock stayed
frozen at the value of the last `retro_run()`, and the queued request timed out
with `-8` after four seconds. The earlier "nested paint" capture is the same
class seen through a different window: a paint running on top of the frame and
waiting for the clock.

The port now also advances the clock inside `lr_port_millis()` itself while a
request is pending, but only when the frame cannot finish on its own:

* a paint callback is on the stack (`nj_vm_in_paint()`, new public accessor), or
* the presented-frame counter has not moved for 100 ms (`nj_vm_frames()` and
  the `req_start_*` fields recorded when the request is queued).

A healthy frame reaches the boundary without help and is never accelerated;
that condition matters because accelerating a frame whose game logic is driven
by the wall clock (the football managers) made the game allocate far more than
it expected and hit `OutOfMemoryError`. The budget is 1200 virtual frames
(~20 s at 60 Hz); a load restores the pre-request clock when it is refused or
times out, since its clock advance was only a way to reach the serve point.

Verified with the mock frontend (`--frames 300 --pace-us 16667`):

```
PASS 空战1944_10321          PASS 迈阿密之夜_9566       PASS 都市赛车6火线追击_2230
PASS 火影忍者汉化版_10735     PASS 明星制造梦工厂_11048   PASS 午夜台球圣诞中文版_8677
PASS 跑跑卡丁车-经典中秋版_2117 PASS 魂斗罗_5652          PASS 电子宠物蛋_11972
PASS 僵尸危机2_4349          PASS 足球经理2010汉化版_4756  PASS 冠军足球经理2010汉化版_5198
```

The remaining documented limitation is a save/load requested while the worker
is inside a long-running native or M3G load that cannot reach a boundary within
the budget; normal play and paused saves that reach a boundary are unaffected.
