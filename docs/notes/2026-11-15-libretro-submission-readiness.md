# 2026-11-15 libretro submission readiness check

Scope: verify that the repository is actually ready for the libretro submission
steps (metadata, CI configuration, artifacts, documentation drafts), and record
the remaining external actions. No upstream pull request, upload or maintainer
contact was made.

Tree under test: `bc49c46` (`v0.5.7-9-gbc49c46`) for the metadata/CI/docs
checks below; the release artifacts were later refreshed on the same day at
HEAD `6b1e9e2` (tree containing `dda4fc2` graphics work), see item 5; the
artifacts were refreshed once more after the state-load fix at HEAD `dd39c1c`
(see the update section at the end).

## What was checked against live upstream sources

- `docs/development/cores/developing-cores.md` was re-fetched. The four steps
  are unchanged: `.info` to `libretro-super/dist/info`, root `.gitlab-ci.yml`,
  optional assets/database playlists, and `libretro/docs` documentation. The
  guide also confirms the `<name>_libretro` file-name contract and that the
  optional `_android` suffix is stripped by RetroArch.
- `libretro-infrastructure/ci-templates` (`linux-cmake.yml`,
  `windows-cmake-mingw.yml`, `android-cmake.yml`) were fetched and their
  variable contract checked against our root `.gitlab-ci.yml`: `CORENAME`,
  `LIBNAME`, `BUILD_DIR`, `EXTRA_PATH`, `CMAKE_SOURCE_ROOT`, `CORE_ARGS`, plus
  the template image/toolchain assumptions. Our file extends exactly those
  templates and sets `EXTRA_PATH: libretro`, matching the CMake output dir.
- `libretro-database` lists `dat/Mobile - J2ME.dat` among 54 platform folders,
  so `database = "Mobile - J2ME"` in the `.info` is valid.
- `docs/meta/core-template.md` and the docs README "Adding a new core" list were
  fetched; `docs/libretro/nano_j2me.md` already covers background, requirements,
  extensions/database, features, directories, geometry/timing, core options,
  fonts, controllers and compatibility, and names the exact upstream
  destinations (`docs/library/`, `mkdocs.yml`, `guides/core-list.md`,
  `development/licenses.md`).

## Local checks performed

1. **Metadata**: `src/port/libretro/nano_j2me_libretro.info`
   (`c0f30aea…`) is JAR-only, `needs_fullpath = "true"`, Core Options 2.0,
   `savestate = "true"` / `savestate_features = "basic"`, `hw_render = "false"`.
   It ships byte-identically in all eight packages.
2. **CI simulation, all three templates**: `tools/simulate_ci_builds.sh`
   reproduces the template command shape (`cmake -DCMAKE_BUILD_TYPE=Release
   $CORE_ARGS ...` then `cmake --build ... --target nano_j2me_libretro`) and
   reads `CORE_ARGS` straight out of the committed `.gitlab-ci.yml`. At this
   revision all three contracts pass:
   - Linux (default Unix Makefiles generator):
     `libretro/nano_j2me_libretro.so`, 3,969,632 B.
   - Windows (mingw-w64 with `CMAKE_SYSTEM_NAME=Windows`, what the MXE
     `*-static-cmake` wrapper supplies upstream):
     `libretro/nano_j2me_libretro.dll`, 3,537,295 B.
   - Android (NDK 30.0.16248370, `-DANDROID_ABI=arm64-v8a`):
     `libretro/nano_j2me_libretro.so`, 11,543,552 B; the template's
     `after_script` renames it to `nano_j2me_libretro_android.so`.
   Each artifact lands at `$BUILD_DIR/$EXTRA_PATH/$LIBNAME`, where the
   templates collect it. The CI-simulated Linux core also passed the mock
   frontend matrix (20/20), and the shipping Windows static build is separately
   validated by the Wine fixture suite (4/4).
5. **Fresh eight-target build**: first at HEAD `bc49c46` into
   `/var/tmp/nj2me-submission-dist`, then refreshed at tree `6b1e9e2` into
   `/var/tmp/nj2me-submission-dist-r9` and copied to `build/libretro-dist/`
   (previous tree backed up to
   `/var/tmp/libretro-dist-20261115-bc49c46-backup.tgz`); after the state-load
   fix it was rebuilt once more at HEAD `dd39c1c` into
   `/var/tmp/nj2me-submission-dist-r12` and copied over
   `build/libretro-dist/` (r9 tree backed up to
   `/var/tmp/libretro-dist-20261115-r9-backup.tgz`). Current sizes/hashes are
   in `build/libretro-final-report-20261115.md`. 16 files per
   directory, identical `.info`, license/notice set and `BUILD_STATUS.txt`; the
   three Linux cores are byte-identical between the two builds. Sizes and
   SHA-256 values are in `build/libretro-final-report-20261115.md`.
6. **Artifact validation** (refreshed build at `6b1e9e2`): linux-x86_64 mock
   frontend 20/20; real 3D corpus title 浴火银河2完整版 through the mock
   frontend PASS (36 checks, 120 frames, visual required); real RetroArch
   1.22.2 LoopDemo 120 frames and TextDemo 180 frames PASS; windows-x86_64
   Wine fixtures 4/4 PASS. Android/ARM cores were ABI/dependency-checked only
   in this rebuild (no device run available). Seven M3G titles were also
   checked through the mock frontend at the real-time cadence
   (`--pace-us 16666`): 浴火银河2完整版, 蓝牙真3D格斗, 急速狂飙完美破解版,
   浴火银河2瓦尔基里, 极品飞车16：亡命狂飙, 3D激爆拳击 and 3D跳台滑雪2011
   all pass 120-frame visual checks, and the two slow starters also pass at
   600 frames. The same harness run unpaced (`--pace-us 0`) starves the VM
   worker and shows blank frames for 3D激爆拳击; that run-pacing limitation is
   documented in `docs/LIBRETRO.md`.
7. **Local test suite**: isolated RelWithDebInfo build (CLI + libretro + tests,
   SDL2/embedded/web/GLES2-accelerated port off) passed **183/183** CTest tests
   in **338.26 s** at `bc49c46`, again **183/183** in **344.82 s** after the
   artifact refresh at `6b1e9e2`, and **183/183** in **328.80 s** after the
   state-load fix at `dd39c1c`, including `test_libretro_frontend` and
   `no_statics`. A separate build directory under `/var/tmp` is used so the
   user's in-progress `build/` trees are not touched.
8. **Upstream patches, exact and build-validated**: `libretro-docs-nano_j2me.patch`
   (5 files, +229 lines) and `libretro-super-nano_j2me-info.patch` (one added
   file) were generated inside clean shallow clones of `libretro/docs` master
   `d83b34b21b8a2c14db573aea66d876e343d316ca` and `libretro-super` master
   `fb15e630655375a50317be2a7953477ef9a5d12a`, and each was re-checked with
   `git apply --check` on a reset checkout. The docs patch adds the library
   page with upstream front matter plus every index update: `mkdocs.yml` nav
   entry under `Mobile Emulation`, a `core-list.md` row, a `licenses.md` row
   (MIT code + OFL font data + Apache AMR data) and a `see-also.md` `## J2ME`
   section listing FreeJ2ME, FreeJ2ME-Plus, nano_j2me and SquirrelJME. With the
   patch applied, `mkdocs build` (mkdocs 1.6.1 with the repository's
   `docs/requirements.txt`) succeeds in 8.68 s and renders
   `site/library/nano_j2me/index.html`; the only warning is the plugin's
   missing-git-log note for the freshly added file. Evidence:
   `.local-tools/debug-runs/submission-20261115/libretro-docs-mkdocs-build.log`.
9. **Wider content sampling with the shipped core**: eight 2D corpus titles
   (超级马里奥, 三国杀, 拳皇98, 街霸2, 仙剑奇侠传, 雷霆战机, 灵异村庄,
   美女滑雪胜地) passed mock-frontend visual checks at 60 Hz cadence over 300
   frames (2-100 distinct sampled frames, content in 314-316/316 frames).
   A 24-title CLI motion sample (`tools/check_corpus_motion.py --count 24
   --frames 300 --seed 21`) reported 24/24 titles changing frames and 0
   static/black titles, and an eight-title WAV-audio sample found 5/8 delivering
   PCM in a 240-frame window (the other three are menu/early-game sections).
   Evidence logs are in `.local-tools/debug-runs/submission-20261115/`
   (`2d-titles-mock-paced.log`, `motion-sample-24.log`, `audio-probe-wav.log`).
   A MIDI re-sample at 600 frames (`--require .mid`) reported 3/8 titles
   delivering PCM (街霸2 86,238; 网球大满贯 1,183,744; 圣百合女子学园
   1,275,810 samples) and 5 silent; the earlier 240-frame window under-reported
   because menu phases had not reached playback (evidence
   `audio-probe-midi-600.log`).

## Remaining external steps (not performed)

- Upstream CI pipeline execution and artifact collection after maintainer
  registration of the repository/core.
- `libretro-super` pull request adding `dist/info/nano_j2me_libretro.info`.
- `libretro/docs` pull request adding the library page, `mkdocs.yml`,
  core-list, licenses and see-also entries, validated with `mkdocs build`.
- Optional: retroarch-assets icons and real database entries for playlists.
- Any acceptance, release timing or Core Downloader decision belongs to
  libretro maintainers.

## Files

- Metadata: `src/port/libretro/nano_j2me_libretro.info`
- CI: `.gitlab-ci.yml`
- Docs draft: `docs/libretro/nano_j2me.md`
- Submission kit: `docs/LIBRETRO_SUBMISSION.md`
- Artifact report: `build/libretro-final-report-20261115.md` (ignored by git)
- Evidence logs: `.local-tools/debug-runs/submission-20261115/` (ignored by git)

## Update, 2026-11-15 (state-load fix and r12 artifact refresh)

While adding `tools/check_libretro_states.py` (corpus-level libretro save/restore
probe), one of twenty sampled titles failed the mock frontend's
`unserialize_after_reset` check: `电子宠物蛋_11972`. Root cause: after
`retro_reset` the fresh VM boots a title whose startup parks in a blocking
native waiting for virtual time; the frontend is blocked inside
`retro_unserialize()` and cannot call `retro_run()`, so the virtual clock stays
frozen, the VM worker never leaves its frame, and the 4 s state-request timeout
expires without the load being served.

Fix: `lr_frame_hook()` advances the virtual clock while a LOAD request is
pending (a load replaces the VM wholesale, so the restored state is unaffected;
save requests never advance the clock). Fifteen added lines in
`src/port/libretro/nj_port_libretro.c`. Documented in `docs/LIBRETRO.md` and
`docs/notes/2026-11-15-libretro-load-state-in-blocking-frame.md`.

Re-checks after the fix:

- `电子宠物蛋_11972` passes `unserialize_after_reset` with the harness restored
  to its original 4-frame post-reset cadence; the 20-title probe is 20/20.
