# Cheat search: finding the value you want to patch

The cheat engine (0.5.14) can apply codes, and the Android panel (0.5.15) can
edit the code file from inside a game.  Both ask the player for an arena
offset, which is easy to get from a frontend cheat search and impossible to
guess by hand.  Round 96 adds the engine side of that search, so a future
in-app UI can do "first search / narrow / generate code" without a new arena
access path.

## API

Declared in `include/nj2me/nj_vm.h`, implemented in `src/core/nj_cheat.c`:

```c
nj_u4 nj_vm_cheat_find(nj_vm *vm, const nj_u1 *pattern, nj_u2 plen,
                       nj_u2 align, nj_u4 *offsets, nj_u4 max);
nj_u4 nj_vm_cheat_filter(nj_vm *vm, const nj_u4 *in_offsets, nj_u4 count,
                         const nj_u1 *pattern, nj_u2 plen,
                         nj_u4 *out_offsets);
int   nj_vm_cheat_read(nj_vm *vm, nj_u4 offset, nj_u1 *buf, nj_u2 len);
```

* `nj_vm_cheat_find` scans the whole arena for a byte pattern of 1..16 bytes
  and stores up to `max` absolute offsets.  `align` filters by offset
  alignment: 0/1 keep every hit, 4 keeps only 4-byte-aligned hits, which is
  what a frontend wants when the player searches an int.  The return value is
  the number of offsets stored; when it equals `max` the scan stopped early,
  so more hits may exist.
* `nj_vm_cheat_filter` narrows a candidate list: it keeps only the offsets
  whose bytes still match the new value.  `out_offsets` may be the same array
  as `in_offsets`, so the UI can filter in place.
* `nj_vm_cheat_read` copies arena bytes, for showing the current value of a
  candidate.  Returns 0 when the range is not fully inside the arena.

Offsets are the same absolute arena offsets a cheat code names, so a search
hit converts to a code by formatting `<offset>:<hex bytes>` and reloading the
file; no translation table is needed.

## Semantics and limits

* The arena is only read.  No Java object is written, so a wrong candidate
  costs a slot in the UI list, nothing else.
* A search is a snapshot: the caller should run it between frames (the Android
  UI will do it from the input poll / frame boundary) so the values are
  consistent.  Nothing is paused.
* Patterns are limited to 16 bytes (`NJ_CHEAT_MAX_BYTES`), matching the value
  size of a cheat code, and the engine keeps at most `max` hits per search.
* `align` is a filter, not a promise: the same 4-byte value can appear at
  unaligned offsets; asking for alignment 4 is a heuristic for "this is an
  int", not a proof.

## Tests

`tests/test_cheat.c` gained `test_search()`, which pokes known patterns into
the free tail of a real 64 KiB arena and checks: three copies found in
ascending order, alignment filtering drops the deliberately unaligned hit,
`max` truncation, a pattern ending exactly at the last byte is still found,
filter keeps only the rewritten copy using an in-place buffer, reads succeed
on candidates and refuse out-of-range offsets, and malformed arguments (NULL
buffers, empty or over-long pattern, zero max) return 0 instead of crashing.
`test_cheat` is now 49 checks (was 33).

## Next

* Android in-app UI: a "搜索数值" row in the in-game panel (first search with
  the current value, "缩小范围" with the new value, then write the surviving
  offset as a code line and reload through the existing `nativeCheatReload`).
* Expose the search over JNI (`nativeCheatSearch`) from the Android port.
* libretro frontends already have their own search over `SYSTEM_RAM` and the
  memory map, so the libretro port needs no new export.

## Android panel (round 97)

The in-game panel has a **搜索作弊数值** row.  It drives the API from the run
thread through `android_input_poll()` (the same frame-boundary handshake as the
save state and cheat reload), exposed to Java as
`GameActivity.nativeCheatSearch(handle, value, width, out)` and
`GameActivity.nativeCheatNarrow(handle, candidates, count, value, width, out)`:

1. "搜索作弊数值" asks for the number the player can see (decimal or `0x…`)
   and the value width (1/2/4 bytes).  The panel sends the value as a
   little-endian pattern and gets back up to 256 arena offsets.
2. The candidate dialog lists the offsets.  **缩小范围** asks for the new value
   (width is fixed) and calls `nativeCheatNarrow`; **重新搜索** starts over.
3. Tapping an offset appends `<offset>:<little-endian bytes>` to the game's
   cheat file (`<jar dir>/cheats/<stem>.txt`) and reloads it through
   `nativeCheatReload`, so the patch is live immediately and is picked up by
   the next launch too.

Verified on the API 36 emulator with `tests/fixtures/loopdemo.jar`:
`cheats: search find value=0x1/1 in=0 -> 9 hit(s)`; narrowing the same value
reported `in=9 -> 9`; tapping `0x1F0` wrote `1F0:01`
(`cheat file …:1: slot 0, 1F0:01`, `cheats: reload … -> 1 code(s)`), and a
restart logged the startup `cheats: …loopdemo.txt` + `applied 1 cheat code(s)`.
The panel row then showed "已启用 loopdemo.txt".

Limits: the value width must match between the first search and narrowing (the
panel keeps it); the candidate list is capped at 256 offsets and shows the
first 30; a value that is not in the arena yet (for example a counter the game
only creates after a new game) simply yields zero hits.
