# libretro cheat codes (round 81)

2026-11-17

The core used to export `retro_cheat_set()`/`retro_cheat_reset()` as no-ops
and advertised `cheats = "false"`: a libretro cheat code is normally an
address/value pair over a flat address space, and the J2ME runtime has no flat
address space (values live inside managed Java objects). The arena window
(`RETRO_MEMORY_SYSTEM_RAM` + `SET_MEMORY_MAPS`) already let a frontend cheat
search scan and patch memory, but a saved cheat entry had no way to work.

## What was added

`retro_cheat_set(index, enabled, code)` now accepts one core-specific code
form:

```
<arena offset>:<hex bytes>
```

- offset: hexadecimal byte offset into the SYSTEM_RAM window (`@` prefix and
  `=` separator are also accepted);
- bytes: 1–16 hex bytes written at that offset;
- the bytes are re-applied at the start of every `retro_run()`, so a game
  writing the original value back does not cancel the cheat;
- `enabled = false` clears the slot, `retro_cheat_reset()` clears every slot;
- malformed codes are ignored with a `[nano_j2me] ignoring cheat` warning.

`.info` now advertises `cheats = "true"` (both tracked copies, the super patch
and the docs page are in sync).

## Why offset-based

A cheat search in RetroArch (or any frontend using the memory map) reports the
matched byte as an offset inside the exposed SYSTEM_RAM descriptor. Feeding
that offset back as a cheat is the only stable way to address the arena: heap
object addresses change every time the JVM allocates, while the arena base is
stable for the VM's lifetime. The window is raw and unsynchronised, exactly as
documented under "Exposed memory" — a wrong offset can corrupt an object, so
cheats are a deliberate local convenience feature, not a save channel and not
a shipped cheat database.

## Verification

New harness mode `--cheat-check` and CTest gate `check_libretro_cheat`
(`tests/test_libretro_frontend.c` `check_cheat()`) run the API end to end with
`loopdemo.jar`:

1. `retro_cheat_set(0, true, "<arena size - 32>:A5")` → the byte reads `0xA5`
   after one frame;
2. the harness overwrites the byte with `0x00`, runs one more frame → the byte
   is `0xA5` again (per-frame re-apply);
3. `retro_cheat_set(0, false, NULL)` → the byte stays whatever the harness
   writes;
4. a malformed code (`not-a-cheat`) does not patch memory;
5. `retro_cheat_reset()` stops writes.

All five checks pass (`check_libretro_cheat`, ~4 s). CTest count is 196.

## Not included

- No cheat database and no Game Genie/GameShark/AR decoding (impossible over a
  managed heap).
- The Android APK has no cheat UI; cheats are a libretro-core feature for now.
- 8-bit/16-bit multi-byte writes are supported by giving 2/4/8/16 hex bytes,
  but the offset must be the exact object offset from a search.