- Mock frontend matrix 20/20; real RetroArch 1.22.2 LoopDemo 120 and TextDemo
  180 frames PASS; 浴火银河2完整版 3D title through the mock frontend 36/36;
  Windows Wine 4/4.
- Full CTest 183/183 in 328.80 s.
- All eight release targets rebuilt at `dd39c1c` (`--out
  /var/tmp/nj2me-submission-dist-r12`), `.info` byte-identical, tree copied to
  `build/libretro-dist/`; CI template simulation re-run for all three templates
  (Linux 3,969,632 B, Windows 3,537,295 B, Android arm64 11,543,776 B).
- Commit `dd39c1c` pushed to origin/main.

## Update, 2026-11-15 (save clock assistance and r13 artifact refresh)

A 30-title libretro visual probe through the mock frontend (sample chosen from
the r9 sweep rows, `--frames 240 --pace-us 16667 --visual-required --static-ok
--min-nonblack 8`, seed 13) passed 29/30. The failure was
`僵尸危机2_4349`: `serialize_writes_state: retro_serialize(140816448) returned
0` with the core logging `save-state request refused (-8)`.

Root cause: this title runs a `callSerially` Runnable at the frame boundary that
busy-waits on `System.currentTimeMillis()`. While the frontend is blocked inside
`retro_serialize()` it cannot call `retro_run()`, so the virtual clock stays
frozen; the worker spins inside one `nj_midp_frame()` (gdb `SIGSTOP` capture;
about 300,000 `port_idle` calls in 30 s) and the request times out after 4 s.

Fix: while a SAVE request is pending, `lr_idle()` and `lr_frame_hook()` advance
the virtual clock by at most `LR_STATE_CLOCK_MAX_FRAMES` (300 frames, about 5 s)
so the frame can finish and the snapshot is taken at the next boundary. The
snapshot may include that much additional game time; the timeout and refusal
fallback are unchanged. Documented in `docs/LIBRETRO.md` and
`docs/notes/2026-11-15-libretro-save-while-paused.md`.

Re-checks after the fix (commit `db276f8`):

- `僵尸危机2_4349` mock frontend: 35 checks, 0 failed (1 skipped by
  `--static-ok`); `电子宠物蛋_11972` load-after-reset still 34/34.
- `tools/check_libretro_states.py` 20-title probe: 20/20 round trips.
- 30-title visual probe: 30/30 (was 29/30).
- Mock frontend matrix 20/20; real RetroArch 1.22.2 LoopDemo 120 and TextDemo
  180 frames PASS; Windows Wine 4/4.
- Full CTest 183/183 in 327.03 s.
- All eight release targets rebuilt at `db276f8` (`--out
  /var/tmp/nj2me-submission-dist-r13`), `.info` byte-identical, tree copied to
  `build/libretro-dist/`; CI template simulation re-run for all three templates
  (Linux 3,969,632 B, Windows 3,537,295 B, Android arm64 11,543,984 B).

## Update, 2026-11-15 (r14: media diagnostics and wider probes)

Wider evidence and diagnostics were added on top of the save clock assistance:

- Level-3 `media: started Player ...` diagnostic in `player_start`
  (`src/midp/nj_lib_media.c`), so audio probes can separate "the title created a
  Player but never started it in the sampled window" from "the title never
  created a Player". `tools/check_corpus_audio.py` now passes `--verbose 3` and
  reports `started=N` with `sound` / `silent-started` / `silent-nostart`
  classes. Re-running the MIDI sample at 600 frames: 3 titles delivered audio,
  the 5 silent ones are all `silent-nostart` (two create the built-in MIDI
  renderer but never start it; three never create a Player). A broader 12-title
  600-frame sample: 9 delivered audio, 3 `silent-nostart`.
- A known remaining save limitation was isolated and documented: a save
  requested while the frontend is paused cannot be served when the current frame
  is a nested native paint that waits on the virtual clock
  (`火影忍者汉化版_10735`). gdb capture shows `k.paint(...)` sleeping with
  `in_c_call=1`, `in_paint=1`, so `nj_vm_can_save()` refuses and the
  between-frames serve point is never reached; throwing more virtual frames at
  it only slows the nested pump. Recorded in `docs/LIBRETRO.md` and the
  "Known remaining case" section of
  `2026-11-15-libretro-save-while-paused.md`. The 50-title visual probe result is
  49/50 on this title and 0 visual failures elsewhere.
- r14 re-validation (commit `30e9c83`): mock frontend matrix 20/20; real
  RetroArch 1.22.2 LoopDemo 120 and TextDemo 180 frames PASS; Windows Wine 4/4;
  CI template simulation all three contracts PASS
  (`/var/tmp/ci-sim-r14`, Linux 3,969,648 B, Windows 3,537,310 B, Android arm64
  11,546,848 B); 20-title libretro state probe 20/20; full CTest 183/183 in
  332.37 s.
- All eight release targets rebuilt at `30e9c83` (`--out
  /var/tmp/nj2me-submission-dist-r14`, `.info` byte-identical) and the tree
  copied to `build/libretro-dist/` (previous tree backed up to
  `/var/tmp/libretro-dist-20261115-r13-backup.tgz`).

## Update, 2026-11-15 (long-run and option-matrix probes)

Two wider probes were added after the r14 refresh, with tracked tools so the
evidence is reproducible; see
`docs/notes/2026-11-15-long-run-and-option-matrix-probes.md` for commands and
raw numbers.

- `tools/check_corpus_deep.py`: titles that the sweep marked `ok` run for 1800
  frames at 30 fps (60 s of emulated time each). 24-title sample: 24/24 passed;
  48-title sample: 46 passed + 2 key-script/menu early exits, 0 failed.
- `tools/check_libretro_option_matrix.py`: eight real titles × eleven
  non-default core-option profiles through the mock frontend: 88/88 passed
  (screen 176x208/320x240, rotation 90/180, heap 32, builtin font, sound off,
  pointer off, Nokia 6230 device, zh-CN locale), each also running the harness's
  save-state round-trip checks.

## Update, 2026-11-15 (r15: corpus rendering fixes)

The whole-corpus libretro rendering probe
(`tools/check_libretro_corpus_visual.py`, 373 sweep-ok titles through the mock
frontend) found six titles painting a black screen. Three real bugs were fixed
(paint waiters held by `callSerially` dispatch, `Displayable` sizes returning 0
before `show()`, and the libretro startup grace being shorter than a slow
loader); details in `docs/notes/2026-11-15-libretro-black-frames.md`. The load
clock assistance is now bounded by the same budget as saves.

Re-validation at revision `88b4e5d`:

- Whole-corpus visual probe: 373 titles, **362 passed, 11 failed**; the
  remaining failures are the documented paused-save limitation, two
  `serialize_after_refused_restore` edge cases, harness watchdog on heavy
  titles, and two titles that need `nano_j2me_device=nokia6230`.
- Mock frontend matrix on the r15 linux core 20/20; RetroArch LoopDemo 120 and
  TextDemo 180 PASS; Windows Wine 4/4; libretro state probe 20/20; full CTest
  183/183 in 344.55 s.
- CI template simulation `/var/tmp/ci-sim-r15`: all three contracts pass
  (Linux 3,969,648 B, Windows 3,540,116 B, Android arm64 11,551,400 B).
- All eight release targets rebuilt at `88b4e5d` (`--out
  /var/tmp/nj2me-submission-dist-r15`, `.info` byte-identical) and the tree
  copied to `build/libretro-dist/` (previous tree backed up to
  `/var/tmp/libretro-dist-20261115-r14-backup.tgz`).

## Update, 2026-11-15 (r16: paused-clock-poll fix)

* Production fix at `e625707`: a paused state request is now served for titles
  that busy-poll `System.currentTimeMillis()` inside a frame. The port assists
  from `lr_port_millis()` only when a paint callback is on the worker stack
  (`nj_vm_in_paint()`) or the presented-frame counter has not moved for 100 ms,
  bounded by 1200 virtual frames; a load restores the pre-request clock when it
  is refused or times out. An unconditional assist was rejected because it made
  timer-driven titles (the football managers) hit `OutOfMemoryError`.
* Validation at the r16 artifacts: whole-corpus rendering probe 373 titles →
  **370 passed, 3 failed** (two need `nano_j2me_device=nokia6230`, one is a very
  slow 3D loader); libretro state probe 50/50; mock matrix 20/20; RetroArch
  LoopDemo 120 + TextDemo 180 pass; Wine 4/4; CI template simulation Linux /
  Windows / Android all pass; CTest 183/183 in 366.18 s.
* Artifacts: `--out /var/tmp/nj2me-submission-dist-r16`, eight targets, hashes
  in `build/libretro-final-report-20261115.md` and the readiness table in
  `docs/LIBRETRO_SUBMISSION.md`; `build/libretro-dist/` swapped to r16 (r15
  tree backed up to `/var/tmp/libretro-dist-20261115-r15-backup.tgz`).
* Commit: `e625707` "fix(libretro): serve paused state requests that poll the
  clock" (plus this documentation refresh).

## Update, 2026-11-15 (r17: device properties and generic-handset retry)

* Production fix at `011247e`: the port now installs `port.device_property` and
  answers `com.nokia.keyboard.type` (`None`), `com.nokia.mid.ui.version`
  (`1.1`) and `microedition.platform` (`nano_j2me/libretro`) the way the other
  host ports do. `bubblet.jar` called `.equals()` on the first one from its
  constructor and died with `NullPointerException` before its MIDlet started.
  A load that still fails while `nano_j2me_device=generic` is retried once with
  the documented `nokia6230` profile (both attempts logged, profile kept for
  `retro_reset`, explicit selection disables the retry); this fixes
  `1945空战英雄_6105.jar`, which branches on `microedition.platform`.
* Validation at the r17 artifacts: whole-corpus rendering probe 373 titles →
  **372 passed, 1 failed** (only the very slow `毁灭战士3_10598` loader, which
  cannot finish inside the sampling watchdog); libretro state probe 50/50; mock
  frontend matrix 20/20; real RetroArch 1.22.2 clean 600-frame exit; Wine 4/4;
  CI template simulation Linux / Windows / Android all pass in one run
  (3,973,848 / 3,541,251 / 11,555,520 bytes at `/var/tmp/ci-sim-r17/...`);
  CTest 183/183 in 348.03 s.
* Artifacts: `--out /var/tmp/nj2me-submission-dist-r17`, eight targets, hashes
  in `build/libretro-final-report-20261115.md` and the readiness table in
  `docs/LIBRETRO_SUBMISSION.md`; `build/libretro-dist/` swapped to r17 (r16
  tree backed up to `/var/tmp/libretro-dist-20261115-r16-backup.tgz`).
* Commit: `011247e` "fix(libretro): answer device properties and retry
  generic-handset load" (plus this documentation refresh).

## Update, 2026-11-15 (600-frame corpus probe)

