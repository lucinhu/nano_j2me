# nano_j2me 0.5.40

This release is the libretro-focused follow-up to 0.5.39.  It closes the last
"renders in the CLI, black through the core" case that was a runtime ordering
problem rather than a title defect: a screen made current by
`Display.setCurrent()` is now painted before background threads get their next
bytecode quantum, matching how a handset's event thread services a show
request.

## Highlights

- **Show requests are ordered before background threads** (`src/core/nj_thread.c`).
  The scheduler stops a pass when `fresh_paint` is pending and no nested C
  pump is active, and the frame loop leaves as soon as the request is pending,
  so the frame paint gate services it in the same frame.  `Skater_by_BFLM`
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
| linux-x86_64 | 4,048,832 | `6685baa6c2112f58b69f52a9d66db990787e6dd47c9f1dc01efdf49bdb231ef8` |
| linux-aarch64 | 3,523,352 | `bff2084c6ba7fb623da5eec6f1721ba8b336aca35744d88f5fe536334fd4be0a` |
| linux-armv7 | 3,176,104 | `9183759c43d66342d8ddc1d5cb8a31395fe31a2a270a3af20fd466a26a158f57` |
| android-arm64 | 3,769,000 | `1394bb8a4cc4d1ac82c1e25cc230d28440efe52f51ee850c3e475f13fac39736` |
| android-armv7 | 2,865,164 | `c39b729725fe8b53427a6878938f5240d5e4fab6146781084078105bf61cff46` |
| android-x86_64 | 3,794,912 | `88797322e10fa8c2afb38a19e6d9601f38edaf705cc5025dc11c0a04181c6d2e` |
| windows-i686 | 3,601,408 | `218c4af94dc2aadaee0a1df520de271d93f3d76851a9b34eed9c28f943be0ed8` |
| windows-x86_64 | 3,371,008 | `200e8958584df4734a51c99e2adc147241a4379d691bbce55139fe32da35b944` |

`.info` SHA-256 `3b9f28c32b4fa61b99e0a50298963f477fb339cb9e1a600d449c7ac2564cebab`
(identical in both copies).  Cores archive
`nj2me-0.5.40-libretro-cores.zip`: 12,176,722 B, 128 entries,
SHA-256 `73211989a818c18a7b9400080ee3100c93216e30413c8e993d109e81673e2182`.

## Android package

versionCode 540, versionName 0.5.40, minSdk 21.  Five split APKs:

| ABI | size (B) | SHA-256 |
|---|---|---|
| arm64-v8a | 1,058,972 | `a907af1953c2b6d111b32a37b5940846d68a6fba2cd80095514fbef56d82f7cb` |
| armeabi-v7a | 1,067,166 | `2f3c048973c4134cd594e19549cb4ead673da168fe0275972cfe124a0eb30ef7` |
| x86 | 1,046,678 | `497fcb07084f467e261786ea776ddae6854d0bc22e6c8d59c5995059ad82d214` |
| x86_64 | 1,058,969 | `37b27f2b6a6738465b8f8dbc653a2ae91c4b7377f4745436314c4b1b09ac0f5e` |
| legacy (ARMv5, API 5) | 741,472 | `7b7727160e3fe9fe9cb29bf4bfc929fa0a3d42956480fc49300687a774fe2acb` |

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
