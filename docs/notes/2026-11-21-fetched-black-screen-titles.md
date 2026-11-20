# Three fetched titles paint nothing through the core (round 135, 2026-11-21)

The 862-title libretro visual probe on a HEAD core (with the round-134
`Boolean.TRUE/FALSE` fix) ends **840 passed / 2 known limitations / 3 failed**.
The three failures were all fetched archive titles, and all three also render
black in the CLI, so the sweep's `ok` verdict for them is too generous:

| title | sweep row | CLI | libretro probe | FreeJ2ME-Plus |
|---|---|---|---|---|
| `vault/GoldenSolitaire.jar` | ok, 600 frames / 600 paints | rc=0, 300 paints, **gfx=0** | black (max non-black = 0) | language-selection screen renders (73,983 non-black px) |
| `vault/NeonRunner.jar` | ok, 600 frames / 600 paints | rc=0, 300 paints, **gfx=0**, 36.6M bytecodes | black | language-selection screen renders (8,312 non-black px) |
| `vault/Tomb Raider Anniversary v0.3.2 (CLDC-1.0, MIDP-2.0) (s40v2) (128x128).jar` | ok, 600 frames / 601 paints | rc=0, 300 paints, **gfx=0** | black | (one of the 17 Tomb Raider device builds) |

Evidence recorded while triaging:

* `GoldenSolitaire` logs two caught exceptions (`InvalidRecordIDException` on
  the first `RecordStore.getRecord`, then `ArrayIndexOutOfBoundsException at
  a.Code(ILjava/lang/Object;)V pc=11`), then paints blank for every frame with
  only ~240k bytecodes in 300 frames.  Heap/device variations (`--heap 32
  --device nokia6230`, 900 frames) change nothing: still `gfx=0`.
* `NeonRunner` throws `NullPointerException at j.paint(Graphics) pc=290` on
  every paint.  Disassembly shows the paint path begins with
  `getfield a:Lk; getfield k.a:B` — the renderer object `this.a` is null, so
  the game's own language screen never draws.  This is a title-initialisation
  assumption, not a runtime API that returns null.
* Both references draw a language selection screen in FreeJ2ME-Plus, so the
  titles themselves are fine.

Why the sweep said `ok`: its verdict only checks exit code, frame count,
paint count and uncaught exceptions; a Canvas that paints an empty frame
passes.  The stats line already carries the draw-call counter, so
`tools/sweep_compat.py` now records `gfx` and `natives` in every row, which
makes "painted 600 frames but made 0 draw calls" visible when reviewing a
sweep without re-running a visual probe.

Next step for these three: reproduce the initialisation that builds the
language screen (RMS/config load path for GoldenSolitaire, the `k` renderer
construction for NeonRunner) and decide title-by-title whether the runtime is
missing an API or the port needs a device profile.  Until then the visual
probe keeps reporting them as failures rather than suppressing them.

## NeonRunner: the run() thread is alive but its renderer slot is cleared

`--where` (round 137) shows the game thread parked exactly where it should be
for a frame-paced title:

    where: thread 2 running depth=3
    where:   j.k()V pc=12
    where:   j.j()V pc=634
    where:   j.run()V pc=228

`j.k()` is `repaint(); serviceRepaints();` (`pc=12` is the return past the
service call), called from the screen animation loop `j.j()` which `run()`
reaches after setting the state byte to 6 and installing the first renderer
(`this.a = new k(...)` at bytecode 220-221).  Yet every paint in state 6
throws `NullPointerException` at `getfield a:Lk` (pc≈290), i.e. `j.a` is null
again by paint time.  The class reassigns `j.a` from several pre-built screen
renderers (`j.c`, `j.e`, `j.f`, `j.g`, `j.j`, `j.b`) inside its screen-switch
methods, so one of those slots is null; the renderer construction path
(`j.a(int...)` / `k.<init>`) either threw into a game-side catch or never ran.
The thread is not dead (no uncaught-exception line, and `--where` reports it
running every time), so this is a game-initialisation ordering issue under the
runtime, not a scheduler hang.  Next step would be a runtime trace of the
`k` constructor / `j.a` helper to see which side throws; `--where` plus the
sweep's new blank-canvas note is enough to find titles like this without a
visual probe.
