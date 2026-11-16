# nano_j2me 0.5.11

Release date: 2026-11-16. Highlights, verification evidence and upgrade notes
for the 0.5.11 libretro core package (r30).

## Highlights

### Frame pacer: idle titles no longer burn a CPU core

0.5.10 paced runs at the default 30 fps cap could keep a host CPU core busy for
the whole run: several 2D titles burned 9-10 s of CPU for 10 s of wall time at
`--fps 30`. The frame pacer handed its idle budget to the cooperative
scheduler, whose idle pass had no way to sleep for a bounded time, so it spun
until the budget expired (up to ~535k scheduler passes per frame; see
`docs/notes/2026-11-16-paced-cpu-burn-investigation.md`).

0.5.11 lets the pacer own the nap: the scheduler now reports whether an idle
pass did any real (non-main-thread) work, returns early when it did not, and
the pacer sleeps for a bounded slice of the remaining frame budget. Measured on
300 frames at `--fps 30` (heap 32, user+sys CPU):

| title | before | after |
| --- | --- | --- |
| 魔塔_8052 | 9.87 s | **0.09 s** |
| 火影忍者_11531 | ~9.0 s | **0.07 s** |
| 合金弹头4(完美汉化版)_4053 | ~10.0 s | **0.08 s** |
| 世嘉头文字D－宿命对决_1247 | ~10.0 s | **0.05 s** |

All four still run at the requested 30.0-30.1 fps. Unpaced throughput is
unchanged (bench.jar 62,513,694 bytecodes; 毁灭战士3_10598 3000 frames in
16.20 s) and loader-heavy titles still get CPU (Minecraft PE 1.41 B bytecodes
in 10.0 s at 30 fps).

## Artifacts

Stripped libretro cores for eight targets, plus `.info`, license/notice tree
and `docs/` for each. `.info` SHA-256 is
`67048b11bdefad3448aab34b83d42fe32ec2a1d3bd149ac7b617e38356c46d0e`
(identical in every directory) and declares `display_version = "0.5.11"`.

| target | size | SHA-256 |
| --- | --- | --- |
| android-arm64 | 3,559,672 | `6de50818786605a493ae6e9a6d6790631b271a3e1cc66a397ebfd3d2f83ea054` |
| android-armv7 | 2,747,076 | `35ee559ba38f98b25e338ed7123b7fd6e4ecbd7214537053e1ae20edd0e17cd1` |
| android-x86_64 | 3,599,920 | `f17761e4003d36f0888df1be25c8432772e43e317b2fbbc09dbc66f4e27a8013` |
| linux-aarch64 | 3,325,256 | `e2c90af45b7d4cf5e8fb145441da83dff30306bca5d9d54571879681f4cf162f` |
| linux-armv7 | 3,032,868 | `fc3dc72a7357cd30c64378330ae5c2f76536b8064f9587961a77293a44ba3058` |
| linux-x86_64 | 3,843,912 | `e29593a0e59e56da252855047f65b3bdf331997db6e8168e5098c058bb6b01af` |
| windows-i686 | 3,453,440 | `a8f4e1d84653a7fc4a2f8c5ac60b9b256b3ccc6df9c02496cc7638d8bffb05b1` |
| windows-x86_64 | 3,228,160 | `e2c1d1196db39aec564d19d27a1bd35089fbe03efd2fbd1b7de62ee841c21652` |

Release archive `nj2me-0.5.11-libretro-cores.zip` (11,742,221 bytes,
SHA-256 `86babcd96fc0e66203aa7516a714bada81dec995840d74c2f733397197f07992`)
contains all eight target directories; `SHA256SUMS-libretro-cores.txt` lists
every file.

## Verification

- Mock libretro frontend conformance matrix (linux-x86_64 core): 20 passed,
  0 failed, 0 skipped.
- Real RetroArch 1.22.2, headless: LoopDemo 120 frames PASS (core log markers
  verified).
- Windows-x86_64 DLL under Wine: PE exports check PASS; four fixtures PASS at
  both 120 and 300 frames (audiodemo included). Note: the 120-frame audiodemo
  audio check is timing-sensitive under Wine and flaked once out of seven
  consecutive runs on a loaded host; 300 frames passed 3/3 and 120 frames
  passed 6/7 — treat a lone zero-audio sample as a Wine scheduling artifact
  before investigating the core.
- MinecraftJ2ME_PE state round trip through the core: 45 checks, 0 failures,
  `serialize_size=157593664` (save-state bound sized for dense arenas).
- Core option matrix: 88/88 profile runs passed (8 titles x 11 profiles).
- Libretro state probe: 12/12 titles serialized, restored and remained live.
- Full CTest suite at release tree `ca0e43d`: **192/192 passed**.
- Whole-corpus headless sweep: 372 ok / 27 title-exit / 2 title-exception /
  2 no-frame / 0 timeout / 1 bad-jar (same distribution as the r54b baseline).

## Known limitations

- `nano_j2me_heap=auto` resolves to host RAM/16 clamped to 32-128 MB; state
  files include the heap budget, so a 128 MB heap produces ~140-157 MB raw
  states. Use `nano_j2me_heap=16`/`32` for small states and rewind.
- Rewind works when the frontend's buffer holds at least two snapshots; it is
  a frontend feature with no determinism/netplay/runahead claim.
- Cheats: code-based cheat codes are not supported; the JVM arena is exposed
  as `RETRO_MEMORY_SYSTEM_RAM` and published via `SET_MEMORY_MAPS` for a
  frontend's search-and-write workflow. Raw writes bypass the Java memory
  model.
- Titles whose loader renders for minutes (毁灭战士3_10598) need a long
  frontend load timeout; that is game-side loading work, not a hang.
- A fast-forward/unpaced frontend can save a state while the MIDlet is still
  starting; such an early state restores into a VM that dies on a later frame.
  Real-time (60 Hz) frontends are not affected; the corner is documented in
  `docs/notes/2026-11-15-libretro-async-load-early-state.md`.
- Nested-paint save (火影忍者_10735) remains a documented limitation.

## Upgrade notes

- Save states embed a version/font/identity CRC; states written by 0.5.10 or
  older are refused by 0.5.11 (and vice versa). Start a new state after
  upgrading.
- `display_version` is now `0.5.11`; the libretro-super `.info` patch in
  `docs/libretro/upstream/` is regenerated from it (baseline
  `libretro-super` `fb15e63`).
- Windows PE artifacts remain byte-reproducible (`-Wl,--no-insert-timestamp
  -Wl,--build-id=none`, no GNU strip), so two clean builds of the same tree
  produce identical DLLs.

## Upstream

The ready-to-apply patches and PR texts live in `docs/libretro/upstream/`:
`libretro-super-nano_j2me-info.patch` (one added `.info`) and
`libretro-docs-nano_j2me.patch` (5 files). The two pull requests opened with
explicit authorization remain open and mergeable:
<https://github.com/libretro/libretro-super/pull/2203> and
<https://github.com/libretro/docs/pull/1239>. No merge or acceptance is
claimed.
