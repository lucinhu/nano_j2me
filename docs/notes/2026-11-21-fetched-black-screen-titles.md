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

## Round 142: the NeonRunner renderer field goes null between a write and a read

Temporary interpreter tracing (a `NRDUMP=1` guard around the `putfield` /
`getfield` handlers in `src/core/nj_interp.c`, reverted immediately) produced a
concrete anomaly for `NeonRunner.jar`:

```
NRDBG putfield pc=224 field=a desc=Lk; op=181
NRDBG put  aLk recv=0x7fda35e021b0 val=0x7fda35e02620   <- non-null renderer
NRDBG get  aLk recv=0x7fda35e021b0 val=(nil)            <- same object, null
NRDBG putfield pc=75 field=a desc=Lk; op=181
NRDBG put  aLk recv=0x7fda35e021b0 val=(nil)            <- this.a = this.c, c was null
NRDBG get  aLk recv=0x7fda35e021b0 val=(nil)
```

Class `j` is heavily obfuscated and legally keeps many duplicate field *names*
with different descriptors (`private k a; private static j a; static Graphics a;
private e a; private k c; public int c; ...`), so field resolution has to match
on name **and** descriptor.  The write and the read above hit the same receiver
address with the same resolved field, yet the value read back is null, and the
game thread then copies the null `c:Lk` over `a:Lk` (run() bytecode 358-361).
`c:Lk` is only assigned inside method `b()` (bytecode 642), which run() calls
earlier (234) - the trace never shows that `putfield #136`(c:Lk), so `b()` is
returning/branching before it.  A direct `javap -p -c` read of the class is the
reference for the expected sequence: `run()` sets `c:B = 6` at 213-216, builds a
`k` and stores it into `a:Lk` at 219-221, then enters `j()`.

Next step for this title: check whether a GC cycle runs between the write and the
read (GDB watchpoint on the field slot, or run with GC tracing) - if the arena
slot is overwritten by a sweep/clear, that is a runtime bug, not a title bug;
otherwise trace method `b()` to see which branch skips the `c:Lk` assignment.

### Round 143 correction: which fields the trace actually touched

The second, more precise trace printed the resolved `nj_field` pointer and
offset, and the two "aLk" labels were not the same field:

```
NRDBG put aLk recv=...1b0 val=...620 f=...b7a0 off=208   <- this.a:Lk  (run 221)
NRDBG get aLk recv=...1b0 val=(nil) f=...b7c0 off=216    <- this.c:Lk  (never written)
NRDBG put aLk recv=...1b0 val=(nil) f=...b7a0 off=208    <- this.a = this.c (run 361)
```

So there is no same-field write/read inconsistency: `c:Lk` (offset 216) is
simply never written.  According to `javap -p -c`, the only `putfield #136
(c:Lk)` is inside method `b()` at bytecode 642, and `run()` calls `b()` at
bytecode 234 before the 358-361 `this.a = this.c` copy.  Either `b()` never
completes that assignment (its only `return` is at 1004; the exception table
covers only 264-268 and 278-324) or it unwinds early in a way we do not see in
the log.  Next step: gdb breakpoint on `nj_field_write_ref_fast`/the putfield
handler filtered on offset 216 plus a breakpoint on `b()` entry, and check
whether the constructor of `k` at b()+630 throws; if it throws, why the thread
keeps running (the MIDlet thread is still inside `j.k()/j.j()/j.run()` per
`--where`).

## Round 146: the sizeChanged() ordering fix was attempted and reverted

The round-143 trace explained the NeonRunner mechanism, and round 144 tried the
obvious fix: deliver `sizeChanged(w,h)` from `Display.setCurrent()` instead of
from the first paint.  That worked for NeonRunner (8,312 non-black pixels on
the CLI and through the libretro core, matching FreeJ2ME-Plus) but the round-145
libretro corpus probe caught a regression on an in-repo game:

```
家园5汉化版_10376.jar (Townsmen5)
  released r40 core: PASS, 72,592 non-black pixels (3/3 runs)
  fixed core:        startApp() failed:
                     java.lang.ArrayIndexOutOfBoundsException @ Townsmen5.LA+8
```

`Townsmen5.LA(int)` indexes `g:[[S` and is reached from the game's
`Canvas.sizeChanged()` -> `Townsmen5.F(I)`; the game expects `sizeChanged()` to
arrive after `startApp()` has finished initialising its tables.  The new
delivery point is inside `startApp()`, so the game's own initialisation code
runs against half-built state and throws.

Two follow-up variants were tried and neither kept both titles working:

1. skip delivery when the surface is zero-sized - Townsmen5 still failed;
2. defer delivery to the next frame boundary (a pending-canvas slot flushed at
   the top of `nj_midp_frame()`) - Townsmen5 passed, but NeonRunner went black
   again because its MIDlet thread installs the renderer before the boundary,
   so the late `sizeChanged()` overwrites it with a still-null slot.

Decision: revert the ordering change (`c6a823d`) instead of shipping a
regression.  After the revert a rebuilt core renders Townsmen5 with 72,592
non-black pixels again, and NeonRunner is back to the documented limitation
(0 non-black pixels, `gfx=0`).  A safe fix has to deliver `sizeChanged()` at a
runtime-controlled point that is after `startApp()` returns but before any game
thread can install UI state; that is a `nj_run`/VM-level pacing change, not a
Canvas-class change, and it is deferred until it can be done with the same
A/B evidence this round used.

## Round 166: sizeChanged() after startApp() does not fix NeonRunner

The round-144 attempt delivered `sizeChanged()` from `setCurrent()` (inside
`startApp()`) and fixed NeonRunner but broke Townsmen5.  The documented safe
point was "after startApp() returns, before any game thread runs", so round 166
implemented exactly that as an experiment:

* a new Canvas field `cSizeReady` and a public
  `nj_midp_deliver_pending_size(vm)` delivered `sizeChanged(screen_w,
  screen_h)` for the current Canvas from the runner right after the
  synchronous `startApp()` returned, before the cooperative scheduler ran a
  game thread; `canvas_visible()` then used the flag so the first paint did not
  deliver it a second time;
* debug output confirmed the call happened: `SZDBG current=… canvas=1`,
  `SZDBG delivered 240x320 exc=0`.

Result: **NeonRunner stayed black** (`frames=300 paints=300 gfx=0`, the same
`NullPointerException at j.paint(Graphics) pc=290` on every paint, renderer
field still null), while Townsmen5 was unaffected (`gfx=79988`, non-black
pixels 76,800/76,311 at 150/300 frames).  The experiment was reverted.

