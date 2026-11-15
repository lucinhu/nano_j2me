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
   (5 files, +213 lines) and `libretro-super-nano_j2me-info.patch` (one added
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
