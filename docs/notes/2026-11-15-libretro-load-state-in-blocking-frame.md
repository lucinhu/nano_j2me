# libretro: loading a state while the VM is inside a blocking frame

Date: 2026-11-15

## Symptom

`tools/check_libretro_states.py` runs the mock frontend
(`tests/test_libretro_frontend.c`) over sampled corpus titles and requires the
serialize round trip, including the harness check `unserialize_after_reset`
(the same game state must load into a fresh VM after `retro_reset`). On the
20-title probe, 19 titles passed and one failed:

```
电子宠物蛋_11972.jar   FAIL   unserialize_after_reset: same-content state must
                             load into a fresh VM after reset
```

The core logged

```
[core][warn] [nano_j2me] save-state request refused (-8); the game continues
```

`-8` is `NJ_ERR_STATE` (`include/nj2me/nj_types.h`), which
`lr_request_state()` uses for "the request was never served".

## Root cause

`retro_reset()` unloads and reloads the content, so the harness restores a
state into a booting VM:

```
retro_reset -> lr_unload -> lr_load(&info, 1)   (new VM, new worker thread)
run 4 frames
retro_unserialize(state)
```

The state request is served by `lr_run_frames()` between two frames, and only
when `nj_vm_can_save()` allows it. `lr_request_state()` waits
`LR_STATE_TIMEOUT_MS` (4 s) for that serve point.

For this title, the first post-reset frame is long: the MIDlet's startup
sleeps inside a blocking native call, which the VM pumps through
`lr_frame_hook()`. A frontend that is blocked inside `retro_unserialize()`
cannot call `retro_run()`, so the virtual clock does not advance, the sleep
never expires, and the VM worker never leaves that one frame. The request
never reaches the between-frames serve point and times out. Debug
instrumentation confirmed `lr_serve_state_request()` was never called with
`kind == LR_REQ_LOAD` after the reset, and the frame that started when the
request was posted returned only after roughly the 4 s timeout.

Titles that boot quickly (or whose first post-reset frame ends promptly) were
unaffected, which is why only one of the 20 sampled titles failed and why the
published device tests never hit it.

## Fix

`src/port/libretro/nj_port_libretro.c`, `lr_frame_hook()`: while a LOAD
request is pending, advance the virtual clock by one frame per pump. The load
replaces the VM (with the clock stored in the state), so advancing time here
cannot alter the restored state; it only lets the blocking native finish, the
frame reach its boundary and `lr_run_frames()` serve the load. Save requests
are untouched: the hook never advances the clock for them, and the existing
rule "a save request interrupts a host wait without advancing game time"
still holds.

## Validation

With the fix and the unchanged mock frontend:

* `电子宠物蛋_11972.jar` now passes `unserialize_after_reset` (34/34 checks).
* `tools/check_libretro_states.py` 20-title probe: 20 round trips, 0 failed
  (was 19/20, log `.local-tools/debug-runs/submission-20261115/libretro-title-states-20.log`).
* Mock frontend matrix (`tools/check_libretro_frontend.sh`): 20 passed,
  0 failed, 0 skipped.
* Real RetroArch 1.22.2: LoopDemo 120 frames PASS, TextDemo 180 frames PASS.
* `浴火银河2完整版_7689.jar` through the mock frontend at 60 Hz:
  36/36 checks PASS (non-black and animation checks).
* Windows x86_64 DLL under Wine: 4/4 fixtures PASS.
* Full CTest suite on the isolated build: see the report below
  (`/tmp/r12-ctest.log`).

## Limits

* Only load requests fast-forward the clock. A save request still needs the VM
  to reach a safe boundary; a busy title can still refuse a save.
* A state load can therefore advance the game by a few virtual frames before
  the snapshot is applied. The applied snapshot restores the stored clock, so
  the final game time comes from the state, not from the fast-forward.
* This does not make loading a state a general "block until serviced" call: the
  4 s timeout remains, and a frontend that never ticks during a load still
  gets a refusal rather than a hang.

## Files

* `src/port/libretro/nj_port_libretro.c` — 15 added lines in `lr_frame_hook()`.
* `docs/LIBRETRO.md` — save-state paragraph updated.
* `tools/check_libretro_states.py` — corpus-level state probe that found it.