Conclusion: NeonRunner's renderer-null is not a sizeChanged() delivery-order
problem after all — the callback runs before the game's paint thread and the
game still copies a null `c:Lk` over `a:Lk`.  The remaining lead is the game's
own swallowed initialisation path (`run()` calls `b()`, which should write
`c:Lk` at bytecode 642; the trace never sees it).  Chasing it needs an
exception-trace of `b()`'s catch range, not another Canvas-ordering change.

### Round 169: NeonRunner boot sequence traced further (2026-11-22)

Re-examined `NeonRunner.jar` with the 0.5.25 CLI.  The title never leaves its
boot/loading loop: `--where` after 600 paced frames (10 s) and after 2400
frames (80 s) both show the game thread at

    j.b(I)V pc=15
    j.j()V pc=747 (or pc=634 calling j.k())
    j.run()V pc=228

i.e. inside `j.run()`'s `j()` loading-animation loop, and every presented frame
throws `NullPointerException at j.paint(Graphics) pc=290` (600/600 frames).
`javap` shows paint pc 284 is `getfield a:Lk` (the boot renderer), and pc 290
compares `k.a` with 127 — so the thrown NPE means `j.a` is null while
`j.c == 6` (paint's tableswitch case 6).  `j()` itself can null `j.a` at pc 278
(`a = this.c:Lk`, only when `state == 2 && !e.a`) and later clears it at pc 599
(`c = 7`).  `j()` exits only when `j.a` exists, `k.a == 15`, and 5 s have passed
since `j.h`; `run()` only calls the real loader `b()` at bytecode 234 *after*
`j()` returns, so the loading progress must happen inside the paint state
machine (paint case 6 writes `j.a` at pc 915).

`static a()Z` (run bytecode 12) checks `RecordStore.openRecordStore("names")`
and chooses the direct-load path when records exist; a fresh RMS takes the
animation path above.  FreeJ2ME-Plus renders the game's language menu after
20–40 s (screenshot: ENGLISH / ČEŠTINA / DEUTSCH / FRANÇAIS / ITALIANO /
ESPAÑOL), so the title is expected to reach an interactive screen.

A repeated run with a pre-existing `COMMON.rms` still threw the same 300 paint
NPEs, so this is not just the fresh-RMS branch.  Next lead: instrument the
first `j()`/paint interaction (the very first `Display.setCurrent()` paint
happens from `Main.startApp()` before `Thread.start()` sets `j.a` at run
bytecode 221; determine whether the runtime re-presents a frame while `j.a` is
null and whether the game relies on a paint-side loader step that our
synchronous frame pump never re-enters after the exception).  `GoldenSolitaire`
remains separately blocked on a game-side `InvalidRecordIDException` +
`ArrayIndexOutOfBoundsException` in `a.Code(ILjava/lang/Object;)` during boot.

### Round 170: it *was* a sizeChanged() ordering problem (2026-11-22)

The round-166 experiment delivered `sizeChanged()` after `startApp()` returned
but explicitly **before any game thread ran**, and that was still too late:
this runtime's `Thread.start()` lets the new thread execute `run()` before the
caller gets the CPU back, so the game thread had already stored its boot
renderer by the time the callback was delivered.  Tracing the field writes
proved it.  A temporary `NRDBG` print in the interpreter's `putfield` path for
the `j.a:Lk` field (and one in `canvas_call_paint_rooted`) showed, on the
first frame:

    NRDBG write a:Lk pc=224 o=… v=0x…620 in j.run
    NRDBG write a:Lk pc=72  o=… v=(nil) in j.sizeChanged
    NRDBG paint 0 canvas=… self=… a=(nil) c=6

`j.sizeChanged(int,int)` for width 240 executes `a = this.b; hideNotify();`
(the 240-wide "restore" path).  The runtime delivered that first
`sizeChanged(240, 320)` lazily from `canvas_visible()`, which
`canvas_call_paint_rooted()` calls on the first paint — after `run()`
bytecode 221 had installed the boot renderer.  The game's own callback then
overwrote it with the not-yet-set backup, so every paint dereferenced null.
On a handset the Canvas is shown while the MIDlet thread is still inside
`setCurrent()`, i.e. before the game thread's `run()` body gets going, so the
order is: `sizeChanged` (null → harmless), then the thread stores the boot
renderer.

**Fix**: the first `sizeChanged()` is now delivered from
`nj_midp_set_current()`, synchronously while the MIDlet thread still owns the
stack, and the delivered size is remembered in two new Canvas fields
(`cSizeW`/`cSizeH`).  `canvas_visible()` calls the same helper and therefore
still delivers resizes and still retries on the first paint if the title threw
out of the synchronous callback (the exception is dropped and the fallback
keeps the old lazy behaviour for titles that assume post-`startApp()` state).
`showNotify()` keeps its old "first paint" timing, so the 开心农场
`setCurrent(); setFullScreenMode(true);` constructor sequence still does not
see a visible Canvas.

Results:

* **NeonRunner now renders its language menu** (240x320, 8,312 non-black
  pixels, matching the FreeJ2ME-Plus reference); before the fix every frame
  was 0 non-black pixels with one paint NPE per frame.
* A/B against the same build from `dada347` with identical flags:
  `家园5汉化版` 600 frames gfx 68,263 → 67,790 (0.7%, scheduling noise),
  `开心农场_240` gfx 1,200 → 1,198 with one fewer first-frame paint — no
  visual or behavioural change in the sampled titles.
* New regression fixture `tests/sizeorderdemo` + `tools/check_sizeorder.sh` +
  `test_sizeorder` CTest gate: a Canvas whose game thread stores a renderer
  and whose `sizeChanged(240,…)` restores it from a null backup.  The gate
  **fails on the pre-fix build** (all-black frame, no `paint renderer` line)
  and passes after it.
* Full serial CTest after the change: **203/203**; the new gate makes it 204.
* `GoldenSolitaire` remains black for its own game-side boot exception
  (unrelated).

Lesson: a "deliver it after startApp returns, before game threads" theory can
still be too late because `Thread.start()` in this runtime is not deferred to
the next scheduler pass; only delivery *inside* `setCurrent()` matches the
handset ordering.

## Round 171: GoldenSolitaire resolved - `Class.forName()` did not initialise

The last black title from the round-135 table was a runtime bug after all:
the game's resource manager calls `Class.forName("p")` to force interface
`p`'s static initialiser (its fields allocate archive handles), and our
`Class.forName` returned without running `<clinit>`, so the archive tables
were sized without those slots and every paint threw a caught
`ArrayIndexOutOfBoundsException` in `a.Code(ILjava/lang/Object;)V`.

With the fix (`2c4b62d`, same details as the round-171 section of
`2026-11-21-api-coverage-audit.md`), GoldenSolitaire paints its language
selection screen (73,978 non-black pixels, reference 73,983), the sweep row
reads 600 frames / 599 paints / gfx=18,875 / ok, and the libretro visual probe
passes.  Of the original three black titles, NeonRunner (round 170) and
GoldenSolitaire (this round) are now fixed; the 128x128 Tomb Raider device
build is still to investigate.

## Round 176: five more blank-but-painting fetched titles (2026-11-22)

The 646-JAR fetched cache and the post-release r46 libretro visual probe
surfaced five titles that paint (non-zero `gfx`) yet produce only black frames
through both the CLI and the core at 300-900 frames:

- `Catan The Seafarers_320x240_Nokia_E71.jar`
- `Carcassonne_1.0.8_320x240.jar`
- `Jewel Explosion_320x240.jar`
- `Xmas Jewel Explosion__320x240.jar`
- `PAC-MAN (176x220) (1.0.0).jar` (the Sprint PCS rebuild; the round-175 API
  work made it *run*, but it paints black)

`MidnightPool1_1.0.0_320x240.jar` also probes black. For PAC-MAN the CLI dump
has zero non-black pixels at frames 100-600 while the stats report
`gfx=2402`; verbose logging shows one caught
`StringIndexOutOfBoundsException at java/lang/String.substring(II)` followed by
`all threads are waiting; assuming the app is done` on every frame. The sweep
classifies these `ok` because it only counts frames/paints/gfx; the visual
probe is the gate that sees them.

Not yet root-caused; likely candidates are a paint/renderer path that only
draws black (a Sprite/LayerManager consumer) or a title-side wait for input
that the generic key script does not satisfy. Next step is to dump the canvas
draw calls and check whether images decode, then compare against FreeJ2ME-Plus
for one title.

## Round 177: the blank class was mostly a canvas-size mismatch (2026-11-22)

Re-tested all six titles with explicit canvas sizes. `Carcassonne`,
`Catan The Seafarers`, `Jewel Explosion`, `Xmas Jewel Explosion` and
`MidnightPool1` are landscape (`320x240`) builds. On the default portrait
canvas they poll for portrait assets they do not ship; on 320x240 the four
purely 2D ones render in both the CLI and the libretro core. This was a probe
configuration error, not a renderer defect: the probe now selects the row's
screen size (`tools/corpus_screens/` plus a `_<w>x<h>` name suffix rule, with
`nano_j2me_screen` and `--expect-screen` on the libretro side).

Still open: `MidnightPool1` renders under the CLI at 320x240 but stays black
through the core and its `retro_serialize()` is refused with the round-176
state-service timeout; `PAC-MAN (176x220)` is black at every size we can
configure, its constructor trips over an empty Sprint `Muglet` URI and it then
waits on carrier state - the FreeJ2ME-Plus reference hangs on it as well, so
it is treated as a title-side/network limitation.

## Round 178 (2026-11-22): MidnightPool - a paint() busy-wait the libretro clock cannot satisfy

`MidnightPool1_1.0.0_320x240.jar` renders through the CLI at 320x240 (300
frames, `paints=300 gfx=4933`, ~4.7-5.4k non-black pixels per frame) but is
pure black through the libretro core at both 300 and 900 frames, with any heap
option (16 MB or auto).  The state service also refuses its save with
`retro_serialize(...) returned 0` / `save-state request refused (-8)`.

A temporary frame-dump option in the mock harness (`--dump-dir`,
`--dump-every`, RGB565->PPM) confirmed every sampled core frame is all zero.
Instrumenting the port (`--where` through `nj_vm_describe_threads` plus a
`DBGSTAT` line per 60 `retro_run` calls) showed the VM advances one frame then
stays inside a single paint forever:

```
where: thread 2 running depth=5
where:   java/lang/System.currentTimeMillis()J pc=0
where:   b.c()V pc=53
where:   b.paint(Ljavax/microedition/lcdui/Graphics;)V pc=280
where:   javax/microedition/lcdui/Canvas.serviceRepaints()V pc=0
where:   b.run()V pc=92
```

Disassembled `b.c()`:

```
a = this.b; b = System.currentTimeMillis(); G = (int)(b - a);
while (G < 50 && !bl) { Thread.yield(); b = System.currentTimeMillis(); G = (int)(b - a); }
```

so every paint waits for 50 ms of `System.currentTimeMillis()` progress - on a
handset that is wall time and the loop exits after ~50 ms.  In the libretro
port `lr_port_millis()` returns the virtual clock during `LR_PHASE_RUN`, and
that clock only advances on the frontend thread in `retro_run()`; the VM
thread is inside the paint, the frame never completes, `frames=1 paints=2`
after 300 `retro_run` calls, and once a save request is pending the 20 s
clock-assist budget is consumed and the clock freezes (`now=25110` for
millions of polls) - hence the save refusal.

Two experiments were run and reverted:

* Clock assist during any paint in RUN phase (return
  `virtual_ms + real_us_since_paint_start` while `nj_vm_in_paint()`), leaving
  `vclock_us` untouched.  `DBGPAINT` showed the returned value advancing with
  real time as intended, but the title still failed to complete the paint and
  stayed black with 162 M `currentTimeMillis` calls per session.
* Harness pacing changes (16.7/40/80 ms per `retro_run`): no effect.

So the remaining difference is not the pace or the heap; the core and the CLI
agree on the game's own timer only if paint wins 50 ms of real time, and the
current VM/frontend handoff does not give it that while a frame is in
progress.  Documented as a known core-path limitation (see
`docs/LIBRETRO.md`), not a rendering bug: the title's own paint limiter
requires wall-clock progress the synchronous frame model does not deliver for
this shape of app.  `PAC-MAN (176x220)` is a different, title-side case (empty
Sprint `Muglet` URI); the other landscape titles render fine with the round-177
per-title screen.



## Round 210: two K700 black titles from the new batch

- `TheMummy_K700F.jar` (87,718 B, 9 classes, "The Mummy [terjef] 1.1.9"):
  black through the CLI at 240x320, 176x208 and 176x220 (600-frame frame dumps
  all zero) and black through the libretro core at 300/900 frames.  Only
  exception in the log: `javax/microedition/rms/InvalidRecordIDException` from
  `RecordStore.getRecord` on the fresh RMS (caught by the title).  FreeJ2ME-Plus
  at 176x208 renders the game's splash ("PRESS START"), so this is ours.
- `Midnight Pool2.jar` (184,586 B, 5 classes): correction after frame dumps -
  the CLI renders the "DO YOU WANT SOUND?" prompt (240x320: 4,361 non-black
  pixels at frame 300, same as the FreeJ2ME reference), so the title is fine.
  Through the libretro core it stalls inside its first frame: one present at
  load, none afterwards, virtual clock advancing normally (NJ_LR_PROF
  posted=1, vm_frames=0), and `retro_serialize()` refused with -8.  This is
  the long-frame startup-stall class, not a black rendering bug.
- `TheMummy_K700F.jar` is still genuinely black: the CLI completes 300 frames
  with `paints=300, gfx=0` (its paint() never issues a draw call), stalls
  outright at 30 fps pacing (120 s, no frame budget completion), and FreeJ2ME
  renders the splash.  Next step: trace the MIDlet's Timer/loader path and the
  first Canvas' displayable setup (its only logged exception is a caught
  `InvalidRecordIDException` from a fresh RMS).

