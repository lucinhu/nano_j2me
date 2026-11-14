# CLI manual states on titles that run their loop inside startApp()

Status: fixed (runner). A title whose whole frame loop lives inside
`startApp()` — 仙剑奇侠传完美版 is one — could not be snapshotted from the CLI
at all; `--save-state` silently produced nothing. It now starts the MIDlet as a
VM thread when a snapshot was asked for, and the state is written and resumed.

## The symptom

The corpus state probe (`tools/check_corpus_states.py`, added 2026-11-14) ran
phase A with

    nj2me --port headless --size 240x320 --heap 16 --fps 30 \
        --run-frames 240 --save-state /tmp/x.state --save-state-frame 200 \
        --keys @.../keys.txt tests/games/仙剑奇侠传完美版_11857.jar

and no state file appeared; the run warned that it "ended before the requested
frame boundary". `tests/fixtures/loopdemo.jar`, the fixture for the same shape,
behaved the same way. Two of the three sampled titles *did* save, so the option
itself was fine.

## Why there was no boundary to snapshot at

The synchronous `nj_vm_start_app()` call enters Java from C. A title that
sleeps between its own frames parks inside a native call; the runner's frame
hook (`run_frame_hook` in `src/runner/nj_run.c`, driven by `nj_midp_frame`)
keeps presenting frames while that native call is live, so the entire frame
budget can be spent before `startApp()` returns. The explicit frame loop that
honours `--save-state` is only entered after that, and by then the run is over.

Taking the snapshot from the hook is not an option: `nj_vm_can_save()`
(`src/core/nj_save.c`) refuses while `vm->native_depth != 0`, while
`vm->in_frame_hook` is set, or while any frame is marked native — a C stack
frame that the blob cannot represent sits between the Java frames and the code
that would resume them (the Doom constructor-join case,
`docs/notes/2026-10-21-snapshot-frame-boundary.md`).

## What changed

In `src/runner/nj_run.c`, when `--save-state` was requested, the MIDlet is now
started with `nj_vm_start_app_async()` instead of `nj_vm_start_app()`. The
cooperative scheduler then parks the application thread at every frame
boundary and returns to the runner, which is exactly the arrangement the
libretro port uses for its manual states — and the arrangement `nj_vm_can_save()`
is written for. On failure the code falls back to the synchronous start.

Without `--save-state` nothing changes: the default run path is still the
synchronous start, and counters were verified identical on 仙剑 (240 frames,
227 paints, 3,165,064 bytecodes, 27,034 natives, 806,912 PCM frames — same
before and after).

`--help` under `--save-state-frame` now says the MIDlet is started as a VM
thread so `startApp()`-loop titles can be snapshotted, and that a title which
never reaches a frame boundary at all (Doom 3) still cannot be.

## Evidence

* 仙剑奇侠传完美版_11857: save → `saved state to /tmp/x.state at frame 200`,
  16,803,666 B; restore → `resumed GameMIDlet from /tmp/x.state`, 60 frames,
  exit code 0.
* loopdemo.jar: save at frame 40 → 329,242 B; restore → `resumed LoopDemo
  from ...`, 30 frames.
* `tools/check_corpus_states.py --count 6 --seed 4 --frames 240
  --state-frame 200` → 6/6 titles round trip, 0 failed; the 18-title run in
  `.local-tools/debug-runs/goal13-verify/state-probe.log` passes every title
  whose MIDlet can be constructed.  The one failure, 1945空战英雄_6105,
  fails before any frame in its own constructor (`Class.newInstance()` on a
  package-private class, which the spec makes an IllegalAccessException);
  with the profile the title is documented to need (`--device nokia6230`) it
  saves at frame 60 and resumes cleanly, so it is a device-variant title, not
  a state defect.
* The requested frame is interpreted as *presented* frames and the snapshot is
  taken at the first boundary at or after it.  An exact-equality test was not
  enough: a title that sleeps inside `startApp()` or in a native call gets
  extra frames from the frame hook during one loop iteration
  (杀鬼子-敌后武工队 presents 240 frames in ~180 iterations), so equality at
  200 never fired although the run did 240 frames.
* `check_state_loop` now runs in ctest against `loopdemo.jar` (registered next
  to `test_loop_frames` in `CMakeLists.txt`); it asserts the state file, the
  `saved state to` line, `resumed LoopDemo from`, the resumed frame count, and
  that the loader does not refuse the file.

## Limits

* Doom 3's constructor joins its own threads, so the run never returns to the
  runner between the first native call and the end; async start does not help
  there and the run still reports "never reached a frame boundary". That is a
  property of the snapshot format, not of this fix.
* The snapshot is still a manual/CLI and libretro-manual-state feature: no
  rewind, runahead or netplay, and external RMS/files are not rewound by a
  state (documented in `docs/LIBRETRO.md`).
