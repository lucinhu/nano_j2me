# Street Fighter II: the loader that ran twice and then hung itself

2026-10-27. 街头霸王2冠军典藏版_1812: answering the opening sound-on/off prompt
with the left soft key ("yes") starts the title's asset loader, and the run
hung in an infinite loop instead of reaching the main title screen.

## The short version

- Not a title defect and not a decode/resource defect: the runtime re-entered
  `GameCanvas.paint()` recursively. The title's loader is a state machine
  kept in `paint()`, so one state advance per frame is contract; the runtime
  advanced it twice in one frame, which skipped the step that fills the
  sample table (`bf.dB`), and the next frame dereferenced that NULL array.
  `paint()` threw `NullPointerException`, the title caught it with an empty
  `catch` handler that is itself a `goto` self-loop, and the run hung.
- Root cause: the frame pump that runs while a Java thread is parked in a
  blocking native (`src/core/nj_vm.c:1499-1503`, hook in
  `src/runner/nj_run.c:720`) pumped a whole nested `nj_midp_frame()` while the
  *active* frame was still inside `paint()` - the title sleeps in
  `Thread.sleep()` between asset reads from `paint()`. The nested frame did
  not paint (the paint was already in flight) but its tail at
  `src/midp/nj_lib_midlet.c:469` cleared `vm->painted_in_frame`, so the very
  next pump iteration saw "not painted yet" (condition at
  `src/midp/nj_lib_midlet.c:434`) and re-entered `paint()`.
- Fix: a paint is atomic, so no frame is pumped while one is being delivered.
  `nj_midp_paint()` now marks `vm->paint_active` for the whole call, blocking
  included (`src/midp/nj_lib_lcdui.c:5488-5502`, field at
  `src/core/nj_internal.h:674`), and `nj_midp_frame()` returns `NJ_OK`
  immediately when a pump offers a frame during that window
  (`src/midp/nj_lib_midlet.c:325`). The pump keeps its scheduler running, so
  the other Java threads still progress and the blocked paint resumes. Frames
  pumped while no paint is executing are untouched: a MIDlet that runs its own
  loop in `startApp()` or a parked `TimerTask` still gets presented frames,
  which `tools/check_timer_wait.sh` pins down.
- Verified against the reference runtime: FreeJ2ME-Plus plays the same key
  sequence through the sound prompt, the ~6 s loading screen and reaches the
  same main title screen ("街头霸王" logo + character art). Our runtime now
  dumps the same screen (`/tmp/sf2rev-frames/00600.ppm`).
- Regression: `tools/check_sf2_loader.sh`, registered with ctest as
  `check_sf2_loader`; 600 frames, soft key at frame 400, expects exit 0 and no
  `NullPointerException`. The budget is 600 on purpose: 550 frames passes even
  with the defect present.

## How it was found

1. The NPE was the title's own, at `c.bZ()V pc=44` (`baload` of field `dB:[B`,
   null). `dB` is assigned in exactly one place, `c.bB()` state `d==1`
   (`bB` pc 1220-1239). It is read by `c.bA()` -> `c.bZ()` from state `d==6`.
2. Instrumenting field writes showed `d` going `0 -> 1 -> 2` with **no**
   `putfield dB` and no `c.d(int)` call in between: the jump from 1 to 2 was
   taken at the `d==0` branch's `d++` site, not the `d==1` branch's. The state
   machine had been advanced twice.
3. Instrumenting native calls put the smoking gun in the stack: the outer
   frame was `nj_midp_paint -> c.paint -> c.bB -> c.n(I) -> c.a(String) ->
   Thread.sleep(J)`. The pump then ran a nested frame while that sleep was
   blocked; the nested frame's paint was skipped (`painted_in_frame` still
   1), but its exit cleared the flag, so the *second* pump iteration painted
   again and re-ran the loader's `d==0` branch (`d` still 0). The sleep
   resumed, the outer branch finished and wrote `d=2`, and the `d==1` step
   that loads `dA`/`dB` never ran again.
4. `c.bZ()` has its own exception table: the throw at pc 37-69 jumps to pc 72
   `pop; goto 73`, and pc 73 is `goto 73`. That is why the failure mode is a
   silent infinite loop rather than a crash report - and why `--run-frames`
   is the only way to see it (it happens around frame 560-570).

## Evidence

- Repro (before fix): `--port headless --heap 16 --run-frames 600 --size
  176x208 --fps 30 --keys "400:soft1"` on the title jar -> timeout, log
  `/tmp/sf2-clean.log` equivalent, `thrown: java/lang/NullPointerException at
  c.bZ()V pc=44`; last dumped frame 00560.
- After fix: same command exit 0, `run ended: the frame budget ran out`,
  frames `/tmp/sf2rev-frames/` reach the main title screen at frame 600/700
  (identical, 126 colours), matching the reference screenshot
  `/tmp/refsf2-long/04-after6s.png`.
- Control: the same title without keys runs its static sound question screen
  at any budget; RMS content is irrelevant (pre-seeded and empty `--rms-dir`
  behave the same). The only variable is whether the loader runs long enough
  to hit its first sample-table read.