* Re-ran the whole-corpus rendering probe at **600 frames per title** (10 s of
  real-time cadence at 60 Hz, up from the 300-frame/5 s default) against the
  r17 core, 10 jobs: **373 titles → 372 passed, 1 failed**. The single failure
  is still `毁灭战士3_10598.jar` (`rc=-1`, the very slow M3G loader that cannot
  finish inside the 240 s probe watchdog); nothing new failed at the longer
  horizon, so the rendering and serialize round-trip checks are stable through
  10 s of play in every other sweep-ok title.
* Evidence: `.local-tools/debug-runs/goal16-libretro-corpus/full-600f.log`
  and the refreshed `visual.jsonl` (373 rows).
* No source change in this step; it is evidence for the submission materials
  only.

## Update, 2026-11-15 (real RetroArch with corpus titles)

* Four real corpus titles were loaded through RetroArch 1.22.2 (not only the
  fixtures) with the r17 linux-x86_64 core and run for 300 frames each: the 3D
  `浴火银河2完整版_7689.jar`, `仙剑奇侠传完美版_11857.jar`,
  `拳皇98终极之战_7534.jar` and `雷霆战机2012BT版_3101.jar`. Every run exited 0
  with the core log markers verified (`running <class> (<game> <version>)` /
  `run ended`); logs `r17-ra-real-<title>.log` in
  `.local-tools/debug-runs/submission-20261115/`.
* This closes the gap between "mock frontend" and "real frontend" evidence:
  content loading, MIDlet start, frame delivery and a clean shutdown all work
  through RetroArch itself for real J2ME titles, not only for the fixture JARs.

## Update, 2026-11-15 (bundle validator and full suite)

* Added `tools/check_libretro_submission_bundle.sh`: one offline command that
  re-checks the whole local submission bundle — source/bundle/release-tree
  `.info` byte-identity, the `libretro-super` patch adding exactly that file,
  the five files touched by the `libretro/docs` patch and the library page's
  front matter, plus optional clean-clone apply and `mkdocs build` via
  `--apply-clones --mkdocs`. It is registered as the
  `check_libretro_submission_bundle` CTest case and needs no network when run
  without clone arguments.
* Full local suite after adding that test: **184/184 passed in 339.51 s**
  (`/tmp/r23-ctest3.log`). Two earlier runs overlapped a concurrent session's
  in-progress interpreter edits and showed transient compile failures in
  `check_threaded_dispatch` / `no_statics`; they cleared once that working tree
  settled, and the final clean run passed all 184.
* The bundle validator itself reports 41 passed, 0 failed when pointed at clean
  `libretro-super` (`fb15e63`) and `libretro/docs` (`d83b34b`) clones with
  `--apply-clones --mkdocs /var/tmp/mkvenv/bin/mkdocs`.

## Update, 2026-11-15 (real-RetroArch corpus batch)

* New maintainer tool `tools/check_libretro_retroarch_corpus.sh`: runs the
  existing single-title real-RetroArch end-to-end check over a set of corpus
  titles serially, keeps one log per title, and prints a PASS/FAIL summary. It
  asserts, per title, a clean RetroArch exit after the requested frame count and
  the core's forwarded `[nano_j2me] running <class> (<game> <version>)` /
  `[nano_j2me] run ended: ...` markers.
* Evidence on the r17 linux-x86_64 release core:
  - eight corpus titles × 600 frames (3D 浴火银河2完整版, 仙剑奇侠传完美版,
    拳皇98终极之战, 雷霆战机2012BT版, 灵异村庄, 超级马里奥, 三国杀单机战功修改版,
    美女滑雪胜地): **8/8 passed**;
  - four of them × 1800 frames (3D, 仙剑, 雷霆战机, 超级马里奥): **4/4 passed**.
  - Total **14/14 runs** with clean exits and verified log markers; logs under
    `.local-tools/debug-runs/submission-20261115/ra-corpus*/`.
* The runs are unpaced because RetroArch's null video driver does not throttle
  (600 frames finish in under a second), so this is an integration check of the
  frontend/core contract, not a wall-clock frame-rate measurement. Real-time
  cadence and pixel/black-frame checks remain covered by
  `tools/check_libretro_corpus_visual.py` (mock frontend, `--pace-us 16667`) and
  `tools/check_libretro_states.py`.

## Update, 2026-11-15 (round 27): rebuild reproducibility check

A rebuild of all eight targets from the r17 tree (no source change since
`011247e`, only documentation commits) was compared with the shipped
`build/libretro-dist` tree:

- Linux x86_64/aarch64/armv7 and all three Android cores reproduce
  **byte-identically** (same SHA-256);
- the two Windows DLLs have identical sizes but differ in exactly six bytes
  (`COFF` timestamp, PE checksum and the linker build-id/debug entry), i.e. the
  PE outputs are functionally identical and not byte-reproducible across
  rebuilds without a `SOURCE_DATE_EPOCH`-style timestamp fix.

So the release table in `build/libretro-final-report-20261115.md` remains the
canonical artifact identity; upstream CI-produced Windows binaries will not
match those hashes byte-for-byte either. This is a property of the PE output,
not of the core sources.

## Update, 2026-11-15 (round 29: requirements audit and r18 revalidation)

All eight targets were rebuilt at HEAD `d10fe50` into
`/var/tmp/nj2me-submission-dist-r18`. Linux and Android cores are byte-identical
to the r17 package (same SHA-256), and the Windows DLLs differ only in PE
timestamp/checksum bytes, so the canonical package stays r17.

Revalidation on that build: mock frontend 20/20, real RetroArch LoopDemo 120
frames PASS and corpus batch 8/8 x 600 frames PASS, Wine 4/4, libretro state
round trips 50/50, whole-corpus rendering probe 372/373 (only the documented
slow 3D loader times out), CTest 184/184 in 369.41 s, CI template simulation
3/3. Evidence under `.local-tools/debug-runs/submission-20261115/`.

The upstream requirement audit in `docs/LIBRETRO_SUBMISSION.md` now maps each
`developing-cores.md` step and each libretro API obligation to its local
evidence; only the two upstream PRs and the maintainer-run CI remain, and both
need explicit authorization.

## Update, 2026-11-15 (round 30): byte-reproducible Windows PE output

The round-27 check left one reproducibility gap: two builds of the same tree
produced Windows DLLs with identical code but six differing bytes.  The cause
was two-fold: the linker writes the build time into the COFF header, and GNU
`strip` (run by the release script on the installed copy) rewrites that header
with the current time when it rewrites a PE file.

Fix (tools only, no core sources):
- `tools/toolchain-mingw-w64.cmake` and `tools/toolchain-mingw-w64-i686.cmake`
  initialise `CMAKE_SHARED_LINKER_FLAGS` with
  `-Wl,--no-insert-timestamp -Wl,--build-id=none`;
- `tools/build_libretro_cores.sh` passes those flags explicitly to the Windows
  targets, appends `-s` at link time when stripping is requested, and skips the
  post-link GNU `strip` for `windows-*` targets (that step was what re-stamped
  the header).

Verification: two independent `--clean` builds of both Windows targets are now
byte-identical, and `objdump -p` shows `Time/Date Thu Jan 1 08:00:00 1970`
(Unix epoch). The resulting r19 DLLs are:

| Target | Size | SHA-256 |
| --- | --- | --- |
| windows-i686 | 3,428,352 B | `ee490673d218829ec8e8ea7ab85c48d70dd4d43cfbc21e35dd5df58f5335e47b` |
| windows-x86_64 | 3,211,776 B | `8bcdbd2ac402808c0325304175f1eeb2e69bcca401a832a52f9872d886543158` |

All eight targets were rebuilt cleanly into
`/var/tmp/nj2me-submission-dist-r19`; Linux/Android hashes equal the r17 table
(byte-identical), the two Windows rows are the values above.  That tree is now
the canonical `build/libretro-dist` (r17 backed up to
`/var/tmp/libretro-dist-20261115-r17-backup.tgz`).  Spot validation on r19:
linux-x86_64 mock-frontend matrix 20/20, real RetroArch 1.22.2 LoopDemo 120
frames PASS, windows-x86_64 DLL under Wine 4/4.

Consequence for submission: release artifacts, including the Windows pair, are
now reproducible from the same source tree and toolchain, so the SHA-256 table
is stable across rebuilds and can be quoted in the upstream PR without the
"Windows differs in metadata" caveat.

## Update, 2026-11-15 (round 31): full validation matrix on the canonical r19 package

The **r19** tree in `build/libretro-dist/` (the byte-reproducible package from
round 30) was run through the full validation matrix:

- mock frontend matrix on `linux-x86_64`: 20 passed, 0 failed
  (`.local-tools/debug-runs/submission-20261115/r19-mock.log`);
- whole-corpus rendering probe: 373 titles at 300 frames → **372 passed,
  1 failed** — only the documented slow `毁灭战士3_10598` loader times out
  (`r19-visual.log`, `goal16-libretro-corpus/visual.jsonl`);
- libretro state round trips: **50/50** (`r19-state50.log`);
- real RetroArch 1.22.2 corpus batch: **8/8 titles x 600 frames passed**
  (`r19-ra-corpus.log`) plus the LoopDemo 120-frame check;
- libretro CI template simulation: **3/3 contracts pass**
  (`/var/tmp/ci-sim-r19`, log `/tmp/r19-ci-sim.log`);
- full CTest on `/var/tmp/nj2me-lrstate-build`: **184/184 in 395.37 s**
  (`/tmp/r19b-ctest.log`).

One investigation this round was reverted and left in `docs/PERFORMANCE.md`
instead of the release: the round-28 M3G fast-span "parity failure" turned out
to come from comparing two separate runs of a title whose loading animation is
not run-to-run deterministic (two identical general-path runs of `毁灭战士3_10598`
differ from frame 120). The span fast path is therefore unproven, not disproven;
the retry needs a deterministic span-level fixture (see the PERFORMANCE.md
section). No release artifact is affected — the campaign touched no shipped
code path and the tree was reverted.

## Update, 2026-11-15 (round 32): M3G span fast path and r20 package

The M3G deterministic-prefix method proposed in the round-31 note worked:
for `毁灭战士3_10598` the first 120 presented frames are run-to-run
reproducible, and the previous build and the fast-path build produce
byte-identical frames over that prefix (13/13 dumped frames at
`--dump-every 10`). The Gouraud + nearest-textured + MODULATE span fast path
was therefore shipped after measuring **-4.6% user CPU** on the 20k-frame
profile run (median of 5 runs each: 29.21 s -> 27.88 s; details in
`docs/PERFORMANCE.md`).

r20 package: rebuilt from a clean worktree at `91392ee` plus the patch into
`/var/tmp/nj2me-submission-dist-r20`, and installed as canonical
`build/libretro-dist` (r19 backed up to
`/var/tmp/libretro-dist-20261115-r19-backup.tgz`). Validation: mock frontend
20/20 on the packaged linux core, whole-corpus rendering probe 372/373 (same
single slow-loader failure; no rendering regression), full CTest 184/184 in
458.09 s, real RetroArch LoopDemo PASS, Wine 4/4. Windows cores remain
byte-reproducible (zeroth COFF timestamp, stripped at link), so the table's
Windows hashes change with the code but are stable across rebuilds.

