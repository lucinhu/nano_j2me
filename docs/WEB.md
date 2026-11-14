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
| Verify | Node API contract + 41-title corpus audit + real-Firefox page test (Playwright), see [Verification](#verification) |
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
  search, sorting, deletion and a settings page (language, default screen size).
- **Game view** — a `<canvas>` sized to `njw_width()`×`njw_height()` and scaled
  with `image-rendering: pixelated`, an `requestAnimationFrame` loop that calls
  `njw_frame(dt_ms)` exactly once per frame, a loading overlay until the first
  `Displayable` exists, then pause/resume, speed (100/150/200 %), mute,
  three save states per game, PNG screenshot download, fullscreen, restart and
  exit-to-library.
- **Input** — keyboard (arrows = D-pad, Enter/Space = Fire, F1/F2 = soft keys,
  Backspace = Clear, digits, `*`/`#`), an on-screen phone keypad for touch devices
  with multi-touch and slide-off cancel, pointer pass-through for games that use
  canvas pointers, and a hidden textarea bridge for MIDlet text fields using the
  native-editor callbacks (`njw_text_*`).
- **Audio** — a pull loop drains `njw_audio_read()` into Web Audio buffers
  scheduled roughly 100–150 ms ahead, resumes the `AudioContext` on the first
  user gesture, and suspends it on pause.
- **Persistence** — game JARs and save-state slots live in IndexedDB; emulated
  file system (RecordStore and `FileConnection`) lives on an IDBFS mount at
  `/nano_j2me`, written by the shell before `njw_start()` and synced to
  IndexedDB automatically.
- **Mock mode** — served straight from `web/` (no wasm), or with `?mock=1`, the
  shell loads `web/js/mock-module.js`, an animated 240×320 test pattern behind the
  same facade, so the entire UI is testable before the engine exists. The page
  never throws when the module is missing; it shows a banner pointing at
  `tools/build_web.sh`.

To use the shell with a custom engine build, serve `build/web/`; to develop the
shell alone, serve `web/` (`python3 -m http.server 8001 -d web`).

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
  frame pumps. While the first `Displayable` has not appeared yet, a frame may
  internally run up to four pumps (`NJW_FRAME_LOADING` says so); this is how
  splash screens and constructor-time waits work without blocking the tab.
- The internal frame pacer is disabled (`nj_vm_set_frame_cap(vm, 0)`; the port
  reports `fps_cap = 0`), so `nj_midp_frame()` never sleeps. Display pacing is the
  host's business: the shell uses `requestAnimationFrame`.
- `njw_stop()` performs the `nj_run()` teardown (bounded `destroyApp()`, app close,
  VM destruction) and can be followed by another `njw_start()`.

### The virtual clock

The emulated clock is owned by the host, not the wall clock:

- Until the MIDlet instance exists ("load phase") `sleep_ms()` and `idle` advance
  the virtual clock by the full requested amount, so a constructor that sleeps
  (common in splash-screen MIDlets) completes inside `njw_start()` instead of
  freezing a JavaScript frame.
- During normal play each `njw_frame(dt_ms)` adds `dt_ms` (clamped to 0–250 ms) to
  the virtual clock once. Inside a single pump, `sleep_ms()` fast-forwards by
  small amounts (1 ms for the first few calls, then at most 50 ms per call) so a
  Java thread waiting on a timer, a blocking native or a `Thread.join()` can make
  progress instead of deadlocking the pump. `idle()` never advances the clock.
- `njw_set_speed(percent)` (10–800) scales the virtual clock exactly like
  `nj_vm_set_speed()` on the desktop ports, and does not jump the clock when
  changed.

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
| Input | `njw_key`, `njw_pointer`, `njw_text_input`, `njw_text_edit`, `njw_text_replace`, `njw_text_active`, `njw_text_current`, `njw_text_state` |
| Audio | `njw_audio_available`, `njw_audio_rate`, `njw_audio_channels`, `njw_audio_read`, `njw_audio_note_playing` |
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
port contract (`0` = down, `1` = up, `2` = move). Text entry is driven by the
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

### JAR metadata and save states

`njw_probe(path)` opens the JAR in a small temporary VM and copies the
manifest-derived metadata the library UI needs (title falls back to the file name,
plus vendor, version, icon resource bytes and the MIDlet class list) and then
tears the VM down; `njw_start()` re-creates it. This is cheap enough to call on
import. Save states use the same whole-VM snapshot format as the desktop ports:
`njw_save_state(void **blob, int *len)` returns a malloc'd blob (release with
`njw_free`), `njw_load_state(blob, len)` must be called at a frame boundary, and
a blob from a different JAR, ABI, pointer size, endianness or screen geometry is
refused. **A failed `njw_load_state()` leaves the VM unusable** — the same rule as
the native API — so the shell reports "save incompatible, restart required" and
requires a fresh `njw_start()`.

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
| `game15.jar` | ≈6,100 fps | ≈16,200 fps | 2.7× |
| `bubblet.jar` | ≈3,800 fps | ≈7,700 fps | 2.0× |
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
tools/check_web.sh --node --corpus   # API contract + pinned corpus, about 26 s
tools/check_web.sh --all             # those two plus the Playwright page test
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
the render assertions to settle).

