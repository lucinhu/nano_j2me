# A desktop client, drawn by the emulator itself

## What changed

The SDL2 build of `nj2me` is now a desktop client, not only a game window. Run
it with no `.jar` and it opens a library: every runnable suite in the games
directory gets a row with its icon, its own MIDlet name, the file name and its
size. Enter or a click runs the selection, `O` opens a file browser (`Enter`
runs, `Backspace` goes up, `Esc` cancels), `R` rescans, `S` opens settings,
`Esc`/`Q` quits. Dropping a `.jar` on the window runs it; dropping a directory
makes it the library and rescans; an unreadable drop is a toast, not a crash.
Closing a game window - or `F10` in the game - returns to the library, and the
game window's usual close button still exits the whole client when no library
is involved.

The command line grew three options and no surprises: `--client` forces the
client, `--client game.jar` runs that game first and falls back to the library,
`--games <dir>` (or `NJ2ME_GAMES`) points the library at a directory, and
`--print-library` prints what the library sees - useful when a title does not
appear in it. A jar given without `--client` behaves exactly as before. `--run-frames N`
in client mode runs N frames of the first game and then exits the whole client,
which is the seam the end-to-end test uses.

Three new pieces carry it:

| File | What it does |
|---|---|
| `src/client/nj_library.c` | scans a directory, probes each jar (readable ZIP, manifest, non-empty `MIDlet-1` class), reads the name/icon the Android launcher reads, decodes icons lazily |
| `src/client/nj_client_ui.c` | the whole launcher: library/browser/settings modes, selection, scrolling, toast, hit-testing, drawing; it never touches SDL - it fills an `nj_surface` and nothing else |
| `src/port/posix/nj_sdl2_client.c` | the window: one ARGB8888 streaming texture, the event pump (keys, mouse, wheel, `SDL_DROPFILE`), a 30 Hz launcher loop, and the game loop that calls `nj_run` and comes back |
| `src/client/nj_client_config.c` | `<XDG_CONFIG_HOME|~/.config>/nano_j2me/client.conf`: games dir, last browser dir, scale, on-screen keys, fps cap |

`nj_port_sdl2` also learned two things. `SDL_DROPFILE` of a runnable jar ends
the current run the same way the window close button does (`NJ_EV_QUIT`), and
`F10` does the same; a rejected drop leaves a running game untouched. The
pending path is handed over by `nj_port_sdl2_take_dropped()`, which the direct
command-line loop and the client both poll after `nj_run` returns, so a jar
dropped mid-game starts next instead of being lost.

`data/nj2me.desktop` and `data/nj2me.png` (256×256, drawn with Pillow) are
installed for application menus when the CLI target is built.

## Why draw the launcher with the runtime's rasteriser

The obvious implementation is a toolkit - FreeJ2ME's launcher is a Swing
window, and GTK/Qt/NFD would each do the job. That would break the project's
first design commitment (plain C99, no dependencies) for the sake of a list of
filenames, and it would add a second text stack next to the one already
carrying CJK: the built-in bitmap faces already render Chinese, which most of
the corpus filenames need.

So the launcher is drawn with `nj_gfx` into an ARGB8888 surface and handed to
the window as one streaming texture per frame. The 12×16 and 12×20 cells are
the ones the games use, icons are decoded with the same `nj_image_load`, and
the model is a plain C object that a unit test can drive headless: no window,
no display server, no toolkit. The layout adapts to window resizes, and the
whole launcher - model, drawing and event mapping - is a few thousand lines
that the existing test harness can exercise deterministically.

Unifying "window closed" and "the client wants the game to stop" on
`NJ_EV_QUIT` is deliberate too: that path already stops a MIDlet cleanly through
`nj_vm_stop_reason`, is what `SDL_QUIT` did, and gives `nj_run` its exit code
without any new teardown code. The runner still does not own the window
system - the client decides whether a finished run means "show the library
again" or "exit".

## Verification

```
$ build/nj2me --print-library --games tests/games | tail -1
400 games, 399 runnable
```

`tests/test_library.c` covers 252 checks over real fixtures - the Chinese-named
corpus jars, the three deliberate broken archives, temp directories for scan
order and duplicate handling - and `tests/test_client_ui.c` 191 checks driving
the model through every mode, a resize, drops, and `row_rect` hit-testing.
`tests/test_client_config.c` (36 checks) covers defaults, XDG paths, UTF-8
round-trip and hostile files; `tests/test_port_drop.c` (20 checks) covers the
port side under the SDL dummy driver: refused `.txt`, accepted `.jar`, short
`take_dropped` buffer, `F10`, `SDL_QUIT`, and re-entry after shutdown.
`tools/check_sdl2_client.sh` is the end-to-end test: it captures a launcher
frame headlessly, asserts `--print-library` output, checks that a broken jar is
reported `bad` rather than runnable, and runs `--client game.jar --run-frames 4`.

Full suite: `ctest --test-dir build -j6` is 137/137. The launcher was also
rendered on the real display (`DISPLAY=:1`), which wrote the same frame:
`.local-tools/debug-runs/client-shot/real.ppm`.

Screenshots are captured without a display or by the client itself:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  NJ2ME_CLIENT_FRAMES=40 NJ2ME_CLIENT_SCREENSHOT=/tmp/launcher.ppm \
  build/nj2me --client --games tests/games
```

`docs/images/readme-desktop-client.png` is that frame; the settings, browser and
toast states were rendered by the model test and the throwaway harness used
while developing it.

Two deliberate limits: icons are blitted 1:1 - the rasteriser has no scaled
blit - so an icon larger than the 32x32 tile is centre-cropped instead of
shrunk; and in a window too narrow for the keyboard hints the hint row is
replaced by the three clickable buttons instead of clipping mid-glyph.

The settings page described here was later widened from five rows to twelve,
and the twelve game keys became rebindable; that round is
`docs/notes/2026-10-31-key-mapping-and-settings.md`.
