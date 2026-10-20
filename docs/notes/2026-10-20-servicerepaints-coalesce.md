# 2026-10-20 - serviceRepaints: one synchronous paint per frame

## Symptom

Corpus game-188 `扫雷行动` (Minesweeper Mobile / JAMine) timed out at 35s.
Measured with `--run-frames 120 --stats`:

```
frames=120 paints=70320 gfx=20113229 fps=4.7
bytecodes=2.5G (98 M/s) heap=45617392/45617648   <- heap pinned at the ceiling
```

586 paints per frame.  The rendered frames were correct (a static
"Choose language" menu), so this was purely a paint-rate problem, not a draw
bug.

## Root cause

`f.run()` in the JAR is a **sleepless spin loop** (~40 bytecodes per pass),
and the game overrides `flushGraphics()` itself as:

```java
public final void flushGraphics() {
    repaint(0, 0, getWidth(), getHeight());
    serviceRepaints();   // synchronous paint on EVERY call
    as++;
}
```

nano's `c_serviceRepaints()` (src/midp/nj_lib_lcdui.c) honoured "blocking"
by painting synchronously on every call, unconditionally.  One
`NJ_SCHED_QUANTUM` (20000 bytecodes) therefore ran ~586 loop passes =
586 full paints + buffer copies = ~211ms inside a single quantum -> 4.7fps ->
corpus timeout.

FreeJ2ME coalesces instead: `repaint()` stacks into `needsRepaint`
(unionizing the clip region) and `serviceRepaints()` waits for the
frame-driven paint thread to clear it.  Effect: at most one paint reaches the
screen per display frame; a spin loop self-throttles.  MIDP's contract agrees
- repaint requests are cumulative, serviced "before the next paint".

## Fix

`c_serviceRepaints()` now only paints when the current frame has not
presented yet:

```c
if (vm->repaint_pending && !vm->in_paint && !vm->painted_in_frame) { ... }
```

`painted_in_frame` is set by the first paint/flush of the frame
(nj_midp_paint / gc_flush_region) and cleared at the end of every
`nj_midp_frame`, so this caps synchronous paints at one per frame while
keeping the synchronous behaviour the `visible=true; repaint();
serviceRepaints()` idiom needs (its first call in a frame still paints).
`repaint_pending` stays set for the next frame instead of being burned.

## Evidence

`扫雷行动` after the fix (`--run-frames 120 --stats`):

| metric | before | after |
|---|---|---|
| paints | 70320 | **115** |
| fps | 4.7 | **30.3** |
| time | 25.5s | **3.96s** |
| heap | 45617392/45617648 | **686256/686576** |

- Corpus-shaped run (`--port headless --run-frames 180`, 35s timeout):
  **exit 0, 180 frames in 6.0s** (was timeout).
- Input path intact: `--keys "60:17,120:17"` selects English at frame 60 ->
  "Enable Sound?" Yes/No dialog, stable through frame 300 (f11-strip.png).
- Baseline comparison, same three sync-idiom titles, before vs after:
  仙魔飞行棋 600/600 paints 30.2fps (unchanged); 鲁宾逊漂流记 9/9 paints
  30.1fps (unchanged); 地心探险 335->325 paints, identical rendering
  (earth-strip.png, title menu intact).
- 僵尸危机2 (the callSerially fix) still 600 paints/600 frames, 24.9fps,
  exit 0.
- Full ctest **84/84 pass** (216s), build clean 0 errors/warnings.

Runs: `.local-tools/debug-runs/corpus-20261017/game-188/{f9,f11,earth}/`,
strips `f9-strip.png` (menu stable), `f11-strip.png` (key input),
`earth-strip.png` (regression).
