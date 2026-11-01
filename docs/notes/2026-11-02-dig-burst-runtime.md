# The Minecraft dig burst: where the 8 ms goes, and the Vector scan inside it
# (2026-11-02)

Holding the destroy key in the saved Minecraft PE world costs three frames of
~7.6, ~7.5 and ~5.7 ms instead of the steady ~1.6 ms, once per chunk-mesh
rebuild.  This note profiles that burst on the desktop, separates the game's own
rebuild work from the runtime's, and removes the runtime's share: the M3G
`Group.addChild()` duplicate check was scanning a 700-child `java.util.Vector`
with a full interpreter round trip per element.

Round 1 of the investigation had already established that the burst is real,
reproducible, and lives entirely in the game's Java thread (`nj_scheduler_frame`
= 100% of the spike; no GC, no file I/O, no input/media/paint time).  What it
did not answer was which part of the game-thread work belongs to the runtime.

## Reproduction

Saved world (Creative, 240x320, game paused after frame 2599), resumed from
`.local-tools/mc051/world.state` with a fresh copy of `fs/` and `rms/` per run
(the game writes the world back on exit):

```sh
taskset -c 13 ./build/nj2me --port headless --heap 64 --size 240x320 --fps 0 \
    --rms-dir RUN/rms --fs-dir RUN/fs \
    --load-state .local-tools/mc051/world.state --run-frames 700 \
    --keys '5:pressed@120,160 15:dragged@120,250 25:released@120,250 \
            40:101:down 330:101:up' MinecraftJ2ME_PE_0.6.1_V2.2.jar
```

