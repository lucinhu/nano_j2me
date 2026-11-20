# nano_j2me 0.5.21

0.5.21 fixes the Android in-game 保存状态 feature. The game panel's save now
works for titles whose frame loop runs inside the frame hook (`LoopDemo` and
most `GameCanvas` titles): the port asks for a *boundary save* and the runtime
stores a MIDlet thread parked in `serviceRepaints()`/`paint` as
"resumed after the call returns" instead of refusing the snapshot. Verified on
the emulator: 保存状态 writes a real 330,016-byte state, and 恢复进度 from the
library card resumes the game with the normal screen.

## Highlights

* **Boundary save API** (`nj_vm_save_file_boundary()`, `src/core/nj_save.c`,
  declared in `include/nj2me/nj_vm.h`): ports that only get control from the
  frame hook (the Android launcher serves its save request from the input poll
  inside `nj_midp_frame()`) can now take a snapshot. The hook re-entrancy guard
  is allowed for this call, a thread parked in a frame-wait native is stored as
  `RUNNING` with the native dispatch frame dropped, and the restored thread
  resumes after the parked call - which is what the game would do anyway.
  Every other guard (`in_paint`, `in_call_pump`, exception construction, frame
  validation) is unchanged, and the same function on an idle VM behaves exactly
  like `nj_vm_save_file()`.
* **Covered by `test_save`**: 2641 checks, 0 failures, including the new
  idle-VM boundary-save equivalence check; `test_save`, `test_vm`,
  `test_lifecycle`, `test_thread` and the libretro frontend all pass.
* **Reason for the old failure**: the save was refused with
  `in_frame_hook + in_c_call` at every frame boundary for `serviceRepaints()`
  titles; retrying across 200 boundaries never found a clean one (the game
  thread is always parked there). The libretro port already serves state
  requests between frames, so only Android was affected.
* **Bare drags reach the Canvas again** (`src/midp/nj_lib_lcdui.c`): the
  multi-touch owner gate dropped a pointer move that arrived without a recorded
  down, which also broke mouse-style drags and the `test_pointer` fixture.  A
  move/lift with no owner now claims the single pointer for its own id; a real
  second finger is still filtered out.  `tools/check_pointer.sh` takes the
  binary as an argument now, so the check tests the built tree instead of a
  shared `build/nj2me` (it had stayed green against a stale binary).
* The libretro core is feature-identical to r39 otherwise; the boundary-save
  API is unused there.

## Artifacts

| target | size | SHA-256 |
|---|---:|---|
| linux-x86_64 | 3,856,264 | `b5146bb56cfa772f819a84f055cbd4567f9a0dbc7720b398209827485f4a74c1` |
| linux-aarch64 | 3,332,912 | `5843586c74def8d8c2ed77ac5cada9d7efd101e5b4ef67e4cd8047ad68731c68` |
| linux-armv7 | 3,040,344 | `d07d7670680d8bd5b6edff3a46fcfbd9278f134c97973e722af0da8b40428f9c` |
| android-arm64 | 3,567,712 | `a2649d459d90ae929fb21fad4580dea3d115d4a8cbe5f0a172bca0a7ba4f62f1` |
| android-armv7 | 2,752,568 | `b6aa6e09d39e671c35752481423d2358ad4747f4fdf144674540ed57cdefd2a4` |
| android-x86_64 | 3,608,136 | `46f293b8d501d636e74a9bd3aa1f571967d871d425e5eefe6ef4dacf4156826f` |
| windows-i686 | 3,462,144 | `791ca67b71f217415fdc8462a6026152949b30ebca5e4481448b34e2000414cb` |
| windows-x86_64 | 3,237,888 | `5e8abec823c48bdea9a494abbe893be8c860bcc921536a955c0d53451142486f` |

Release bundle: `nj2me-0.5.21-libretro-cores.zip` (11,709,796 bytes,
SHA-256 `e385253f651351f83554c332f4a68f5089a8a33c7021d438e009434f939b00d5`),
containing all eight targets with `.info`, licenses and notices.  Metadata:
`.info` SHA-256 `e974a5c60a8cb6034d3507b2a6b3e0ce9b628575bb6c303e7bab0bd0c55f17f9`.

### Android APKs

| file | version | size | SHA-256 |
|---|---|---:|---|
| `nj2me.apk` | 0.5.21 (versionCode 521), 4 ABIs, minSdk 21 | 3,299,690 | `676ba4eeaedd396d6bb0dba674aa129ff4f5316954e7b3332d27ad74f974e141` |
| `nj2me-legacy.apk` | 0.5.21-legacy, minSdk 5 | 704,323 | `cfab936550c0948fc59acb1af50ff257ca6eb3649c2750234cd2a1dde8940913` |

## Verification

* full CTest: 199/199
* libretro mock conformance matrix: 20 / 20
* real RetroArch 1.22.2: LoopDemo 120 frames PASS
* Windows x86_64 under Wine: 4 / 4 at 300 frames
* submission bundle: 32 / 32
* emulator (Android 16/API 36, headless AVD): 保存状态 -> 330,016-byte file,
  force-stop + 恢复进度 -> `resumed LoopDemo from …loopdemo.jar.state`, normal
  game screen rendered
* whole-corpus libretro visual probe on the released core: see
  `docs/LIBRETRO_SUBMISSION.md` round-141 entry

## Upgrade notes

* Save states from 0.5.8-0.5.20 are refused by the version CRC; start a fresh
  state after upgrading.  Android states saved by 0.5.20 on frame-driven titles
  were 0-byte/refused, so nothing usable is lost.
* Android: install over the previous APK; existing games, settings and cheat
  files are kept.

## Upstream

Ready-to-post material lives in `docs/libretro/upstream/`; PRs
libretro/libretro-super#2204 and libretro/docs#1239 track the current metadata
and library page.  No merge is claimed.
