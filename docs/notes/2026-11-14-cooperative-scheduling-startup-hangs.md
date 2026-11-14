# Monitor ownership on Thread.run() and paint() handshakes under cooperative scheduling

2026-11-14. Two small regressions seen in the collected J2ME corpus were
traced to gaps between what the bytecode contract guarantees and what this
runtime's cooperative scheduler actually interleaves. Both are fixed in the
worktree (no commits); this note records the mechanism, the fix, and the
evidence, so the invariants can be reused for the next title.

## Summary

- `public synchronized void run()` must own the receiver's monitor before its
  first bytecode. `nj_thread_start()` pushed the `run()` frame directly,
  bypassing the interpreter's synchronized-method entry, so the first
  `wait()`/`notify()` inside `run()` threw `IllegalMonitorStateException`.
  This broke `三国塔防－曹操传_1610.jar` (IMS at `Object.notifyAll`) and
  `空战1944_10321.jar` (IMS at `Object.wait`, then an uncaught NPE in the
  worker). Fixed by acquiring the monitor in `nj_thread_start()` and handing
  it over when contended.
- A `paint()` that busy-waits on a field published by a loader thread hung
  `明星制造梦工厂_11048.jar`: the title's worker does
  `new = false; repaint(); serviceRepaints(); ...; new = true`, and
  `serviceRepaints()` returned immediately in this runtime, so the
  `new == false` window fitted inside one worker quantum and the spinning
  `paint()` never observed it. Fixed by making `serviceRepaints()` park while
  another thread is delivering a paint, and by waking that waiter after the
  paint completes. Two scheduler fairness/clock fixes were needed for the park
  to be observable at all.

## 1. Synchronized `run()`

Root cause: `nj_thread_start()` (`src/core/nj_thread.c`) pushed the entry frame
like any other call. `interp_invoke()` (`src/core/nj_interp.c`) acquires the
class/receiver monitor for `ACC_SYNCHRONIZED` methods, and that is why ordinary
synchronized calls worked, but the scheduler starts `Thread.run()` itself, so
that path never ran. A thread in that shape owns nothing, and
`Object.wait()`/`notify()` correctly refuse.

Fix (`src/core/nj_thread.c`):

- after the entry frame is pushed, if the run method is `ACC_SYNCHRONIZED`,
  resolve the monitor (class object for static, local 0 for instance) and call
  `nj_vm_monitor_enter()`;
- tree cases: 0 = acquired, record it on the entry frame; 1 = contended, park
  with `wait_kind = NJ_WAIT_ENTER` and leave the frame at pc 0 with no monitor
  recorded; 2 = too many monitors, pop and throw `IllegalStateException`;
  null receiver, pop and throw NPE.
- `mon_wake_waiters()` (`src/core/nj_thread.c`) recognises that parked shape
  via `mon_start_pending()` (BLOCKED on `NJ_WAIT_ENTER`, top frame is a
  synchronized method at pc 0 without a monitor) and, instead of expecting a
  monitorenter re-execution, pushes the monitor for the waiting thread and
  records it on the entry frame.
- `nj_thread_start()` callers must not clobber the park: `src/core/nj_lib_thread.c`
  and the TimerThread path in `src/midp/nj_lib_util.c` only set
  `state = RUNNING` when the start actually left the thread runnable.
- No state-format change: the handover reuses `NJ_WAIT_ENTER`; there is no new
  wait kind, so the v3 snapshot envelope and its `wait_kind > 2` rejection stay
  valid.

## 2. `serviceRepaints()` must block while a paint is in flight

The MIDP contract is that `serviceRepaints()` blocks until the pending
repaints have been serviced. This runtime implemented it synchronously
(`src/midp/nj_lib_lcdui.c`, `c_serviceRepaints`): if a paint could be delivered
in the current frame it painted inline; otherwise it returned immediately.
That is fine for titles that use `serviceRepaints()` as a paint barrier inside
the event thread, but observably wrong for the pattern

