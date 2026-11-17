# nano_j2me 0.5.13

Released 2026-11-17. This release adds libretro cheat support and fixes the
mock-frontend key-script parser; the emulator core is otherwise the 0.5.12
multi-touch release.

## Highlights

- **Cheat codes in the libretro core.** `retro_cheat_set()` now accepts a
  core-specific form, `<arena offset>:<hex bytes>`, over the arena window that
  the core already exposes as `RETRO_MEMORY_SYSTEM_RAM` and publishes through
  `SET_MEMORY_MAPS`. The bytes are re-applied at the start of every
  `retro_run()`, so a game writing the old value back does not cancel the
  cheat; `retro_cheat_reset()` and disabling the entry stop the writes.
  `.info` advertises `cheats = "true"`. The offset comes from a frontend cheat
  search over the same window — no cheat database is shipped and no
  Game Genie/GameShark/Action Replay decoding is claimed. See
  [LIBRETRO.md](LIBRETRO.md#cheats) and
  [the design note](notes/2026-11-17-libretro-cheat-codes.md).
- **Mock-frontend key scripts parse pointer taps.** The libretro harness used
  to split its `--keys` file on every comma, so `60:pressed@40,50` lost its tap
  and curated pointer scripts silently ran only their joypad entries. This is
  fixed; with taps reaching the core the whole-corpus libretro probe reports
  385 titles -> 382 ok rows + 3 documented known limitations, 0 unexplained
  failures, 380 animated / 5 static (see
  [the classification note](notes/2026-11-17-static-title-classification.md)).

## Libretro cores (r32)

Eight targets built from the `0.5.13` tree. All eight are stripped and carry
the same `.info` (SHA-256
`cd52e1a0f260aeb818a51c1af7f09dcd62ab85db7595b0b88753a41f77131530`).

| target | file | bytes | SHA-256 |
|---|---|---|---|
| linux-x86_64 | `nano_j2me_libretro.so` | 3,843,912 | `7fad2162f36cd70539d66824b1ab2ae7e7079ec5fd32c95cc77f321714688cbb` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,327,432 | `8c9e6de40b3fb16bac159fb03686e129db8e7717375b8469165f55826436430d` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,035,396 | `96bd5368600d945ac976a641e8fc754de510fcf6b26d8403663c3cc518fcb9f1` |
| android-arm64 | `nano_j2me_libretro.so` | 3,561,992 | `b12112e0634e3b8fe9d1c3acb88834768e1ac21a96e195dc50f305d3705efa29` |
| android-armv7 | `nano_j2me_libretro.so` | 2,748,740 | `d12211b91ecf4f9dcb21b11079131bcd1eea5e1e9d1b36323bf1cebb9c5c4eef` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,602,224 | `b756a3aa26c8bffee9bb0b7764cd506adda6431984eb91ab5bcfb30723f8137b` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,456,000 | `969b34806418a88f7ba81523474a7f8d3d36df5e1c9d711c8eddff0fe051b66d` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,231,744 | `14049f2b9e85bcc099298caf07eb31bac5095b0bc1dd5e2111311fc744a246a4` |

Archive: `nj2me-0.5.13-libretro-cores.zip` (11,759,853 B, SHA-256
`a33afe194976d9bfadaf06123b64e1b143f91c1625ff9c6ec81ddf25f5b7db10`),
each target directory carries the core, `.info`, `LICENSE`, `BUILD_STATUS.txt`
and the docs/licence tree.

## Android APK

- `nj2me.apk` — versionCode 513, versionName `0.5.13`, four ABIs
  (arm64-v8a, armeabi-v7a, x86, x86_64), debug-signed, no game bundled
  (3,275,114 B, SHA-256
  `2a11894a7098ea9766c5b7b234374c20713b7dc51e06184e08ae23c6bbff04e7`).
- `nj2me-legacy.apk` — versionName `0.5.13-legacy`, API 5 / ARMv5
  (701,371 B, SHA-256
  `1dde7bc69c8b388b55a9bb69898b848e10df73894c6e41c4870bf713350b2436`).

The APK has no cheat UI: cheat codes are a libretro-core feature in this
release.

## Verification

- libretro mock-frontend matrix: **20 passed, 0 failed, 0 skipped**.
- Real RetroArch 1.22.2 LoopDemo 120 frames: PASS.
- Windows x86_64 `.dll` under Wine 300 frames: **4 passed, 0 failed**.
- New CTest gate `check_libretro_cheat`: PASS (apply, per-frame re-apply after
  a simulated game overwrite, disable, malformed code, reset).
- Submission bundle: **32 passed, 0 failed**.
- Full CTest at the release tree: **196/196 PASS**.

## Upgrade notes

- Save states carry the core version string in their CRC; states written by
  another version are refused (the core continues running). Re-save after
  updating.
- Cheats are re-applied once per `retro_run()`; a cheat enabled while a state
  is restored keeps applying to the arena of the restored VM (offset meaning
  may change if the object layout differs).
- The Android APK keeps the 0.5.12 feature set apart from the version string.

## Upstream submission

The two ready-to-apply patches (libretro-super `.info`, libretro/docs page)
were refreshed for this release and remain open for review:

- libretro/libretro-super#2204 — `info: add nano_j2me (Mobile - J2ME)`
- libretro/docs#1239 — `docs: add nano_j2me (Mobile - J2ME)`

No merge or acceptance is claimed.
