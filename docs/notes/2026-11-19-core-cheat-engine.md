# Core cheat engine: one table for libretro, the CLI and Android (2026-11-19)

## Why

The 0.5.13 cheat feature lived in the libretro port
(`src/port/libretro/nj_port_libretro.c`), so only a frontend could use it. The
user asked to be able to cheat in general; moving the engine into the runtime
means the same codes work in the libretro core, the headless CLI and (next) the
Android build, and is the prerequisite for file-based cheats on a device.

## Design

- `src/core/nj_cheat.c` owns parsing, the per-VM table and the raw write:
  - `nj_vm_cheat_add(vm, code)` — parse one code and install it; returns the
    slot index (or a negative status). At most 32 codes, 16 bytes each.
  - `nj_vm_cheat_load(vm, path)` — one code per line, blank lines and `#`
    comments skipped, malformed lines warned and skipped. Returns the number of
    added codes, or `NJ_ERR_FORMAT` when the file cannot be opened.
  - `nj_vm_cheat_disable(vm, slot)` / `nj_vm_cheat_clear(vm)`.
  - `nj_cheat_apply(vm)` — bounds-checked copy of every enabled patch into the
    non-moving JVM arena obtained through `nj_vm_memory_region()`.
- `nj_midp_frame()` calls `nj_cheat_apply()` at every frame start, so a game
  that writes the old value back cannot cancel an enabled code.
- The libretro port keeps its API contract: `retro_cheat_set()` maps the
  frontend index to a core slot, `retro_cheat_reset()` clears the table, and
  `retro_run()` re-applies the table once more before the frame so a frontend
  observes `cheat_set()` on its next call even when the worker has not reached
  a frame boundary yet.
- The runner installs `--cheat`/`--cheat-file` codes before the MIDlet starts;
  the CLI help documents the format. The .info still advertises `cheats=true`
  and the libretro documentation keeps the arena-offset format section.

## Format

    <arena offset>:<hex bytes>

`@`/`0x` prefixes and `=` instead of `:` are accepted; the offset is
hexadecimal into the `RETRO_MEMORY_SYSTEM_RAM` window and the value is 1..16
hex bytes. Same format everywhere.

## Verification

- `tests/test_cheat.c`: 33 checks — valid/malformed parsing, slot reuse,
  disable/clear, apply/re-apply, out-of-range skip, file load with comments and
  a malformed line, missing file.
- CTest `test_cheat`, `check_libretro_cheat` (frontend-level end-to-end via the
  mock harness), `test_libretro_frontend` all green.
- CLI smoke: `--cheat-file /tmp/cheats.txt --cheat 30:11` on loopdemo logs
  `applied 3 cheat code(s), re-applied at every frame` and runs to completion.

## Limits

- No cheat database and no third-party code formats; codes are raw arena
  writes and can corrupt objects if the offset is not from a deliberate search.
- A state restore replaces the arena, so codes apply to whatever object
  currently occupies the offset.
- The Android APK has no cheat UI; instead it reads a text file next to the
  game: `<jar dir>/cheats/<stem>.txt`, then `<files dir>/cheats/<stem>.txt` and
  `<rms dir>/cheats/<stem>.txt` (same format, one code per line). Push the file
  with adb or a file manager on the app-specific storage; a missing file means
  no cheats. The lookup runs on the run thread before `nj_run()`.