## Round 212: TheMummy is timer-driven; the stall is in its startup loop

Deeper tracing of `TheMummy_K700F.jar` (176x220, heap 16):

- `TheMummyMidlet`'s constructor starts two `java.util.Timer`s; the first
  task (`h`) is scheduled with `scheduleAtFixedRate(delay 0, period 20 ms)`
  and its `run()` is the game's whole per-tick logic (`f.d()`).  The MIDlet's
  `startApp()` then constructs canvas `a(String,int,int,int,int)` and switches
  between canvases `a`, `f` and `d` with `Display.setCurrent`.
- Frame dumps and `--stats` show `paints=300, gfx=0`: `a.paint()` returns at
  its first check unless `Displayable.isShown()` **and** the class' loader flag
  `a:Z` are both true; nothing draws until that loader finishes.
- Unpaced (`--fps 0`) the CLI spends only ~30 ms of wall time on 1200 frames,
  and the virtual clock follows real time there, so the 20 ms timer ticked
  only **twice** in an entire run: the game logic never advances and the
  loader never completes.  This is expected for a real-time-driven title, not
  the bug.
- Paced (`--fps 30`) the timer does run - 1,251 `h.run()` executions in 25 s
  with timestamps 20 ms apart - but the frontend never completes even 30
  frames.  The canvas loader calls `Canvas.repaint()` followed by
  `Canvas.serviceRepaints()` in its loop (`f` class, bytecode 826/830), i.e. it
  waits for paint, while `paint()` waits for the loader flag: the two meet and
  the startup never terminates under the frame pacer.  Next step is to trace
  `a.b()` (the loader, declared `throws Exception`) and find which flag
  `paint()` reads, then decide whether the runtime must deliver that paint
  from the timer thread's `serviceRepaints()` without the frame boundary.

