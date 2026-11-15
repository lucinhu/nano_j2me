# nano_j2me 0.5.9 — save-state buffer bound fix (patch release)

0.5.9 is a small patch release over 0.5.8. It fixes a libretro save-state bug
where titles that fill the VM heap could not save at all, and it hardens the
media fuzzing infrastructure.

## Highlights

### libretro core

- **Save states work for object-dense titles.** The state buffer size used a
  flat 4 MiB allowance for the per-object metadata sections (a 2-byte class
  index per live object plus one record per live ref slot). Those sections grow
  with arena density: `MinecraftJ2ME_PE_0.6.1_V2.2.jar` fills its 128 MB auto
  heap and adds ~7 MB of metadata, producing a 141,365,895-byte blob against
  the 140,816,448-byte frontend buffer, so `retro_serialize()` silently
  failed. The bound now allocates `4 MiB + thread stacks + heap/8` (capped at
  half the arena) and the writer refuses with `NJ_ERR_STATE` instead of
  handing back a blob that neither the frontend buffer nor the loader accepts.
  With a 128 MB auto heap the state buffer becomes 157,593,664 bytes.
  Details: `docs/notes/2026-11-16-state-buffer-bound.md`.
- The synchronized-callback monitor fix from 0.5.8 and all earlier r26 content
  are included unchanged.

### Test and validation infrastructure

- `tools/fuzz_media.py` can now damage several media entries in one archive
  (`multi` mode) and does so in a new CTest gate (`fuzz_media_multi`). A
  3000-case multi-target campaign was clean (0 crashes, 0 hangs).
- `tests/test_save.c` mirrors the documented state-buffer formula instead of
  hardcoding the old constant.

## Artifacts

Built from clean tree `2efd2c7` with
`tools/build_libretro_cores.sh --jobs 12 --out /var/tmp/nj2me-submission-dist-r27 all`
(eight targets, 16 files each: core, `.info`, LICENSE, BUILD_STATUS.txt and the
licence/notice docs). The `.info` is identical in all eight targets,
SHA-256 `8cd968c9bcab94c798b62e6ac3467d5f1d0c1117a10b8fd6113347bc45c33c97`,
`display_version = "0.5.9"`.

| Target | Core size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,843,912 | `3e167228644c68bc64293fb86db371f9b096f256fc4f32c7ece4fc9b6fe12d70` |
| linux-aarch64 | 3,324,712 | `daa219235b2aa1c86be2d75c1a2c496dc969278b0b25ab366ffa3a7c7a961bdb` |
| linux-armv7 | 3,032,580 | `c44a0437a17d7a3b02b61034cffa10cfb51eaa4a656f9aea60a180f6ff1e34f8` |
| android-arm64 | 3,557,496 | `7852d996a4051d2e6fe6ef16e8105f2068a7547dd9e7039858403aff0d9ebae1` |
| android-armv7 | 2,745,172 | `d6355503a67c703808c2f50bf1551e0600c26fb0d67196678ffbf3d49aa55e33` |
| android-x86_64 | 3,597,648 | `3ee448916d7d5efafb25af3d076d54706aa2950e070848abf2665518874bfcb9` |
| windows-i686 | 3,452,416 | `4122ecbece527bdf1a07ee288eca33dbf1ec8f58b7d0627fa1f3d897f6fa88e4` |
| windows-x86_64 | 3,227,648 | `95abe1323ce7d3f5f3c1ad2b755e0ee5eb4d1f6d8be3d68dd6be6ec7f9f9685b` |

The downloadable archive is `nj2me-0.5.9-libretro-cores.zip` with
`SHA256SUMS-libretro-cores.txt` next to it.

## Verification

- Mock libretro frontend conformance matrix: **20 passed, 0 failed, 0 skipped**.
- `MinecraftJ2ME_PE_0.6.1_V2.2.jar` save-state round trip on the r27 core:
  `serialize_size=157,593,664`, 45/45 checks (write, restore, reset, refusal
  paths) passed.
- Real RetroArch 1.22.2, `tests/fixtures/alphademo.jar`, 300 frames: PASS.
- Windows x86_64 core under Wine: 4/4 fixtures PASS.
- Full serial CTest before packaging: **190/190 passed in 357.09 s**.
- 60-title libretro state probe on the fixed core: 60/60 round trips.

## Known limitations

- A save taken while an unthrottled/forward-warping frontend runs (frames far
  faster than real time, before the MIDlet construction completes) can restore
  into an invalid VM; 60 Hz frontends are unaffected. See
  `docs/notes/2026-11-15-libretro-async-load-early-state.md`.
- `毁灭战士3_10598.jar` is a very slow loader (loading animation rendered in a
  software M3G loop before `retro_load_game()` can return); give frontends a
  long load timeout.
- Auto-heap states are large: the state buffer now follows the arena
  (`heap + 4 MiB + thread stacks + heap/8`), about 157 MB with the default
  128 MB auto heap. Use `nano_j2me_heap=16`/`32` for smaller states.
- Rewind works only when the frontend buffer fits the state (see
  `docs/LIBRETRO.md`).
- Known-issue list otherwise unchanged from 0.5.8.

## Upgrade notes

- The state envelope carries a version CRC, so states written by 0.5.8 are
  refused by 0.5.9 (and vice versa) — expected for an engine version change.
- No configuration changes are required.