## Update, 2026-11-15 (round 32b): r21 package with real-time pacing

After the concurrent landing of `4ae7790` (real-time virtual clock and audio
pacing), the canonical package was rebuilt once more from a clean worktree at
`a90831e`: `/var/tmp/nj2me-submission-dist-r21` installed as
`build/libretro-dist`, r20 backed up. The Windows cores remain deterministically
reproducible; Linux/Android hashes changed only because the core code changed.
Validated: mock frontend 20/20, corpus rendering probe 372/373 (unchanged),
RetroArch LoopDemo PASS, Wine 4/4, and a clean-worktree CTest of **181/181 in
364.63 s**.

## Update, 2026-11-15 (round 34): canonical r21 rebuild and r22 hold

The M3G opaque-clear change (`6f61472`) is in the source tree and measured
(README performance note: -1.3% CPU on the profiled title, deterministic
prefix byte-identical, rendering tests 37/37), but it is **not packaged yet**.
Packaging at HEAD exposed an interaction with the concurrent frame-rate
change: with the new default 30 fps cap, pause-time save/load requests can
time out while the MIDlet's frame pacer waits on a virtual clock that a
paused frontend never advances. Evidence: the mock-frontend conformance suite
failed the zero-frame `serialize_writes_state` check 3/3 under load (passes
intermittently on a quiet host), and a whole-corpus rendering run on that
package dropped to 275/373 with every failure being a paused-state serving
check. The r21 source revision `a90831e` (before the 30 fps default) is
unaffected: zero-frame conformance passes and the canonical package below is
rebuilt from it.

The canonical `build/libretro-dist/` was rebuilt cleanly from `a90831e` and
revalidated: mock frontend matrix 20/20, RetroArch LoopDemo 120 frames PASS,
Windows x86_64 under Wine 4/4, zero-frame save/load conformance PASS.
Byte hashes:

| linux-x86_64 | 3827480 | `8ad3bf7ac27e80752d92f3c3c9a846b4842b14ed2fdd3dd62ac12e4274f1bb9f` |
| linux-aarch64 | 3308328 | `76965a0ccc945bd6e70ef1ef29e24ba3ea560cea5a82cb75cccdd16970717b07` |
| linux-armv7 | 3016728 | `bfdc12d17d1a0555af0569b98173bad3c1a07b24218f6505510a8a8fdcc02726` |
| android-arm64 | 3537360 | `56ff856b3479c8779a788d0380cf2b323ab1453d6c4924a08b1b9e1447543d7b` |
| android-armv7 | 2726152 | `cbfcb14fa7cfd59b0f2ac339b768560332c2eae42c806db237ee0ee608e5d427` |
| android-x86_64 | 3583512 | `bdda0f21705a8a7a7ddf4272ae4ff164c762f6b3b64282717abcb571ae83c126` |
| windows-i686 | 3434496 | `03980172fa20cf754747831439f387d168a7e746333df8fc53e11bea576ef430` |
| windows-x86_64 | 3213312 | `a450a4dab82aee289bd842392919bc026616f61af14143b92880bec8e45abf47` |

Note on hashes: a local rebuild is not guaranteed bit-identical to an earlier
local build of the same revision (toolchain and build-directory inputs differ),
so the table above is the authoritative set for the package currently in
`build/libretro-dist/`. Upstream libretro artifacts are produced by the
project's own CI/buildbot from the submitted commit, not from these local
files.

The paused-state robustness issue is the next priority: the frame pacer must
not be able to strand a pending state request (candidate: skip pacing while a
request is queued, or bounded clock assistance in the pacer wait, gated like
the round-27 experiment).

## Update, 2026-11-15 (round 34b): r22 released after the paused-state fix

The r22 package was held earlier in this round because the concurrent 30 fps
default stranded pause-time save/load requests. The fix (`14752c9`) adds
`nj_port.state_request_pending`: the libretro port sets it while a state
request waits for the frontend, and `frame_pace()`/`frame_pace_wait()` skip
pacing meanwhile, so the runtime reaches the next frame boundary and serves
the request. Only frames during which a request is already queued are
affected; normal playback and the frame-rate cap are unchanged.

The package was rebuilt cleanly from `14752c9` and validated under heavy
concurrent load (full CTest running in parallel with a 10-job corpus probe):

- mock frontend matrix 20/20;
- real RetroArch LoopDemo 120 frames PASS (core log markers verified);
- Windows x86_64 under Wine 4/4;
- whole-corpus rendering probe: 372/373, the only failure being the
  documented slow `毁灭战士3_10598` loader, with no paused-state failure
  (before the fix the same run produced 98-120 paused-state failures);
- full CTest: 181/181 in 332.23 s.

Canonical `build/libretro-dist/` (rebuilt from `14752c9`):

| target | size (bytes) | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3827480 | `fb14d1caa77324da5873f752e05385de5fb6b390edae5bbb700594cbb63c365c` |
| linux-aarch64 | 3309664 | `b8f89a528198a2ee6a49d99d4414d5531fc9e1786ae09d705ec701cea4fb89b9` |
| linux-armv7 | 3017748 | `3b98467d953a97286de58d20cfa47fa28f1a44f643fb5aaab7afdbcfb0071a98` |
| android-arm64 | 3539256 | `5dd5bce780ff4d30621b37028f152b2d1ebfe2b20d327db0eae1813e333b3a12` |
| android-armv7 | 2727236 | `dd876d8e5b50d136d8ad1cfe09b4245501aa9cda6a547349b4b67a6318f6b252` |
| android-x86_64 | 3585488 | `5fc4123b1082556e8632a3797458034ce4ddb7eb3521a60a563d361d3e4002ec` |
| windows-i686 | 3437568 | `9694297d3148e9297cd1a3022fcb4655e72de7410d0e7c3cc71c733629382c58` |
| windows-x86_64 | 3214336 | `52a7d92059b9705156c3100385284a367308ee33c8eb426c57540abcea0e235e` |

`.info` unchanged (`c0f30aea…`). The older r21 round-34 note above remains as
history; this r22 section supersedes it for the package identity.

## Update, 2026-11-15 (round 35): end-to-end submission rehearsal

Upstream baselines were re-checked and are unchanged since the patches were
generated: `libretro/docs` master is still `d83b34b2` (2026-11-07) and
`libretro-super` master is still `fb15e63` (2026-11-14), so both patch files
apply as published.

Rehearsal run from the current tree (`c20e752`, the r22 release revision):

- offline bundle validator with clean clones and a real docs build:
  `tools/check_libretro_submission_bundle.sh --super-clone /var/tmp/lr-super
  --docs-clone /var/tmp/lr-docs --apply-clones --mkdocs
  /var/tmp/mkvenv/bin/mkdocs` -> **41 passed, 0 failed** (resets both clones
  to the baselines, applies both patches, checks the five touched docs files,
  the `.info` payload and front matter, and renders the library page);
- fresh `git clone` of `origin/main`: the exact libretro CI template configure
  and build (Release, `-DNJ_BUILD_LIBRETRO=ON`, `-DNJ_BUILD_TESTS=OFF`, no
  zlib package) produces a linux libretro core from a clean checkout; that
  clone-built core passes the mock-frontend conformance matrix **20/20**, and
  the bundle validator passes 16/16 in the clone (source/bundle `.info`
  identity path);
- CI template simulation at the release revision:
  `tools/simulate_ci_builds.sh --out /var/tmp/ci-sim-r22` -> all three
  contracts satisfied: linux 3,978,112 B, windows 3,545,006 B, android arm64
  11,579,752 B (template rename to `nano_j2me_libretro_android.so`).

What remains external is unchanged and needs explicit authorization: opening
the two upstream pull requests (info file into `libretro-super`, docs page and
index edits into `libretro/docs`), and letting the maintainers' buildbot
produce the official artifacts.

## Update, 2026-11-15 (round 39): r23 package with the cheat-search memory window

r23 differs from r22 by one commit (`d2ed8d8`): the JVM arena is now exposed
as `RETRO_MEMORY_SYSTEM_RAM` so a frontend cheat search can scan and patch it.
The heap is a single non-moving arena (objects are never relocated) reserved
in full at VM creation, so the base pointer is stable for the VM's lifetime.
`retro_cheat_set()`/`retro_cheat_reset()` remain no-ops -- libretro cheat
codes cannot describe a managed heap -- and `.info` `cheats = "false"` is
unchanged (honest about code-based support). The window is raw and
unsynchronised: a write bypasses the Java memory model and can corrupt
objects, so it is a deliberate user action (frontend "search and write"), not
a save channel; `VIDEO_RAM`/`SAVE_RAM`/`RTC` still report NULL/0.

Artifacts built from a clean tree at `d2ed8d8` into
`/var/tmp/nj2me-submission-dist-r23` and installed as canonical
`build/libretro-dist/` (the previous shared tree was backed up to
`/var/tmp/libretro-dist-20261115-concurrent-1831-backup.tgz`; the canonical
r22b tree is still kept at `/var/tmp/nj2me-submission-dist-r22b`):

| target | size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3827488 | `c70d3a437427b95bea7cd5c2ca63d40c75239e5f92d73751351dffcd8e5df514` |
| linux-aarch64 | 3310936 | `d6e7d40fd87cf2f6f2a5fad9d5ec9ab8cff15063468b03b53c78718add7017dd` |
| linux-armv7 | 3018856 | `770cfd87b9f20fb302873d9ca4792150448153788b4b0ffd22e7fee15fc1a8a0` |
| android-arm64 | 3540688 | `d01679423777da75121dfe97c997b38a6c3842d94ae1a6b79c8a8a4b8e945c00` |
| android-armv7 | 2728072 | `9cdd205eee8eefe7a275106cf6c3a7068482a045b9f6c5afc115b446a86f0246` |
| android-x86_64 | 3586856 | `316f258e1116e6b6bebacb24ab4bb66aeca7993793846f5aa3f5c39136c1d867` |
| windows-i686 | 3439616 | `89780270b7c391fb52d317eab716c6af283b4285841c6e3381c2be53ccc84361` |
| windows-x86_64 | 3216384 | `429aaf66548cecf614cc9ed3beea6480290e0eec3f4320a899024b860fc449a1` |

`.info` unchanged (`c0f30aea...`). The Windows DLLs remain byte-reproducible
(PE timestamp epoch, no post-link `strip`).

Validation on r23: mock frontend matrix **20/20**; real RetroArch 1.22.2
LoopDemo 120 frames PASS; Windows x86_64 under Wine **4/4**; whole-corpus
rendering probe **372/373** at 300 frames (the only failure is the documented
slow `毁灭战士3_10598.jar` loader); libretro state round trips **50/50**; full
CTest **184/184 in 336.94 s** from the same source tree (that run includes the
new memory-window checks: SYSTEM_RAM non-NULL, size in range, unknown ids
NULL/0, pointer and size stable across frames).

## Update, 2026-11-15 (round 40): r24 package with the arena memory map