Instrumentation used for this trace (a `NJ_TIMER_DBG` line in
`src/midp/nj_lib_util.c` `timer_pump`) was reverted afterwards; the worktree is
clean.

## Round 213: the second timer's helper is stuck runnable inside a C call

The round-212 trace left one question: timer 1's helper cycles every 20 ms but
`Timer.schedule(task, 250)` on timer 2 never fires. The round-213 thread-state
dump answers it.

The two helpers behave differently from the start: thread 2 (timer 1) enters
`nj2me/util/TimerThread.pump`, is scheduled with `wake_at = now + 20`, and the
scheduler genuinely wakes it (`SCHED wake t=2 in_c_call=0`). Thread 3 (timer 2)
sleeps once with `wait=250`, and then never appears in a wake again. A periodic
dump of the thread table shows why:

```
SCHEDT pass=201 id=2 state=2 incall=0 frames=1 wake=...342   (SLEEPING, normal)
SCHEDT pass=201 id=3 state=1 incall=1 frames=6 wake=...292   (RUNNING, in_c_call, stale wake)
```

`NJ_THREAD_RUNNING` plus `in_c_call=1` is the "parked inside a nested C call"
shape the scheduler's in_c_call branch in `src/core/nj_thread.c` hands back to
its owner (`parked_ready = 1; continue;`), on the assumption that the pump that
owns the C stack will resume the thread. But this thread is not owned by a pump
any more: its native (`timer_pump`) already returned `NJ_ERR_TIMEOUT` and
unwound. So nothing ever resumes it, the one-shot task `c` never runs,
`TheMummyMidlet.a()` never constructs the gameplay canvas, canvas `a.c()` is
never called, the paint gate `a:Z` stays 0, and every frame is black.

`wake_at` stays set on the RUNNING thread, so the scheduler's SLEEPING branch
never sees it again; the 1,251 later 20 ms wakes in the trace are all timer 1.

Next: find which `nj_call`-style C-to-Java bridge leaves `in_c_call` set when a
blocked native pump gives up (the clear sites are `src/core/nj_vm.c:1557`,
`:1778`, and the `stop_requested` early exit around `:1766`), and either clear
it on every exit path or let the scheduler re-enter a thread whose C call has
already unwound. Then re-check TheMummy's black screen and the other timer-driven
titles.

## Round 214: the stale C-call flag is cleared on the abort path too

The round-213 analysis identified the zombie shape (`state=RUNNING`,
`in_c_call=1`, stale `wake_at` for the timer-2 helper). The only path in
`src/core/nj_vm.c`'s blocked-native pump that skipped restoring
`th->in_c_call` was the embedder-stop exit: it set `st = NJ_ERR_HALT` and broke
out of the pump loop before `th->in_c_call = prev_in_c_call;`. That left a
thread marked "inside a C call" while its C caller was gone.

The restore now happens before the `stop_requested` check, so every exit from
the pump loop (runnable, dead, halt deadline, scheduler give-up, embedder stop)
leaves the flag as it found it.

Effect on the title: the second timer's one-shot task now executes
(`TheMummyMidlet.a()` builds the canvases, and the game reaches its RecordStore
load, logging `InvalidRecordIDException` from `RecordStore.getRecord`), which it
never did before. The title still does not complete a frame, so the remaining
stall is further down the startup path than the timer queue; the next lead is
the post-load paint/loader handshake (the round-212 `repaint()` +
`serviceRepaints()` loop).

