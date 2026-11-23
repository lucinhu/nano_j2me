# nano_j2me 0.5.40

This release is the libretro-focused follow-up to 0.5.39.  It closes the last
"renders in the CLI, black through the core" case that was a runtime ordering
problem rather than a title defect: a screen made current by
`Display.setCurrent()` is now painted before background threads get their next
bytecode quantum, matching how a handset's event thread services a show
request.

## Highlights

- **Show requests are ordered before background threads (libretro)**
  (`src/core/nj_thread.c`, `include/nj2me/nj_port.h`).  The libretro port sets
  the new `show_before_background` capability; when `fresh_paint` is pending
  and no nested C pump is active the scheduler stops the pass and the frame
  loop leaves, so the frame paint gate services the show request before the
  loader thread continues.  The CLI/headless runner keeps its historical
  interleaving (its timer-fairness and loader fixtures assume it).  `Skater_by_BFLM`
  (previously black through the libretro core, correct in the desktop CLI) now
  renders through every frontend: 7,480-8,004 non-black pixels, 47/47 content
  frames, 47/47 harness checks.
- Whole-corpus libretro visual probe on the release core over the merged
  1,664-title set: **1,659 passed, 1 known limitation, 4 failed** — the four
  failures are the documented set (`TheMummy_K700F`, `PAC-MAN (176x220)`,
  `assault`, `Asphalt4EliteRacing_1.0.0_320x240`).  `Skater_by_BFLM` is gone
  from that list.
- No compatibility regressions: a 150-title A/B CLI sweep (600 frames,
  18 jobs) shows 0 verdict changes and identical frame counts; 48 titles show
  small paint-count shifts (a paint-loop title that previously painted ~94
  times per frame now paints fewer).
- `docs/LIMITATIONS.md` now records the ordering fix, and
  `docs/notes/2026-11-23-first-paint-delivery.md` has the round-248 section
  with the mechanism and evidence.

## Libretro package (r59)

| target | size (B) | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,048,832 | `0d33d24b9b3a2e5c22cacdd926c25842d3bd72fdfbf1c93d000f84c57afd7ff1` |
| linux-aarch64 | 3,523,384 | `8ed4be8af20d6845fac29aa27f032dc1b7e9737c541db52cef66a1493bec7dd7` |
| linux-armv7 | 3,176,312 | `e69ada88ab0465841d35991b286c2ba412873436ba83d93a4bee88eb9631f338` |
| android-arm64 | 3,769,048 | `8b299cc8c9911b522607a6e920c2acf97779dc3b14be3defce36a0271d1d2365` |
| android-armv7 | 2,865,308 | `493a13c6a4150798e9c76e2f58151918f7382033272922fff25d5bdc9ec11fed` |
| android-x86_64 | 3,795,024 | `c2480eb09ec8d3c6e1cae18da13cf09baad3e891c0fd96d89e78c0ac52262eca` |
| windows-i686 | 3,601,408 | `539eaaba4a7d3f104873b58fe8bfa2b85e95b2eaacdf1bf80e21afbd46ba4353` |
| windows-x86_64 | 3,371,008 | `cd7403b697e7799fe5f18ac3b5a36b83478359daf7cba602700a6d219cda5c96` |

`.info` SHA-256 `3b9f28c32b4fa61b99e0a50298963f477fb339cb9e1a600d449c7ac2564cebab`
(identical in both copies).  Cores archive
`nj2me-0.5.40-libretro-cores.zip`: 12,176,826 B, 128 entries,
SHA-256 `547ee5a88c4323cc34f99bc0f4c590709b276f6848ac75652be48faf0de1b29f`.

## Android package

versionCode 540, versionName 0.5.40, minSdk 21.  Five split APKs:

| ABI | size (B) | SHA-256 |
|---|---|---|
| arm64-v8a | 1,058,972 | `be20ef13da50d20dd8e0b5da20fffcc0f9a8e5fb85674c933e616086cf19c59e` |
| armeabi-v7a | 1,067,166 | `a94289d1df427216dd8dcaf27de2ab469946f1907863253dc5ca1538a40ecf84` |
| x86 | 1,046,678 | `c6bf4740e749fa5cbfe4cb5ea803c9d0680816f074aa31c60f301655a036fb0e` |
| x86_64 | 1,058,969 | `70bf1ae13fed96460158fe6ef68078dbeec7ecbd975ecd4c79f81f1372c38d6e` |
| legacy (ARMv5, API 5) | 741,420 | `bd9dd4d7f7359683efaabbc2018ac240d744a02589cedb943fe6aadaae94fe5c` |

## Verification

- Full CTest: 210/210 passed on the release tree.
- Libretro mock frontend matrix: 20 passed, 0 failed, 0 skipped.
- Real RetroArch 1.22.2: LoopDemo 120 frames PASS (exit 0, log markers verified).
- Windows x86_64 under Wine: 4 passed, 0 failed at 300 frames.
- Submission bundle validator: 32 passed, 0 failed.
- Whole-corpus visual probe: see above (1,659 / 1 / 4).

## Upgrade notes

- Save states from 0.5.8-0.5.39 are refused by the version CRC; create a fresh
  state after upgrading.  State format is unchanged from 0.5.39.
- The behaviour change is a scheduling-ordering refinement; no core option
  changed.  `nano_j2me_deterministic` still replays frame-driven titles
  byte-identically.
- No determinism/netplay/runahead claims; cheats and the exposed memory map
  work as before.

## Upstream submission

- `libretro-super` fork branch `add-nano_j2me` commit
  `ea88342883b03cb1ec74e7d10632ed28aea0927e` on `libretro-super` master
  `55e50a81195eaf8dd1b7720a601282459832ced1`; PR
  libretro/libretro-super#2204 is open and mergeable.
- `libretro/docs` PR libretro/docs#1239 is open and mergeable at head
  `8ef5c42f6272c6fea84a5643f111e685b8d1f32e` against docs master
  `96ee874e010a33b3fb1a79cf3cf833fc17189cfc`.
- Parent sources: https://github.com/lucinhu/nano_j2me.
