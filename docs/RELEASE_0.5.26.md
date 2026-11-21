# nano_j2me 0.5.26

Released 2026-11-22.  Two black-screen titles were fixed by making lazy
reference points match MIDP/JVMS semantics; both now have regression gates, and
the Android builds are per-ABI APKs.

## Highlights

* **NeonRunner** now renders its language menu.  The runtime delivered the
  first `Canvas.sizeChanged()` lazily on the first paint, i.e. after the
  game-started thread had installed its boot renderer; the title's own
  240-wide handler then overwrote that renderer with a null backup and every
  paint threw.  The first callback is now delivered from
  `Display.setCurrent()` while the MIDlet thread still owns the stack.
* **GoldenSolitaire** now renders its language-selection screen.  Its
  obfuscated resource manager calls `Class.forName("p")` for the side effect of
  running an interface static initialiser whose fields allocate archive
  handles; our `Class.forName()` returned without initialising the class, so
  the archive lookup table was sized without those slots and the first indexed
  store threw on every paint.  `Class.forName()` now initialises the class
  (JVMS 5.5) exactly like `Class.newInstance()` has since 0.5.24.
* New CTest gates: `test_sizeorder` (fails on the pre-fix build, passes after)
  and `test_classinit` (asserts the initialiser runs *during* `Class.forName`,
  not when the field is first read).

## Verification

* Full serial CTest: **205/205 PASS** (366.94 s on the release tree).
* Libretro mock frontend matrix: **20/20**.
* Real RetroArch 1.22.2: LoopDemo 120 frames **PASS**.
* Windows x86_64 under Wine: **4/4** at 300 frames.
* Submission bundle check: **32/32**.
* `NeonRunner`: 600 frames / 476 paints / gfx=77,655 (was gfx=0);
  libretro visual probe passes.
* `GoldenSolitaire`: 600 frames / 599 paints / gfx=18,875 (was gfx=0);
  frame dump language screen 73,978 non-black px (reference 73,983);
  libretro visual probe passes.
* Compatibility matrix regenerated deterministically (1502 titles / 1483 ok).
* Whole-corpus re-sweep on 0.5.26 (735 titles): **720 ok / 4 title-exception /
  4 bad-jar / 3 timeout / 2 title-exit / 2 no-frame / 0 failure**, no
  regressions against the previous sweep; the two fixed titles now draw real
  content (GoldenSolitaire gfx 19,274, NeonRunner gfx 77,656).
* Whole-corpus libretro visual probe on the released r45 core (720 ok rows,
  300 frames): **717 passed, 3 known limitations, 0 failed**.

## Core artifacts

| target | bytes | SHA-256 |
|---|---|---|
| linux-x86_64 | 3,987,376 | `14da9efb8bb42030565391609f834907ed42139236386d167b2c37f6e5021775` |
| linux-aarch64 | 3,461,888 | `1afe92eb69dd82a28fa8d3e15407b6c9ee00b1890e3315d3060888887d8f11ae` |
| linux-armv7 | 3,133,712 | `8b33803b866c16b6825620c7962d9b6a3a08c5be15a89146d5729187d8da8aa1` |
| android-arm64 | 3,703,272 | `00c343c9c49b1069fe7056eebc60abf61c4cb35798957050d94d756eaf57071b` |
| android-armv7 | 2,828,772 | `2e64c0379c959ea09c375f40214c966d39cae7286f94bd14e9bb4adef072403a` |
| android-x86_64 | 3,734,560 | `1b00e8db8f662e53d291fc1c374b43c4773cea9d20f6f81c327047102f7b2e2b` |
| windows-i686 | 3,557,888 | `1d5bd0cee3a09cdf11ff3e218e3dacf61304c68f0ddee7b3ef234d56c1661afc` |
| windows-x86_64 | 3,329,024 | `574ac3b6bed529b19f4b3708660e42d8b11695629a653d9c89bc82b5f4f6f576` |

The `.info` metadata is identical in both copies (SHA-256
`390991aaf9d767b425ccb7b2f5533246f151de058dbd66fcfab84005fa73b252`,
`display_version = "0.5.26"`).

Cores archive: `nj2me-0.5.26-libretro-cores.zip` (12,056,316 bytes as published;
128 entries = 8 targets x 16 files) SHA-256
`a35ae3a0bf37d542f2d41e001ea659af7501dbb7829553263034425ff1886311`.

## Android builds (per ABI)

| APK | bytes | SHA-256 |
|---|---|---|
| arm64-v8a | 1,005,724 | `48b713b5ecb984002be05a7a82be6c64aa627aefd1f4e23779986cb676498c49` |
| armeabi-v7a | 1,013,918 | `daec5979a159d7694d9fc483dd0c95c987df475e458cd8277236689791ab253e` |
| x86 | 993,430 | `727f12fe0b77dee1b7a399db657139848b7a0691ab106147353a9bb5fcc1fd5d` |
| x86_64 | 1,005,721 | `9cba57067717b373d5c9500f612e95fff2a3cd3ac5053ba57d6446997b2ff6e5` |
| legacy (ARMv5/API 5) | 729,618 | `6a7b12d5018bd72458f04237aedaf532a24ccdbe606ae08a4edc773e94631f32` |

`versionCode = 526`, `versionName = 0.5.26` (`0.5.26-legacy` for the legacy
APK).

## Upgrade notes

* Save states from 0.5.8-0.5.25 are refused by the state version check
  (`save states from 0.5.8-0.5.25 are refused by the version CRC`); restore in
  the old build and re-save after upgrading.
* No determinism, netplay or runahead claim is made.  `SYSTEM_RAM` memory
  exposure is a debugging/cheat window, not a save channel.

## Upstream submission

* `libretro/libretro-super#2204` - core info for 0.5.26 (fork branch
  `add-nano_j2me`, commit `68bccb1` on master `55e50a8`), OPEN/MERGEABLE.
* `libretro/docs#1239` - library page for this core (fork branch
  `add-nano_j2me`, built on docs master `96ee874`), OPEN/MERGEABLE.

Neither PR has been merged or reviewed by upstream.