Regression evidence: full serial CTest 210/210 in 365.99 s with the change
(`/tmp/r214-ctest.log`), including `test_vm`, `test_thread`, `test_save`,
`test_libretro_frontend`, `check_stack_trace`, `test_classinit`.

## Round 215: after the timer runs, the splash loader spins against the frame pump

With the round-214 fix the second timer's one-shot task now executes. What it
does is a three-stage splash loader: `TheMummyMidlet.a()` builds `/L0.png`,
`/L1.png`, `/L2.png` canvases in turn, and each stage runs
`a.a()` — a loop that calls `f.g()` (the game tick), then
`Canvas.repaint()` + `Canvas.serviceRepaints()`, and exits only once the
MIDlet's tick counter `a` reaches the per-canvas deadline `e = a + 150`.

Evidence from a thread-stack dump (temporary `NJ_WHERE_EVERY` hook, reverted):

```
thread 3 running depth=6
  f.g()V pc=24
  a.a()V pc=43
  themummy/TheMummyMidlet.a()V pc=150
  c.run()V pc=7
  nj2me/util/TimerThread.pump(Ljava/lang/Object;)Z pc=0
  nj2me/util/TimerThread.run()V pc=7
```

The loop never advances `a`: the tick counter is incremented by timer 1's
`h.run()` every 20 ms, but while the loader spins inside
`serviceRepaints()` the timer helper's native pump is the only thing that could
deliver the tick, and the nested C-call pump path (`vm->in_call_pump` set) only
sleeps 1 ms per pass instead of stepping the scheduler. So the splash loop
waits for a deadline nothing can advance, the frame budget never completes, and
the title hangs (paced only; `--fps 0` finishes 30 frames in 4 ms because the
virtual clock never reaches the 250 ms timer, and a 5-frame paced run exits
before the timer fires).

So the TheMummy black screen is a frame-pump reentrancy problem in disguise:
`repaint()`/`serviceRepaints()` called from a timer-task thread inside a native
pump must be able to deliver the tick that its own loop waits for. Next:
instrument `vm_invoke`'s nested pump to confirm the missing scheduler step, then
either step the scheduler from the nested pump or let `serviceRepaints()`
deliver a synchronous paint on the calling thread.

Temporary `NJ_TIMER_DBG` and `NJ_WHERE_EVERY` instrumentation was reverted; the
worktree is clean.

## Round 216: the timer ticks fine; the splash loader waits for game-state flags

The round-215 reentrancy hypothesis was testable and is now disproved. With an
env-gated trace in `timer_pump` (temporary, reverted) a paced run shows timer 1's
20 ms task `h` executing normally while the loader loops:

```
601 timer-task runs in 12 s, 2251 in 45 s (th=2 cls=h, exactly 20 ms apart)
1 run of the one-shot task c (th=3)
```

So `TheMummyMidlet.a` is incremented at the required rate and the timer helper
is not starved by the nested C-call pump.

Re-reading `a.a()` (the splash loop) explains the remaining spin. It loops
while `MIDlet.a < e` **or** `a.g != -1`, where `e = MIDlet.a + 150` at canvas
construction; the three stage canvases are built with `(g, c) = (-1,0)`,
`(-1,1)`, `(28,2)`. Stages 1 and 2 exit on `a >= e` (about 3 s each). Stage 3
cannot exit that way because `g = 28`; it only leaves early when
`f.c[4]` or `f.c[5]` is true (bytecode pc 50–66 of `a.a()`). After 45 s the
loader is therefore in stage 3, waiting for two `f.c[]` flags that the game
logic has not set.

Those flags are entity marks written by the level loader in `b.b(...)`/its
update (`getstatic f.c:[Z; ... bastore`, b.class bytecode ~4965–5042, indexed
by entity id, plus `g.a(5, false)` side effects). Our timer ticks call `f.d()`
50 times a second, but the flags stay false, so the remaining question is which
part of the game init never runs on our runtime — with FreeJ2ME the same title
reaches its "PRESS START" screen. Next: trace `b.b`/`f.d` state transitions
during stage 3 (or compare the entity table state after the same number of
ticks under FreeJ2ME) to find the missing init step; keys are not delivered
during the loader loop because the frame counter does not advance, so this is
not an input problem.

Temporary `RUN task=`/`NJ_TIMER_DBG` tracing was reverted; the worktree is
clean.

## Round 219: TheMummy loader structure and where the wait really is

Read the full loader chain from the class files (jar at `/tmp/r210-mummy`):

- `TheMummyMidlet.a()` (instance method, called from the 250 ms TimerTask
  `c.run()`) runs the whole boot sequence: it builds splash canvas `a` with
  `/L1.png` (`g=-1, c=0`), calls its `a.a()` loading loop; builds `/L2.png`
  (`g=-1, c=1`), calls `a.c()` (paint gate) then `a.a()`; builds `/L3.png`
  (`g=28, c=2`) and calls `a.a()`; then (offsets 161+) builds the real
  canvases, `d`, calls `d.a()` (the game loop) and starts over.
- Exit conditions inside `a.a()`: it loops while `MIDlet.a < e || a.g != -1`.
  The clean exit needs `MIDlet.a >= e` **and** `a.g == -1`.  With `a.g` set from
  the constructor, stages 1-2 (`g=-1`) exit after `e = a+150` ticks (about 3 s
  at the 20 ms timer).  Stage 3 has `g=28`, so it can only leave through the
  early branch when `c != 1` and (`f.c[4]` or `f.c[5]`); the `c=1` stage skips
  that check.
- `f.c` is written **only inside `f.class`** (by the entity tick `f.g()`);
  every `b.class` access to `f.c` is a read (`baload`) used by game logic.
  `f.a()` clears it.  So the flags are entity/alive marks for the world, and
  they never become true while the boot sequence is still running.
- Stage 3's loop body is: check shown; maybe `MIDlet.a(1)`/`MIDlet.c()`;
  `f.g()`; flag check; `Canvas.repaint(); Canvas.serviceRepaints();`.  The
  whole loader runs on the 250 ms timer helper thread — i.e. inside the
  scheduler's C-call pump for that thread — and it calls `serviceRepaints()`
  in a tight loop.
- The new hypothesis is therefore about the pump, not the clock: when the
  loader thread calls `serviceRepaints()` while its own C-call pump owns the
  frame, the paint that the loop waits for cannot run (the nested
  `in_call_pump` path only sleeps and steps nothing), so the world tick that
  would set `f.c[4]/f.c[5]` never gets a frame either.  That is consistent with
  every observation so far (2251 timer-1 ticks, flag-never-set, paint gfx
  never counted) and with the round-215 stack.
