# Synchronized callbacks run through the C bridge without their monitor

Date: 2026-11-16 (round 54)

## Symptom

`冰淇淋少女成长日记_7307.jar` threw

```
thrown: java/lang/IllegalMonitorStateException at java/lang/Object.notify()V pc=0: (no message)
```

once per frame for the whole sampled run, and left the run at frame 290 of 600.

A minimal fixture reproduces it: a `Canvas` subclass whose `paint()` is
declared `synchronized` and calls `notifyAll()` on itself.  With the old
runtime the paint threw `IllegalMonitorStateException` on its first call and
the run stopped after two frames.

## Root cause

`Object.notify()`/`notifyAll()` require the caller to own the object's monitor
(nj_lib_object.c:177-206).  The interpreter acquires that monitor when it
executes an invoke opcode reaching a synchronized method, and
`nj_thread_start()` does the same for a synchronized `run()`
(test_monitor_compat.c covers that path).

MIDlet callbacks are not reached through an invoke opcode.  The runtime calls
Java from C through the bridge `vm_invoke()`
(src/core/nj_vm.c:1334).  It pushed the frame and ran `nj_exec()` directly,
never looking at `NJ_ACC_SYNCHRONIZED`, so `Canvas.paint()`, `keyPressed()`,
`commandAction()` and the other callbacks ran **without the monitor their Java
declaration requires**.  A synchronized callback that calls `wait()`,
`notify()`/`notifyAll()`, or that relies on excluding another thread, then
behaved incorrectly: the common shape - `paint()` synchronizing on the canvas
and notifying a loader thread - threw on every frame.

## Fix

In `vm_invoke()`, after the Java frame is pushed and before `nj_exec()`:

* a synchronized instance method acquires the receiver's monitor, a
  synchronized static method acquires the class mirror (`nj_class_object()`);
* if another thread owns it, the bridge pumps `nj_scheduler_step()` with
  `in_c_call` set, up to a **100 ms grace period**; the scheduler's
  `mon_wake_waiters()` recognises the entry frame (`mon_start_pending()`) and
  hands the monitor over, exactly as it does for a synchronized `run()`;
* if the monitor is still held after 100 ms, the bridge clears the pending
  wait and runs the callback **without the monitor**, recording it on the
  entry frame only if it was actually acquired.  This keeps the pre-fix
  forward progress for titles whose callbacks legitimately contend (an
  unbounded wait deadlocked the frame painter: `极品飞车16：亡命狂飙_1519`,
  `极品飞车13之变速英文3D版_6646` and `荣誉勋章之空降神兵改良版_2993` stopped
  making progress after the first unbounded version);
* when the monitor is acquired, it is recorded on the entry frame, so the
  normal return/unwind paths (`nj_interp.c` DO_RETURN and the frame-unwind
  path) release it.

## Regression fixture

`tests/lockdemo/LockDemo.java` now has

```java
static class Screen extends Canvas {
    static int paints;
    protected synchronized void paint(Graphics g) { ...; paints++; notifyAll(); }
}
```

and the run prints `LOCK counter=400 overlap=false paints=<n>`.
`tools/check_lockdemo.sh` gained two checks: no `IllegalMonitorStateException`
in the output, and `paints` greater than zero.  Against the pre-fix binary the
same fixture throws `IllegalMonitorStateException` and stops at two frames;
with the fix it runs the full frame budget.

## Verification

* `冰淇淋少女成长日记_7307.jar`: 0 `IllegalMonitorStateException`; the title
  still ends itself at frame 290 (game-side), which is its pre-existing
  behaviour.
* `check_lockdemo.sh`: PASS with the new checks; pre-fix binary reproduces the
  exception (2 frames).
* The three titles that the unbounded version deadlocked now finish the
  600-frame probe: `极品飞车13之变速英文3D版_6646` `frames=616`,
  `极品飞车16：亡命狂飙_1519` `frames=600`, `荣誉勋章之空降神兵改良版_2993`
  `frames=600`.
* `test_monitor_compat`, `test_thread_lifecycle`, `test_vm`,
  `check_threaded_dispatch`, `test_blocked_init`: PASS.
* Full 404-title sweep and full CTest re-run recorded with the round-54
  evidence; any verdict change is listed in
  `docs/notes/2026-11-14-corpus-regression-sweep.md`.

## Known limits

The 100 ms fallback means a contended synchronized callback can still run
without excluding the other thread (the old behaviour); the monitor is only
guaranteed when it was free or released within the grace period.  This is a
deliberate trade: exclusive semantics for the common case, forward progress
for titles whose display thread and game thread wait on each other.

`native` methods declared synchronized are out of scope: neither the
interpreter's native-callee path nor the bridge takes their monitor (no corpus
title depends on it; the class library's own natives are not synchronized).
