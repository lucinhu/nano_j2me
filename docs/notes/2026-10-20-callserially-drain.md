# 2026-10-20 - callSerially drain: one self-requeuing runnable per frame

## Symptom

Corpus game-087 (`tests/games/僵尸危机2_4349.jar`, Zombie Infection 2) timed out
in the 395-JAR corpus run: no frames presented in 35s, single thread stuck in
`State: R` burning CPU (345M bytecodes/s), `frames=5 paints=5` at 0.4fps.

## Root cause

The title's pump (`i.run()`) does `repaint`, draws, spins out its own 40ms
frame interval in a `Thread.yield` loop, then `Display.callSerially(this)` to
queue the next pass - the classic J2ME per-frame pump.

`display_drain_queue()` (src/midp/nj_lib_lcdui.c) re-checked `Vector.size()`
on every iteration, so runnables queued *while draining* were picked up in the
same drain. One `nj_midp_paint()` therefore executed the whole chain up to the
`guard < 64` cap: **64 pump passes x 40ms = 2.56s per frame** (matches the
measured `time=12.811s` for 5 frames exactly). The frame loop starved and the
corpus timed out. Mutation check: reverting the fix makes the new regression
test run 64/128/192 times in frames 1/2/3 - the cap, per frame.

## Fix

Snapshot the queue length before the loop: only runnables queued before the
drain run now; a re-queue waits for the next frame. This is what MIDP
specifies - callSerially runs "after the current event has been processed,
before the next paint", and a re-queue issued *during* the drain is a new
event for the next paint. FIFO is preserved (element 0 stays the oldest;
appends land after the snapshot count).

## Evidence

- `僵尸危机2_4349.jar`: 900f/600f headless runs now reach the "DO YOU WANT
  SOUND?" dialog at ~25fps (exit 0; was timeout). NPE at `e.b()V pc=714`
  (missing `URL-OPERATOR` property - the carrier JAD supplied it) is caught by
  the game's own handler, as in FreeJ2ME.
- Regression test `test_callserially_once_per_frame` in tests/test_lcdui.c:
  builtin Runnable fixture that re-queues itself; asserts exactly 1 run per
  paint for 3 frames. Fails with 64x per frame against the old drain.
- 84/84 ctest, 0 build warnings.

Runs: `.local-tools/debug-runs/corpus-20261017/game-087-zombie/{fix.log,fix-*.ppm}`.
