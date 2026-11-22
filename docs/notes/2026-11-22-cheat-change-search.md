# Cheat search: control-variable filtering and one-shot writes (2026-11-22)

The Android cheat panel could already search an exact value, narrow the
candidate list and write the result back as `<offset>:<hex bytes>` codes that
are re-applied every frame.  Two things were missing from the classic memory
scanner workflow, and the user asked for both:

1. **Unknown-value search**: record a baseline, let the game change the value,
   then keep the addresses whose value moved (and reject the ones that did not,
   or that moved the wrong way).
2. **Single write vs continuous write**: a code freezes the value every frame;
   sometimes the player wants to poke it once and let the game write back over
   it.

## Core API

`include/nj2me/nj_vm.h`, implemented in `src/core/nj_cheat.c`:

```c
nj_u4 nj_vm_cheat_snapshot(nj_vm *vm, nj_u1 *buf, nj_u4 cap);
nj_u4 nj_vm_cheat_scan_changed(nj_vm *vm, const nj_u1 *prev, nj_u4 prev_len,
                               nj_u2 width, int mode,
                               nj_u4 *out_offsets, nj_u4 max);
nj_u4 nj_vm_cheat_filter_changed(nj_vm *vm, const nj_u4 *in, nj_u4 count,
                                 const nj_u1 *prev, nj_u4 prev_len,
                                 nj_u2 width, int mode,
                                 nj_u4 *out_offsets, nj_u4 max);
int   nj_vm_cheat_write(nj_vm *vm, nj_u4 offset, const nj_u1 *bytes, nj_u2 len);
```

`mode` is one of `NJ_CHEAT_CHANGED` (0), `NJ_CHEAT_UNCHANGED` (1),
`NJ_CHEAT_INCREASED` (2), `NJ_CHEAT_DECREASED` (3).  Comparing is little-endian
unsigned over `width` (1, 2 or 4) bytes; wrapping around 0 is treated as a plain
"changed" for the increase/decrease modes.  `nj_vm_cheat_snapshot(vm, NULL, 0)`
returns the arena size, which is what the JNI layer uses to size its baseline
copy.  `tests/test_cheat.c` covers the four relations, the cap, out-of-range
offsets and the one-shot write (the suite is now 70 checks).

## Android plumbing

`src/port/android/nj_port_android.c` gained request modes 3–6, served at the
same frame boundary as the existing search modes:

* 3 — snapshot the arena into `cheat_snapshot` (`cheats: snapshot N byte(s)`)
* 4 — full change scan against the baseline (`cheats: change scan filter=…`)
* 5 — filter the current candidate list against the baseline
* 6 — one-shot write (`cheats: single write width=… -> ok/refused`)

JNI: `nativeCheatSnapshot`, `nativeCheatChanged`, `nativeCheatFilterChanged`,
`nativeCheatWriteOnce`.  The Java side keeps `cheatBaselineSet`; after a
successful filter it rebases the baseline (`nativeCheatSnapshot`) so repeated
passes keep narrowing, exactly like a physical "changed/unchanged" scanner.

## UI

Panel → **搜索作弊数值** → **变化搜索** opens:

```
变化搜索（控制变量法）
1 字节 / 2 字节 / 4 字节
记录基准（重设）
变了 / 没变 / 变大 / 变小
```

Tapping a candidate now asks **持续修改** (write a code, re-applied every
frame) or **单次修改** (one write; the game can overwrite it).

## Verification (AVD API 36, x86_64)

```
I nj2me : cheats: snapshot 125829120 byte(s)
I nj2me : cheats: change scan filter=0 width=4 -> 73 hit(s)
I nj2me : cheats: snapshot 125829120 byte(s)
I nj2me : cheats: read value(s) width=4 count=73
I nj2me : cheats: single write width=4 -> ok
```

The candidate dialog listed the hits with live values (`0xBB0 = 11397
(0x2C85)`, …), and the width/relation buttons narrowed as expected.

## Limits

* The baseline is a full copy of the arena, so recording it costs one arena of
  native memory (the JNI layer refuses cleanly when the allocation fails).  On
  a phone where the automatic heap picked a very large arena prefer narrowing
  the search with a known value first, or lower the game's heap.
* A change scan over the whole arena is noisy by design; the point of the
  workflow is the narrowing steps.
* Candidate lists keep the first 512 hits and display the first 30.
