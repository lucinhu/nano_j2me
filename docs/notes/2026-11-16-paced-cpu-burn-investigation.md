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

## Addendum, same round: the two-title slowdown is cap-independent

After the first write-up, two more experiments narrowed the mechanism (all
reverted; tree stays at `97fe1f7`):

1. **Main-thread skip alone.**  Only the `scheduler_step()` change (do not
   count a main thread with no Java frames as "ran") was applied, no host
   nap, no progress early-return, no sleep cap.  Measured on the same CLI:

   | title | wall | CPU | fps |
   | --- | --- | --- | --- |
   | 魔塔_8052 | 12.22 s | 0.37 s | **24.6** |
   | 火影忍者_11531 | 12.89 s | 0.05 s | **23.3** |
   | 合金弹头4(完美汉化版)_4053 | 49 s CPU 5.01 s | 5.01 s | 30.0 |

   So the CPU win (×20–×200) and the fps loss (30 → 24.6/23.3) both come
   from that single change: letting `scheduler_step()` reach its idle
   branches changes how the frame pace is spent, not just how much CPU it
   burns.

2. **Budget-capped scheduler sleep.**  Passed the remaining call budget
   into `scheduler_step()` and capped its two internal `port_sleep()`
   deltas by it (so a 50 ms sleeper cannot oversleep a 4 ms pass), on top
   of the main-thread skip.  Result unchanged: 魔塔 24.5 fps CPU 0.38 s,
   火影 23.2 fps CPU 0.05 s, 合金弹头4 30.0 fps but CPU still 4.98 s.

3. **The pacer's wall time is not the wait it thinks it is.**  One trace
   of `frame_pace_wait()` (instrumented only, no semantic change) showed
   `wait=33` but **`spent=54 ms` with `iters=1`**: the single
   `nj_run_pending(4 ms)` call inside it took 54 ms even with the sleep
   cap in place.  So something inside the first scheduler pass sleeps a
   whole 50 ms (the value `have_earliest` caps at), and the pacer's own
   deadline cannot be honoured.  At `--fps 60` the same title also lands
   at 24.6 fps, i.e. the frame period is determined by that 50 ms sleep
   whenever the scheduler reaches the idle branch, not by the requested
   cap (uncapped runs are 30.0 fps / 33 ms).

   That explains the two-title symptom precisely: the cap is not being
   applied because the inter-frame sleep overshoots it.  The remaining
   question is _which_ sleeper has a 50 ms deadline and why
   `port_sleep()` waits the full 50 ms instead of the pass budget; that
   is the next thing to instrument (log `earliest`, `now` and the delta
   in every `port_sleep()` call, plus the requesting thread).

So the earlier "safe fix needs earliest deadline plumbing" conclusion is
strengthened: the pacer must own the sleep, and `scheduler_step()` must
never sleep past the caller's budget/wait.  Until both are true, any idle
branch turns a 30 fps cap into ≈24.6 fps for those titles.

## Resolution, round 61 (2026-11-16)

The fix landed the same day: `scheduler_step()` now reports progress, a main
thread with no Java frames is not counted as work, and
`nj_scheduler_run_until_idle()` returns as soon as a pass makes no progress
instead of spinning out the budget.  The pacer keeps ownership of the host
sleep through a new optional `frame_wait` port hook (headless only; the
libretro port's clock is advanced by the frontend, so it stays NULL and keeps
the pass loop).  Sleeps inside a scheduler pass are clamped to the caller's
remaining budget, which is what fixes the 24.6 fps overshoot.

Measured: the four titles in the table above went from 9–10 s CPU per 300 paced
frames to 0.05–0.11 s at the same 30.0 fps and 10.01 s wall time; see
`docs/PERFORMANCE.md` for the before/after table.  Full CTest 192/192, real
RetroArch and the libretro option matrix still pass, and the 404-title sweep
is unchanged.
