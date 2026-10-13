# OPEN: a thread whose `run()` returned stays RUNNING

Status: unfixed on purpose. Tried and reverted (2026-10-14) because the correct
fix regressed `毁灭战士3_10598`; the analysis is written down so the next
attempt starts from the measurement instead of the symptom.

## Symptom

A Java thread whose `run()` returns keeps `state == NJ_THREAD_RUNNING` with an
empty frame stack. Nothing crashes, and that is the problem:

- `nj_exec()` on it returns `NJ_OK` immediately (it returns early when
  `th->nframes <= stop_depth`), so the scheduler calls it once per pass forever;
- `alive` stays true, so a program whose only Java thread has finished cannot
  report itself finished;
- `Thread.join()` on it parks until its own timeout, because nothing notifies
  the joiners when a thread ends.

`模拟人生2_10762` shows the state in the log: its game thread returns from
`run()` (the title's own catch-all handler swallowed
`Failed to locate content: 34795`) and the MIDlet goes on "running" with a black
screen until the frame budget runs out.

## What was tried

In `scheduler_step()`, right after `nj_exec()`:

```c
} else if (!t->is_main && t->nframes == 0u) {
    t->state = NJ_THREAD_DEAD;
}
if (t->state == NJ_THREAD_DEAD && t->obj) {
    (void)nj_vm_monitor_notify(vm, t->obj, 1);   /* wake joiners */
}
```

plus two consequences that are needed once threads can be DEAD: a DEAD main
thread is exempt (the runner drives it), and the "all threads are waiting;
assuming the app is done" branch must not fire while the MIDlet's own thread
still exists, because the runner *is* that thread's event source.

## What it broke

`毁灭战士3_10598` went from `PASS (static picture)` to
`FAIL (could not start: construction of Doom/Doom blocked …)`. Its music thread
returns from `run()` while `Doom/Doom`'s constructor sits in an untimed
`Object.wait()` on the MIDlet's own thread (thread 1, `state=3`,
`wait_deadline=0`), pumped by the C caller in `vm_invoke()`.

With the finished thread left RUNNING, the pass looked busy: `nj_exec()` on its
empty stack returned `NJ_OK`, so `ran` was true, no idle sleep was taken, and the
C-call pump kept driving frames quickly enough for the constructor to be woken
before the 5 s `NJ_CALL_BLOCK_MS` deadline. Marking the thread DEAD made the pass
look idle instead, and the run died at exactly `time=5.0xx` — measured, not
guessed. Neither `ran = 1` for a DEAD thread nor skipping the idle sleep when
`vm->in_call_pump` is set restored the old timing by itself; the hook-driven pump
still lost the race.

## Where to start next time

The real difference is not `ran` but *what wakes the constructor*: something in
the frame-hook path (the runner dispatches `paint()` into the parked thread) is
what unblocks `Doom/Doom`, and the old behaviour only got away with it because a
finished thread kept the scheduler's pass busy. A fix needs either

- an explicit "the C caller is driving this thread" wake-up path, so a parked
  constructor is resumed by the frame hook deterministically rather than by
  timing, or
- `join()`/`alive` handled without changing the scheduler's idle behaviour at all
  (mark the thread finished for `join()` and for the finished-program decision,
  but keep the pass busy and the pump timing identical).

`tests/timerdemo` and `tests/waitdemo` are the fixtures that touch this ground;
`tools/check_timer_wait.sh` is what caught the first ordering mistake in the
`in_c_call` skip. A new fixture should assert both halves: a joined thread is
released, and a MIDlet whose constructor waits on another thread still starts.
