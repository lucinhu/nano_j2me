# nano_j2me 0.5.16

**Released 2026-11-19.** This release ships the second half of the cheat
support: the Android in-game panel can now **search the Java arena for a value
the player can see**, narrow the candidate list with a new value and turn the
one remaining offset into a cheat code — no external frontend needed. The
libretro core is feature-identical to 0.5.15 (r35 repackages the same core plus
the new read-only search helpers; nothing in the `.info` or frontend contract
changed).

## Highlights

- **In-game cheat value search (Android).** The panel has a new
  **搜索作弊数值** row:
  1. enter the number the game shows (decimal, or `0x…`) and its width
     (1/2/4 bytes) — the panel asks the run thread to scan the arena between
     frames;
  2. the candidate dialog lists the matching arena offsets; **缩小范围** asks
     for a new value and filters the existing list;
  3. tapping an offset appends `<arena offset>:<little-endian bytes>` to the
     game's cheat file (`<jar dir>/cheats/<stem>.txt`) and reloads it, so the
     patch is live immediately and is picked up on the next launch too.

  The scan itself is the round-96 runtime API
  (`nj_vm_cheat_find/filter/read()` in `src/core/nj_cheat.c`); the arena is
  only read, on the run thread, through the same frame-boundary handshake as
  the save state and cheat reload. Up to 256 candidates are kept and the first
  30 are listed.
- The cheat engine from 0.5.14/0.5.15 is unchanged: `<offset>:<hex bytes>`
  codes shared by the libretro core (frontend cheat search supplies offsets),
  the headless CLI (`--cheat`, `--cheat-file`) and the Android panel editor.
- Everything else from 0.5.15 is unchanged (multi-touch, memory-window/memory
  map export, rewind sizing, the save-state fixes).

## Artifacts

### libretro cores (r35)

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3,848,064 | `3c993241be743fc7a82395cfd7f4dd420cda43a09e21029ba641a80b9d8a3305` |
| linux-aarch64 | 3,331,296 | `8844bc88e15213cd218ac7b1e88978949bf5a545938a09ee2cd03762fcab586a` |
| linux-armv7 | 3,038,512 | `b782c0d16dd89f2e9fce17b7f29ae8160786f5e8380436dc54ac3b42030e8324` |
| android-arm64 | 3,565,904 | `424e41db70cf6aaddd794889a39acd8dcbc2759d5afc47b1b3907e2a37262b07` |
| android-armv7 | 2,751,136 | `a234b145abfe857038451d90b39874c47d8ae67fae2bf2daeca7eb5acc2465fe` |
| android-x86_64 | 3,606,040 | `df12b5e210b7cc0d07f878e5be1a510e87599030708a695d4962c19d1cf573da` |
| windows-i686 | 3,459,584 | `119d1f4021c4097c14f37e50a0f301377b1bbe455763d84482379194626eaa3f` |
| windows-x86_64 | 3,235,328 | `1e0a8479e19acf46bbd2f7c30aff6146962c3314433553c6e6cb2c7d877fe701` |

Metadata `.info` SHA-256:
`e372f211ed6c26e55bd23160e14d5e55422a54452209a7ed65032bf55c3c57da`
(`display_version = "0.5.16"`).

Package: `nj2me-0.5.16-libretro-cores.zip`, 11,346,011 bytes, SHA-256
`bfeb3738a9b45251ea4a5726e275c6ed3c5de9bfddee863a56a3ea2e26a4cb3e`.

### Android APKs

| file | version | bytes | SHA-256 |
| --- | --- | ---: | --- |
| `nj2me.apk` | 0.5.16 (`versionCode 516`), 4 ABIs, minSdk 21 | 3,295,594 | `763ed720e7c3650a04be2303e7b48e6418842c7a27bee6e81178e0e607d3f931` |
| `nj2me-legacy.apk` | 0.5.16-legacy, minSdk 5 | 703,979 | `c594b9e6241144bc7b0a1747a20e76ce12255b1083725cbebe4f53fd0efd4fad` |

## Verification

- Mock libretro frontend conformance matrix: **20 passed, 0 failed,
  0 skipped**.
- Real RetroArch 1.22.2 (Linux, headless): LoopDemo 120 frames **PASS** with
  the core log markers.
- Windows x86_64 core under Wine: **4 passed, 0 failed** at 300 frames
  (LoopDemo with save-state checks, AudioDemo with nonzero audio).
- Submission bundle check: **32 passed, 0 failed**.
- Full serial CTest on the bumped tree: **197/197 passed in 347.93 s**.
- Android: the value search was exercised on the API 36 emulator with
  `tests/fixtures/loopdemo.jar`: `cheats: search find value=0x1/1 in=0 -> 9
  hit(s)`, narrow `in=9 -> 9`, tapping `0x1F0` wrote `1F0:01`
  (`cheats: reload … -> 1 code(s)`), and a restart logged the startup load and
  `applied 1 cheat code(s), re-applied at every frame`. `check_android_java`
  33 checks passed; the legacy APK compiles.

## Upgrade notes

- Save states written by 0.5.8–0.5.15 are refused by the version CRC in the
  state envelope, as before; states are not portable across releases.
- Cheat files (`<jar dir>/cheats/<stem>.txt`) keep working; arena offsets are
  raw arena positions, so a code that was found for one game build may not
  point at the same object after the game changes its allocation pattern. The
  in-game search is the supported way to rediscover an offset.
- The libretro core's `.info` display version moved to 0.5.16; the metadata
  otherwise matches 0.5.15 (same options, features and cheat format).

## Upstream submission

- `libretro/libretro-super#2204` — adds
  `dist/info/nano_j2me_libretro.info` (0.5.16). OPEN / mergeable.
- `libretro/docs#1239` — adds the library page
  `docs/library/nano_j2me.md`. OPEN / mergeable.

Both are awaiting review; no merge or acceptance is claimed.
