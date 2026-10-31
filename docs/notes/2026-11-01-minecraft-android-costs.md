# Minecraft PE on Android: where world creation and digging go (2026-11-01)

The report was "creating a world is very slow, and digging is laggy" **on the
Android APK**.  Both answers come down to one number: this title is interpreter
bound, and every APK until now shipped the portable switch interpreter while
the desktop numbers were measured on the replicated one.  Nothing in the file
I/O, the GC, the 3D upload path or the block-break bookkeeping is pathological;
the phone is simply executing the game's own Java, bytecode by bytecode.

The measurements below use the x86_64 API-21 Android emulator (KVM, software
surface) and the desktop headless port for the parts an emulator cannot see:
per-frame counters, gprof and a dig replay.  The emulator is a useful *relative*
instrument (same APK script, same world, alternating runs) but it is not a
phone: it is pinned at its 60 fps cap in world (the frame counter never left
59.3/60.0 fps), so it cannot reproduce a mid-range phone's frame drops.

## 1. Creating a world

Fresh world, Create-World screen -> Start, the game's own timers in logcat
(`MCJ: GEN`, `MCJ: LOAD`), 240x320; two runs per build, interleaved on the same
emulator (sA, tB, tC, sD):

| build | GEN (terrain) | initial 22 MB save | load-back prepare |
| --- | ---: | ---: | ---: |
| desktop headless, threaded default | 10.9-14.3 s | 2.4-3.2 s | 2.6-3.5 s |
| Android APK, portable switch | 69.1 / 78.3 s | 15.6 / 18.3 s | 21.0 / 20.3 s |
| Android APK, replicated interpreter | 45.2 / 42.6 s | 11.3 / 10.6 s | 13.6 / 11.6 s |

So the APK the report was written against spent about **1 min 50 s** of game
time from Start to the first in-world frame; with the replicated body it is
about **1 min 08 s**, against 16-21 s on the desktop.  The remaining gap is
interpreter throughput on the phone, not a missing optimisation in a phase.

Evidence that it is **not** I/O, GC or the loading screen:

* A per-opcode/per-native instrumented desktop run of the same creation replay
  counted **4.97 G interpreter dispatches in ~10.7 s** during generation, with
  **1.26 M native calls totalling 25 ms**; the save window is another 2.58 G
  dispatches with 260 k natives.  GC ran 7-9 times for 121 ms in a whole 37 s
  run, and live heap was 3.4-3.8 MB during generation (~10 MB at load peak).
* The 22 MB `chunks.dat` save takes **2455 ms on btrfs and 2501 ms on a tmpfs
  `/dev/shm` store** with the same build and replay - identical to within noise,
  so reopening the host file per write (the snapshot-safe `FileOutputStream`
  design) is not costing seconds on a page-cached file.
* The loading screen repaints 28 times during a 320-frame generation window, so
  it is not competing with the generator thread.

Per-frame counters during the **read-back** phase show the shape even more
plainly: 4.4 M interpreter dispatches and 10-20 native calls per frame, zero
Graphics calls, zero triangles.  That is a pure Java parse loop.

Existing-world load on the same saved world (752 chunks, view distance 4):

| phase | desktop headless | Android emulator |
| --- | ---: | ---: |
| `MCJ: LOAD read` | 3.1-3.6 s | 24.3 s |
| `MCJ: LOAD prepare` | 3.0-3.6 s | 25.8 s |

A gprof of the desktop load replay (Android feature set: switch interpreter,
`-Os`) puts **69.3% of the whole run's self time in `nj_interp_run`**; the next
entries are the in-world rasteriser after loading (`jr_emit` 12.3%) and HUD
blits (`nj_region_draw` 2.4%).  There is no C-level hotspot to fix in the load:
the time is inside the game's Java.

## 2. Digging (holding to break a block)

The break itself is the game rebuilding that chunk's render geometry in Java.
Reproduced on the desktop port (saved world, pointer drag to aim at sand, hold
raw key 101 until the block disappears), with temporary per-frame instrumentation
(two CSVs: frame wall time + VM counter deltas, and the per-phase
input/scheduler/media/paint/present/pace split):

