# Save-state buffer bound: object metadata scales with the arena

## Symptom

`MinecraftJ2ME_PE_0.6.1_V2.2.jar` fails its libretro save-state round trip on
the 0.5.8 release core:

```
serialize_size=140816448 (save states supported)
FAIL serialize_writes_state: retro_serialize(140816448) returned 0
```

The core prints no warning: `retro_serialize()` rejects the caller buffer
silently (`blob_size > size`), so a user sees "save state" do nothing.  The same
run passes on a RelWithDebInfo build of the same tree, which is why earlier
ad-hoc probes missed it - the faster release build has loaded and allocated
more by the time the frontend serializes.

## Root cause

`retro_serialize_size()` returns

```
state_bound = nj_vm_blob_max(vm) + envelope + 2 framebuffers
nj_vm_blob_max(vm) = heap_max + NJ_MAX_THREADS*NJ_MAX_STACK_SLOTS*8 + 4 MiB
```

The flat 4 MiB allowance is meant to cover the object metadata sections (a
2-byte class index per object plus one record per live ref slot).  That is not
a constant: it scales with how densely the arena is packed with small objects.

Minecraft fills its whole 128 MB auto heap (`heap.bump = 134,217,728`,
`used = 130,281,152`) and adds about 7.1 MB of object metadata, so the blob is
141,365,895 bytes against the 140,816,448-byte buffer the frontend allocated.
`nj_load_blob()` uses the same bound, so even a bigger frontend buffer would
have been refused as corrupt.

## Fix

`src/core/nj_save.c`:

- `NJ_SAVE_META_MAX(vm)` = 4 MiB + thread stacks + `heap_max/8` (class index
  per object + one record per ref slot, both bounded by arena density), capped
  at half the arena so an explicit huge heap cannot make the state buffer
  absurd;
- `NJ_SAVE_BLOB_MAX(vm)` = `heap_max + NJ_SAVE_META_MAX(vm)`;
- the writer now refuses with `NJ_ERR_STATE` if the blob would exceed that
  bound, instead of handing back a blob that neither the frontend buffer nor
  the loader accepts.

For a 128 MB auto heap the state size grows from 140,816,448 to 157,593,664
bytes (about +16.8 MiB of worst-case buffer).  Titles with normal heap sizes are
unaffected: with a 16 MB heap the bound is still dominated by the 4 MiB floor.

`tests/test_save.c` now mirrors the documented formula instead of hardcoding the
old one.

## Evidence

- Before: mock harness on the 0.5.8 release core -
  `FAIL serialize_writes_state: retro_serialize(140816448) returned 0`.
- After (`/var/tmp/nj2me-lrstate-build`):
  `PASS serialize_writes_state: retro_serialize(157593664) returned 1`,
  `PASS unserialize_restores_state: retro_unserialize(157593664) returned 1`,
  `[summary] checks=45 failed=0 skipped=0`.
- `tests/test_save` - 2640 checks, 0 failures.
- The 100-title libretro state probe that surfaced the failure was rerun with
  the fixed core; see the round-55 report for the distribution.

## Release note

0.5.8 ships the too-small bound (Minecraft-class titles cannot save state).
The fix is part of the next release.
