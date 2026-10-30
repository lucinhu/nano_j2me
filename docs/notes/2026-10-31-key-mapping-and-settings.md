# Settable game keys, and a settings page that covers the port

## What changed

The desktop client's settings page grew from five rows to twelve, and the
twelve game keys are now rebindable from the launcher itself.

**Key mapping.** The launcher's settings page opens a 按键映射 page with one row
per action: 上 / 下 / 左 / 右 / 开火 / 左软键 / 右软键 / 清除键 / 游戏键 A / B / C / D.
`Enter` on a row captures the next key (`按下新按键…`, `Esc` cancels), and the
captured name is stored in `client.conf` as `map_up = W` (and so on), using the
name `SDL_GetKeyName()` prints - the same vocabulary the page shows, not a
numeric code. `Backspace` on a row returns it to its default.

The port half is a small public API in `include/nj2me/nj_port.h`:

```c
enum nj_port_bind { NJ_PORT_BIND_UP, ..., NJ_PORT_BIND_GAME_D, NJ_PORT_BIND_COUNT };
int nj_port_sdl2_bind(nj_port *port, int bind, int key);   /* SDL_Keycode, 0 = default */
int nj_port_sdl2_bound(const nj_port *port, int bind);     /* 0, a custom code, or -1 */
int nj_port_sdl2_key_for(const nj_port *port, int key);    /* NJ_KEY_*, or -1 */
```

`key_for()` is the lookup the event path itself uses, so it is testable
without a window. Digits, `*` and `#` stay fixed: they are the phone keypad,
not an action. `F3` (speed), `F5` (save), `F10` (end run), `F11` (fullscreen),
`F12`/`Tab` (on-screen keys) and `Esc` are the port's own controls and are
refused as bindings - binding the key that ends a run to "fire" would make a
game unplayable with no way back.

**More settings.** The page now also edits the scale filter (最近邻/线性),
fullscreen, renderer (软件/加速), heap size (自动/8/16/32/64 MB), mute and the
network switch. They travel to the port as the environment knobs it already
documents for the command line - `NJ_SDL2_FILTER`, `NJ_SDL2_FULLSCREEN`,
`NJ_SDL2_ACCEL`, `NJ_SDL2_MUTE`, `NJ_SDL2_NO_NETWORK` - through one helper,
`nj_client_apply_game_settings()`, which also installs the bindings and returns
the heap size for `nj_run_opts.heap_bytes`. `F11` toggles fullscreen during
play; the key map line the port logs at startup names it.

A direct run reads the same `client.conf`: `nj2me game.jar` now honours the
saved heap, filter, fullscreen, renderer, mute and network choices, while the
explicit `--scale`, `--fps`, `--keys` and `--heap` flags still win over the
file.

## Why the settings live in the port's environment, and the bindings in the port

The SDL2 port builds its window and renderer in `display_init()`, before
`nj_run()` exists, and it has no setters - by design, since a mutable setter
callable from a UI thread is the kind of thing the port contract (a UI thread
may only touch the VM through `input_poll`) exists to prevent. The environment
is already how the port's documented knobs work (`NJ_SDL2_ACCEL`,
`NJ_SDL2_CAPTURE`, `NJ_SDL2_KEYS`), so the client sets the variables and lets
the port read them; the client-side helper is named for that split.

The bindings are different: they are read on every key event, on the run
thread, inside the port's event loop. Putting the table in the port keeps one
lookup for `KEYDOWN` and `KEYUP` and makes a remap take effect without
restarting a game. A slot with a binding has exactly that key - the slot's
default key stops working for it, otherwise rebinding "fire" to `J` would leave
`Enter` also firing. Binding a key another slot uses moves it (the previous
slot reports `-1` from `bound()` and falls back to its defaults); the client
warns in its toast when the move happens.

The contract stayed where it was: the port validates and stores; the client
decides the file format, the page, the capture overlay and the messages.

## Verification

- `tests/test_port_binds.c` - 96 checks: default table, remap/reset, reserved
  refusals, move semantics, real `SDL_PushEvent` + `display_present` events,
  the env knobs, `take_dropped()` on a fresh port, second-port re-entry.
- `tests/test_client_config.c` - 72 checks: new defaults, bind table, round
  trip through `client.conf` of all settings and `map_*` lines, only changed
  bindings written, missing/hostile files.
- `tests/test_client_ui.c` - 322 checks: all twelve rows and values, scale/heap/
  fps wraps, the 按键映射 page (navigation, scrolling, pointer), the capture
  overlay, every capture rule, Backspace reset, save/load round trip, reset.
- `check_sdl2_client` grew a "settings reaching a direct run" section: it writes
  a `client.conf` with `filter_linear`, `heap_mb`, `mute`, `network` and two
  `map_` lines and runs a game directly, asserting the run exits cleanly.
- Full `ctest`: **138/138**, 62.31 s. `tools/check_no_statics.sh` reports no
  writable statics across 75 object files; `apps/nj2me_main.c` compiles with
  and without `NJ_HAVE_SDL2`.
- Screens: `.local-tools/debug-runs/settings-shot/frame-settings.png`,
  `frame-binds.png`, `frame-capture.png`, `frame-binds-bound.png` (720x540);
  two of them are in `docs/images/readme-client-settings.png` and
  `docs/images/readme-client-binds.png`.

## Stated limits

- Key names come from SDL, so a binding follows a key's logical name rather
  than its position; a layout that produces a name SDL does not know is
  refused by the port and shown as 无法识别的按键.
- `=` cannot be bound: `client.conf` uses it as the `key = value` separator.
- One key per action. There are no per-game profiles; the same bindings apply
  to every title. Settings are read when a game starts, except `F11`, which is
  a live toggle.
- The bind page offers no mouse capture: a mouse click cannot be assigned to a
  game key, only a keyboard key.
