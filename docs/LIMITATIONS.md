# Known limitations

This page collects the behaviours that are still limited, on purpose or by
open work, and points at the evidence.  It is meant to be read alongside
[docs/COMPATIBILITY.md](COMPATIBILITY.md) (API surface) and
[docs/CORPUS.md](CORPUS.md) (corpus results): the corpus page says *which*
titles run, this page says *where the runtime's model differs from a handset*
and what that means for a frontend.

Nothing here is presented as working when it is not.  Status values:

- **open** — reproduces, understood at least in part, no safe fix yet.
- **workaround** — a way to get the behaviour, with its cost.
- **by design** — a deliberate difference, documented rather than planned.

## 1. Frontend and frame model

### 1.1 Handset event-thread semantics is the main structural gap

A handset runs `startApp()`, the loader threads and the display in a
cooperative event model: `setCurrent()` returns, `showNotify()`/`sizeChanged()`
arrive on the event thread, the first paint follows, and only then do
background threads get time.  The runtime runs MIDlets on a cooperative VM
thread with a frame pump, which is close enough for almost every corpus title
but not identical.  The libretro port now services a show request between
bytecode quanta (round 248): when `Display.setCurrent()` makes a screen
current, the scheduler pass stops and the frame pump paints that screen before
any background thread (a loader started by `startApp()`) gets its next
timeslice.  This is the opt-in `show_before_background` port capability; the
CLI/headless runner keeps its historical interleaving.  `Skater_by_BFLM` therefore renders through the libretro
core as well as the CLI.  `TheMummy_K700F` used to expose the same difference
from the other direction: its whole game loop runs inside a `TimerTask`, and
the timer helper executed the task to completion in one C→Java call.  Round 4
moved timer-task dispatch into Java (`nj2me.util.TimerThread.run()`) so the
scheduler preempts it at quantum boundaries; the title now renders through the
CLI and the core and its state requests succeed.  No corpus title currently
depends on full handset event-thread ordering:

| Title | Symptom | State |
|---|---|---|
| `TheMummy_K700F` | was black through every frontend because its TimerTask-hosted game loop never returned and starved the boot-handshake timer | **fixed** (round 4) — timer tasks now run as Java and are preempted; documented in `docs/notes/2026-11-23-timer-tasks-as-java-threads.md` |
| `Skater_by_BFLM` | previously black through the libretro core because the loader's first timeslice ran before the first paint; now renders (7480-8004 non-black px) after the round-248 show-request break | **fixed** — documented in `docs/notes/2026-11-23-first-paint-delivery.md` (round 248) |

### 1.2 State requests only land between frames

The libretro state service serves `retro_serialize()`/`retro_unserialize()`
between `nj_midp_frame()` calls with a 4 s wait.  A title whose single frame
runs longer than that (a game loop inside the MIDlet constructor, a very slow
loader, one long paint) refuses the snapshot.  This is a limitation of the
synchronous frame model, not of the state format; the state itself is sized
and written correctly.

| Title | What happens | Workaround |
|---|---|---|
| `毁灭战士3_10598` | whole game loop runs inside the MIDlet constructor | none through the frontend; the core intentionally gives it a long load |
| `vault/Asphalt4EliteRacing_1.0.0_320x240` | renders, but one frame exceeds the service window; manual state refused | none |
| `vault/assault.jar` | very slow loader (~250 s to first screen) | wait |

`TheMummy_K700F`, which previously sat in this table because its boot
handshake never completed, now renders and saves through the core after the
round-4 timer-task change (see 1.1).

Evidence: `docs/LIBRETRO.md` save-state section (round-176 notes),
`docs/CORPUS.md` round-233 visual probe, round-239 state probe
(1,660/1,664 round trips; the four refusals are exactly these titles).

A round-251 attempt to serve the request from the frame hook, i.e. at an
inner frame boundary pumped while the title sleeps, was rejected by the
save-state safety rules rather than by the clock: a parked stack can contain
nested native pump frames (timer task running inside `timer_pump`), and the
boundary rules can only drop one native dispatch frame, so the snapshot is
not representable and a load would not have a stack to resume.  Serving stays
at outer frame boundaries only, so the 4 s refusal remains documented
behaviour rather than a fixable timing artefact for this title class.