- Next experiment for round 220: instrument `c_serviceRepaints()` branch
  selection under a paced TheMummy run (paint wait, inline paint, frame wait,
  coalesce) and check whether a frame ever completes while the boot thread
  spins; if the frame wait is the blocker, the fix is to let the outer frame
  pump step the scheduler instead of sleeping when `in_call_pump` is set.

## Round 220: the boot thread's repaint storm never lets a frame finish

Instrumented `c_serviceRepaints()` branch selection (temporary, reverted) and
took periodic `nj_vm_describe_threads()` snapshots with a `SIGALRM` handler
during a paced run (`--fps 30 --run-frames 60`, 176x208, heap 16).

Measured (timeout 15 s, killed at rc 142):

- **195,400 `Canvas.serviceRepaints()` calls** by the boot thread in ~15 s,
  with `frame=1 painted=1` constant.  Every branch counter stayed 0: no
  paint-wait, no inline paint, no coalesce (`repaint_paced`), no frame wait.
  So each `repaint(); serviceRepaints();` pair returned immediately -
  `repaint_pending` was already 0, and `paint_active` was not set from the
  boot thread's point of view.
- Thread snapshot while hung:
  - thread 1 (`(current)`, depth 3) in `Graphics.drawImage()` <-
    `b.a(Graphics)` <- `a.paint(Graphics)` - i.e. the frame's paint is
    executing (and `b.a()` pc 730 is only the tail of the world draw loop;
    it is not a blocking instruction).
  - thread 2 sleeping in `TimerThread.run()` (timer 1 helper).
  - thread 3 (depth 6) in `Canvas.repaint()` native <- `a.a()` <-
    `TheMummyMidlet.a()` <- `c.run()` <- `TimerThread.pump()` - the boot
    thread's repaint storm.
- The frame never advances past 1 even though the paint is a normal, short
  world draw.  Combined with `c_repaint_all()` doing
  `nj_midp_repaint(); c_paint_now();` and `c_paint_now()` being skipped while
  `paint_active` is set, the observed shape is a livelock between the frame
  paint and the boot thread's repaint loop: the boot thread re-requests a
  paint on every iteration, `serviceRepaints()` cannot service it (the frame
  paint owns `paint_active`), and the frame paint never returns to the frame
  loop so the request is never fulfilled.
- Remaining question for the next round: whether the paint is entered with
  `paint_active` already restored to 0 (so the boot thread's repaints should
  coalesce but `paint_owner`/thread identity mismatches hide them), or whether
  the frame paint itself is being re-entered by the repaint path.  Candidate
  fixes: (1) make `c_serviceRepaints()` on a non-frame thread coalesce the
  request (set `th->repaint_paced`/pending) instead of returning, and let the
  frame loop paint once; (2) let the paint body service at most one pending
  request per frame and drop additional ones from other threads;
  (3) keep the frame pump from re-entering paint from a timer helper.  All
  need frame-dump parity checks on a normal timer-driven title.

## Round 221: paint is healthy - the nested frame pump is the livelock

Follow-up instrumentation (temporary, reverted): entry/exit counters around
`nj_midp_paint()` printing thread id, `prev=paint_active` and `in_frame` for a
paced TheMummy run (8 s, killed at rc 124).

Result: **683,229 paint() entries and the same number of exits in 8 s**, every
one on **thread 1**, with `prev=0` (no nesting), `in_frame=1` constant and
`st=NJ_OK, exc=0`.  So paint returns normally every time; round 220's "frame
paint stuck inside `drawImage`" reading was an alarm-sample artifact.  The
problem is the rate: ~85k paints/s inside a single in-flight frame.

Interpretation: the splash loader runs entirely inside the 250 ms timer
thread's nested `nj_call()` and never returns, so the runtime's frame pump
(`vm_invoke`'s frame hook while a native call is in progress) keeps pumping
frames; each pumped frame paints the splash canvas and presents, but the
loader's own `repaint(); serviceRepaints();` storm keeps the frame from ever
completing (in_frame pinned at 1).  The loader therefore never sees the
world-tick state it waits on (`f.c[4]/f.c[5]`), and the run spins with paint
at 100% CPU forever.

What this rules out and what it does not:

- Not a paint bug, not a paint deadlock, not a stuck scheduler step: paint
  executes and returns, exceptions are clear.
- Not (only) timer starvation: timer 1 ticks at exactly 20 ms and timer 2's
  250 ms task does run (rounds 214/216).
- Still open: why the loader's early stage-3 exit condition never becomes
  true while the world tick `f.g()` is called from the loader loop at 50 Hz.
  Next diagnostic should record `f.c[4]/f.c[5]` (and the `f` world state
  flags they depend on) at a few points instead of only at the end: a
  read-only `--watch-static` style probe on `f.c` would show whether the
  flags are ever written and then cleared, or never written at all.
- Candidate runtime fix (unchanged, needs a real trigger): bound the nested
  frame pumping for a timer-task helper so a non-returning task cannot pin
  `in_frame` forever; e.g. after N pumped frames without progress, stop
  pumping and let the task run to completion/timeout.  This is dangerous for
  titles that rely on pumped frames during a blocking native (纽约人生_1339),
  so it needs the corpus visual probe as the regression gate.

## Round 222: the loader is not hung - it advances its stages and then waits for a boot handshake we never perform

Direct probes of the game's own statics during a paced run (temporary
`isShown()` instrumentation, reverted; class names corrected to
`themummy/TheMummyMidlet`, `a`, `f`):

- `TheMummyMidlet.a:I` (the 20 ms timer 1 tick counter) grows exactly one per
  20 ms - 37 at 0.6 s, 259 at ~6 s, 418 at ~9 s.  Timer handling is correct.
- Loader stages advance on schedule: stage 1 (`a.g=-1`, `a.c=0`) until
  `MID.a >= e` (~150 ticks = 3 s); stage 2 (`a.g=-1`, `a.c=1`) another 3 s;
  **stage 3 (`a.g=28`, `a.c=2`) starts around `MID.a=418` (~8.5 s)** and then
  stays forever.
- The boot handshake flags are never set: **`MID.b:Z = 0` and `MID.d:I = 0`**
  for the whole run.  `TheMummyMidlet.a(I)` (which sets both and calls
  `f.a()`) is only invoked from the loader loop's `if (!isShown())` branch;
  our `Displayable.isShown()` returns true for the current canvas from the
  very first frame (deliberate design, see `d_isShown`), so the branch never
  runs.  `TheMummyMidlet.c()` - which clears `MID.b` and calls `f.a()` - is
  gated on `MID.b` being true, so world init never runs either.
