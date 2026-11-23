# Timer tasks as Java threads (round 4, 2026-11-23)

`TheMummy_K700F` never got past its loading screen: the log showed the frame
counter stuck at 8 (about 267 ms at 30 fps) with a million `paint()` calls in
15 s and no progress.  It turned out the title's whole game loop runs inside a
`java.util.TimerTask`, and the runtime ran timer tasks to completion inside a
single C→Java call.

## The bug

`java.util.Timer` is implemented by a C-built helper class
`nj2me/util/TimerThread` (embedded compiled bytes in
`src/midp/nj_lib_util.c`).  Before this round its `run()` was
`while (pump(timer)) {}` and the native `timer_pump` ran the due
`TimerTask.run()` directly:

```c
nj_call(vm, task, TT_CLS, "run", "()V", NULL, NULL);   /* to completion */
...
nj_thread_sleep(vm, th, 0);
return NJ_ERR_TIMEOUT;
```

`nj_call` cannot suspend a C-to-Java caller — the interpreter absorbs every
`YIELD` and keeps running — so the task owned the VM thread for its entire
lifetime.  `TheMummyMidlet.a()V` (called from the 250 ms timer task) builds
the splash screens and then enters

```java
while (f.c) { if (f.i == 0) g.a(); f.e(); f.b(); }
```

an endless loop that paints but never returns.  The frame pump could not get
the VM back, the 20 ms timer task `h.run()` (which drives the loader
handshake) was starved, and the frame counter froze at the first timer tick.

## The fix

Move the task dispatch into Java so the scheduler preempts it like any other
thread.  The helper class is now:

```java
package nj2me.util;
public final class TimerThread {
    public Object timer;
    public Object task;
    public TimerThread(Object t) { this.timer = t; }
    public void run() {
        for (;;) {
            if (!pump(timer)) return;
            Object t = task;
            if (t == null) continue;
            task = null;
            try {
                ((java.util.TimerTask) t).run();
            } catch (Throwable e) {
                ((java.util.Timer) timer).cancel();
                return;
            } finally {
                afterRun();
            }
        }
    }
    public static native boolean pump(Object timer);
    public static native void afterRun();
}
```

- `timer_pump` no longer calls `run()`: it publishes the due task into
  `TimerThread.task`, sets `th->in_timer_cb`, sets the return value to `1`
  and returns `NJ_OK`.  The Java loop calls `run()` as ordinary bytecode, so
  every scheduler quantum boundary applies.
- `afterRun()` (new native) ends the quantum after one task and parks the
  helper, exactly like the old pump's `nj_thread_sleep(vm, th, 0)`, so a
  periodic task runs once per scheduler turn instead of looping inside one.
- A task that throws cancels its timer and the helper exits; previously the
  helper thread died with an NPE and the timer stayed scheduled.

Two paint details needed to keep the old callback semantics:

- `c_paint_now()` still paints inside `repaint()` when called from a timer
  callback (`th->in_timer_cb`), because titles like TombRaider call
  `repaint()` and then spin until the paint publishes.
- `c_serviceRepaints()` parks a timer callback until the frame ends (the
  existing paced-repaint waiter), which paces a timer-hosted game loop to
  about one step per displayed frame.  Without it TheMummy painted 2,533
  frames per display frame (1.5 M paints for 600 frames).

## Evidence

- `test_timer` PASS, `test_timer_fairness` 0 failures, and the 11-test
  timer/repaint regression group passes (`test_save`, `test_lifecycle`,
  `test_util`, `check_sf2_loader`, `check_paint_pump`, `check_timer_wait`,
  `check_repaint`, `check_paint_budget`, `test_thread_lifecycle`).
- Full serial CTest: **210/210 passed in 366.07 s**.
- TheMummy through the CLI (176x208, heap 16, 30 fps, 600 frames):
  `frames=600 paints=609 gfx=11,610 fps=30.0` rc=0; dumped frames show the
  splash at frame 150 (14,970/36,608 non-black) and the menu/game screen at
  frames 300-600 (34,897/36,608).
- TheMummy through the libretro core (mock frontend, 300 frames at 60 Hz):
  `PASS`, 47 checks / 0 failed, `frames_with_content=297/317`,
  `distinct_frame_hashes=3/317`, and the serialize/save checks now pass
  (previously the state request was refused at the 4 s deadline because the
  boot handshake never finished).
- 100-title CLI A/B against the previous build: 0 verdict differences; a
  30-title stats sample shows 25 identical frame/paint pairs and five titles
  with ±1 paint or a few gfx calls — scheduler timing noise, no behaviour
  change.
