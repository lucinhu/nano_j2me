# The library finds games, the pad plays them

## What changed

The desktop client grew the four things a library window is unpleasant without,
and the port grew a controller:

- **Search.** Typing in the library filters by display name, file name or MIDlet
  class. `Backspace` edits, `Esc` clears the filter before it means "quit",
  `Enter` runs the first match, and the bar's right side counts matches out of
  the whole library (`1 / 400 项`).
- **`F1`, the shortcut sheet.** A modal list of every key the library, a game
  window and the settings page accept, plus the path of `client.conf` and the
  note that the library remembers its size. `F1`, `Esc` or `Enter` closes it.
- **Icons are scaled.** The row icon box is 32x32; smaller images are enlarged
  nearest-neighbour (a 16x16 icon stays crisp), larger ones are reduced with an
  alpha-weighted box filter, and the result is composited over the row. The old
  renderer cropped anything that was not 32x32.
- **The library window remembers its size.** Dragging the window's edge writes
  `window = 900x700` to `client.conf` when the client exits; the next start
  opens there. `--client --run-frames` screenshots keep the same size.
- **A thirteenth settings row, 手柄.** Controllers are on by default; the row
  turns them off, and the value is saved as `gamepad = 0`.
- **Game controller support in the SDL2 port.** A d-pad, the left stick and the
  face buttons drive the twelve game keys, with hotplug and no keyboard-only
  blind spots: `A` fire, `B`/`X`/`Y` game A/B/C, `LB`/`RB` soft keys, `Back`
  clear, `Start` game D, d-pad and left stick directions. `NJ_SDL2_GAMEPAD=0`
  disables the subsystem entirely.

The matching rules are deliberately simple: ASCII is compared case-insensitively,
non-ASCII codepoints are compared byte-for-byte (so a Chinese title search is an
exact substring), and the filter is capped at 120 bytes on a UTF-8 boundary so a
pasted paragraph cannot grow the bar without bound.

## Why a fixed pad map and not more bindings

The keyboard page already owns the twelve game keys; letting a pad binding fight
a keyboard binding would make both pages harder to explain and to test, and a
gamepad's face buttons are physically labelled. The pad therefore maps to the
same `NJ_KEY_*` values once, in the port, and a player who dislikes it turns the
whole subsystem off in the settings page (`NJ_SDL2_GAMEPAD=0`). The port tracks
the pressed state per direction, so a held stick produces one `KEY_DOWN` and one
`KEY_UP` instead of a repeat storm; pressing both the d-pad and the stick toward
one direction releases only when both have let go, and removing the controller
releases every held key.

## Verification

- `./build/test_client_ui` - 436 checks, 0 failures (search, help sheet, scaled
  icons, thirteen settings rows).
- `./build/test_port_gamepad` - 45 checks, 0 failures (virtual controller:
  button map, hold without repeats, stick and deadzone, hotplug, removal,
  `NJ_SDL2_GAMEPAD=0`).
- `./build/test_client_config` - 83 checks, 0 failures (`gamepad` and `window`
  round-trip, malformed sizes ignored, missing file keeps the defaults).
- `ctest --test-dir build -j8` - 140/140, 53.58 s.
- `tools/check_no_statics.sh` - OK (75 object files).
- Headless screenshots: `.local-tools/debug-runs/polish-lead/frame-*.png`
  (search bar with `1942`, the `F1` sheet, the 手柄 row, the bottom of the
  thirteen rows).
- A saved `window = 900x700` opens a 900x700 launcher and survives the exit
  save; `check_sdl2_client.sh` pins that path.

## Limits

- The pad map is fixed, not rebindable (see above).
- Search matches name/file/class only, not descriptions a MIDlet does not
  declare; no fuzzy matching.
- Icons fill the 32x32 box without preserving aspect ratio.
- The caret in the search bar is `|` because the builtin bitmap font has no
  U+258F; the text itself is UTF-8 and Chinese input works.
- The `F1` sheet is a reference, not a key editor; editing stays in the settings
  and binds pages.