- Stage 3's exit condition (from `a.a()` bytecode) is `c:I != 1` and
  (`f.c[4]` or `f.c[5]`); the background loop condition is
  `MID.a < e || g != -1`, and with `g=28` only the early exit can leave.
- `f.c[]` is written only by `b.class`'s collision/update code
  (`getstatic f.c:[Z; ... bastore` around bytecode 4965-5042) i.e. from the
  game update, which needs the world init above; consistent with
  `f.c[4]/f.c[5]` staying 0.

Experiment: temporarily made `isShown()` return `F_C_VISIBLE` (false until the
first paint delivers showNotify, environment switch) - behaviour did not
change: `MID.b` still 0, stage 3 still reached, `f.c` still 0.  Reverted; the
design comment already records that using `F_C_VISIBLE` there broke startup
wrappers on other titles, so that is not the fix.

Conclusion / next step: TheMummy is not hung and not timer-starved - it waits
for a MIDP boot handshake (`isShown()` false at least once right after
`Display.setCurrent()` so `MIDlet.a(1)` runs) that our synchronous
`setCurrent`/`isShown` model never produces.  Next probes: (1) count
`TheMummyMidlet.a(I)`/`c()` invocations to confirm neither runs; (2) check what
`TheMummyMidlet.a(I)` and `c()` do to world state; (3) decide whether a
narrow, opt-in "shown only after first showNotify" model is worth trying for
`com.nokia.mid.ui.FullCanvas` titles, with the 开心农场/startup-wrapper
regressions as the guard.

Round-222 addendum: a one-shot "first isShown() returns false" experiment (so
the loader would take the `MIDlet.a(1)` branch) also did not make the title
progress past stage 3 - the run still hung.  So the missing handshake is at
most one prerequisite of several; the next probe should first count
`TheMummyMidlet.a(I)`/`c()` calls and observe the `f` world state after each,
instead of inferring it from the loader flags.

## Round 223: Skater_by_BFLM is a first-paint timing class

`Skater_by_BFLM.jar` renders its title screen in FreeJ2ME-Plus but is black
through nano_j2me (CLI prints one
`NullPointerException at javax/microedition/lcdui/Graphics.drawImage(...)` per
paint, core fails `visual_nonblack`).

Structure: `Skater.startApp()` creates the `FullCanvas` subclass `b`, calls
`Display.setCurrent(b)` and only then starts thread `Skater$a`; that thread
calls `b.int()`, which loads `/res/img/sence.png`, `/res/img/trush.png`,
`/res/img/sewer.png`, `/res/img/car.png`, sets static `Q=4` and repaints.
`b.paint()` loads its cover image `/res/img/cover.png` **only** inside the
`Q==0` loading branch (`pc 13-19`, guarded by a catch that swallows the
exception) and draws it from the `Q==4` branch (`pc 450-465`).

On our runtime the first frame boundary comes after the loader thread has
already advanced `Q` to 4, so the cover load is never executed and every paint
hits `drawImage(null, ...)` at pc 468 (verified with temporary
`NJ_IMGDBG`/`NJ_DRAWDBG` instrumentation: loader images created, no
`cover.png` attempt; the null image is `new`).

Candidate fix and rejection: painting the newly current Canvas synchronously
inside `Display.setCurrent()` (mirroring a handset where the event thread may
paint before the app thread continues) fixed the title - `cover.png` loaded,
8,064 non-black pixels, 2,406 gfx calls vs 600, zero null-image NPEs - but a
150-title A/B sweep found `Vans_Skate_And_Slam.jar` hanging (ok -> timeout)
and several paint-pacing changes, because some titles' `paint()` expects the
MIDlet thread to still own the stack.  The experiment was reverted; the
change would need a delivery point that cannot nest the painter (for example
a queued first-paint request served at the next frame boundary before the
next timeslice), which remains open work.

### Round 225 addendum: why TheMummy's `!isShown()` branch cannot run in this runtime

Re-read `NJFN(d_isShown)` in `src/midp/nj_lib_lcdui.c`: it returns `cur == self`
directly, with the comment recording that keying it on `F_C_VISIBLE` (the
"showNotify delivered" flag) made a startup wrapper resume its application a
second time immediately after `setCurrent()`, before its loader finished.
So `isShown()` is deliberately identity-based: after `Display.setCurrent()`
returns, the canvas reports shown even though no paint or `showNotify` has
been delivered yet.

`TheMummyMidlet.a()` runs from the 250 ms `TimerTask` immediately after
`setCurrent()` and only calls `TheMummyMidlet.a(1)` (which sets `MID.d`/`MID.b`
and kicks the world init) from its `if (!isShown())` branch.  In this runtime
that branch is unreachable by construction; on a handset `setCurrent()` posts
the show request to the event thread, so a freshly started background thread
can observe `isShown() == false` until the event thread shows the Displayable.
Making that observation possible here would require modelling the event-thread
ordering of `setCurrent`/show/paint (the round-170 `sizeChanged` delivery was
the first piece of that model), not a local tweak in `isShown()`: the F_C_VISIBLE
variant was tried in round 222 and rejected both because `MID.b` still stayed 0
and because it is the shape that breaks the startup wrappers above.

Status: documented limitation of the synchronous display model; the title is
black only through this startup path (it renders in FreeJ2ME-Plus).  A faithful
fix is a structural event-thread model, tracked here as open work.

### Round 230: MidnightPool1 through the core — the clock is fine, the frame pump stalls

The round-176/178 note attributed `MidnightPool1` to its paint loop
busy-waiting on `System.currentTimeMillis()`.  Round 230 re-measured that with
instrumentation in `lr_port_millis` (env `NJ_MPDBG`, reverted afterwards).

`b.class` `c()` is the per-paint limiter:

    p++;
    a = b; b = System.currentTimeMillis(); G = (int)(b - a);
    while (G < 50 && !bl) { Thread.yield(); b = System.currentTimeMillis(); G = (int)(b - a); }

During a 120-frame core run the virtual clock advanced at the frontend pace
(`calls=200000 now=50 vclock=50`, later `calls=4000000 now=373`), i.e. the
50 ms delta is observed and the limiter exits normally.  So the loop is *not*
starved of wall time.