r24 adds `RETRO_ENVIRONMENT_SET_MEMORY_MAPS` on top of r23: the core publishes
the JVM arena as a single `RETRO_MEMDESC_SYSTEM_RAM` descriptor (same
non-moving arena as the `retro_get_memory_data(SYSTEM_RAM)` window). Frontends
prefer the map for cheat search and memory viewers, and it is republished
after every state load because a load replaces the VM. `.info` now declares
`memory_descriptors = "true"` (previously `"false"`); the `.info` file is
therefore `740df7353c181bd76955f04a64508c4c1d1994509c1b4290b3b420b04da424e1`
and the prepared `libretro-super` patch was regenerated from it (still one
added file, still applies cleanly to `fb15e63`). The mock frontend gained
checks for the map contract (published, one SYSTEM_RAM descriptor, pointer and
length match the data window, republished after a restore).

Artifacts built from a clean tree at `d2ed8d8` + this change into
`/var/tmp/nj2me-submission-dist-r24` and installed as canonical
`build/libretro-dist/` (r23 tree kept at `/var/tmp/nj2me-submission-dist-r23`):

| target | size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3827488 | `d9feff13199f659cfc36e93e5fe4268d29bb3c8686e38f43bc38541a1899e56e` |
| linux-aarch64 | 3311256 | `fa14b7ff96d45fac82d38de8f042001b72da6eae4035844aad68c497a937a52f` |
| linux-armv7 | 3019208 | `79b0c7088bc5dd77112b21738b4f0d36bf6c1623138592b134588d2c89291d26` |
| android-arm64 | 3541296 | `7155c0ac69263d6aac4120eddcc417ef2437c76dc4adb472c1deb59477e08c3a` |
| android-armv7 | 2728408 | `c711a57b26ce2af3eeec8173a1482222f7da62de7a3ca81a286c9cbe273c0f8f` |
| android-x86_64 | 3587544 | `ee088275b662a47b20c9de14740b36072958386908e060c98017243af0a1b4d7` |
| windows-i686 | 3440128 | `1836e67609627420227be13040bacdb65905866c137210f79cf3df7ba1dc3873` |
| windows-x86_64 | 3216896 | `0d3095d6094754751a65c8464fe9c0ddaf05043311c912494816e0180154ab81` |

`.info` `740df735...` identical in all eight targets.

Validation on r24: mock frontend matrix **20/20** (now 45 checks per fixture
including the memory-map contract); real RetroArch 1.22.2 LoopDemo 120 frames
PASS; Windows x86_64 under Wine **4/4**; whole-corpus rendering probe
**372/373** at 300 frames (only the documented slow `毁灭战士3_10598.jar`);
libretro state round trips **50/50**; full CTest **184/184 in 339.43 s**;
offline submission-bundle validator **41 passed, 0 failed** with clean clones
and a real `mkdocs build` (and **32/32** including the release-tree `.info`
identity).

## Update, 2026-11-15 (round 41): frontend rewind measured and checked

Rewind was previously documented as impractical without measurements. With
the current core it is a sizing question, not a functional blocker: RetroArch
feeds its rewind ring buffer from `retro_serialize()`, and the snapshot size
tracks the Java heap budget.

Measured with the r24 linux-x86_64 release core, RetroArch 1.22.2 headless
(null drivers), loopdemo, 600 frames, `rewind_buffer_size = 64` (RetroArch
reports a 67 MB buffer):

| heap option | snapshot size | rewind result |
| --- | --- | --- |
| `nano_j2me_heap=16` | 23,375,936 B | buffer initializes, granularity 2 runs clean in ~2.5 s (0.5 s without rewind); history fits |
| `nano_j2me_heap=16`, granularity 60 | 23,375,936 B | clean in ~0.5 s (sparse snapshots) |
| auto (128 MB here) | 140,816,448 B | clean exit but frontend logs "rewind buffer capacity insufficient" -- no history stored |

So rewind is usable with `nano_j2me_heap=16` and a buffer of at least twice
the snapshot size; history fidelity stays VM-only (external RMS/files are not
rewound and native media handles are lost on restore), so it is documented as
a convenience rather than a validated guarantee. New tracked check
`tools/check_libretro_rewind.sh` encodes the working setup and fails if the
buffer does not initialize or the frontend reports a capacity error. The
helper `tools/check_libretro_retroarch.sh` gained an `EXTRA_CFG` environment
hook so any extra `retroarch.cfg` lines (rewind, runahead, ...) can be tested
without editing the script.

## Update, 2026-11-15 (round 42): metadata re-scan and bundle revalidation

Re-ran the metadata and bundle gates against the r24 release metadata
(`memory_descriptors = "true"`, `.info` SHA-256 `740df7353c181bd76955f04a64508c4c1d1994509c1b4290b3b420b04da424e1`):

- RetroArch 1.22.2 core-info scan with `libretro_info_path` pointing at the
  fresh metadata (null menu/video/audio/input drivers) exited 0 and logged
  `[INFO] [Core info] Wrote to cache file: "/tmp/coretest42/info/core_info.cache"`,
  so the new `memory_descriptors` field parses cleanly.
- `tools/check_libretro_submission_bundle.sh` -> **32 passed, 0 failed** with
  the r24 release tree.
- Whole-corpus headless revalidation on the same tree: 371/404 ok with the two
  adjudicated timing diffs recorded in
  `docs/notes/2026-11-14-corpus-regression-sweep.md` (no emulator regression).

## Update, 2026-11-15 (round 43): the submission kit reproduces from a clean public clone

Goal: prove that everything needed to submit builds from what upstream will see
(a fresh clone of `git@github.com:lucinhu/nano_j2me.git`), not from local
untracked state.

- Clone: `git clone --depth 1 git@github.com:lucinhu/nano_j2me.git` at HEAD
  `3684016`; `.gitlab-ci.yml`, `src/port/libretro/nano_j2me_libretro.info`,
  `tools/simulate_ci_builds.sh` and `docs/libretro/upstream/` are all tracked
  and present.
- `tools/simulate_ci_builds.sh --out /var/tmp/r43-clone-ci --jobs 8` inside the
  clone: **all three upstream CI template command shapes satisfied** -- linux
  `libretro/nano_j2me_libretro.so` 3,978,184 B, windows
  `libretro/nano_j2me_libretro.dll` 3,546,661 B, android
  `libretro/nano_j2me_libretro.so` 11,588,344 B (the android template renames it
  to `nano_j2me_libretro_android.so`).
- `tools/check_libretro_submission_bundle.sh` inside the clone: **16 passed,
  0 failed** for the bundle-only checks (gitignored fixtures and the release
  tree are absent in a clean clone, as designed); with
  `--dist /var/tmp/nj2me-submission-dist-r24` it reports **32 passed, 0 failed**.
- Clone-built linux core: real RetroArch 1.22.2 `loopdemo` 120 frames PASS with
  the `running` / `run ended` core markers verified; whole-corpus visual probe
  with that core = **370 passed / 1 failed out of 371 sampled** (the single
  failure is the documented very-slow `毁灭战士3_10598.jar` loader). The mock
  matrix reports 6 passed / 11 skipped because the gitignored fixture JARs are
  not part of the public tree -- expected, not a build problem.
- Conclusion: the libretro submission kit (core, metadata, CI config, patches,
  docs page) is reproducible from the public clone alone; no local-only file is
  required to apply the two upstream patches or to build all eight targets.

## Update, 2026-11-15 (round 47): upstream library page refreshed for the new features

The copy-ready `libretro/docs` page was stale on two points that the core now
implements:

- exposed memory: the page now says `SYSTEM_RAM` publishes the JVM arena as a
  memory map (for frontend cheat search) and that there is no `SAVE_RAM`;
  a short "Cheat search memory" section explains the raw-window semantics,
  the `nano_j2me_heap` sizing and the re-publish after a state load;
- rewind: the feature table no longer says "not advertised or validated"; it
  describes the measured sizing rule (16 MB heap fits a normal frontend
  rewind buffer, auto-heap states do not) and keeps the no-determinism claim.

`libretro-docs-nano_j2me.patch` was regenerated from the same clean
`d83b34b2` clone: 5 files, **+229 lines** (the library page is 219 lines).
Validation: `git apply --check` on a clean clone passes, `mkdocs build` renders
`site/library/nano_j2me/index.html` (97,916 B) with rc=0, and
`tools/check_libretro_submission_bundle.sh --super-clone /var/tmp/lr-super
--docs-clone /var/tmp/lr-docs --apply-clones --mkdocs
/var/tmp/mkvenv/bin/mkdocs` passes all checks.

## Update, 2026-11-15 (round 48): real-frontend save-state round trip

New tracked tool `tools/check_libretro_retroarch_state.sh` checks the path a
user actually takes: launch 1 plays with `savestate_auto_save = "true"` so
RetroArch writes the state on unload; launch 2 runs with
`savestate_auto_load = "true"` and must log
`[nano_j2me] resumed from a save state`.

Measured with RetroArch 1.22.2 and the canonical r24 linux-x86_64 core
(sha256 `d9feff13...`): PASS. The raw loopdemo snapshot is 140,816,472 bytes
(auto heap) and RetroArch's `.state.auto` file is 170,087 bytes, i.e. the
frontend compression makes even the auto-heap states practical on disk. The
check is documented in `docs/LIBRETRO.md` (save-state paragraph and maintainer
verification block).

## Update, 2026-11-15 (round 51): ready-to-post PRs, clean-clone rerun

- Added ready-to-post PR descriptions next to the patches:
  `docs/libretro/upstream/PR-libretro-super.md` and
  `docs/libretro/upstream/PR-libretro-docs.md` (titles, baselines, exact
  commands, bodies, checklists). Opening the PRs now needs no rewriting.
- Fresh `git clone --depth 1` of `origin/main` at `52752b0`: CI template
  simulation 3/3 PASS (linux 3,978,184 B / windows 3,546,661 B / android
  11,588,344 B); bundle validator 16 passed in the clone and 32 passed from
  the main tree with `--dist /var/tmp/nj2me-submission-dist-r24`; clone-built
  Linux core passed the mock matrix 20/20 and a real RetroArch run.
- Still external, still unauthorized: opening either PR and letting upstream
  CI build/register the core.

## Update, 2026-11-15 (round 52): r25 candidates built, release held

Two r25 candidates were built from clean detached worktrees (fresh worktrees
needed `tools/build_libretro_cores.sh` to create its gitignored `build/` log
directory; that fix is commit `43a38b3`), and both were validated. Neither was
installed as the canonical `build/libretro-dist`; r24 stays canonical.

Candidate A, tree `a296ce8` (hardening only), kept as
`/var/tmp/nj2me-submission-dist-r25-a296ce8`:

| target | size | sha256 |
|--------|------|--------|
| linux-x86_64 | 3,839,800 | `b708ed57…` |
| linux-aarch64 | 3,322,920 | `c1413080…` |
| linux-armv7 | 3,030,604 | `3bc5916d…` |
| android-arm64 | 3,555,592 | `74df6ed7…` |
| android-armv7 | 2,743,548 | `78571561…` |
| android-x86_64 | 3,595,824 | `444543fe…` |
| windows-i686 | 3,451,392 | `c0edf5f2…` |
| windows-x86_64 | 3,226,624 | `eb285860…` |

