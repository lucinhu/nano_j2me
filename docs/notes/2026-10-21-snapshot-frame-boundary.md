# A snapshot needs a frame boundary — and a run that never reaches one now says so

Status: fixed (runner diagnostics). Ongoing limitation: a title whose first
native call consumes the whole run cannot be snapshotted in the middle, by
construction of the snapshot format.

## The symptom

`--save-state FILE --save-state-frame N` on `毁灭战士3_10598.jar` wrote no
file and printed nothing at all:

    /tmp/nj-def/nj2me --port headless --size 240x320 --heap 16 --fps 30 \
        --run-frames 700 --save-state doom.njs --save-state-frame 300 \
        tests/games/毁灭战士3_10598.jar

The run itself was fine — 700 frames, 360 paints, `tris=189110`, 28.8 fps — and
`--keys` worked on the same title, so option parsing was not the problem.
The control did save:

    ... --run-frames 120 --save-state control.njs --save-state-frame 60 \
        tests/fixtures/slimevolley.jar
    saved state to control.njs after 60 frame(s)

## Why no snapshot was attempted

Doom's MIDlet constructor starts the two threads that run the game and then
joins them (`.local-tools/debug-runs/doom-round17-static/verified-original.javap`,
`Doom.Doom.<init>`: `Thread.start` at pc 74 and 89, `Thread.join` at pc 93 and
100; the second joined thread is `Doom/m`, whose `run()` is the menu loop and
then the game). The constructor therefore does not return while the game runs.

The runner's frame budget is spent by the frame-hook pump that keeps the
display alive while a Java thread is parked in a blocking native
(`run_frame_hook`, `src/runner/nj_run.c`). When the budget runs out there,
`nj_vm_new_midlet()` returns NULL with the stop reason "the frame budget ran
out", the run jumps to the `done:` label, and the explicit frame loop — the
only place `--save-state` was honoured — is never entered.

Taking the snapshot from the pump would not help. `nj_vm_can_save()` refuses
while a native call is on any thread's stack, and it refuses re-entrantly from
inside its own frame hook (`src/core/nj_save.c`, the comment on
`native_depth != 0`: "A native call means a C stack frame is live between the
Java frames we would save and the code that would resume them. Nothing in the
blob can represent it"). The pump *is* inside that native call, so there is no
representable point at which to write the file. An unrestorable snapshot would
be worse than none.

## What changed

The runner now distinguishes three outcomes at the end of a run
(`src/runner/nj_run.c`, right after the `done:` label):

* a snapshot was attempted — the existing "saved state to ..." or "cannot save
  state to ... (n); the run continues" message stands;
* no frame boundary was ever reached — "this title never reached a frame
  boundary (its first native call consumed the whole run). A snapshot cannot
  represent a live C stack frame, so there is nowhere to take it.";
* boundaries were reached but the requested one was not — "the run ended
  before the requested frame boundary N. Ask for a boundary inside the run
  (--save-state-frame).".

The first case is a limitation of what a snapshot can represent; the second is
a wrong request, and only that one is the caller's to fix, so they are worded
apart. `--help` documents the limitation under `--save-state-frame`.

## Evidence

* Real title: `.local-tools/debug-runs/doom-snapshot-r6/log` — the 700-frame run
  above now ends with the "never reached a frame boundary" warning, followed by
  the usual stats line; the control
  `.local-tools/debug-runs/doom-snapshot-r6/doom.njs` does not exist and
  `control.njs` from slimevolley is 338176 B.
* Blocked constructor without a real game: `tests/fixtures/waitdemo.jar`
  parks in its constructor until a worker thread wakes it
  (`--prop fixture.wait.ms=3000`); with a 30-frame budget the whole run is
  spent in that native, and the warning above is what the runner prints.
* Boundary beyond the run: `fielddemo.jar` with `--run-frames 5` and
  `--save-state-frame 50` reports the second message.

## Tests

`tests/savestate/run.sh` gained two sections (registered as
`check_sdl2_savestate`):

* section 4 runs `waitdemo.jar` with `--prop fixture.wait.ms=3000` and a
  30-frame budget, asserts the "never reached a frame boundary" message, exit
  code 0, and that no snapshot file exists;
* section 5 runs `fielddemo.jar` for 5 frames with `--save-state-frame 50`,
  asserts the "before the requested frame boundary 50" message and that no
  file exists.

Sections 1–3 (save, reload, garbage file) are unchanged.

## Limits

* A title that spends its entire run inside a native call still cannot be
  snapshotted mid-run; the runner now names the reason instead of leaving a
  silent hole. Doom 3 and the Need for Speed titles are of this shape.
* When the budget ends during the constructor pump, `--keys` still works: the
  pump feeds the key script with `nj_vm_frames(vm)`, which is why Doom's menus
  could be driven before this change. Only snapshots were silently skipped.
