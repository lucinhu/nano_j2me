# 2026-11-15 libretro submission readiness check

Scope: verify that the repository is actually ready for the libretro submission
steps (metadata, CI configuration, artifacts, documentation drafts), and record
the remaining external actions. No upstream pull request, upload or maintainer
contact was made.

Tree under test: `bc49c46` (`v0.5.7-9-gbc49c46`).

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
5. **Fresh eight-target build** at HEAD into `/var/tmp/nj2me-submission-dist`
   (copied to `build/libretro-dist/`), 16 files per directory, identical
   `.info`, license/notice set and `BUILD_STATUS.txt`. Sizes and SHA-256 values
   are in `build/libretro-final-report-20261115.md`.
6. **Artifact validation**: linux-x86_64 mock frontend 20/20; real RetroArch
   1.22.2 LoopDemo 120 frames and TextDemo 180 frames PASS; windows-x86_64
   Wine fixtures 4/4 PASS. Android/ARM cores were ABI/dependency-checked only
   in this rebuild (no device run available).
7. **Local test suite**: isolated RelWithDebInfo build (CLI + libretro + tests,
   SDL2/embedded/web/GLES2-accelerated port off) passed **183/183** CTest tests
   in **338.26 s** at this revision, including `test_libretro_frontend` and
   `no_statics`. A separate build directory under `/var/tmp` is used so the
   user's in-progress `build/` trees are not touched.

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