Candidate B, tree `43a38b3` (hardening + `a2abdee` async construction), kept as
`/var/tmp/nj2me-submission-dist-r25`:

| target | size | sha256 |
|--------|------|--------|
| linux-x86_64 | 3,843,896 | `80ed47e8…` |
| linux-aarch64 | 3,323,832 | `1052b596…` |
| linux-armv7 | 3,031,724 | `fbdafb4b…` |
| android-arm64 | 3,556,584 | `29bd43a4…` |
| android-armv7 | 2,744,492 | `a7c9457e…` |
| android-x86_64 | 3,596,848 | `a2489bf6…` |
| windows-i686 | 3,451,392 | `5bd016b8…` |
| windows-x86_64 | 3,226,624 | `da391cc2…` |

`.info` is unchanged in both (`740df7353c181bd76955f04a64508c4c1d1994509c1b4290b3b420b04da424e1`).
Validation on both: mock matrix 20/20, real RetroArch LoopDemo 120 frames PASS,
Windows x86_64 under Wine 4/4, corpus visual probe 370/371 passed plus the one
known Doom3 loader limitation and 0 failures. Candidate A's libretro state
probe is 50/50; candidate B's is 49/50 because of the early-save regression
described in `docs/notes/2026-11-15-libretro-async-load-early-state.md`.

Release decision: hold r25, keep r24 canonical, and revisit after the
async-load/state path settles (the same area is being iterated as `a2abdee` /
`68bffb9`). The visual probe for both candidates ran against the private core
copies, so the shared-tree rebuild the concurrent session does cannot skew it.

## Update, 2026-11-16 (round 52b): r25 released

The r25 package built from `43219e6` is installed as the canonical
`build/libretro-dist` (previous mixed tree backed up to
`/var/tmp/libretro-dist-20261115-r24-backup.tgz`). It carries the malformed
bytecode array-operand guards, the GIF canvas budget, the async constructor
load path, and the `SYSTEM_RAM` memory map. Validation: mock 20/20, real
RetroArch LoopDemo 120 PASS, Wine 4/4, corpus visual 370/371 + 1 known Doom3
loader limitation, state probe 50/50 paced, full CTest 189/189 in 349.38 s.
Hash table and the early-save caveat are in `docs/LIBRETRO_SUBMISSION.md`.

The earlier "held" decision is superseded: the unpaced state-probe failure was
diagnosed as a frontend-cadence artifact of the async load (see
`docs/notes/2026-11-15-libretro-async-load-early-state.md`), the state probe now
paces frames like a real frontend, and the recommended `load_ready` gate is
recorded for the load/state owner rather than blocking the release.

## Update, 2026-11-16 (round 53): r25 validation refresh

No core change; r25 (tree `43219e6`) stays the canonical package. Re-ran the
submission checks and extended the adversarial campaigns:
`docs/notes/2026-11-16-r25-validation-and-fuzz-campaigns.md` has the table
(CTest 189/189, option matrix 88/88, real RetroArch corpus 8/8, 600-frame
visual probe 370/371 + 1 known limitation, state probe 50/50, 5000-case
class/JAR/media fuzz all clean). The only remaining external steps are still
the upstream CI run and the upstream PRs, both waiting for explicit
authorization.

## Update, 2026-11-16 (round 54): 0.5.8 release and upstream PRs

The author authorized upstream submission, and the version moved to 0.5.8:

- Release package r26 built from clean tree `52c04a0`, eight targets, `.info`
  `48ceccf6fc7110a605809c90877a4e16d57ff5e0275503c88ac1f6385790b791`
  (`display_version = "0.5.8"`); canonical copy in `build/libretro-dist/`;
  GitHub release `v0.5.8` with `nj2me-0.5.8-libretro-cores.zip`.
- Validation: mock 20/20; real RetroArch alphademo 300 frames PASS; Wine 4/4;
  corpus 372 ok / 27 title-exit / 2 title-exception / 2 no-frame / 0 timeout /
  1 bad-jar; full serial CTest 189/189 in 357.49 s.
- Upstream PRs opened from forks:
  `libretro/libretro-super#2203` (branch `add-nano_j2me`, commit `4ec537f`)
  and `libretro/docs#1239` (branch `add-nano_j2me`, commit `fc6c6c2`).
  Remaining external steps: upstream CI and review.

No upstream merge or acceptance is claimed.

One rebase was needed after the first submission: `libretro/docs` master moved
to `177c669990b659e7cfd00add4564b85c2a692179`, so the docs fork branch was
rebased, the patch regenerated and `mkdocs build` re-verified. The pages above
describe the original `d83b34b2` generation; the current patch is valid against
`177c6699`.

## Update, 2026-11-16 (round 55): 0.5.9 patch release for the state-buffer bound

The save-state buffer bound was too small for object-dense titles: a title that
fills its 128 MB auto heap could not save at all, because `retro_serialize()`
silently rejected the buffer while the core printed nothing
(`docs/notes/2026-11-16-state-buffer-bound.md`). The fix sizes the metadata
allowance from the arena (`4 MB + thread stacks + heap/8`, capped at half the
arena) and makes the writer refuse explicitly instead of returning an unusable
blob.

- `MinecraftJ2ME_PE_0.6.1_V2.2.jar` now saves and restores: mock harness
  45/45 checks, `serialize_size = 157,593,664`.
- Full serial CTest: **190/190 in 357.09 s**.
- 60-title libretro state probe with the fixed core: 60/60 round trips, 0 failed.
- Version moved to **0.5.9**; release package r27 built from clean tree
  `2efd2c7` (eight targets, `.info`
  `8cd968c9bcab94c798b62e6ac3467d5f1d0c1117a10b8fd6113347bc45c33c97`,
  `display_version = "0.5.9"`), canonical copy in `build/libretro-dist/`,
  GitHub release `v0.5.9` with the eight-core zip.
- The two upstream PR branches were updated to the 0.5.9 `.info` file.

## Update, 2026-11-16 (round 64): 0.5.11 package (r30) and validation

0.5.11 ships the pacer-owned idle nap (paced runs no longer burn a CPU core;
`docs/PERFORMANCE.md`). The r30 package was built from clean tree `ca0e43d`:
`.info` `67048b11bdefad3448aab34b83d42fe32ec2a1d3bd149ac7b617e38356c46d0e`
(`display_version = "0.5.11"`), eight targets installed as the canonical
`build/libretro-dist/`. Validation: mock frontend 20/20; real RetroArch
loopdemo 120 frames PASS; Wine 4/4 at 120 and 300 frames (one Wine-timing flake
on the 120-frame audiodemo audio sample out of seven runs, 300 frames 3/3);
Minecraft PE state round trip 45/45; option matrix 88/88; state probe 12/12;
full CTest 192/192; corpus sweep 372 ok / 27 title-exit / 2 title-exception /
2 no-frame / 0 timeout / 1 bad-jar. Release archive SHA-256
`86babcd96fc0e66203aa7516a714bada81dec995840d74c2f733397197f07992`;
GitHub release `v0.5.11`. PRs #2203/#1239 refreshed with the 0.5.11 `.info`;
no merge claimed.

## Update, 2026-11-16 (round 71): 0.5.12 release with multi-touch

The r31 package from tree `21a79f7` (multi-touch `12d3fa4` + 0.5.12 metadata)
is installed as `build/libretro-dist/`. `.info` SHA-256
`288a1aa423b16e46f4f397d9b86959b31bd6e01a45af22b71216d2d70850db73`
(`display_version = "0.5.12"`); the libretro-super patch was regenerated with
`git apply --check` against `fb15e63`.

Validation: mock frontend matrix 20/20 on the Linux core; option matrix 66/66
(6 titles x 11 profiles); real RetroArch 1.22.2 loopdemo 120 frames PASS;
Windows x86_64 under Wine 4/4 at 300 frames; Android APKs built at 0.5.12
(`nj2me.apk` 3,275,114 B, `nj2me-legacy.apk` 701,372 B); full CTest 193/193 at
the multi-touch tree; 404-title corpus sweep 387 ok / 12 title-exit / 2
title-exception / 2 no-frame / 1 bad-jar.

Release archive `nj2me-0.5.12-libretro-cores.zip` SHA-256
`7467092702a939b7c0dc16c3d3a025fab5d8f41337c5eb48f59eb0d451d7d58b`;
GitHub release `v0.5.12` (also ships the two APKs). PRs #2204/#1239 refreshed
with the 0.5.12 `.info`; no merge claimed.

## Update, 2026-11-16 (round 73): libretro multi-touch gate and state sample

- New CTest `check_libretro_multitouch`: the mock frontend answers
  `RETRO_DEVICE_ID_POINTER_COUNT=2` with independent per-index coordinates
  (`--multitouch-check`) and the gate runs `tests/fixtures/multitouchdemo.jar`
  through the real core; the fixture must print `MPT id=0` and `MPT id=1`
  lines for both fingers while `CANVAS down` appears exactly once. 47 checks,
  0 failed; full suite is now 194/194.
- Paced libretro state probe on the shipped r31 Linux core (private copy,
  SHA-256 `a5e7b8ec…`): 100 sampled titles, 120 frames, seed 41 → **99/100
  round trips**. The single failure is the known `毁灭战士3_10598.jar` slow
  loader (the 120-frame window ends before its 190-pass software-M3G loading
  animation finishes, so a state request is refused by design) — no real
  save/restore failure among the sampled titles.
- The other three Galaxy on Fire 2 releases now render 3D in probes with the
  same dismissal key script (`m3g` 119k–223k calls over 600 frames), see
  `docs/notes/2026-11-16-corpus-keys-and-sweep.md`.

## Update, 2026-11-16 (round 74): key scripts reach 3D gameplay through the core

The mock frontend gained `--keys PATH`, which replays the corpus `frame:key`
script as the matching joypad buttons (A=FIRE, X=SOFT_LEFT, B=SOFT_RIGHT and
the arrows) before each `retro_run()`. New CTest `check_libretro_3d_keys` runs
`浴火银河2完整版_7689.jar` through the real core with its dismissal script and
requires an animated, non-black M3G scene: **917 frames, non-black 76800/76800,
183 distinct frames, audio nonzero 1.3M samples**, 47 checks 0 failed, 23.9 s.
The gate skips when the gitignored corpus is absent.

## Update, 2026-11-16 (round 75): pointer taps in key scripts, whole-corpus gameplay probe

The mock frontend `--keys` script now also replays `pressed@X,Y` entries as
real single-pointer events (canvas pixels converted to the centered libretro
axis), so the corpus scripts that tap a loading/billing screen work through the
core ABI. `tools/check_libretro_corpus_visual.py` automatically loads
`tools/corpus_keys/<jar stem>.txt` when present, so menu-gated titles are judged
on their gameplay screen instead of a static menu.