```
worker:  new = false; repaint(); serviceRepaints(); ...; new = true;
paint:   while (new) { }            // published state must be visible
```

which `明星制造梦工厂_11048` uses to coordinate its splash: the worker quantum
contains the whole `new == false` window, the frame paint only runs after the
worker's quantum, and it spins forever at one bytecode (`dream/g.paint pc=13`).
On a handset the worker is genuinely blocked inside `serviceRepaints()`, so the
window stays open while the event thread paints.

Fix:

- `src/core/nj_internal.h`: `nj_vm` gains `paint_owner`, `paint_waiters[4]`,
  `npaint_waiters`; two helpers are declared.
- `src/core/nj_thread.c`: `nj_vm_thread_wait_paint()` records the caller
  (state `NJ_THREAD_WAITING`, no monitor wait); `nj_vm_paint_done()` wakes all
  recorded threads after the paint.
- `src/midp/nj_lib_lcdui.c`: `nj_midp_paint()` saves/restores
  `vm->paint_owner` around the body (so nested synchronous paints from a UI
  callback keep the outer owner) and calls `nj_vm_paint_done()` when the
  top-level paint returns successfully. `c_serviceRepaints()` parks when
  `vm->paint_active && vm->paint_owner != th`, returning `NJ_ERR_TIMEOUT` so
  the interpreter suspends the calling Java frame; the frame resumes after the
  wake, exactly as for every other blocking native.
- The waiter list is capped at 4; a normal UI has one paint thread and at most
  a handful of serviceRepaints callers, and an overflow falls back to the
  previous immediate return rather than failing the title.

Two pre-existing scheduler behaviours also had to be corrected for the park to
be observable (found while diagnosing this title, kept as general fairness
fixes):

- `src/core/nj_thread.c` scheduler tail: when the only runnable progress is a
  thread parked inside its own C call but a Java thread sleeps on the clock,
  call `port_idle()` alone does not move virtual time, so the sleeper never
  wakes. The scheduler now advances the clock at most 5 ms per pass in that
  state instead of declaring deadlock. Effect on the hang: CPU spin time fell
  from ~95 s to ~5.5 s before the paint fix, and the game now completes.
- `src/core/nj_vm.c` `vm_invoke()` quantum-expiry resume: resuming the
  C-invoked Java frame immediately starved every other Java thread (a `paint()`
  spinning until its loader runs never let the loader run). Before resuming, it
  now gives the scheduler one pass with `in_c_call`/`in_call_pump` guards, like
  the blocked-native pump below it.
- `src/midp/nj_lib_lcdui.c` `c_serviceRepaints()` keeps its existing
  first-paint-per-frame rule (a repaint/serviceRepaints spin must not paint
  hundreds of times per frame); only the cross-thread blocking is new.

## Verification

- `三国塔防－曹操传_1610.jar`: 600 frames/600 paints, no IMS (previous: 1 frame,
  caught `IllegalMonitorStateException at Object.notifyAll`). Only a caught
  game-side `ArrayIndexOutOfBoundsException at cl.a(I)` remains.
- `空战1944_10321.jar`: 600 frames, clean, matching its pre-regression row.
- `明星制造梦工厂_11048.jar`: previously rc=124 after 95 s with no frames;
  now `frames=600 paints=557 bytecodes=168171990 time=42.9s` and a clean
  teardown (`exit status 0`). 42.9 s wall is the title's own ~50 ms pacing,
  not a stall.
- Focused ctest on `build-engine-mem` (17 tests, `-j1`, Release): 17/17 pass,
  89.58 s, including `test_thread_lifecycle`, `check_lockdemo`,
  `check_timer_wait`, `check_repaint`, `check_threaded_dispatch`,
  `check_paint_budget`, `test_display_serially`, `test_blocked_init`,
  `test_timer_fairness`, `test_libretro_frontend`.