The pointer drag aims the crosshair down at the seabed sand, then raw key code
101 (the game's destroy binding, `Keys.java`) is held.  The block breaks; the
frame dumps show the crack, the particles and then the hole.

The bursts are **time-driven, not frame-driven**: the same input script puts
them at frames 308/403/510 in one run and 200/283/390 in another, so frame
indices cannot be compared across runs.  Each 700-frame run contains exactly
three bursts of three frames (the 2-40 startup frames are the world prepare and
are excluded).  An idle run with no input script has no such bursts at all.

## Method

Temporary instrumentation (all of it removed before the commit):

* `NJ_FRAME_LOG`: one CSV row per frame with wall microseconds and VM counter
  deltas (bytecodes, natives, gfx calls, triangles, M3G renders, GC count, heap,
  file opens/writes/reads/bytes).
* `NJ_NATIVE_LOG`: `nj_invoke_native()` wrapped in `rdtsc`, so every native
  function got an exact call count and cycle total per frame; a 1-in-256
  bytecode sample collected the hot Java methods per frame.
* `NJ_PHASE_LOG`: input / scheduler / media / paint / present / pacing split of
  `nj_midp_frame()`.
* Non-PIE build (`-fno-pie -no-pie`) so runtime native addresses decode with
  `nm -n`, plus a `-pg` build for the flat profile.

`nj_midp_frame()` is re-entered for nested frames while a Java thread is parked
in a blocking native, so the CSV writer was guarded with an outer-frame depth
counter; without that guard a nested frame flushed (and cleared) the native
tables mid-burst.

## Where the burst goes

Per-frame attribution of the three burst frames (700-frame dig run, symbols from
`nm -n`):

| frame | wall | bytecodes | native time | what it is |
| --- | --- | --- | --- | --- |
| 1st | 7.6 ms | 2.9-3.4 M | ~0.03 ms | game rebuild: `LevelRenderer.makePart/combine/mergeRects/plane` |
| 2nd | 7.5 ms | 2.0-2.4 M | ~2.8 ms | rebuild + M3G mesh upload (`m3g_varr_set_bytes`), `m3g_removeChild` |
| 3rd | 5.7 ms | 0.7-1.0 M | ~4.4 ms | render the rebuilt chunk: `m3g_render_node`, `m3g_addChild` |

The 3rd frame is where the runtime cost concentrates: `m3g_addChild()` 13 calls
~1.30 ms, `m3g_removeChild()` 10 calls ~1.31 ms, `m3g_render_node()` 2 calls
~1.1 ms.  (Native time is inclusive of nested natives: `m3g_addChild()`'s total
contains the `Vector.indexOf()` native it calls.)

The Java rebuild itself (2.9 M + 2.2 M bytecodes for the first two frames) is
Minecraft's own chunk-mesh bookkeeping and has no emulator-side pathology: the
phase split puts 100% of those frames on the game thread, with the interpreter
at its usual throughput (~250 M bytecodes/s here), no GC, no allocation storm
in the statistics, and no file activity.

## Root cause: one interpreter round trip per Vector element

`LevelRenderer` adds each rebuilt part with `Group.addChild()`.  The M3G
binding, to keep the game's redundant `addChild()` on file-listed children
working, calls `java.util.Vector.indexOf(child)` to decide whether the child is
already present.  `Vector.indexOf()` (like `removeElement()`, `contains()`,
`lastIndexOf()`) compares with `o.equals(element)`.

A group's child vector is not small: for the rebuilt chunk it holds 678-695
nodes.  `obj_equals()` (src/midp/nj_lib_util.c) did that comparison with
`nj_call(vm, o, "java/lang/Object", "equals", ...)`, which resolves the class by
name string, looks up the bridge, initialises the class and enters
`nj_invoke_native()`: ~167 ns per element.  A scan of 690 elements therefore
cost 114 us, measured directly with an `rdtsc` probe inside `vec_indexOf()`
(n=678..695, idx=-1, ~350 k cycles); 13 `addChild()` calls per rebuilt part is
~1.5 ms of the 3rd burst frame.

The compared objects are M3G nodes, whose dynamic `equals()` is
`java.lang.Object.equals` - the runtime's native reference comparison.  So the
scan is equivalent to a reference comparison, and the interpreter round trip is
pure overhead.

## Fix

`obj_equals_is_identity()` in src/midp/nj_lib_util.c resolves the receiver's
dynamic `equals` once per scan (`nj_method_lookup`), and when that resolves to
`java.lang.Object.equals` (a native, `code == NULL`, owner
`java/lang/Object`) the scan compares references directly:

```c
ident = obj_equals_is_identity(o);
...
nj_ref e = nj_array_get_ref(data, (nj_u4)i);
if (ident ? (e == o) : obj_equals(vm, o, e, &err)) { ... }
```

This is exact, not approximate: `Object.equals(o, e)` is specified as `o == e`,
so a different reference is known to be unequal and no call is needed at all.
When a class overrides `equals` (in Java or as another native, e.g.
`String.equals`), `code != NULL` or the owner differs and the old per-element
`nj_call` path runs unchanged.  The resolved method is not cached across calls
or VMs (a static cache keyed by `nj_class *` would be wrong once a VM is
destroyed and a class allocation reuses the address).  Applied to
`vec_indexOf()`, `vec_contains()` and `vec_removeElement()`; 37 insertions, 4
deletions, one file.

## Measurements

Five interleaved rounds per side (before/after binaries alternating, same input
script, fresh `fs/`+`rms/` copy each run), 700 frames, `taskset -c 13`,
`--fps 0`; three bursts per round, 15 bursts per side:

| metric | before | after | change |
| --- | --- | --- | --- |
| burst 1st frame | 7.64 ms | 7.55 ms | -1% |
| burst 2nd frame | 7.56 ms | 7.52 ms | -0.5% |
| burst 3rd frame | 5.75 ms | 3.49 ms | **-2.26 ms (-39%)** |
| burst total | 20.9 ms | 18.6 ms | **-2.3 ms (-11%)** |
| steady frame p50 | 1.556 ms | 1.583 ms | noise |
| post-startup p99 | 6.10 ms | 3.77 ms | -38% |
| post-startup max | ~8.0 ms | ~7.9 ms | unchanged |
| run frame CPU (700 frames) | 1356 ms | 1349 ms | -7 ms/run (0.5%) |

The gain is concentrated where predicted: the 3rd frame's `addChild()` scans
vanish from the native table (after the fix no `m3g_addChild`/`vec_indexOf`
rows survive in burst frames), while the game's Java rebuild frames 1 and 2 are
untouched.  The total run CPU changes by only 0.5% because three bursts of
~20 ms are a small part of a 1.36 s run; a player continuously digging and
rebuilding chunks sees the p99 improvement instead.

## Verification

* Idle world replay before vs after: frame dumps bit-identical except the
  animated cloud band (diff rows only y=2..102 of 240x320; no other pixels).
* Dig-run dumps cannot be pixel-compared: mining, water and camera animation
  are wall-clock dependent, and two identical-binary runs of the same script
  differ *more* (17.6 k/20.4 k/15.1 k/17.3 k/11.1 k/9.7 k/7.4 k pixels at
  frames 100..700) than before-vs-after does (13.6 k/17.8 k/13.9 k/6.3 k/1.5 k/
  0.8 k/0.8 k), so the fix does not change what is drawn.
* `build/test_m3g`: 848 checks, 0 failures.  ASan+UBSan build
  (`-DNJ_SANITIZE=ON`): 848 checks, 0 failures, no sanitizer report.
* `tools/check_m3gdemo.sh`: 148 checks, 0 failures.
* `ctest`: 133/137 pass.  The four non-passing tests are environmental and
  identical on the pre-fix binary: `test_engine`, `test_poke`, `test_ttf` are
  not built in this configuration (optional features) and `test_client_ui`
  reports the same 454 checks / 12 failures (CJK/UI fixtures) before and after.

## What remains

Two of the three burst frames are the game's own 5.5 M bytecodes of chunk-mesh
rebuild plus ~1.8 M bytecodes of rendering; that is inherent title work at the
interpreter's throughput, and no safe runtime-side duplication was found inside
it.  The fixed scan was the only runtime-side cost large enough to matter:
`m3g_removeChild()`'s own `Vector.removeElement()` scan finds its element at
index 0 in this title, so it is cheap and was left alone.