600-frame whole-corpus probe against the r31 core
(`.local-tools/debug-runs/libretro-visual-r75/visual.jsonl`, 10 workers,
paced 60 Hz): **385 titles -> 384 passed, 1 known limitation
(毁灭战士3_10598 slow loader), 0 failed**. This is the strongest libretro-side
compatibility evidence so far: every title the CLI sweep marks `ok` also loads,
paints an animated non-black frame through the real core, and survives the
harness save-state round trips.

## Update, 2026-11-16 (rounds 76-77): animation coverage in the whole-corpus probe

`tools/check_libretro_corpus_visual.py` records `distinct_frames` and
`max_nonblack` per title and, since round 78, feeds every title a key script:
the per-title `tools/corpus_keys/<jar stem>.txt` when present, otherwise the
sweep's generic `keys.txt`. 600-frame (10 s virtual at 60 Hz) probe on the r31
core (`.local-tools/debug-runs/libretro-visual-r78/visual.jsonl`): 385 titles ->
383 passed, 2 known limitations, 0 failed; **381 animated, 4 static** (up from
348 animated when the probe ran with no input at all; the generic recipe alone
turned 33 previously frozen screens into animated gameplay). The 4 remaining
static titles are slow-loader/dialog screens; two are catalogued known
limitations for the state round trip when the title sits inside a long blocking
load (`毁灭战士3_10598`, `台球传奇_87`). The headless CLI (complete key set
incl. digits/star/pointer) stays the authoritative compatibility oracle.
Harness `--keys` also maps digits `0`/`5` to the core options Select/Start.
Full CTest 195/195 (382.23 s).

## Update, 2026-11-17 (round 81): cheat codes and the 0.5.13 package

- The core now implements `retro_cheat_set()`/`retro_cheat_reset()` with one
  documented form, `<arena offset>:<hex bytes>`, re-applied at the start of
  every `retro_run()` over the SYSTEM_RAM arena window; `.info` advertises
  `cheats = "true"`. New CTest gate `check_libretro_cheat` proves apply,
  per-frame re-apply after a simulated overwrite, disable, malformed code and
  reset (`docs/notes/2026-11-17-libretro-cheat-codes.md`).
- The mock frontend's `--keys` parser no longer splits `pressed@X,Y` on the
  comma, so curated pointer scripts actually reach the core; the whole-corpus
  visual probe now reports 385 titles -> 382 ok + 3 documented known
  limitations, 0 unexplained failures, 380 animated / 5 static
  (`docs/notes/2026-11-17-harness-pointer-keys.md`,
  `docs/notes/2026-11-17-static-title-classification.md`).
- Metadata and the r32 package are 0.5.13 (`.info` SHA-256
  `cd52e1a0f260aeb818a51c1af7f09dcd62ab85db7595b0b88753a41f77131530`); the
  libretro-super and libretro-docs patches were refreshed and both upstream PRs
  (#2204, #1239) now carry the 0.5.13 content. Validation: mock 20/20, real
  RetroArch PASS, Wine 4/4, bundle 32/32, full CTest 196/196.

## Update, 2026-11-17 (round 85): 0.5.13 rehearsal against current upstream

- `libretro/docs` master is still `177c669990b659e7cfd00add4564b85c2a692179`,
  so the docs patch (5 files, +241 lines) is rebased at the current master; a
  clean clone + patch builds with `mkdocs` (page 99,546 bytes, only the
  git-date plugin warning for the new file).
- `libretro-super` master moved to
  `1b988c491e31f1cc768572f3d68f45e69d194e5b`; the one-file `.info` patch still
  applies there. The fork PR branch was rebuilt on that commit
  (`5bbf1e7`, parent = current master) and PR #2204 was reopened — it is now
  OPEN/MERGEABLE with the current base. `tools/check_libretro_submission_bundle.sh`
  now records the new upstream baselines; with `--apply-clones --mkdocs` it
  reports **41 passed, 0 failed**.
- The exact upstream CI template command shapes were simulated again on the
  0.5.13 tree (`tools/simulate_ci_builds.sh --out /var/tmp/ci-sim-r32`):
  **3/3 contracts pass** — linux `nano_j2me_libretro.so` 3,995,448 B, windows
  `nano_j2me_libretro.dll` 3,562,524 B, android `nano_j2me_libretro.so`
  11,670,424 B (template renames to `_android.so`).
- Both upstream PRs remain ready: libretro/libretro-super#2204
  (head `5bbf1e7`, OPEN/MERGEABLE) and libretro/docs#1239 (head `05577de5`,
  OPEN/MERGEABLE). No merge or acceptance is claimed.

## Update, 2026-11-17 (round 86): longer corpus run on the 0.5.13 core

- The whole-corpus libretro visual probe was re-run at 1,800 frames (30 s of
  virtual time) with 10 workers on the r32 linux core
  (`.local-tools/debug-runs/libretro-visual-r86`): 372 rows -> **370 ok + 2
  documented known limitations, 0 unexplained failures**. Only 3 rows stayed
  static (the carrier-SMS dialog, the 3D pool attract/menu and the music
  prompt), median 76 distinct frames per title, and no new crash or state
  refusal appeared at the longer length.
- This confirms the 600-frame numbers' state/crash profile is not an artefact
  of the shorter run; the 1,800-frame sweep is the load-timeout and
  leak/hang check used for the release evidence.

## Update, 2026-11-19 (round 87): 0.5.14 cheat engine and r33 package

- Cheat engine moved into the runtime (`src/core/nj_cheat.c`), shared by the
  libretro core, the headless CLI and the Android build; `tests/test_cheat.c`
  (33 checks) runs as CTest `test_cheat`; `check_libretro_cheat` still passes.
- Metadata bumped to 0.5.14 (`.info` SHA-256 `497b24a6…`); r33 package built
  from `51c892c` and installed as canonical; mock 20/20, real RetroArch PASS,
  Wine 4/4 at 300 frames, option matrix 66/66, states 12/12, full CTest
  197/197 PASS, `check_port_sources` green after adding the new core file to
  the embedded port list.
- Release `v0.5.14` published with the r33 zip and both APKs; upstream super
  PR branch refreshed with the 0.5.14 `.info` (no merge claimed).

## Round 90: rehearsal on the 0.5.14 tree (2026-11-19)

`tools/check_libretro_submission_bundle.sh` is 32/32 on the tree; with clean
`libretro-super`/`libretro/docs` clones at the recorded baselines plus real
`mkdocs` it is 41/41.  A fresh `--depth 1` clone of origin/main at `6a478a4`
satisfies all three upstream CI template build contracts, and the Linux core
built inside that clone passes the mock matrix 20/20 and a real RetroArch
LoopDemo run.  Details and hashes are in `docs/LIBRETRO_SUBMISSION.md`
("Round-90 end-to-end rehearsal").

## Round 93: 0.5.15 with the Android cheat editor (2026-11-19)

Metadata bumped to 0.5.15 (`.info` SHA-256 `d9f10709…`) and r34 rebuilt from
`f18ed5c`; the core is feature-identical to r33 and the release adds the
Android in-game cheat editor (`docs/notes/2026-11-19-android-cheat-editor.md`).
`.info` super patch regenerated cleanly against super master `1b988c49…`;
bundle check 32/32 on the new canonical tree.  r34 validation: mock 20/20,
RetroArch LoopDemo PASS, Wine 4/4, full CTest 197/197.  Release `v0.5.15`
published with the r34 zip and both 0.5.15 APKs; details live in
`docs/RELEASE_0.5.15.md`.

## Round 94: rehearsal on the 0.5.15 tree (2026-11-19)

`tools/check_libretro_submission_bundle.sh` is 32/32 on the release tree and
41/41 with clean upstream clones at the recorded baselines plus real `mkdocs`.
A fresh `--depth 1` clone of origin/main at `8b20d50` satisfies all three
upstream CI template build contracts, and the CI-built Linux core passes the
mock matrix 20/20 and a real RetroArch LoopDemo run.  PRs #2204 (head
`576d189`) and #1239 (head `5917f7d`) remain OPEN/MERGEABLE with no comments;
details in `docs/LIBRETRO_SUBMISSION.md` ("Round-94 end-to-end rehearsal").

## Round 95: fuzz coverage and a release-artifact corpus probe (2026-11-19)

- Fuzz campaigns on the 0.5.15 CLI are clean: `fuzz_classfile` 3000×2
  mutations on the MIDlet class and 3000×2 in all-class mode, `fuzz_jar`
  2000×2, `fuzz_media` 3000×2 in multi-target mode — 0 crashes, 0 hangs
  (evidence in the three fuzzing notes).
- Whole-corpus libretro visual probe on the r34 core (300 frames, 6 jobs,
  387 titles): **383 passed, 3 known limitations, 1 failed**.  The failure,
  `杀鬼子-敌后武工队_2185.jar` (`unserialize_restores_state: retro_wunserialize
  returned 0`), does not reproduce: two standalone 300-frame paced runs with
  an immutable private copy of the r34 core pass all 46 checks.  Classified as
  a load-sensitive state flake under six parallel probes (each core instance
  runs a 128 MB arena), not a regression — the same probe class passed 383/385
  without failures in round 78.
- 2D perf note: the two "slow" titles in the round-60 survey are self-pacing
  spin waits (`Thread.yield` busy loops), not raster work; recorded in
  `docs/PERFORMANCE.md` so future 2D optimisation effort targets the right
  titles.

## Update, 2026-11-19 (round 98): 0.5.16 / r35 released

- 0.5.16 ships the Android in-game cheat **value search** on top of the round-96
  runtime API (`nj_vm_cheat_find/filter/read()`); the libretro core is
  feature-identical to r34 (same options, cheats and save states).
- r35 built from clean worktree `64ad3cf`, `.info` SHA-256
  `e372f211ed6c26e55bd23160e14d5e55422a54452209a7ed65032bf55c3c57da`
  (`display_version = "0.5.16"`); super patch regenerated from a fresh clone at
  `1b988c491e31f1cc768572f3d68f45e69d194e5b` and applies cleanly.
- Full validation green: mock 20/20, real RetroArch LoopDemo 120 PASS, Wine 4/4
  at 300 frames, bundle 32/32, full CTest 197/197 in 347.93 s.
- Release assets: cores zip 11,346,011 B
  `bfeb3738a9b45251ea4a5726e275c6ed3c5de9bfddee863a56a3ea2e26a4cb3e`,
  APK 3,295,594 B `763ed720…`, legacy APK 703,979 B `c594b9e6…`; tag
  `v0.5.16` + GitHub release published.
- Upstream PRs #2204 (super, branch rebuilt on current master) and #1239
  (docs) remain OPEN/MERGEABLE with the 0.5.16 body; awaiting review, no merge
  claimed.

## Update, 2026-11-19 (round 99): 0.5.16 submission rehearsal and r35 corpus probe

- Fresh `git clone --depth 1` of origin/main at `de58803`: CI template
  simulations **3/3** (linux 4,000,096 B, windows 3,567,227 B, android
  11,707,944 B); clone-built Linux core passed mock **20/20** and real
  RetroArch LoopDemo 120 frames.
