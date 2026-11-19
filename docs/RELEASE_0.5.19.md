# nano_j2me 0.5.19

Released 2026-11-20.  This is a libretro-package release of the runtime as
revision `994cdca`; the only runtime change since 0.5.18 is the full-screen
mode fix below.  APKs and the CLI are built from the same tree.

## Highlights

* **Full-screen mode changes on a visible Canvas are applied**, not rejected
  with `IllegalStateException`.  `The Sims 3 Winter Edition` calls
  `setFullScreenMode(true)` from its paint loop; every paint used to throw, the
  title swallowed the exception and painted pure black for the whole run.  It
  now renders its main menu (600 frames, 817 paints, 83 259 blits).  A scan of
  all fetched corpus titles shows no other title affected by the rule.  See
  `docs/notes/2026-11-20-fullscreen-visible-toggle.md`.
* The `setCurrent()` / `setFullScreenMode()` ordering rule that `开心农场`
  needs is unchanged (the mode change from a constructor still happens before
  the first presentation); `test_fullscreen` now covers both phases.
* Corpus/probe tooling: sweeps and libretro probes now record and replay each
  row's absolute `jar_path`, so fetched archive titles are covered by the
  libretro evidence too.

## Libretro cores (r38, commit `994cdca`)

`.info` SHA-256 `67a021b3f061fe6ffe97911642432bbc216db9b74d9961b77bc375e0edf42a07`,
`display_version = "0.5.19"`.

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3 848 064 | `223a3800a7ccf16819840797576bcf9f515cfe40be6582f4b5f1a9ba2b2dbd95` |
| linux-aarch64 | 3 331 856 | `c2ae862032a789ba10af259d4e3a55223e1ffd36e00d8aa4d15548b620019934` |
| linux-armv7 | 3 039 264 | `1a209cd333a35e0cb9145dd4a941dace8b66dd4a10cb2273621c43c2d0d63cfb` |
| android-arm64 | 3 566 544 | `b1e4ec0f45d2c72f80d0bbcbc4fd041c83bf31d810dd49aad22ead51f740836d` |
| android-armv7 | 2 751 776 | `3dfe85d30875bcd8ac383e5cfacc322bf2ab92a16fe5f8f5d77aaf4fe5f7e3fb` |
| android-x86_64 | 3 606 984 | `e2721e3c369ca434720bdf29147d3af3d3e7457bc12259169229eeb94eb9367d` |
| windows-i686 | 3 460 096 | `37b898c11ad8e376d4881dc6d6b7901bee75cfff3d5d7e4d40e79d2ad3314151` |
| windows-x86_64 | 3 236 352 | `fba7b6e548a6f13a5650ddc728733f5efd2c5fe759aa21216b1036511be2c0a3` |

## APKs

| artifact | bytes | SHA-256 | version |
| --- | ---: | --- | --- |
| `nj2me.apk` | 3 295 594 | `c9e2720714f4b68d28ed8518ace4d9c02ef26531f124fabd3e782453c087c5c0` | versionCode 519, 0.5.19, 4 ABIs, minSdk 21 |
| `nj2me-legacy.apk` | 704 160 | `65c28703caa705d5c3002ee932828618728db1a68de28ae2f5579117760319d0` | 0.5.19-legacy, minSdk 5 |

## Verification

* mock frontend conformance matrix: **20/20**;
* real RetroArch 1.22.2, loopdemo, 120 frames: **PASS**;
* Windows x86_64 under Wine 11.22, 4 titles at 300 frames: **4/4**;
* submission-bundle validator: **32/32**;
* full serial CTest on the release tree: **199/199 in 350.20 s**;
* whole-corpus libretro probe on the released r38 core: 5

## Upgrade notes

Save states written by 0.5.8–0.5.18 are refused by the version CRC; states
from 0.5.19 are loadable by 0.5.19.  The cheat search/editor, multi-touch and
memory-map features are unchanged from 0.5.17/0.5.18.

## Upstream

Ready-to-post PRs: <https://github.com/libretro/libretro-super/pull/2204>
(`.info` only) and <https://github.com/libretro/docs/pull/1239> (library page).
No merge or acceptance is claimed.
