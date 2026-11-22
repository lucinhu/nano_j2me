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
