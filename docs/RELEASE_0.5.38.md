# nano_j2me 0.5.38

This release fixes the core-only black screen that affected the Midnight Pool
family: the first load never anchored the virtual load clock, so the libretro
core's `System.currentTimeMillis()` reported the host uptime, the VM clock
folded a 2^32 epoch, and a single scheduler frame pass could take ~9 seconds.
The titles rendered in the CLI but stayed black and refused save states
through the core.

## Highlights

- **Load clock anchored on the first load.** `lr_load()` now sets
  `load_ms = 0` and `load_start_us = lr_real_us()` as soon as it enters
  `LR_PHASE_LOAD`; before this, only `lr_unload()` set those fields, so the
  first load's LOAD-phase clock returned the host uptime (about 146,331,262 ms
  on the test machine, i.e. ~40.6 h).  `nj_now_ms()` treats a 32-bit port
  clock that moves backwards as a wrap and adds `0x100000000` to its epoch, so
  that reading pushed the VM clock forward by an epoch; the scheduler's frame
  budget, wake deadlines and `port_sleep()` deltas all run on that clock and
  one frame pass took seconds.
- **Midnight Pool family fixed through the core.** `MidnightPool1` previously
  reported 0 non-black pixels of 76,800, one distinct frame and
  `FAIL serialize_writes_state`; with r57 it renders up to 8,348 non-black
  pixels, 308/317 sampled frames with content and 85 distinct frames, and
  passes the whole 45-check harness including save/restore
  (`serialize_size=157,593,664`).  `Midnight Pool2`, `Midnight Pool` and
  `MidnightPool_K700_EN` pass as well.  `Asphalt4EliteRacing` now renders but
  still refuses the serialize check (another long-first-frame case), and
  TheMummy/Skater remain the documented open items.
- **Whole-corpus probe.** 1,664 titles through the r57 core: **1,658 passed,
  1 known limitation** (`毁灭战士3`, constructor-time loading screen), **5
  failed** (`TheMummy_K700F`, `PAC-MAN (176x220)`, `assault`,
  `Skater_by_BFLM`, `Asphalt4EliteRacing`).  The r56 probe had nine failures;
  the four Midnight Pool variants now pass and there are no new failures.

## Core packages (r57, build revision `84875d6`)

| target | size (bytes) | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,048,832 | `60e5409e9badcfc915f26cc9d5358af8ab878c055edb15bdd5a550264035135d` |
| linux-aarch64 | 3,522,824 | `8f00303eee1203c8645b25dfe45c6204be97a6a4443e9563b8fb0ac0ccc93d65` |
| linux-armv7 | 3,175,576 | `a6a4655b7a26361a90a0691212dbf1f75adb055691cc9fa455ca9b4a08eabe1e` |
| android-arm64 | 3,768,456 | `13059bc22389718c1ca671d69c1005247b3cb7ec0705c4c9b3a85b2460bfd5aa` |
| android-armv7 | 2,864,716 | `0eb4d85ff2e1348fff077ab98d5b27b1a8fcbfbb9e623401a737c7f06899b48a` |
| android-x86_64 | 3,794,480 | `fc57ec88c6e5fffc6511bc22dad52b496917e703b51df233a4a39003ca0aca63` |
| windows-i686 | 3,600,896 | `35f27b16a851de185d01e8c994ad9a53460587a24b1b59efc13088ab9dd75692` |
| windows-x86_64 | 3,371,008 | `e36dda09e22a0ce5cd161cd060098d2d39abe3c082265cab915fe8e3be8eae7e` |

Archive `nj2me-0.5.38-libretro-cores.zip`: 12,174,950 bytes, 128 entries,
SHA-256 `f7b1b69f4cc563e66fb3395d2a8fc204ca636de1dcc00b488f0aa57063d2c1a9`.
`.info` SHA-256:
`c3455a6c5cfa3998d0aaba5c0e5372367ba966dc0c5e76c7a4f9de4145cd88d2`
(`display_version = "0.5.38"`).

## Android APKs (versionCode 538)

| ABI | size (bytes) | SHA-256 |
|---|---|---|
| arm64-v8a | 1,058,972 | `729c675cd44f3fc1c4c6520a25963b185824e7b349b389f2be875a16e8b6c4dd` |
| armeabi-v7a | 1,067,166 | `190a6c8e327763970192c3965b8375ddc4c176ecabce699b2f1a00b47faa7f5c` |
| x86 | 1,046,678 | `66fbd865fb2be1ba5a1579f4b765786a1b9af160bedd8a90746592768bacf1ee` |
| x86_64 | 1,058,969 | `5051614f77151a782c902beebac46d40559be108a438f5c2d6c8da64d083a0eb` |
| legacy (ARMv5, API 5) | 741,355 | `f68d510a79147654185dd2f770bd8b82e4fceb79bb05a2b9f72df5816556774b` |

## Verification

- Full serial CTest: **210/210 passed in 370.24 s**.
- Libretro mock frontend matrix: **20/20 passed**.
- Real RetroArch 1.22.2 LoopDemo: 120 frames PASS.
- Windows x86-64 under Wine: **4/4 passed** at 300 frames.
- Submission bundle validator: **32/32 passed**.
- Whole-corpus libretro probe (r57): 1664 titles -> 1658 passed,
  1 known limitation, 5 failed (see above).

## Upgrade notes

Save states from 0.5.8-0.5.37 are refused by the version check; this release
does not change the state format beyond that.  The libretro core option set and
the memory map / SYSTEM_RAM cheat window are unchanged.

## Upstream

- Core `.info` PR: libretro/libretro-super#2204 (branch `b1436c4`, OPEN/MERGEABLE).
- Library page PR: libretro/docs#1239 (head `95a1d0a`, OPEN/MERGEABLE).

No merge or acceptance is claimed.
