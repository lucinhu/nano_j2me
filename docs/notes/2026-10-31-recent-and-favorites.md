# The library remembers what ran and what was starred

## What changed

Three answers to questions the library window could not answer: "which one was
that game", "where is the one I keep coming back to", and "what is this window".

- **最近玩过.** The library list opens with a dim `最近玩过` header and the last
  ten jars that ran, newest first, then a `全部游戏` header and the whole
  directory. The entries come from `recent = <path>` lines in `client.conf`,
  resolved back through `nj_library_find_path()`; a jar that moved or was
  deleted simply drops out, and when nothing resolves there are no headers at
  all and the list is exactly the flat list it used to be. Header rows are not
  selectable - arrows, page keys, Home/End, the wheel and clicks skip them - and
  the headers scroll with the content instead of sticking.
- **Favorites.** `F2` stars the selected row (and unstars it again); a star
  `★` is drawn in its own 18 px column, in amber, so names stay aligned whether
  or not the row is starred. The star is saved as a `favorite = <path>` line and
  comes back on the next start. Toasts say `已收藏「name」` / `已取消收藏「name」`.
- **Every run is remembered.** Picking a game, dropping a jar on the window, a
  jar dropped on a running game that becomes the next run, and `--client <jar>`
  all record the path through `nj_client_ui_record_run()`: newest first, one
  entry per path, capped at ten. `take()` calls it for the library's own RUN;
  the SDL loop calls it for the jars it starts itself.
- **The game window names the game.** It used to be one more `nano_j2me` in the
  task bar; it is now `nano_j2me - <display name>` from the manifest (the file
  name when the manifest has none).
- **The settings page's 恢复默认 keeps user state.** Recent jars and stars are
  not preferences, so the reset puts the switches, binds and window size back
  and leaves both lists alone.

No new file format was invented for this: the file stays the same
`key = value` file, with two new repeated keys:

```ini
recent = /home/you/games/1942空战_10403.jar
recent = /home/you/games/AlphaDemo.jar
favorite = /home/you/games/AlphaDemo.jar
```

## Why a list in client.conf and not a games database

The client already has exactly one durable file, and it is a few hundred bytes
that the settings page rewrites on every change. Ten paths and a handful of
stars fit there without a second format, a second writer or a schema: the loader
appends repeated keys (deduplicating, dropping non-absolute lines), the saver
writes the arrays in order, and an older client that does not know the keys
ignores them. A recent entry is a path, not metadata, because the library
already owns the name, icon and validity of every jar; the section is a view of
the same entries, so a moved directory means the recent row disappears instead
of pointing at something stale. Paths are compared as exact strings - the same
rule the library uses for duplicates - and favorites are kept in the order they
were starred rather than sorted, so a star never reshuffles the library
underfoot.

The per-launch cap (10) and the favorite cap (32) keep `nj_client_settings` a
fixed, zero-initialised struct: nothing is allocated, freed or leaked, and the
whole thing is still one `memcpy` away from its defaults.

## Verification

- `tests/test_client_ui.c`: 544 checks (was 436), including section presence and
  absence, header skipping for keys/wheel/clicks, recent order and cap, a stale
  path being skipped, star hit geometry and `nj_client_ui_selected_favorite()`,
  `F2` toggling in both directions, `take()`/`record_run()` recording, filter
  hiding the sections, reset preserving the lists, and two 30-row scroll walks
  over the sectioned list.
- `tests/test_library.c`: 390 checks (was 252), including `nj_library_find_path()`
  hits, misses, lookups during a progressive scan, and misses after clear.
- `tests/test_client_config.c`: 121 checks (was 83), including recent/favorite
  order, deduplication, relative-path rejection, caps and the empty-list file.
- Full `ctest`: 140/140 in 53.9 s; `tools/check_no_statics.sh` clean (the reset
  snapshot moved to the stack after the first run flagged two writable globals).
- `tools/check_sdl2_client.sh` now also proves that `--client <jar>` writes the
  jar as `recent = ...` and that a second launch keeps one line, not two.

## Limits

- A recent or favorite path must be a path the library scan produced; a jar
  launched from a relative command line (`nj2me game.jar` from another
  directory) is recorded as typed and will not resolve into the section.
- Favorites are shown with a star, not as a separate section, and are not
  sorted - the library order stays the library order.
- The `★` column is reserved on every row, so names start at the same x even in
  a library with no favorites at all.
- The section exists only in the unfiltered library; typing a search collapses
  to the flat, filtered list (stars still show).
- The same jar intentionally appears twice when it is both recent and in the
  full list; both rows run it.
- Favorites past 32 drop the oldest starred; recent past 10 drop the oldest run.