* steady dig frames: **1.5 ms p50** (p95 2.3 ms, p99 7.7 ms);
* the frames in which the block's chunk is rebuilt: **7.7 / 7.2 / 5.3 ms**, and
  **100% of that is inside the scheduler step on the game thread** - input,
  media, paint, present and pacing are all 0.00 ms;
* the burst is ~6 M Java bytecodes, +~120 KB heap, **no GC, no file I/O**;
* `VertexBuffer.setPositions/setColors/setTexCoords` only store array
  references (plus the three copied bias floats), so there is no hidden
  per-call copy in the M3G upload path at break time.

At 60 fps that 20 ms burst fits in a desktop frame budget; a phone whose
interpreted throughput is several times slower turns three frames into a visible
stutter, and in creative a held finger breaks a block every 10 ticks, rebuilding
the chunk each time.  The emulator's 60 fps cap means it cannot show the drop;
the desktop CPU-time measurement is the evidence for the cause, and the
interpreter change below is the fix that reaches it: it cuts every phase of the
same interpreted work.

## 3. The fix: the replicated interpreter body in the Android APK

`NJ_THREADED_DISPATCH` (the replicated computed-goto body) has been the CMake
default for native GCC/Clang builds since `197536a`, but `tools/build_android.sh`
compiles the sources by hand and was not passing the define at all, so every
APK shipped the portable switch.  It now builds the replicated body by default
(`NJ_ANDROID_THREADED=0` restores the switch for A/B).

`nm` confirms the pair really differs: the switch `.so` has no
`nj_interp_run.nj_dispatch` label table, the threaded one does, and the `.so`
grows by 28 KB, exactly the second opcode implementation.  The measured effect
on a fresh Minecraft world (the table in section 1) is **-40% terrain
generation, -35% on the 22 MB save and -39% on load-back prepare**, about
**1.65x** for the creation phase as a whole.  This is a much bigger relative win
than the -6% the same switch gave on a `-O3` desktop build, which is the point:
clang `-Os` on the phone compiles the switch dispatch badly, and the computed
goto does not pay that cost.

Device-level equivalence on the same emulator, switch APK vs threaded APK
(each fixture launched through the same `GameActivity` intent):

* `tests/fixtures/bench.jar` prints `BENCH sum=-136042548 s=5` on both builds,
  byte for byte, plus identical lifecycle logs - the VM kernels agree, including
  fields, arrays, branches and the static-table loops.
* `tests/fixtures/keysdemo.jar` and `tests/fixtures/colordemo.jar` produce
  identical logcat output and **0 differing pixels** between their screenshots.

So the replicated body is not just faster on the phone build; it executes the
same program with the same results.

## 4. Bugs found along the way

The M3G audits that this investigation triggered found five real bugs (all
fixed, all with regression checks in `tests/test_m3g.c`): `VertexArray.get` read
before the backing array on a negative `firstVertex`; `VertexArray.set`
multiplied `numVertices*numComponents` in 32 bits (zero-size allocation followed
by an out-of-bounds store - ASan SEGV and UBSan signed overflow before the fix);
`TriangleStripArray` constructors accepted strip lengths past the index array
(renderer then read past the Java `int[]`); `Image2D(fmt,w,h,byte[])` accepted
short or NULL pixel storage (texture sampler over-read); and
`Group.addChild` accepted self/ancestor edges, making render/find walks recurse
until the stack dies.  They are correctness bugs rather than performance bugs,
but two of them (the strip-table and Image2D over-reads) are exactly the kind of
thing a loader can feed a phone with a different amount of heap.

## What remains

The interpreter is the lever and the ceiling.  A production JIT would change
this workload by a multiple, but the ARM64 baseline JIT in this tree is
incomplete (static fields and non-int arrays are still RV32-only, and its
end-to-end numbers on a real board were once *worse* than the interpreter), so
it is not something to turn on for a shipped phone APK without device testing.
The safe, measured actions taken here are the replicated interpreter for the
Android build and the M3G-side work; the next useful step on this title is a
real ARM64 phone profile of the same three phases (GEN / save / read-back) with
this APK, which this machine cannot provide.