- 45-title sample sweep with the fixed headless binary (`/var/tmp/mini-sweep`):
  43/45 ok; the two non-ok rows are known non-regressions (`1945空战英雄_6105`
  needs `--device nokia6230`; `鲁宾逊漂流记汉化BT版_4677` is a corrupt JAR).
- Temporary `SIGUSR1` thread-dump instrumentation used for the diagnosis was
  reverted; only the production changes above remain.

## Open follow-ups

- Full 404-title sweep with the fixed binary is still to be run and recorded as
  the regression baseline (`.local-tools/debug-runs/compat-20261113-sweep-keys`
  is the old pre-fix baseline).
- `--heap 16` OOM retry is still a manual/sweep-script policy; the runtime
  could retry the title at the next heap size itself for the libretro core.

## Frame-boundary serviceRepaints blocking (2026-11-14, round 3)

The paint fix above makes `serviceRepaints()` park until an in-flight paint
finishes, but the call also returns too early in the common case: a worker
thread that publishes state, calls `repaint(); serviceRepaints();` and undoes
the publication before the next frame paint can run. Under a cooperative
scheduler that whole window fits inside one 20000-bytecode quantum.

- `src/core/nj_internal.h`: `nj_vm` gains `pace_waiters[4]`, `npace_waiters`
  and `in_frame` (nesting depth of `nj_midp_frame`), plus
  `nj_vm_thread_wait_frame()` / `nj_vm_frame_done()`.
- `src/core/nj_thread.c`: a frame waiter is a plain `NJ_THREAD_WAITING` thread
  kept in `pace_waiters`; the scheduler treats it as progress (never
  "all threads are waiting" deadlock) and `nj_vm_frame_done()` wakes it.
- `src/midp/nj_lib_midlet.c`: `nj_midp_frame()` increments `in_frame` and all
  of its exits funnel through `frame_return()`, which decrements and wakes
  frame waiters at the outermost frame.
- `src/midp/nj_lib_lcdui.c` `c_serviceRepaints()`: a non-main thread that calls
  it with a repaint pending, outside a callback/paint, parks until the frame
  completes (`return NJ_ERR_TIMEOUT` so the interpreter suspends it). The main
  thread and callbacks keep the inline path.

Verification: `check_repaint`, `check_paint_budget`, `check_paint_pump`,
`test_display_serially`, `test_gamecanvas_flush`, `test_m3g*`, saved-Graphics
tests and `test_libretro_frontend` all pass (27/27 focused ctest, 87.7 s).

## Perf probe: many "slow" titles are self-paced

`--stats` bytecode counts are dominated by timer busy-waits, not emulation
cost. Profiling native calls (temporary instrumentation, removed) on
`魂斗罗_5652.jar` showed 95.8 M `System.currentTimeMillis` calls for 60 frames
(~1.6 M/frame): the title runs its own `while (now - last < ~70 ms)` loop and
caps itself at ~14 fps regardless of `--fps`. With `--fps 60`, other high
bytecode titles (`3D激爆拳击_8805`, `哆啦A梦-秘密道具大作战_6557`,
`波斯王子经典版_9715`, `合金弹头2011_5629`) all hold 59.5–60 fps within their
2 s budget (308–363 M bytecodes/s), so the interpreter is not saturating on
them; their bytecode totals are self-imposed timer loops.

Follow-ups from this measurement:
- A cheap `System.currentTimeMillis()` fast path (skip the frame push/pop) was
  prototyped but not kept: it does not change self-paced fps, only the wasted
  CPU between ticks. Revisit only if battery/thermal behaviour matters.
- Frame budget uses the virtual MIDlet clock; for a thread that busy-waits on
  it, the budget is enforced by the 256-round cap rather than milliseconds.
  Keeping titles at their own pace is correct; do not "fix" it by throttling
  Java threads.
