# Running nano_j2me in a web browser (WASM)

nano_j2me builds to WebAssembly with [Emscripten](https://emscripten.org/).
The whole runtime — interpreter, class library, software rasterizer, decoders,
M3G/Micro3D — is compiled once and runs in the browser without a JVM, a server
component or a native helper process. Game JARs are opened locally; nothing is
uploaded.

This document covers the browser build itself, the JavaScript⇄C API, the bundled
web shell, persistence, audio and verification. The port follows the same
contract as the other targets: everything OS-specific lives behind one
[`nj_port`](PORTING.md) implementation, here `src/port/web/nj_web.c`, and the
runtime keeps no mutable global state (the single runner instance in that file is
the Emscripten export ABI, not shared MIDlet state — exactly like the libretro
core).

## Status

| Item | State |
|---|---|
| Build | Emscripten 7.0.0 (`emcc`), `-sMODULARIZE` ES module + `.wasm`, single-threaded, no Asyncify |
| Module size | `nj2me.js` ≈ 92 KiB, `nj2me.wasm` ≈ 2.1 MiB (release) |
| CLI-free | the browser target builds the runtime library plus `src/port/web/nj_web.c` only |
| Verify | Node API contract + 42-title corpus audit + real-Firefox page test (Playwright), see [Verification](#verification) |
| Networking, sockets, Bluetooth, sensors | not exposed in the browser build (hooks are `NULL`; the APIs refuse by name) |
| JIT | unavailable (no executable pages in wasm); the interpreter remains the engine, as on every other target |

The build is deliberately synchronous: no pthreads, no `SharedArrayBuffer` and no
COOP/COEP headers are required, so it runs from any static file host, including
GitHub Pages-class hosts and `python3 -m http.server`.

## Quick start

Prerequisites: a C toolchain is not needed — only the Emscripten SDK and
CMake/Ninja. The repository vendors nothing.

```sh
# 1. Install the Emscripten SDK into .local-tools/emsdk (git-ignored).
tools/emsdk_setup.sh            # clone + install + activate "latest" (7.0.0)
                                # afterwards: source .local-tools/emsdk/emsdk_env.sh

# 2. Build the browser module and assemble the served directory.
tools/build_web.sh              # -> build/web/ (shell + nj2me.js + nj2me.wasm)

# 3. Serve it and open the URL in a desktop or mobile browser.
python3 -m http.server -d build/web 8000
# http://127.0.0.1:8000/
```

`tools/build_web.sh` locates `emcc` on `PATH`, under `$EMSDK` or in
`.local-tools/emsdk`; `--clean` wipes `build-web/`, `--debug` keeps a debuggable
(-O0) module, `--env-check` only prints the toolchain versions. The equivalent
manual configure is:

```sh
source .local-tools/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DNJ_BUILD_WEB=ON
cmake --build build-web -j8
```

The wasm target is enabled only by `-DNJ_BUILD_WEB=ON` (the default when the
toolchain is Emscripten); on a native configure the option is rejected with a
clear error. Building with `-DNJ_WERROR=ON` is not supported on the wasm target
because the existing headers intentionally repeat a few `typedef`s that clang
diagnoses as a C11 extension — leave `NJ_WERROR` off (the default).

The wasm artifact is reproducible: the build maps the absolute checkout path out
of `__FILE__` (`-ffile-prefix-map`/`-fmacro-prefix-map` in `CMakeLists.txt`), so
rebuilding the same commit from any directory yields the same `nj2me.wasm`
byte-for-byte — the save-state diagnostics in `src/core/nj_save.c` would
otherwise embed the path.

The shell opens the game library first. Use **导入游戏 / Add games** (file picker
or drag and drop), pick a `.jar` with a `MIDlet-1` manifest entry, and click it.
There is no JAD, firmware or BIOS step — the same rule as every other port.

## The bundled web shell

`web/` is a complete, dependency-free front end (plain ES modules, no framework,
no build step of its own). `tools/build_web.sh` copies it into `build/web/`
unchanged and drops the module next to `web/js/module.js`, so the same tree can
be served with or without a build:

- **Library** — imports JAR files, reads manifest metadata through `njw_probe()`
  (title, vendor, version, icon, MIDlet list), stores them in IndexedDB, offers
  search, sorting, a favourites filter (every card carries a star; the mark is
  stored in the game record), a dialog that downloads a JAR from an http(s)
  link, deletion and a language toggle that persists the choice. Above the grid
  a **continue card** names the newest played title (with the exit auto-save's
  thumbnail and frame count when that toggle is on) and opens it in one click;
  the card disappears while the library is being searched or filtered. Cards
  of titles covered by the last full-corpus audit carry its result: a green
  "verified" badge, or an amber warning whose tooltip gives the reason (needs a
  bigger heap, first frames are black, constructor refusal). Warning badges are
  buttons, so a tap on a phone shows the same explanation as a toast.
- **Game view** — a `<canvas>` sized to `njw_width()`×`njw_height()` and scaled
  with `image-rendering: pixelated`, an `requestAnimationFrame` loop that calls
  `njw_frame(dt_ms)` once per display refresh when the **Frame rate** selector
  is on "display refresh", or once per fixed `1000/fps` frame otherwise — the
  default is **30 fps**, the cadence a J2ME-era handset serviced (titles were
  written for 15/20/30 fps, and a 60/120 Hz host would otherwise run their
  per-frame logic two to four times too fast; 60 Hz was not a J2ME phone
  rate), a loading overlay until the first
  `Displayable` exists, then pause/resume, speed (100/150/200 %, applied live
  and remembered per game like the frame rate), mute and a
  volume slider (the host-side WebAudio gain; touching the slider also
  unmutes, like a phone volume key), a **Frame rate** selector
  (display refresh / 60 / 30 / 24 / 20 / 15 fps; applied live and remembered per game
  like the screen size — the shell setting is only the default a new title
  starts with),
  three save states per game (each can be given a name that survives a reload
  and names the exported file) plus one read-only **exit auto-save** row that
  appears once Save on exit has written a snapshot (it shows when it was taken
  and how far the game got, and can be loaded or cleared), local saves carry a 96×128 PNG preview of the
  saved screen; each is exportable as a `.njst` file and importable back into
  any slot — an imported blob shows no preview until it is saved again), a
  **Game memory** selector (16/32/64/128 MiB) that
  reaches `NJW_OPT_HEAP_BYTES` at the next launch for heavy titles, a
  **Screen size** selector (auto/176×208/176×220/128×160/320×240) that sets
  `NJW_OPT_SCREEN_WIDTH`/`NJW_OPT_SCREEN_HEIGHT` at the next launch and is
  remembered per game, because a title was drawn for one resolution, a
  **Startup MIDlet** selector for JARs that ship several MIDlets (stored with
  the game record, applies on the next launch), a **Resume the latest save on
  launch** toggle (off by default; when on, starting a title loads its newest
  save slot before the first frame), a **Save on exit** toggle (off by default;
  when on, leaving a game stores its state on the game record so the next launch
  can continue from it), a one-click **larger-heap restart** on
  out-of-memory errors, PNG
  screenshot download, fullscreen, restart and exit-to-library. The same panel
  has a **Diagnostics** section (heap used/capacity, frames/paints, GC runs and
  bytecode rate) that reads `njw_stats()` — the counters the native `--stats`
  line prints.
- **Input** — keyboard (arrows = D-pad, Enter/Space = Fire, F1/F2 = soft keys,
  Backspace = Clear, digits, `*`/`#`), a standard-mapping gamepad (A = fire,
  B/X/Y/LB = GAME_A..D, RB = clear, LT/Start = soft left, RT/Back = soft right,
  D-pad and left stick with a 0.45 deadzone), an on-screen phone keypad for touch
  devices that presses on touch-down and switches the held key when the finger
  slides to another cap (slide-off cancels), multi-finger pointer pass-through
  for games that use canvas pointers (the first finger keeps Canvas semantics,
  every finger reaches `MultipointTouch`), and a hidden textarea bridge for MIDlet text fields
  using the native-editor callbacks (`njw_text_*`). The tools panel's **Keys**
dialog rebinds the eight game actions (up/down/left/right/fire/soft left/soft
right/clear) to any keyboard key: click an action, press the new key, Esc
cancels, and reset restores the defaults; bindings live on the game record,
apply immediately, and survive a reload.
- **Audio** — a pull loop drains `njw_audio_read()` into Web Audio buffers
  scheduled roughly 100–150 ms ahead, resumes the `AudioContext` on the first
  user gesture, and suspends it on pause.
- **Persistence** — game JARs and save-state slots live in IndexedDB; emulated
  file system (RecordStore and `FileConnection`) lives on an IDBFS mount at
  `/nano_j2me`, written by the shell before `njw_start()` and synced to
  IndexedDB automatically. Only one tab may own the engine at a time:
  `web/js/engine-owner.js` holds a `BroadcastChannel` lease, so a second tab
  that tries to play shows "another tab is running a game" instead of racing
  on the same IndexedDB records; the lease expires after 6 s of silence so a
  crashed or suspended tab cannot block the profile forever.
- **Mock mode** — served straight from `web/` (no wasm), or with `?mock=1`, the
  shell loads `web/js/mock-module.js`, an animated 240×320 test pattern behind the
  same facade, so the entire UI is testable before the engine exists. The page
  never throws when the module is missing; it shows a banner pointing at
  `tools/build_web.sh`.
- **Installable and offline** — `web/manifest.json` declares a standalone PWA
  with 192/512/maskable icons, and `web/sw.js` caches the shell and the engine
  (`js/nj2me.js`, `js/nj2me.wasm`) on install, so a second visit works offline
  from the address bar / home screen. See *Install and offline use* below.

To use the shell with a custom engine build, serve `build/web/`; to develop the
shell alone, serve `web/` (`python3 -m http.server 8001 -d web`).

## Install and offline use (PWA)

The shell is a static site with a service worker, so any static host can serve
it and the browser can install it as an app.

- `web/manifest.json` — `display: standalone`, theme/background colours, and
  four icons (`icon.svg`, 192 px, 512 px, maskable 512 px) drawn from the same
  plate/D-pad mark as the Android launcher and the desktop window icon
  (`tools/gen_brand_assets.py`). Desktop Chrome/Edge show an install button in
  the address bar; Firefox and mobile browsers offer "add to home screen".
- `web/sw.js` — on install it precaches the shell (`index.html`, CSS, icons, all
  `js/*.js` modules) and the engine (`js/nj2me.js` + `js/nj2me.wasm`), then
  serves cache-first. After the engine cache completes, stopping the web server
  and reloading still boots the shell **and the WASM engine**.
- The engine probe in `web/js/module.js` uses a `HEAD` request; the service
  worker answers it from the cached `GET` entry with a headers-only response,
  which is what keeps the offline probe from falling back to mock mode.
- Service workers require a secure context: `https://`, `localhost` or
  `127.0.0.1`. For shell development, `?nosw=1` skips registration and
  unregisters an existing worker so stale caches do not hide edits.
- The cache name is versioned (`nj2me-shell-<date>` in `web/sw.js`): bump it
  whenever shell files change, otherwise the cache-first worker keeps serving
  the previous version. Old caches are deleted on the next activation.
- Once the engine is cached, the library footer reports the visit as offline
  ready (`wasm 引擎已就绪 · 离线可用`, mirrored in
  `document.body[data-offline=ready]`). The library itself lives in IndexedDB,
  so from that point on games imported earlier start and keep drawing with the
  network switched off.

The browser suite (`tools/web_test/web_browser_test.mjs`, run by
`tools/check_web.sh --browser`) checks this path: manifest parse, service-worker
control, engine entries in the cache, the offline-ready footer, a real offline
reload after stopping the static server, and an imported game that starts and
keeps drawing while the server is down. Playwright's Firefox offline emulation
is not service-worker-aware, so the test stops the server instead of using
`context.set_offline()`.

## How the browser build runs without blocking

A browser page cannot call the blocking `nj_run()` loop: JavaScript must return to
the event loop between frames. The web port is therefore a cooperative host-driven
runner built on the runtime's public embedder API:

- `njw_start()` performs the same setup as `nj_run()` (read JAR, create the VM,
  open the app, attach the MIDlet, queue `startApp()`), but returns immediately.
- `njw_frame(dt_ms)` runs **one** `nj_midp_frame()` step (the same frame pump used
  by the SDL2 and libretro ports: input, scheduler slice, media, paint, present).
  The host calls it once per `requestAnimationFrame`.
- `startApp()` is queued with `nj_vm_start_app_async()` and executed by the next
  frame pumps. It is queued **unconditionally**: some MIDlets install their
  `Canvas` in the constructor and keep the real initialisation in `startApp()`,
  exactly as `nj_run()` treats them. While the first `Displayable` has not
  appeared yet, a frame may internally run up to four pumps
  (`NJW_FRAME_LOADING` says so); this is how splash screens and constructor-time
  waits work without blocking the tab.
- The internal frame pacer is disabled (`nj_vm_set_frame_cap(vm, 0)`; the port
  reports `fps_cap = 0`), so `nj_midp_frame()` never sleeps. Display pacing is the
  host's business: the shell uses `requestAnimationFrame` and the **Frame rate**
  setting, and the port is the display for the runtime: a thread parked in
  `Thread.sleep()` or `serviceRepaints()` resumes at the next pumped frame, so
  the interval a title sees is the pump interval (1/30 s by default). A
  repaint-paced loop (`repaint(); serviceRepaints(); step();` with no sleep of
  its own) was measured doing two steps per pump — the inline forced paint
  returned without waiting, so one extra step ran in the frame that had just
  painted — and is now throttled to one step per pumped frame once that shape
  has been seen; a sleep-paced title keeps the ordinary (sub-frame)
  `serviceRepaints()` latency the MIDP idiom
  `state = 1; repaint(); serviceRepaints(); state = 0;` relies on.
- `njw_stop()` performs the `nj_run()` teardown (bounded `destroyApp()`, app close,
  VM destruction) and can be followed by another `njw_start()`.

### The virtual clock

The emulated clock is owned by the host and follows real time:

- Until the MIDlet instance exists ("load phase") `sleep_ms()` and `idle` advance
  the virtual clock by the full requested amount, so a constructor that sleeps
  (common in splash-screen MIDlets) completes inside `njw_start()` instead of
  freezing a JavaScript frame.
- During normal play the clock is the **later of two sources**: a host schedule
  that each `njw_frame(dt_ms)` advances by `dt_ms` (clamped to 0–250 ms), and the
  browser's wall clock since `startApp()` was queued. On a browser both advance
  together, so the emulated clock is exactly real time — a MIDlet that measures
  `System.currentTimeMillis()` deltas runs at the speed of the machine, and a
  host that renders slower than real time cannot fast-forward it past the wall
  clock. The host schedule is what a headless Node harness (which has no wall
  clock to fold in) advances to make progress; the two are combined with
  `max()`, never added.
- `sleep_ms()` does **not** advance the clock during play any more: real time
  does. (An earlier model folded each sleep forward on top of the frame-driven
  clock, which made repaint-paced titles — those that never sleep, e.g. 雷电4 —
  see a clock about 1.3× too fast and animate accelerated.) A host without wall
  time — the Node harness — advances the clock through the `dt_ms` it passes to
  every `njw_frame()`, so a blocked scheduler still makes progress; `idle()`
  never advances the clock.
- `njw_set_speed(percent)` (10–800) scales the virtual clock exactly like
  `nj_vm_set_speed()` on the desktop ports, and does not jump the clock when
  changed.
- Sleep and paint deadlines are resolved at pumped-frame boundaries: the
  runtime sees one display frame per `njw_frame()` call, so `Thread.sleep(33)`
  wakes on the pump that brings the clock past its deadline (about 30 steps/s
  at the 60 fps pump; measured with the synthetic fixtures below), never in the
  middle of a pump. The frame interval a title gets is therefore the pump
  interval: 1/30 s by default, or the chosen **Frame rate**.
- A MIDlet that busy-waits on `System.currentTimeMillis()` (some racing titles
  do) still terminates because the wall clock keeps the emulated clock moving;
  the constructor budget below covers the case where a constructor never returns.
- The wall clock also lets a **constructor that never returns** be reported
  instead of hanging the tab. If the MIDlet constructor keeps pumping frames for
  more than 10 s of wall time (or 20,000 frame-hook pumps), `njw_start()` fails
  with `the MIDlet constructor does not return; this title runs its game loop
  from the constructor, which the browser port cannot interrupt` (see
  [Limitations](#limitations)).

This gives deterministic, frame-driven emulation: 60 `njw_frame(16.667)` calls
advance 60 frames of game time whether the tab renders at 30, 60 or 144 Hz,
subject only to how fast the host pumps.

## JavaScript⇄C API

The API lives in [`include/nj2me/nj_web.h`](../include/nj2me/nj_web.h); every
entry point is exported from the module as `_njw_*` and takes/returns integers
(strings are UTF-8, `const char *`). The module also exports `_malloc`/`_free`
and the usual runtime helpers (`FS`, `IDBFS`, `HEAP*`, `stringToUTF8`, …).

```js
import createNj2meModule from './nj2me.js';
const m = await createNj2meModule({ locateFile: (p) => new URL(p, import.meta.url).href });

// Optional: tune before start (0 means "default").
m._njw_options_reset();
m._njw_option_int(m.NJW_OPT_SCREEN_WIDTH ?? 0, 240);   // enums are documentented in the header
writeString(m, '/nano_j2me/jars/game.jar');             // helper: _malloc + stringToUTF8
m._njw_start(pathPtr, 0);                               // 0 on success, -1 on failure

let raf;
function tick(now) {
  const flags = m._njw_frame(1000 / 60);                // one emulated frame
  if (flags < 0) { fail(m.UTF8ToString(m._njw_error())); return; }
  if (flags & 2 /* NJW_FRAME_DREW */) drawToCanvas(m);
  if (flags & 1 /* NJW_FRAME_RUNNING */) raf = requestAnimationFrame(tick);
  else finish(m.UTF8ToString(m._njw_stop_reason()));
}
raf = requestAnimationFrame(tick);
```

Groups of functions (see the header for exact signatures and semantics):

| Group | Calls |
|---|---|
| Version | `njw_api_version`, `njw_runtime_version` (the `NJ_VERSION_STRING` of the build) |
| Options | `njw_options_reset`, `njw_option_int`, `njw_option_string` (`NJW_OPT_SCREEN_WIDTH/HEIGHT/HEAP_BYTES/SPEED_PERCENT/RMS_DIR/FS_DIR/DEVICE/MIDLET_CLASS/LOAD_STATE_PATH`) |
| Life cycle | `njw_start`, `njw_frame`, `njw_running`, `njw_displayable_ready`, `njw_stop`, `njw_error`, `njw_stop_reason`, `njw_frames` |
| Display | `njw_framebuffer` (RGBA8888), `njw_width`, `njw_height`, `njw_frame_epoch` |
| Input | `njw_key`, `njw_pointer`, `njw_pointer_multi`, `njw_text_input`, `njw_text_edit`, `njw_text_replace`, `njw_text_active`, `njw_text_current`, `njw_text_state` |
| Audio | `njw_audio_available`, `njw_audio_rate`, `njw_audio_channels`, `njw_audio_read`, `njw_audio_note_playing` |
| Diagnostics | `njw_stats` (heap, GC, bytecode, paint and 3D counters — the same numbers the native `--stats` line prints) |
| JAR metadata | `njw_probe`, `njw_app_title`, `njw_app_vendor`, `njw_app_version`, `njw_midlet_count`, `njw_midlet_class_at`, `njw_app_icon` |
| Save states | `njw_can_save`, `njw_save_state`, `njw_load_state`, `njw_alloc`, `njw_free` |
| Tuning | `njw_set_speed`, `njw_speed` |

`njw_frame` returns a bit mask: `NJW_FRAME_RUNNING = 1` (keep pumping),
`NJW_FRAME_DREW = 2` (a new frame is in `njw_framebuffer()`), `NJW_FRAME_LOADING = 4`
(still starting up / no `Displayable` yet). A negative return is fatal; read
`njw_error()`. When the MIDlet exits, the mask loses `RUNNING`; read
`njw_stop_reason()` before `njw_stop()` if the message matters.

### Framebuffer

`njw_framebuffer()` returns the current screen as RGBA8888, `width * height * 4`
bytes, top-down, R,G,B,A byte order — ready for `ImageData` with no JavaScript
conversion. The runtime paints into an RGB565 buffer owned by the port (the format
every port chooses by default); `display_present` expands it to RGBA once per
presented frame and bumps `njw_frame_epoch()`. The pointer stays valid until the
next `njw_stop()`; copy or draw it before pumping the next frame.

### Input

`njw_key(code, down)` queues a key transition using the runtime's `nj_key`
numbers (`0`–`9` are 1–10, `NJ_KEY_FIRE = 17`, `NJ_KEY_SOFT_LEFT/RIGHT = 18/19`,
`NJ_KEY_UP/DOWN/LEFT/RIGHT = 13–16`, …). Queues are drained by the next frame.
`njw_pointer(action, x, y)` uses device pixels and the same action codes as the
port contract (`0` = down, `1` = up, `2` = move); it is the first finger
(pointer id 0). `njw_pointer_multi(id, action, x, y)` queues the same action for
an explicit pointer id 0–255: the Canvas still tracks one owner (first finger),
while `com.nokia.mid.ui.MultipointTouch` sees every id. The JS shell maps each
`PointerEvent.pointerId` to a stable id and falls back to single-pointer
`pointer` calls when an older bundle lacks the export. Text entry is driven by the
runtime: poll `njw_text_active()`/`njw_text_state()` from the page's text bridge,
send committed strings with `njw_text_input()`, editing keys with `njw_text_edit()`,
and whole-field replacements (paste, IME composition) with `njw_text_replace()`.

### Audio

Audio is pull-based: the runtime synthesizes tone/PCM/MIDI into a bounded ring and
the host copies it out with `njw_audio_read(int16_t *dst, int max_frames)` (frames
are sample frames of interleaved channels). The default format is 22050 Hz mono
and follows `pcm_open()` when a game changes it; poll `njw_audio_rate()` and
`njw_audio_channels()` when you open a new audio source. `njw_audio_note_playing(n)`
tells the runtime how many frames the host has scheduled but not played yet, so
`Player.getMediaTime()`/`pcm_queued` stay honest — the shell calls it after each
`njw_audio_read()`.

Two deliberate differences from a native port: `tone_stop`/`pcm_close` do not
flush queued frames (the host pulls once per animation frame; flushing would
delete almost every sound effect, because the virtual clock can finish a blocking
`Manager.playTone()` within one pump), and the ring is bounded so a silent host
cannot grow memory without bound. Mute is a host-side gain of 0; do not stop
reading.

### Diagnostics

`njw_stats(double *out, int count)` copies the engine counters the native
`--stats` line prints — heap capacity/used/peak, GC runs, live objects, thread
count, dispatched bytecodes, native calls, presented frames, `paint()` calls,
`Graphics` calls, 3D renders and triangles — into `out` in `njw_stat` order and
returns `NJW_STAT_COUNT` (13). The shell turns them into a **Diagnostics**
section of the tools panel (heap used/capacity, frames/paints, GC runs and a
live bytecode rate) so a browser bug report can be compared with the command
line; the rates are sampled at 2.5 Hz only while the panel is open, and every
counter is zero before a successful `njw_start()`.

### JAR metadata and save states

`njw_probe(path)` opens the JAR in a small temporary VM and copies the
manifest-derived metadata the library UI needs (title falls back to the file name,
plus vendor, version, icon resource bytes and the MIDlet class list) and then
tears the VM down; `njw_start()` re-creates it. This is cheap enough to call on
import. Save states use the same whole-VM snapshot format as the desktop ports:
`njw_save_state(void **blob, int *len)` returns a malloc'd blob (release with
`njw_free`), `njw_load_state(blob, len)` must be called at a frame boundary, and
a blob from a different JAR, ABI, pointer size, endianness or screen geometry is
refused. Because the blob is self-describing, the shell can also hand it to the
user: each slot has **Export** (download `<title>-slot<N>-<time>.njst`) and
**Import** (pick a `.njst`, validate it through the loader, then store + apply
it). Exported files work in any port build that runs the same title. **A failed `njw_load_state()` leaves the VM unusable** — the same rule as
the native API — so the shell reports "save incompatible, restart required" and
requires a fresh `njw_start()`. Each local save also keeps a 96×128 PNG
preview of the screen at save time (`.slot__thumb` in the slot list); a `.njst`
import has no pixels, so its preview appears only after the restored state is
saved again. The **Save on exit** toggle stores one extra snapshot on the game
record itself (the `autoState` field, with its own 96×128 preview) when the
player leaves a game, keeping the three manual slots untouched; **auto-resume**
treats it as another candidate and restores whichever of the slots or the exit
snapshot has the highest frame count. The tools panel surfaces that record as a
dashed, read-only row: it names the capture time and frame count, offers
**Load** (same failure contract as a manual slot) and **Clear** (drops the field
until the next exit writes it again).

## Persistence

The runtime uses the standard emulated file system through libc (`NJ_USE_LIBC=1`),
so RecordStore and `FileConnection` land in Emscripten's virtual FS. The shell
persists it with IDBFS:

```
/nano_j2me/
├── jars/                  uploaded JAR copies
├── saves/<gameId>/        RMS (RecordStore) directories: a save from the game
└── files/<gameId>/        FileConnection sandbox root (NJW_OPT_FS_DIR)
```

`NJW_OPT_RMS_DIR` and `NJW_OPT_FS_DIR` point at those directories before
`njw_start()`. Without IDBFS (private windows, old browsers) the shell falls back
to periodic `FS.syncfs()` plus `pagehide`/`visibilitychange` flushes, so saves
usually still survive a reload; clearing site data removes them, as with the
desktop client's data directory.

RecordStore files use the same `NJMT`/`NJBG` formats as other ports, so a save
directory can be copied between the browser (exported from the IDBFS mount) and a
desktop install.

## Performance

The interpreter is the engine, and wasm is roughly 1–4× slower than a native
build depending on the workload (uncapped 600-frame runs in Node, release build,
`frame=16.667 ms`):

| Game | wasm | native x86_64 | ratio |
|---|---|---|---|
| `game15.jar` | ≈6,500 fps | ≈16,200 fps | 2.5× |
| `bubblet.jar` | ≈3,900 fps | ≈7,700 fps | 2.0× |
| `slimevolley.jar` | ≈7,900 fps | ≈20,700 fps | 2.6× |
| `1942空战_10403.jar` | ≈7,900 fps | ≈12,500 fps | 1.6× |

Those numbers are headroom, not a frame rate: games are timed by the virtual
clock, so 60 Hz needs 60 pumps/second — a few percent of this budget — and the
browser's rendering path, not emulation, is the usual bottleneck. First load
costs the 2.1 MiB wasm download (compressed ≈0.5 MiB over the wire with standard
HTTP compression) plus module instantiation; heap growth is enabled so large
games can exceed the initial 64 MiB wasm memory without a crash.

## Verification

Runnable from a fresh checkout with the wasm built (`--node --corpus` is the
default and is what CI and these docs run):

```sh
tools/check_web.sh --node --corpus   # API contract + pinned corpus, about 10 s
tools/check_web.sh --all             # those two plus the Playwright page test
tools/check_web.sh --sweep           # full-corpus audit, 403 titles, about 2 min
tools/check_web.sh --perf            # wasm throughput smoke test, about 1 s
tools/check_web.sh --smoke           # minimal module smoke test, no corpus needed
tools/check_web.sh --live            # play a JAR in the deployed demo (needs network)
tools/check_web.sh --live=https://example.dev/   # same, against another deployment
tools/check_web.sh --build --node    # rebuild build/web/ first
```

`tools/check_web.sh` sources the Emscripten environment when needed (emcc on
`PATH`, `$EMSDK`, or `.local-tools/emsdk`), builds on request (`--build`), and
tees everything to `<evidence>/check-web.log`. It exits `0` when every requested
suite passed, `1` on a build or test failure, and `2` on a usage or
missing-artifact error. `--browser` is a graceful SKIP (still exit `0`) only
while the browser test file is absent; now that it is present the hook needs
Playwright for Firefox — resolved from a local `playwright`/`playwright-core`
installation, or from the `playwright-core` bundle inside the Python Playwright
driver (the latter makes the suite work here without `npm install`). Useful
flags: `--evidence DIR` (default
`.local-tools/debug-runs/web-verify-<YYYYMMDD-HHMM>`) and `--frames N` (corpus
frames per title, default 120, range 30–2000 — the first frames are needed for
the render assertions to settle). `--sweep` is deliberately **not** part of
`--all`: it starts all 403 corpus titles in fresh child processes (~2 min) and is
for release/CI runs, while `--all` stays a ~21 s edit loop. `--perf` is likewise
opt-in: it measures four titles and writes `perf-summary.json` next to the log.
`--live` is opt-in too: it opens a **deployed** copy of the shell (default
<https://lucinhu.github.io/nano_j2me/>, or `--live=URL`) with Python Playwright
and headless Firefox, so it needs network access plus
`python3 -m pip install playwright && python3 -m playwright install firefox`.
It exits `2` when Python Playwright is missing and `3` when the site itself is
unreachable, which keeps a flaky network from looking like a shell bug.

- **API test** (`tools/web_test/web_api_test.mjs`) — module load, version,
  manifest probe, error paths, `hello.jar` render, screen-size and speed options,
  keyboard input, pause/resume, save/load round-trip and a rejected load,
  restart-after-stop, the `njw_stats()` counters (heap sanity, frames, paints,
  bytecodes), audio on `tests/fixtures/audiodemo.jar`, and the
  `NJW_OPT_HEAP_BYTES` option on the heap-hungry `愤怒的小鸟_383.jar` (fails at
  the default 16 MiB, completes 90 frames at 64 MiB), and `NJW_OPT_MIDLET_CLASS`
  on the three-MIDlet `book6.jar` (default Dungeon vs. Checkers render different
  framebuffers).
- **Corpus test** (`tools/web_test/web_corpus_test.mjs`) — a deterministic slice
  of the 400-title [compatibility corpus](CORPUS.md) (plus fixtures and one
  regression title) run for 120 frames each with JAR start, drawn-frame and
  non-blank-frame checks. The regression title
  `午夜台球圣诞中文版_8677.jar` installs its `Canvas` in the MIDlet constructor
  and starts its game thread there, which is what caught the runner skipping
  `startApp()`.
- **Browser test** (`tools/web_test/web_browser_test.mjs`, 44 checks) — serves
  `build/web/` on an ephemeral port and drives the real shell in headless Firefox
  through Playwright: the real engine (`kind=wasm`, API 1, runtime 0.5.7),
  zh-CN UI, import `hello.jar` → metadata and icon in IndexedDB, 240×320
  non-blank canvas, keyboard input (nj_key 15/16/17/6) with advancing frame
  numbers, auto-pause on panel open (frames frozen) and resume, a 329,724-byte
  save-state slot written and loaded without a restart (with its 96×128 PNG
  thumbnail decoded in the slot list; empty slots stay hidden), exported to a `.njst`
  download and imported back into another slot (blob size verified), exit →
  library, reload
  keeps the game and the slot, and a wasm-less copy of the site falls back to the
  mock with the "run tools/build_web.sh" banner — with zero page/console errors
  and zero external requests. Five more checks cover the PWA layer: the manifest
  parses (`display=standalone`, four icons including a maskable one), the service
  worker controls the page and has `nj2me.js`/`nj2me.wasm` in its cache, the
  library footer reports offline ready, reloading with the static server stopped
  still boots the shell with `kind=wasm`, and an imported game then starts and
  keeps drawing with no server at all (76,800 non-black pixels, frame counter
  advancing). Another makes `愤怒的小鸟_383.jar` die with an out-of-memory error at the default 16 MiB heap, clicks the error overlay's **restart with 64 MB** action, and asserts the game comes back at 64 MiB and keeps drawing. Another stars one of two imported games, filters the library with the favourites toggle, reloads the page (the star survives, the filter resets), and unfavourites again to reach the empty-favourites state. Two more checks run against a second local server that serves a JAR with CORS headers: a link typed into the URL dialog downloads, imports and plays; a failing link (404) is reported inside the dialog instead of the page. Another pair enables **Save on exit** and **Resume the latest save on
  launch**, plays 60 frames, leaves the game, asserts the exit snapshot landed
  on the game record, reloads the page and starts the title again: the shell
  reports resuming from the exit auto-save and keeps drawing. One check plays a
  title, exits and verifies the library's **continue card** names it, hides
  while the search box has text, and starts the title again when clicked.
  One more check opens the panel, verifies the default 30 fps cap, switches to
  60, 20 and 15 fps and samples the emulated pump rate (about 60, 20 and 15 per
  second), then returns to display-refresh (uncapped) mode. It finishes by
  picking 30 fps, leaving, reloading the page and starting the same title: the
  game record keeps the cap, the select shows it again and the pump rate
  follows it. Another check
  does the same round-trip for the game speed: 100 % → 200 % is applied to the
  engine immediately, stored on the game record, and the reloaded launch comes
  back at 200 %.
  Another enables Save on exit, leaves the game, reopens the title and checks
  the panel's read-only auto-save row shows its date/frame count, loads it on
  purpose, clears it, and verifies the record no longer carries the snapshot.
  One more check names a save slot "Boss 战", confirms the name lands on the
  IndexedDB record (and therefore names the `.njst` download `boss-战-slot1-…`),
  reloads the page and finds the name still in the slot's input. Another imports
  two audited JARs and asserts the compatibility badges: a green "verified" one
  on `hello.jar` and an amber "needs more heap" one on `愤怒的小鸟_383.jar`,
  whose tooltip explains the out-of-memory class of the failure; tapping the
  warning then asserts the same explanation arrives as a toast without leaving
  the library. One check covers
  the game panel's memory selector: picking 64 MiB
  persists it in `nj2me.settings` (localStorage) for the next launch and shows
  the "applies on the next launch" toast. Another opens the diagnostics section
  and checks the heap, frame/paint, GC and bytecode-rate lines look like real
  numbers and that the frame counter keeps advancing while the game runs.
  Another starts a game in an
  isolated context, switches the panel to 176×220, restarts, and asserts the
  engine really hands back a 176×220 canvas (and that the preset was
  persisted). Another fakes a standard gamepad
  (`navigator.getGamepads` stub + `gamepadconnected` event): A maps to `nj_key`
  17, the left stick to 16, B to 21, and every press is released again on
  disconnect. Another check imports the three-MIDlet `book6.jar` in an isolated
  browser context, sees Dungeon/Checkers/Messenger in the startup selector,
  picks Checkers, restarts, and asserts the canvas picture changed and the
  choice was written back to the library record. One more moves the volume
  slider to 40 %, checks the WebAudio gain and the persisted setting, then
  mutes and drags it again to prove that unmutes. Another opens two pages in
  one browser context: while the first tab plays, the second tab is refused
  with the "another tab is running" warning toast and stays on the library;
  after the first tab exits, the second tab can start the same title.
- **Sweep suite** (`tools/web_test/web_sweep_test.mjs`, 403 titles) — the
  full-corpus audit: every JAR under `tests/games/` except the known-bad
  `1945空战英雄_6105.jar` is started in its own Node child process for 90 frames
  with a 25 s timeout. Each title is classified `ok` / `black` (no sampled frame
  had a non-black pixel) / `fatal` / `start` / `probe` / `install` / `load` /
  `timeout`; the run fails on an unexpected class, on a regression against the
  documented expectations (constructor refusal `毁灭战士3`, heap-bound
  `愤怒的小鸟`, invalid JAR `鲁宾逊漂流记`), or when fewer than `--min-ok`
  titles render. `black` is a warning, not a failure: whether a title happens to
  show content on a sampled frame depends on host speed and wall-clock phase.
  Results land in `<evidence>/sweep-403.jsonl` and `sweep-summary.txt`.
- **Perf suite** (`tools/web_test/web_perf_test.mjs`, 9 checks) — the wasm
  throughput smoke test: four light/medium titles (`game15`, `bubblet`,
  `slimevolley`, `1942空战_10403`) are warmed up for 60 frames and then pump 600
  frames each with the fixed `1000/60 ms` timestep. Every title must stay above a
  deliberately conservative floor (≈10 % of the reference machine) so the suite
  catches order-of-magnitude regressions on unknown CI hardware without failing
  slow hosts; the measured fps/ms land in `<evidence>/perf-summary.{txt,json}`.
- **Live test** (`tools/web_test/web_live_check.py`, 7 checks) — walks the
  public deployment the way a visitor does: `index.html` is served, the shell
  boots the wasm engine from the site, `manifest.json` is served and parses,
  `hello.jar` (when `tests/games/` is present) is imported through the file
  input, the tile appears, the game starts, the canvas is 240×320 with content,
  frames advance, and no page/console errors occur. Screenshots plus
  `live-check.json`/`live-check.txt` land in the evidence directory; the tool is
  deliberately outside `--all` because it depends on a deployment and the
  network.

Measured on the reference build (Emscripten 7.0.0, `include/nj2me/nj_web.h`
0.5.7): the API suite passes **36/36 checks in 0.4 s**, the corpus suite
**47/47 checks (42 titles) in 8.7 s**, the browser suite **45/45 checks in about
40–65 s** (a loaded host slows the page test down; every check is bounded), the
perf suite **9/9 checks in about 0.5 s**, and the live test **7/7 checks in
about 8 s** against the deployed demo — `tools/check_web.sh --all` is
**60–105 s** end to end on a busy machine. The browser suite includes the
per-game key-remapping round trip: rebind Fire to `Z`, press `Z`, reload, and
reset back to Space. The sweep suite needs about 65 s for all 403
titles with `--jobs 4` (about 4 min on one job), the perf suite about 1 s. All 40
playable titles draw 120/120 frames (the audiodemo and bench fixtures exit on
their own, as expected), there are no engine `[nj2me] error:` lines,
and the wasm heap stays at 64 MiB.
The known-bad `1945空战英雄_6105.jar` is excluded because it fails natively too.
A real-page smoke run (headless Firefox, `build/web/` served on a local port)
imports `hello.jar` through the shell, renders the 240×320 frame, accepts
keyboard input and returns to the library with no page errors; a `1942空战_10403.jar`
title screen rendered by the browser was pixel-identical to the native
`--dump-frames` PPM.

A separate playability pass drives an animated commercial title
(`蹦球传说美化版_2959.jar`, Bounce Tales) in the same real page: the pump runs at
**60.1 rAF/s** in headless Firefox, nine distinct frames appear across 30
samples over six seconds, arrow keys and FIRE change the screen without
stopping the pump, opening the tools panel freezes the picture (auto-pause) and
closing it resumes, and the library entry survives a page reload. Running
`audiodemo.jar` in that page produces ten engine audio events
(`wav=200000`, `amr=80000`, `adpcm=252500`, state 400 — the
`http=unsupported` line is expected without a network hook), with zero
console/page errors. Evidence: `.local-tools/debug-runs/web-playtest/`.

### Full-corpus audit (403 titles)

The whole compatibility corpus is covered by a runnable audit, not a one-off
experiment:

```sh
tools/check_web.sh --sweep   # 403 titles, 90 frames each, about 2 min, jobs=1
```

`tools/web_test/web_sweep_test.mjs` starts every JAR in a fresh Node child
process, so a single hung title cannot stall the rest; it pumps 90 frames and
samples the framebuffer every sixth frame plus the last one. The known non-ok
titles are pinned as expectations (`毁灭战士3_10598` constructor refusal,
`愤怒的小鸟_383` heap, `鲁宾逊漂流记汉化BT版_4677` invalid JAR), `ok` has a floor
(`--min-ok`, default 380), and a `black` result is a warning, not a failure: the
audit runs at host speed under the folded wall clock, so whether a title shows
its picture within the first 90 sampled frames can shift by a few frames.

Latest run: **393 ok, 7 black, 1 fatal, 1 constructor refusal, 1 invalid JAR,
zero timeouts** in about 65 s (four titles in parallel), RESULT PASS.  The
pacing fix also moved how much a title manages to draw inside the 90-pump
window: 都市赛车6火线追击, 生化危机之恶化 and 宠物王国6 now render where they
used to sample black, while L3D and 急速公路狂飙新年版 now sample a loading
phase; a black result stays a warning, not a regression signal.

| Class | Titles | Verdict |
|---|---|---|
| Black (7) | L3D, 灌篮高手-爆发!樱木, 急速公路狂飙新年版, 开心农场BT版, PC正版-使命召唤5, 乱世Q三国_1649, 有女倾城破解版_11525 | warning — no sampled frame had content within the 90-pump window; most are titles whose load/fade phase the audit samples early (several are black at the same checkpoint natively with `--fps 0`), and which titles land there shifts a little with pacing |
| Fatal (1) | 愤怒的小鸟_383 | Java heap exhaustion; the native build also throws `OutOfMemoryError` at its default heap. With `NJW_OPT_HEAP_BYTES` = 64 MiB the wasm run completes 90 frames (76,639 non-black pixels, 32 colours) just like native `--heap 64` — the shell's "Game memory" selector exposes the same option |
| Constructor refusal (1) | 毁灭战士3_10598 | by design — the MIDlet runs its game loop from the constructor and joins it, so the runner reports the bounded error after ~10 s instead of freezing the page |
| Invalid JAR (1) | 鲁宾逊漂流记汉化BT版_4677 | parity — native also rejects it (`bad JAR or no MIDlet-1`) |

`宠物王国6(去模拟器限制)_12473.jar` now renders (its loading art appears within
the sampled frames), which also shows why frame-exact black is a warning rather
than a pass/fail signal. The audit found and drove the three real fixes listed
above (wall-clock fold, unconditional `startApp()`, bounded constructor).
Evidence: `.local-tools/debug-runs/web-round8-sweep-clean/` (`sweep-403.jsonl`,
`sweep-summary.txt`).

The audit is also productised into the shell. `tools/gen_web_compat_data.mjs`
turns the newest sweep JSONL into the committed `web/data/compat.json`
(schema 1: `generatedAt`, `source`, `frames`, stage counts, and one entry per
audited title keyed by `file name|byte size` with the stage plus an explanation
for non-ok titles):

```sh
node tools/gen_web_compat_data.mjs \
  --from .local-tools/debug-runs/web-round8-sweep-clean/sweep-403.jsonl
```

The library matches an imported JAR by file name and exact byte size, so a
different rip of the same title stays unbadged; the report ships inside the
PWA shell, is precached by the service worker and validated by the module smoke
test. Regenerate it whenever a new `--sweep` run changes the classification.

The same scenarios can be driven by hand with `node` (Node has no IndexedDB, so
mount MEMFS instead of IDBFS) — the harnesses are the reference. Evidence
(screenshots, PNG dumps, logs) goes to `.local-tools/debug-runs/`, never into the
repository.

## Publish the demo (GitHub Pages)

`.github/workflows/web-demo.yml` builds the port and publishes `build/web/` as
a playable demo: it runs on every push to `main` that touches the wasm build
inputs (sources, shell, build scripts) and can be started by hand with
*Actions → Web demo (GitHub Pages) → Run workflow*. The job is a straight
reproduction of the local steps - check out, Node 20, Emscripten 7.0.0,
`tools/build_web.sh`, `tools/web_test/web_smoke_test.mjs` (module, option,
MEMFS and compatibility-report checks, plus a 60-frame `hello.jar` render when
the private corpus is present), upload the site, deploy - and a concurrency group cancels superseded
deployments.

One-time setup: in the repository, *Settings → Pages → Build and deployment →
Source* choose **GitHub Actions**. The demo then lives at
`https://<owner>.github.io/nano_j2me/` (custom domains work through the same
environment). The workflow runs `actions/configure-pages@v5` first, so a
missing Pages site fails with a clear "ensure GitHub Pages has been enabled"
error in the build job instead of a 404 in the deploy job. The workflow also
runs `tools/web_test/web_live_check.py` against the just-deployed URL in a third
job (`verify the deployed demo`), so a deployment that breaks the published
shell is caught without a manual step; that CI run has no corpus, so the live
check verifies the shell/manifest/engine boot but skips the game step there (the
`live-demo-check` artifact keeps the JSON report). For this repository
the demo is live at <https://lucinhu.github.io/nano_j2me/>. No game JARs are shipped; visitors import their own files into
the local library, and because the site is a PWA it keeps working offline after
the first visit. Builds are path-reproducible, so the deployed wasm should
match a local `tools/build_web.sh` byte for byte.

Emulate the CI job locally with:

```sh
tools/build_web.sh
node tools/web_test/web_smoke_test.mjs --evidence .local-tools/debug-runs/web-demo
```

After a deployment, verify the published site the way a visitor uses it:

```sh
tools/check_web.sh --live                              # the demo above
tools/check_web.sh --live=https://your-host.example/   # a custom deployment
```

The check writes `live-check.json`/`live-check.txt` and a game screenshot to the
evidence directory. It is not part of `--all` because it needs network access
and a deployed site (see the environment note in the Verification section); the
Pages workflow runs it automatically after deploying, with the JAR step skipped
because CI has no corpus.

## Limitations

- **No networking/sockets/Bluetooth/sensors** in this build: the optional hooks
  are `NULL`, so MIDlets asking for `Connector` HTTP, sockets, JSR 82 or JSR 256
  get the documented "not available" failures instead of hanging. Adding them is
  a port job (`fetch`/WebSocket shims) and should be a separate, documented step.
- **No JIT**: wasm cannot allocate executable pages, so the interpreter is used
  (the JIT hooks are `NULL`). This is the same situation as the ESP32 target and
  the reason games remain playable rather than fast.
- **No native font files**: the built-in bitmap fonts and in-tree TrueType
  rasterizer are used, like the Android build.
- **MIDlets that never return from the constructor** are refused with a clear
  error after about 10 s (e.g. `毁灭战士3_10598.jar`, whose constructor joins the
  thread that runs the game loop). The native CLI only "runs" those titles
  because `--run-frames` aborts the constructor from the frame hook; a browser
  page cannot be frozen like that, so the port bounds the wait and reports it.
- **Java heap** defaults to the native 16 MiB (`NJW_OPT_HEAP_BYTES` raises it up
  to the wasm memory limit). Heavy titles such as `愤怒的小鸟_383.jar` exhaust it
  exactly as they do in the native build at the default heap; 64 MiB is enough
  for both. The shell's game panel exposes this as a "Game memory" selector
  (16/32/64/128 MiB) applies to the next launch; it is one shell-wide setting,
  not a per-game one, just like the speed and volume controls. An out-of-memory
  error adds a one-click restart with the next heap step (16/32 → 64, 64 →
  128 MiB) to the error overlay and persists the choice.
- **One engine per browser profile.** Tabs coordinate through a
  `BroadcastChannel` lease, so a second tab cannot start a game while another
  one owns the engine (two engines would fight over the same IDBFS records).
  The lock is advisory: browsers without `BroadcastChannel` keep the old
  uncoordinated behaviour, and an owner that stops heartbeating loses the
  lease after 6 s so a crashed tab cannot wedge the profile.
- **Not yet verified**: iOS Safari, very large heap games (>512 MiB) and
  WebKitGTK.
- The wasm target cannot be built with `NJ_ENABLE_JIT`/threaded dispatch enabled
  hooks; the build forces the portable configuration.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `failed to asynchronously prepare wasm` / cannot find `nj2me.wasm` | `locateFile` must return an absolute URL; serve over HTTP, not `file://` |
| Page shows "wasm 未构建 / engine missing" banner | build with `tools/build_web.sh`, serve `build/web/` (not `web/`) |
| Blank canvas forever, `njw_frame` keeps returning `LOADING` | the MIDlet installed no `Displayable`; check `njw_stop_reason()` after 20 virtual seconds, and the console log (`[nj2me] ...`) |
| `njw_start()` returns -1 with `the MIDlet constructor does not return ...` | the title runs its game loop from the MIDlet constructor (e.g. `毁灭战士3`); this cannot be supported without freezing the page, see [Limitations](#limitations) |
| No sound | browsers need a user gesture before audio; click once, check `njw_audio_available()` and the shell's mute/gain path |
| Saves vanish after reload | IndexedDB blocked (private mode) or IDBFS fallback not syncing; check the shell's console warning and `navigator.storage.persist()` |
| `redefinition of typedef 'nj_vm' is a C11 feature` while building | don't use `-DNJ_WERROR=ON` on the wasm target (pre-existing header style) |
| `the display loop failed ... last error: thrown: java/lang/OutOfMemoryError` | the game needs more Java heap: pick a bigger "Game memory" in the tools panel (default 16 MiB, try 64/128 MiB) and restart the game, or click the error overlay's one-click larger-heap action (next step: 16/32 → 64, 64 → 128 MiB); `njw_error()` appends the last escaped exception for exactly this reason |
| Out of memory / `RangeError` in wasm | game needs more heap: raise `NJW_OPT_HEAP_BYTES` (max 1 GiB wasm memory) or lower screen size |
| Build picks up the wrong Emscripten | `source .local-tools/emsdk/emsdk_env.sh` or run `tools/build_web.sh` (it locates the SDK itself) |
| A redeployed shell still shows the old page | the cache-first service worker kept the previous shell: bump `CACHE` in `web/sw.js` (or open once with `?nosw=1`) |
| Offline reload does not start / falls back to mock | service worker needs `https://` or `localhost`, and the engine must have been cached on an earlier online visit; check DevTools → Application → Cache Storage for `nj2me-shell-*` |

## Files

| Path | Role |
|---|---|
| `include/nj2me/nj_web.h` | JavaScript⇄C API and the port's guarantees |
| `src/port/web/nj_web.c` | the Emscripten port: `nj_port` hooks, runner, `njw_*` exports |
| `web/` | the bundled shell (HTML/CSS/ES modules) |
| `web/manifest.json`, `web/sw.js` | PWA manifest and service worker (installable, offline shell + engine) |
| `web/assets/icon*.png` | 192/512/maskable icons rendered from the Android icon design by `tools/gen_brand_assets.py` (`icon.svg` is its vector twin) |
| `tools/build_web.sh` | SDK discovery, `emcmake` build, `build/web/` assembly |
| `tools/emsdk_setup.sh` | installs the Emscripten SDK into `.local-tools/emsdk` |
| `tools/check_web.sh`, `tools/web_test/` | Node API/corpus, full-corpus sweep, throughput, module-smoke and live-demo suites, Playwright browser verification |
| `tools/web_test/web_smoke_test.mjs` | corpus-free post-build smoke test used by the Pages workflow |
| `tools/web_test/web_live_check.py` | plays a JAR in the deployed demo (Python Playwright + headless Firefox) |
| `tools/gen_web_compat_data.mjs` | turns a `--sweep` JSONL report into `web/data/compat.json` |
| `web/data/compat.json` | committed audit results that badge known titles in the library |
| `.github/workflows/web-demo.yml` | builds the port (Node 24 actions), publishes `build/web/` to GitHub Pages and verifies the deployed URL with `web_live_check.py` |
| `build/web/` | served directory: shell + `js/nj2me.js` + `js/nj2me.wasm` (git-ignored) |

The C API is versioned (`NJW_API_VERSION`); the shell checks it at load time and
degrades to mock mode when it does not match the module it loaded. Keep the API
comments in `nj_web.h` truthful when changing behaviour — they are the contract
the shell and third-party embedders program against.