- **API test** (`tools/web_test/web_api_test.mjs`) — module load, version,
  manifest probe, error paths, `hello.jar` render, screen-size and speed options,
  keyboard input, pause/resume, save/load round-trip and a rejected load,
  restart-after-stop, audio on `tests/fixtures/audiodemo.jar`.
- **Corpus test** (`tools/web_test/web_corpus_test.mjs`) — a deterministic slice
  of the 400-title [compatibility corpus](CORPUS.md) (plus fixtures) run for 120
  frames each with JAR start, drawn-frame and non-blank-frame checks.
- **Browser test** (`tools/web_test/web_browser_test.mjs`, 18 checks) — serves
  `build/web/` on an ephemeral port and drives the real shell in headless Firefox
  through Playwright: the real engine (`kind=wasm`, API 1, runtime 0.5.7),
  zh-CN UI, import `hello.jar` → metadata and icon in IndexedDB, 240×320
  non-blank canvas, keyboard input (nj_key 15/16/17/6) with advancing frame
  numbers, auto-pause on panel open (frames frozen) and resume, a 329,724-byte
  save-state slot written and loaded without a restart, exit → library, reload
  keeps the game and the slot, and a wasm-less copy of the site falls back to the
  mock with the "run tools/build_web.sh" banner — with zero page/console errors
  and zero external requests.

Measured on the reference build (Emscripten 7.0.0, `include/nj2me/nj_web.h`
0.5.7): the API suite passes **33/33 checks in 0.2 s**, the corpus suite
**46/46 checks (41 titles) in 26 s**, and the browser suite **18/18 checks in
about 5 s** — `tools/check_web.sh --all` is **32 s** end to end. All 39 playable
titles draw 120/120 frames, there are no engine `[nj2me] error:` lines, and the
wasm heap stays at 64 MiB.
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

The same scenarios can be driven by hand with `node` (Node has no IndexedDB, so
mount MEMFS instead of IDBFS) — the harnesses are the reference. Evidence
(screenshots, PNG dumps, logs) goes to `.local-tools/debug-runs/`, never into the
repository.

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
- **Not yet verified**: iOS Safari, very large heap games (>512 MiB), true
  multi-tab use of the same IDBFS prefix (last writer wins), and WebKitGTK.
- The wasm target cannot be built with `NJ_ENABLE_JIT`/threaded dispatch enabled
  hooks; the build forces the portable configuration.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `failed to asynchronously prepare wasm` / cannot find `nj2me.wasm` | `locateFile` must return an absolute URL; serve over HTTP, not `file://` |
| Page shows "wasm 未构建 / engine missing" banner | build with `tools/build_web.sh`, serve `build/web/` (not `web/`) |
| Blank canvas forever, `njw_frame` keeps returning `LOADING` | the MIDlet installed no `Displayable`; check `njw_stop_reason()` after 20 virtual seconds, and the console log (`[nj2me] ...`) |
| No sound | browsers need a user gesture before audio; click once, check `njw_audio_available()` and the shell's mute/gain path |
| Saves vanish after reload | IndexedDB blocked (private mode) or IDBFS fallback not syncing; check the shell's console warning and `navigator.storage.persist()` |
| `redefinition of typedef 'nj_vm' is a C11 feature` while building | don't use `-DNJ_WERROR=ON` on the wasm target (pre-existing header style) |
| Out of memory / `RangeError` in wasm | game needs more heap: raise `NJW_OPT_HEAP_BYTES` (max 1 GiB wasm memory) or lower screen size |
| Build picks up the wrong Emscripten | `source .local-tools/emsdk/emsdk_env.sh` or run `tools/build_web.sh` (it locates the SDK itself) |

## Files

| Path | Role |
|---|---|
| `include/nj2me/nj_web.h` | JavaScript⇄C API and the port's guarantees |
| `src/port/web/nj_web.c` | the Emscripten port: `nj_port` hooks, runner, `njw_*` exports |
| `web/` | the bundled shell (HTML/CSS/ES modules) |
| `tools/build_web.sh` | SDK discovery, `emcmake` build, `build/web/` assembly |
| `tools/emsdk_setup.sh` | installs the Emscripten SDK into `.local-tools/emsdk` |
| `tools/check_web.sh`, `tools/web_test/` | Node API/corpus and Playwright browser verification |
| `build/web/` | served directory: shell + `js/nj2me.js` + `js/nj2me.wasm` (git-ignored) |

The C API is versioned (`NJW_API_VERSION`); the shell checks it at load time and
degrades to mock mode when it does not match the module it loaded. Keep the API
comments in `nj_web.h` truthful when changing behaviour — they are the contract
the shell and third-party embedders program against.