- Bundle check with fresh upstream clones (super `1b988c49`, docs `177c6699`)
  plus `--apply-clones --mkdocs`: **41 passed, 0 failed**.
- r35 whole-corpus libretro visual probe (300 frames, 6 jobs): 387 titles →
  **383 passed, 3 known limitations, 1 failed**. The failure
  (`杀鬼子-敌后武工队_2185`, save-state restore) passed 45/45 checks in three
  standalone runs with a private core copy → parallel-load state flake, same
  class as rounds 78/95, not a regression.
- Both upstream PRs still OPEN/MERGEABLE with 0 comments / 0 reviews; no merge
  claimed.

## Update, 2026-11-19 (round 101): 0.5.17 / r36 release

- Version bumped to 0.5.17 (commit `d6eef33`); `.info` SHA-256
  `93c4862a…`, super patch regenerated from master `1b988c49…` and applies.
- r36 built and installed as canonical `build/libretro-dist`; mock matrix
  **20/20**, real RetroArch LoopDemo 120 frames PASS, Wine 4/4 at 300 frames,
  bundle check 32/32, full serial CTest **197/197 in 347.29 s**.
- APKs 517 (nj2me.apk 3,295,594 B; legacy 704,229 B); release zip
  11,347,105 B `5876c9f1…`.
- Tag `v0.5.17` and GitHub release published with the four assets (draft and
  prerelease both false). PR #2204 head rebuilt to `bf88d89` with the 0.5.17
  `.info`; PR #1239 head `5917f7d`; both OPEN/MERGEABLE with 0 comments / 0
  reviews, no merge claimed.

## Update, 2026-11-19 (round 102): 0.5.17 submission rehearsal and r36 corpus probe

- Fresh `git clone --depth 1` of origin/main at `084c29a`: CI template
  simulations **3/3** (linux 4,000,096 B, windows 3,567,227 B, android
  11,708,520 B); clone-built Linux core passed mock **20/20** and real
  RetroArch LoopDemo 120 frames.
- Bundle check against baselines (super `1b988c49`, docs `177c6699`) with
  `--apply-clones --mkdocs`: **41 passed, 0 failed**.
- r36 whole-corpus libretro visual probe (300 frames, 6 jobs): 387 titles →
  **383 passed, 3 known limitations, 1 failed**. The failure
  (`杀鬼子-敌后武工队_2185`, save-state write refused) passed 45/45 checks in
  three standalone runs with a private core copy → parallel-load state flake,
  same class as rounds 78/95/99, not a regression.
- PRs #2204 (head `bf88d89`) and #1239 (head `5917f7d`) still
  OPEN/MERGEABLE with 0 comments / 0 reviews; no merge claimed.

## Round-110 update, 2026-11-19

- `docs/libretro/upstream/nano_j2me.md`: added the `Multi-touch` feature row and
  `### Multi-touch` section (RETRO_DEVICE_ID_POINTER_COUNT per-pointer delivery
  into the Nokia MultipointTouch API, up to 4 concurrent fingers; MIDP Canvas
  single-owner unchanged; `nano_j2me_pointer` gate; no RetroPad synthesis).
- Regenerated `libretro-docs-nano_j2me.patch` against docs master `177c6699`
  (5 files, +252 lines); verified with `git apply --check` and `mkdocs build`.
- Rebuilt the `lucinhu/docs` fork branch `add-nano_j2me` as `e34bae2` (parent
  `177c6699`) and force-pushed; PR #1239 head updated. Both PR bodies (#2204 and
  #1239) replaced with the clean posted text (no internal notes); the
  `libretro-super` patch baseline is now `1b988c49`.
- Validation: bundle check **32/32** plain and **41/41** with fresh clones +
  mkdocs. Both PRs OPEN/MERGEABLE, 0 comments / 0 reviews; no merge claimed.

## Round-112 update, 2026-11-19

- Upstream masters moved: `libretro-super` → `55e50a81…`, `libretro/docs` →
  `9399ee32…`.  Both PR branches rebased and force-pushed:
  super `772fcf8` (parent `55e50a81`), docs `51ef4a9` (parent `9399ee32`).
- Patches regenerated against the new masters (`libretro-super` +31 lines,
  `libretro-docs` 5 files +252 lines); `tools/check_libretro_submission_bundle.sh`
  `SUPER_BASE`/`DOCS_BASE` updated; tracked README/PR drafts and the posted PR
  bodies updated to the new hashes and dates.
- Validation: bundle **32/32** plain, **41/41** with fresh clones + mkdocs.
  PRs #2204/#1239 OPEN/MERGEABLE, no CI checks on the fork branches; no merge
  claimed.

## Round-113 update, 2026-11-19

- **0.5.18 / r37 released** (tag `v0.5.18` at `3b96b69`): blit-path field cache
  shipped to cli/APK/libretro (`-4.2 %/-5.1 %` on the unpaced Minecraft loader,
  byte-exact).  r37 package installed as `build/libretro-dist`.
- Verification: mock 20/20, real RetroArch PASS, Wine 4/4 @300, bundle 32/32,
  full CTest 198/198 (360.07 s).  APKs versionCode 518 (0.5.18 + legacy).
- Release assets: cores zip 11,351,007 B, APK 3,295,594 B, legacy 704,234 B
  (+SHA256SUMS).  Upstream PRs unchanged from round 112: #2204 head `772fcf8`,
  #1239 head `51ef4a9`, both OPEN/MERGEABLE; no merge claimed.

## Round-114 update, 2026-11-19

- Released-tree evidence for 0.5.18: whole-corpus libretro visual probe
  **384 titles → 381 passed / 3 known limitations / 0 failed**.
- New tracked tool `tools/check_corpus_cpu.py`: scans every `ok` row with the
  frame cap disabled and reports child CPU; 384 titles scanned — 53 over 1 s,
  top outliers are self-pacing spin loops (`currentTimeMillis`/`Thread.yield`,
  NBA basketball shows 108 M calls each in 300 frames), not runtime overhead.
  Documented in `docs/PERFORMANCE.md` (round 114 section) with the rule to
  profile call graphs before labeling a title slow.

## Round-115 update, 2026-11-19

- Clean-clone rehearsal of the 0.5.18 tree at `cedea62`: CI template
  simulations **3/3** (linux 4,000,128 B / windows 3,567,775 B / android
  11,711,520 B), clone-built core mock **20/20** + real RetroArch **PASS**,
  bundle check **41/41** with the current upstream masters.  No tree changes.

## Round-117 update, 2026-11-19

- Upstream `libretro/docs` page now documents every core option (`screen`,
  `locale`, `fps`, optional `renderer` with hardware builds included); docs
  patch regenerated (5 files, +261 lines), fork branch `3316299` pushed, PR
  #1239 head updated (OPEN/MERGEABLE, 0 comments/reviews).
- Bundle validation: 32/32 plain, 41/41 with baseline clones + mkdocs.

## Round-118 update, 2026-11-19

- Re-checked `.gitlab-ci.yml` against the current `libretro-infrastructure/
  ci-templates` master (fetched from git.libretro.com): all job anchors,
  include files and command shapes still match; no change needed.
- Added `tools/check_libretro_option_docs.py` + CTest gate
  `check_libretro_option_docs`: all 18 core options must be documented on
  `docs/libretro/upstream/nano_j2me.md`; prevents doc drift before submission.

## Round 119 (2026-11-19): measured rewind/runahead cost

- `tools/check_libretro_rewind.sh` now runs the same session twice (rewind
  on and off) and reports the snapshot cost per stored frame instead of only
  checking that the frontend buffer initializes. Measured headless on the
  released core with `nano_j2me_heap=16`, `rewind_granularity=2`, 600 frames:
  2730 ms with rewind vs 491 ms without -> about 7.4 ms per stored frame for a
  25,473,088-byte state.
- `tests/test_libretro_frontend.c` gained `--serialize-timing N`: after the
  frame run it drives a frame and measures N save+restore pairs, printing
  `[timing] state size=... save ... load ...` (mock frontend upper bound; it
  includes the request handshake when no frame loop is driving the VM).
  `tools/check_libretro_state_cost.sh` wraps it. At the auto heap the same
  report shows ~1 s per save/restore for a ~157 MB state, which is the
  capacity story the frontend reports as an insufficient rewind buffer.
- `docs/LIBRETRO.md` Rewind/Runahead bullets now state these measured costs
  instead of guesses: rewind at granularity 2 spends about half of a 60 Hz
  frame on snapshots, and runahead cannot fit even one snapshot per frame
  within the budget at auto-heap sizes.

## Round-124 (2026-11-20)

The corpus sweep's corrected row set (399 ok after the input-recipe and heap
fixes) was re-probed through the libretro core: 399 titles, 396 passed, 3 known
limitations, 0 failed.  `tools/check_libretro_corpus_visual.py` now replays the
row's input recipe (`keys: "none"` = no input) so reclassified titles are not
driven into their exit path again.

### Round 129 (2026-11-20): expanded-corpus probe and a real fix

The 510-title libretro visual probe against the r37 core passed **508**, with
one known limitation and one failure that turned out to be a runtime bug
(`The Sims 3 Winter Edition` painted black after the runtime rejected a
visible-Canvas full-screen change).  Fixed in `cc4acfb`; full serial CTest
199/199.  Probes now honor each row's absolute `jar_path`, so fetched archive
titles are covered by libretro evidence, not just the in-repo set.

### Round 130 (2026-11-20): 0.5.19 / r38

Released 0.5.19 from `994cdca` (r38, `.info` `67a021b3…`) with the
visible-Canvas full-screen fix.  Validation: mock 20/20, RetroArch PASS,
Wine 4/4, bundle 32/32, CTest 199/199 (350.20 s).  The released core was
probed over the whole 510-title rendered corpus: 510 titles, 509 passed, 1
known limitation, 0 failed.  Super fork branch rebuilt on master `55e50a81` as
`99d7d92` (0.5.19 `.info`); PR #2204 OPEN/MERGEABLE, #1239 OPEN/MERGEABLE
(head `3316299`), no merge claimed.

## Round 135 (2026-11-21): 862-title libretro visual probe

The expanded corpus was rendered through a libretro core built at HEAD
(`tools/build_libretro_cores.sh linux-x86_64`, Boolean fix included):
**845 titles -> 840 passed, 2 known limitations, 3 failed**.  The known
limitations are unchanged (`毁灭战士3_10598` slow loader, `台球传奇_87` /
`德州扑克2_3884` load-state timeout during a long blocking phase).  The three
failures are newly fetched titles that also render black in the CLI
(`GoldenSolitaire`, `NeonRunner`, one `Tomb Raider Anniversary` 128x128 build);
they paint frames without issuing a single draw call and the reference renders
a language screen, so they are title-initialisation gaps - see
`docs/notes/2026-11-21-fetched-black-screen-titles.md`.  The sweep now records
the `gfx`/`natives` counters so this class is visible without a visual probe.