### 1.3 Pacing

The vm clock is virtual: one `retro_run()` is one 60 Hz frame.  Titles that
pace themselves on the wall clock are fine, but probes and runners must drive
real frames (`--fps 0` starves wall-time-driven titles, as documented in
`docs/LIBRETRO.md`).  In the libretro core this is invisible to the user (the
frontend paces), and the load clock is now anchored at every phase (0.5.39)
so a first load cannot leak host uptime into the VM clock.

## 2. Rendering

### 2.1 Software rasteriser is the supported renderer

M3G and 2D drawing run on the software rasteriser in every shipped core
(`hw_render=false`).  Raster-micro-optimisation work has reached the point of
diminishing returns (span fast paths, exact-parity-only changes; see
`docs/PERFORMANCE.md` and the round notes).

### 2.2 GLES2 backend is experimental and opt-in

`--graphics gles2` (SDL2 host builds, `-DNJ_SDL2_GLES2=1`) is a real backend,
but measured slower than software on the test GPU (Galaxy on Fire 2: 5.2 s vs
3.8 s user CPU; pure-3D Doom 3: 4.3 s vs 3.0 s) because it commits each
triangle batch through the CPU framebuffer.  It is not used by the released
libretro cores.  The frame-resident whole-frame design is documented in
`docs/notes/2026-11-22-gles2-whole-frame-renderer.md`; the per-transaction
optimisations were measured and rejected
(`docs/notes/2026-11-22-android-gles2-bottlenecks.md`).

### 2.3 Title-side black screens are documented, not hidden

The corpus still contains titles that render black on a handset or on the
FreeJ2ME-Plus reference too (e.g. `PAC-MAN (176x220)` waits on an empty Sprint
carrier URI).  They are listed with their evidence in
`docs/notes/2026-11-21-fetched-black-screen-titles.md` and are not counted as
runtime successes.

## 3. Features and platforms

| Area | Status | Notes |
|---|---|---|
| Netplay | **by design, not advertised** | `nano_j2me_deterministic` replays frame-driven titles identically for single-machine replay, but clock-sampling titles still vary, and frame-locked single-step execution deadlocked (documented).  The core does not claim `retro_netpacket` support. |
| Rewind | **workaround** | rewind works at small heaps; a 128 MB auto-heap snapshot is ~1 s to save/load, so a rewind buffer at that size is not practical.  See `docs/LIBRETRO.md` and `tools/check_libretro_rewind.sh`. |
| Cheats | **workaround** | raw arena-offset writes (`<offset>:<hex bytes>`) applied every frame, no game-genie database, offsets change with heap layout.  In-game search/editor exists on Android; core users can search SYSTEM_RAM. |
| Android 2.x-4.4 | **workaround** | needs the legacy ARMv5 APK (`nj2me-legacy.apk`, API 5); modern APKs target API 21+. |
| Network | **port-dependent** | `http://` uses the port hook; a hook that exposes no TLS session makes `https://` report the JSR 75 "security information unavailable" answer rather than pretending.  The desktop runner needs `--network` and a port with the hook. |
| Determinism option | **limited** | fixed 16667 µs per frame and a VM-seeded `java.util.Random`; titles that read the wall clock outside frames still vary. |
| Audio | **platform-dependent** | MIDI/AMR can use the platform's decoders through port hooks; the built-in path is used otherwise.  Wine audio at 120 frames is timing-sensitive (use 300). |

## 4. Reporting a regression

If a title worked through a released core and stopped, the fastest path is a
row-level check: `.local-tools/debug-runs/` keeps the sweep rows, and
`tools/check_libretro_corpus_visual.py` can replay one row at its own screen
size.  Include the row's `jar_path`, the core hash and the probe output;
`docs/VERIFICATION.md` records which evidence already exists for the title.
