# Paced runs burn a core: investigation, 2026-11-16 (round 60)

## Symptom

A frame-capped headless run of some titles uses the whole wall time as CPU:

| title | `--fps 30`, 300 frames | same title, `--fps 0`, 300 frames |
| --- | --- | --- |
| 魔塔_8052 | wall 10.01 s, **user CPU 10.00 s** | wall 10.00 s, CPU 0.03 s |
| 合金弹头4(完美汉化版)_4053 | wall 10.02 s, **CPU 10.00 s** | CPU 0.03 s |
| 火影忍者_11531 | wall 10.00 s, **CPU 9.01 s** | CPU 0.03 s |
| 世嘉头文字D－宿命对决_1247 | wall 10.01 s, **CPU 10.00 s** | CPU 0.03 s |
| tests/fixtures/loopdemo.jar (control) | wall 10.00 s, CPU 0.05 s | — |

So the pacer, not the game, is burning the CPU: these titles are cheap
(≈0.1 ms/frame of real emulation), but their 30 fps runs spend 33 ms of
CPU per frame.  Titles that self-pace (`魂斗罗_5652` on
`System.currentTimeMillis()`) burn CPU in both modes and are separate.

## Localisation

`gprof` of `魔塔_8052` at `--fps 30` (300 frames):

| function | samples | calls |
| --- | --- | --- |
| `scheduler_step` (`src/core/nj_thread.c`) | 41 % | 160,471,691 |
| `nj_scheduler_run_until_idle` | 33 % | — |
| `hl_millis` (`src/port/posix/nj_port_headless.c`) | 18 % | 321,000,000 |

≈535k scheduler passes per frame.  The pacer's capped path in
`frame_pace_wait()` (`src/midp/nj_lib_midlet.c`) calls
`nj_run_pending(vm, ≤4 ms)` (which is
`nj_scheduler_run_until_idle`) while any scheduler thread exists, so the
4 ms budget is spun away inside `nj_scheduler_run_until_idle`'s loop.
The loop has three idle branches; the "only frame waiters are left" one
calls `port_idle(vm)`, and `hl_idle()` on the headless port is a no-op —
so the loop iterates `scheduler_step` until the budget expires.

The deeper reason the loop does not take an idle branch: `scheduler_step`
marks `ran = 1` for the main thread even when it has no Java frames
(the C runner drives the frame; `nj_exec()` on an empty stack is a
no-op), so "no thread made progress" is never observed.

## Attempts and measurements

1. **Port `frame_wait` hook + nap when all scheduler threads await the
   frame boundary.**  Added a `void (*frame_wait)(void *ud, nj_u4 ms)`
   hook to `nj_port` (headless: `hl_sleep_ms`) and a
   `nj_scheduler_waiting_frames()` helper; the pacer napped only when
   the helper was true.  **No effect**: the dump showed one scheduler
   thread in state RUNNING besides the frame waiter, so the condition
   never held and the 4 ms spin stayed.

2. **Progress reporting in the scheduler + host nap.**  Changed
   `scheduler_step()` to return whether it did anything
   (`ran || parked_ready || have_earliest`), skip threads with
   `nframes == 0` (worker → DEAD, main → skipped), made
   `nj_scheduler_run_until_idle` return `NJ_ERR_TIMEOUT` when a pass made
   no progress, and had `frame_pace_wait` nap the host in 10 ms chunks
   when `nj_run_pending` returned in under a millisecond.
   **CPU fixed**: 魔塔 0.06–0.09 s, 火影 0.07 s, 合金弹头4 0.17 s for the
   same 10 s wall.
   **But two titles slowed down**: `魔塔_8052` 30.0 → 24.5 fps,
   `火影忍者_11531` 30.0 → 23.1 fps, while `合金弹头4` and
   `世嘉头文字D` stayed at 30.0.  Disabling the host nap alone (keeping
   the scheduler early-return) still gave 24.5 / 23.1, so the slowdown
   comes from the scheduler no longer being pumped through the
   inter-frame gap, not from the sleep.  That means some thread of those
   two titles needs scheduler passes *during* the wait (their frame
   interval grew to ≈41 ms).

3. **Reverted all of it.**  A CPU fix that costs 6–7 fps on real titles
   is a regression, so the tree is back at `97fe1f7` and the CPU burn
   stays a known issue.

## What a safe fix needs

Do not simply stop pumping during the gap: at least these titles keep a
thread moving during it.  A safe design needs to know the earliest
deadline at which the scheduler has something to do (Java `Thread.sleep`
wake-up, monitor notification, frame boundary) and then sleep exactly
until that deadline (bounded by the frame deadline), instead of the
current "run a 4 ms scheduler budget in a loop".  That requires:

- `scheduler_step()` to report (a) whether it ran anything and (b) the
  earliest pending wake-up deadline it knows about (it already computes
  `earliest` internally for its `port_sleep()` branch);
- `frame_pace_wait()` to compute `nap = min(remaining, earliest - now)`
  and park with the port's `frame_wait`/`sleep_ms` hook, looping only
  when the wake-up actually became runnable;
- a headless implementation that sleeps for that interval.

The regression above is the evidence that the "earliest deadline" value
must include whatever the two affected titles wait on; a first step is to
dump `earliest`/thread states for 魔塔/火影 during the gap.

## Reproduce

```
# burn (10 s CPU in 10 s wall)
nj2me --port headless --size 240x320 --heap 32 --fps 30 \
      --run-frames 300 --stats --rms-dir /tmp/p tests/games/魔塔_8052.jar

# same title uncapped (~0.03 s CPU)
nj2me --port headless --size 240x320 --heap 32 --fps 0 \
      --run-frames 300 --stats --rms-dir /tmp/p tests/games/魔塔_8052.jar
```

`--stats` reports `time=` and `fps=`; compare with `ru_utime + ru_stime`
from `getrusage(RUSAGE_CHILDREN)`.
