# The frame pacer: spend the wait on the MIDlet's threads, aim at an absolute deadline

`MinecraftJ2ME_PE_0.6.1_V2.2.jar` loaded a world four times slower at
`--fps 30` than uncapped, and in game the frame counter sat under the cap. Both
came from `frame_pace()` in `src/midp/nj_lib_midlet.c`, which is the AMS-side
loop that paces the display.

## The wait used to park the whole VM

With `fps_cap > 0`, the pacer ended a frame by sleeping until the budget was
spent:

```c
sleep_ms(budget - elapsed);
```

That stops everything the VM is running, Java threads included. A title's game
thread therefore only got the 8 ms `nj_scheduler_frame(vm, 8u)` slice the frame
loop grants it - at 30 fps, 8 of every 33 ms, a quarter of the CPU. Minecraft
runs its own `MC.run()` loop with world generation and rendering on a second
thread, which is why a phone that shows 30 fps here showed "very low frame
rate" during loading in this runtime.

The wait is now spent in the scheduler instead:

```c
while (nj_vm_running(vm) && nj_now_ms(vm) < deadline) {
    nj_s8 left = deadline - nj_now_ms(vm);
    if (nj_scheduler_has_threads(vm)) {
        nj_run_pending(vm, (nj_u4)(left > 4 ? 4 : (left > 1 ? left : 1)));
    } else if (vm->port->sleep_ms) {
        vm->port->sleep_ms(vm->port->ud, (nj_u4)(left > 2 ? 2 : left));
    }
}
```

`nj_scheduler_has_threads()` (new, `src/core/nj_thread.c`) answers "does this
VM have any thread the scheduler owns besides the main one". It has to count
*sleeping* threads too: a loader sleeps between chunks and only wakes because
the scheduler advances its deadline, so "nothing runnable right now" is exactly
the state of a loader. An earlier attempt gated the pump on "a runnable thread
exists" and changed nothing at all - the sleeping thread was never woken, so
the numbers matched the old build to the bytecode.

A/B on the same 6000-frame budget at `--fps 30`, both stuck on
`BuildWorldScreen` (the two builds advance the game at different rates, so this
is a work-per-frame comparison, not a navigation one):

| pacer | paints | time | fps | bytecodes | natives |
| --- | --- | --- | --- | --- | --- |
| `sleep_ms` | 32 | 198.8 s | 30.2 | 183,323,816 (0.92 M/s) | 7,107,733 |
| scheduler pump | 1 | 210.1 s | 28.6 | 15,638,863,630 (74.43 M/s) | 929,090,269 |

Same wall clock, same frame cap, 85x the application's own work: the loader is
no longer starved by the display pacing. In a saved world the run reaches the
game menu and holds the cap - `MCJ: HUD3D auto 2d` and
`frames=1200 ... time=40.7s fps=29.5` at `--fps 30`.

## An absolute deadline, in microseconds

The second half is accuracy. The old pacer computed a fresh `budget - elapsed`
per frame, so every overshoot was thrown away; and `budget = 1000 / cap` is
integer milliseconds, so `--fps 60` waited 16 ms - a 62.5 fps cap.

`frame_pace()` now keeps `vm->frame_due_us`, the absolute instant of the next
frame on the MIDlet's clock, in microseconds, and advances it by
`1000000 / cap` each frame. A frame that overran the boundary simply waits less
(or not at all) next time, so the average lands on the cap, and the
sub-millisecond part of the budget is carried. Two bounds keep a fault from
breaking the loop: a deadline more than four budgets in the future is a clock
step or a changed cap and is re-anchored to `now + budget`; a deadline more
than one budget in the past is debt too old to repay and restarts from
`now - budget`, so the cap cannot silently switch off.

| port / title | cap | before | after |
| --- | --- | --- | --- |
| headless, 1942空战_10403 (no extra threads) | 30 | - | 29.9 |
| headless, 1942空战_10403 | 60 | 62.4 | 59.8 |
| headless, 1942空战_10403 | 120 | - | 119.4 |
| headless, Minecraft saved world | 30 | 27.4 | 29.5 |
| headless, Minecraft saved world | 60 | - | 57.9 |

Minecraft's figures include the ~0.6 s state resume in the reported `time`;
traced frame periods are 33.0 ms at `--fps 30` (30.3 fps in steady state). Its
60 fps run is limited by its own work, not the pacer: 1,200 frames of the saved
world rasterise 25.3 M triangles, so the 16.67 ms budget is mostly spent before
the pacer gets to wait. An earlier 60.2 reading came from a build with less
per-frame work and is not a before/after pair. The
`--speed` multiplier keeps its own per-frame path, because a scaled MIDlet clock
and an absolute real-time deadline do not line up; `test_speed` (171 checks)
still passes.

## Evidence and gates

Logs and frame dumps are under
`.local-tools/debug-runs/minecraft-m3g-fix-20261030/` (`fps30.log`, `fps60.log`,
`frames/00400.ppm` with clouds and clean water); the loading A/B logs are in
`.local-tools/debug-runs/mc-user-20261030/`. The temporary `NJ_PACE_TRACE`
printout used to confirm the period was removed.

Full `ctest` **131/131**, sanitized suite **131/131** (ASan + UBSan + LSan),
`tools/check_codec_trim.sh` green, `tools/check_m3g_titles.sh` 26/26 clean, and
the corpus titles keep their frame caps.