What does not happen is presentation: an env-gated trace in
`lr_display_present` showed **exactly one present for the whole run** —
`MPPRES posted=1 rect=0,0 320x240 cur=b` — while the harness frame dumps at
frames 50/100/150/200 were all zero non-black pixels.  The VM frame counter
also stays at 1: the title's own repaint loop keeps the MIDlet frame open, so
`nj_midp_present()` is never reached again and the frontend keeps sampling the
initial black buffer.

Consequence for the limitation note: the correct description is "the game
thread stays in its repaint loop and the core cannot complete a frame; the
clock itself advances normally", not "the paint busy-waits on a frozen clock".
The next diagnostic step (round 231) is to instrument the frame pump and
`serviceRepaints()` interaction to find why the frame never completes.

### Round 231: MidnightPool1 — one frame takes seconds and the VM clock mixes domains

Instrumented the libretro core path (temporary `STG`/`SFDBG`/`FP`/`SIGALRM` dumps in
`src/core/nj_thread.c` and `src/midp/nj_lib_midlet.c`, all reverted afterwards;
the clean core was rebuilt afterwards and the worktree is clean).

Setup: `test_libretro_frontend --core nano_j2me_libretro.so --jar
MidnightPool1_1.0.0_320x240.jar --frames 300 --pace-us 16667 --set
nano_j2me_screen=320x240`.  The session reports `frames=301 video_calls=301`,
non-black pixels 0/301, distinct hashes 1, audio nonzero 0, and only the two
serialize checks fail - i.e. the frontend runs 301 frames while the core keeps
re-sampling one black buffer.

Thread dump during the stall (SIGALRM, `nj_vm_describe_threads`):

    thread 1 running depth=0 (current)
    thread 2 running depth=4
      java/lang/System.currentTimeMillis()J pc=0
      b.c()V pc=50
      b.paint(Ljavax/microedition/lcdui/Graphics;)V pc=280
      javax/microedition/lcdui/Canvas.serviceRepaints()V pc=0
      b.run()V pc=92

Stage markers showed normal progress for VM frame 1 (start through
`frame_pace`).  Frame 2 enters `nj_scheduler_frame()` at t=1 ms and leaves it
at **t=9108 ms** (process CPU clock).  The pass ran one scheduler round and
then broke because `vm->painted_in_frame` became 1: the round executed the
application's own `paint()` through the C-to-Java call bridge, and that call
consumed ~9 s.

The same trace exposed a clock-domain anomaly in `nj_now_ms()`: the frame-start
value was `4294967329` (= 2^32 + 33) and the round-0 value was `4440732978`
(= 2^32 | 145,765,682, and 145,765,682 ms is ~40.5 h, i.e. host uptime).
`lr_port_millis()` returns `lr_real_ms()` (monotonic since boot) for phases
other than `LR_PHASE_RUN`/`LR_PHASE_LOAD`, and `nj_now_ms()` folds 32-bit port
values into a monotonic epoch (`+= 0x100000000` on a backwards step, then
`ticks = (ticks & ~0xFFFFFFFF) | ms`), so a stray real-time reading from a
non-RUN phase is enough to push the VM clock forward by ~40 hours (or an
epoch).  `nj_scheduler_frame()`'s budget check (`nj_now_ms(vm) - start >=
budget_ms`), sleep/wake deadlines and `port_sleep()` deltas all run on that
clock, so a mixed reading also makes one pass look arbitrarily long.

Consequence for the limitation: the core-only black frame is not a paint bug -
the VM spends seconds inside a single frame pass and completes only 2 VM frames
for 301 frontend calls, so the frontend only ever sees the initial frame.  The
clock-domain mix is the strongest candidate for why the pass takes that long;
next steps are to log `s->phase` together with every `lr_port_millis()` call
(or clamp the VM clock to RUN-phase readings only) and re-run this title plus
the CLI/harness regression set.

### Round 232: the core-only black class was an unanchored load clock

Round 231 left the MidnightPool1 core path with a VM clock that mixed the
virtual and real time domains (`nj_now_ms()` values `2^32+33` then
`2^32 | 145765682`, i.e. the host's ~40 h uptime in the low bits).

Root cause: `lr_load()` sets `s->phase = LR_PHASE_LOAD` but only `lr_unload()`
set `load_ms = 0; load_start_us = lr_real_us();`.  On the **first** load
`lr_unload()` never runs, so `load_start_us` was still 0 and the LOAD-phase
clock - `load_ms + (lr_real_us() - load_start_us) / 1000` - returned the host
uptime (~146,331,262 ms) instead of the milliseconds since the load began.
`nj_now_ms()` then treated that as a 32-bit wrap (`scheduler_ticks +=
0x100000000`), and the next small virtual reading folded another epoch in.  The
scheduler's frame budget (`nj_now_ms(vm) - start >= budget_ms`), sleep/wake
deadlines and `port_sleep()` deltas all use that clock, so one frame pass could
run for seconds; the frontend then only ever saw the initial black frame, and
`serialize_writes_state` was refused because the VM thread never reached a
frame boundary.

Fix: anchor the load clock in `lr_load()` itself, right after
`s->phase = LR_PHASE_LOAD`:

    s->load_ms = 0;
    s->load_start_us = lr_real_us();

Evidence (mock frontend, 300 frames, `--pace-us 16667`, 320x240):

- r56 dist core: `non-black pixels 0/76800`, `distinct hashes 1`,
  `FAIL serialize_writes_state` (core log `save-state request refused (-8)`).
- fixed core: `non-black pixels up to 8,348 of 76,800`, `frames_with_content
  308/317`, `distinct hashes 85/317`, all 45 harness checks pass, saved state
  round-trips (`serialize_size=157,593,664`).
- `Midnight Pool2.jar`: dist core FAIL (black + refused save) -> fixed core
  PASS with the same 300-frame setup.
- `Asphalt4EliteRacing_1.0.0_320x240`: still renders and still fails only
  `serialize_writes_state` - that title remains in the long-first-frame class
  for another reason.
- `杀鬼子-敌后武工队_2185`: PASS on both cores (the earlier failure was the
  known 8-job probe flake).
- Mock frontend matrix on the fixed core: 20 passed / 0 failed.

The `nj_now_ms()` clock-domain mixing stays a robustness gap: a stray real-time
reading from any non-RUN phase can still bend the VM clock.  A follow-up could
make `lr_port_millis()` return the virtual clock for every phase (or have
`nj_now_ms()` clamp implausible jumps) but the load anchor is the concrete bug
that affected the corpus.
