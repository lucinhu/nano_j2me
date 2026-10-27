# The frame pump inside a paint: 纽约人生_1339 (2026-10-27, round 92)

## Symptom

At `8a54e23` the full corpus sweep reported a new failure:

```
纽约人生_1339  320x240  ...  FAIL (nothing was ever presented; ...)
```

The title had passed at `9d32766` (before the Street Fighter II paint-atomic
fix, `783bdc2`), and the same regression appeared at every size the sweep
tried.

## What the title does

`纽约人生_1339` keeps its asset loader in `paint()`: the first paint enters the
loader state machine and blocks in `Thread.sleep()` between asset reads.  That
is the same shape as 街头霸王2冠军典藏版_1812 (see
`docs/notes/2026-10-27-street-fighter-loader-hang.md`), and it is why the
blocking-native frame pump in `vm_invoke()` exists at all: a MIDlet that runs
its own loading loop inside a callback still has to present frames.

## Mechanism of the regression

`783bdc2` made `nj_midp_frame()` return early when `vm->paint_active` was set:
"a paint is atomic, no frame may be pumped while one is being delivered".
That stopped the paint re-entrancy - but `run_frame_hook()` counts every frame
the pump asks for, refusal or not:

```c
key_script_tick(...);
(void)nj_midp_frame(vm);   /* returns early while paint_active */
ctx->drawn++;
if (ctx->budget != 0u && ctx->drawn >= ctx->budget) { stop }
```

The loader parks inside `paint()` for a long time (many sleeps, one per
asset).  Every pump iteration was refused in microseconds, `ctx->drawn`
reached the run's whole budget, and the run ended after **one** presented
frame: `frames=1 paints=1 time=0.012s` versus `frames=219 paints=70 time
9.7s` before the fix.  The sweep's verdict logic only sees the dump, so a
title that was still loading looked like one that never drew.

The same count also affected `nj_midp_run()`'s own loop, which counts
attempted frames; a blocked paint on the runner thread would end there too.

## Fix

Keep the nested frame, skip only the paint:

- `nj_midp_frame()` no longer returns early; it does its input drain,
  scheduler slice and present as usual.
- The per-frame paint gate gained `!vm->paint_active`:

```c
if (!vm->paint_active &&
    !vm->painted_in_frame &&
    (!vm->app_painted || vm->app_paint_idle > vm->app_paint_period)) {
    (void)nj_midp_paint(vm);
}
```

So the paint that is still executing cannot be re-entered (the Street Fighter
hang stays fixed), while the pumped frame is a real frame: it advances
`vm->frames`, presents the displayable, and lets the blocked paint resume.
The title then presents at its own loader pace and the run ends on its budget.

## Verification

- `纽约人生_1339` at `320x240`, 200 frames: `frames=200 paints=6 time 7.5s`
  (`frames=1` at `8a54e23`); 30 fps pacing, budget end, no exception.
- Full corpus sweep at the fixed build (`.local-tools/debug-runs/` not
  committed; log `/tmp/corpus-sweep-fix.log`): results recorded in
  `docs/VERIFICATION.md`.
- Street Fighter II still fixed: `tools/check_sf2_loader.sh` passes (600
  frames with `400:soft1`, no NPE, budget end).
- New regression test `tools/check_paint_pump.sh` (ctest
  `check_paint_pump`): 200-frame run must present at least 180 frames.
  At `8a54e23` it fails with `frames=1`; here it passes.
- Other paint-sensitive titles re-checked by the same corpus sweep:
  仙魔飞行棋_417 (synchronous painter), 合金弹头2013BT版_678 (HUD painted in
  `paint()` and continues drawing with the kept Graphics), 开心农场BT版_6075
  (two startup paints then quiet), 炸弹人_10311 (never repaints) all keep
  their verdicts.

## History

- `783bdc2` fixed the Street Fighter hang by refusing pumped frames.
- `8a54e23` merged that with the remote media/`lcdui` work and was pushed;
  the corpus sweep then found this regression.
- Commit for this note: `fix(midp): let the frame pump run its frame while a
  paint is executing` (see `git log`).
