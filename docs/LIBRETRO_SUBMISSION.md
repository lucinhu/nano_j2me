# libretro submission kit

This is a **local preparation kit**, not an announcement of upstream acceptance.
This project repository is committed and pushed as part of routine work, but no
pull request, upload or other interaction with libretro upstream is performed
without further explicit authorization. Runtime code is MIT licensed, built-in
AMR numeric data are separately Apache-2.0, and bundled font data are SIL OFL
1.1. The identified codec source replacements and bounded implementation
reviews are complete, and final local combined-candidate validation passed as
recorded below. Package the final notices/metadata and complete the external
steps below before publication.

## Local materials

| Material | Local source | Intended upstream destination |
| --- | --- | --- |
| Core metadata | [nano_j2me_libretro.info](../src/port/libretro/nano_j2me_libretro.info) | `libretro/libretro-super`, `dist/info/nano_j2me_libretro.info` |
| Buildbot configuration | [root GitLab CI](../.gitlab-ci.yml) | core repository root; integration with libretro infrastructure requires maintainers |
| Library page draft | [nano_j2me library page](libretro/nano_j2me.md) | `libretro/docs`, `docs/library/nano_j2me.md` plus navigation/index updates |
| Copy-ready upstream bundle | [docs/libretro/upstream/](libretro/upstream/README.md) | exact `.info` and documentation-page copies plus index-update checklist for the two PRs |
| `libretro-super` patch | [libretro-super-nano_j2me-info.patch](libretro/upstream/libretro-super-nano_j2me-info.patch) | one-file diff adding `dist/info/nano_j2me_libretro.info`; verified against `libretro-super` master `fb15e63` |
| `libretro/docs` patch | [libretro-docs-nano_j2me.patch](libretro/upstream/libretro-docs-nano_j2me.patch) | 5-file diff: library page with upstream front matter, `mkdocs.yml` nav, `core-list.md`, `licenses.md`, `see-also.md`; verified against `libretro/docs` master `d83b34b`, `mkdocs build` passes |
| CI template simulation | [tools/simulate_ci_builds.sh](../tools/simulate_ci_builds.sh) | local pre-submission check that the Linux/Windows/Android template command shapes build and place artifacts where the templates collect them |
| Local build/user guide | [LIBRETRO.md](LIBRETRO.md) | project documentation |
| Liberation font notice | [Liberation-Fonts-OFL.txt](licenses/Liberation-Fonts-OFL.txt) | accompany source and binary packages using the Latin font tables |
| Source Han font notice | [source-han-sans-OFL.txt](licenses/source-han-sans-OFL.txt) | accompany source and binary packages using the CJK font tables |
| Third-party MIT notices | [third-party-code-MIT.txt](licenses/third-party-code-MIT.txt) | accompany included puremp3, NLayer, OxideAV, Mascot and libretro material |
| AMR data license and notices | [Apache-2.0](licenses/AMR-Apache-2.0.txt), [PacketVideo/3GPP notices](licenses/AMR-OpenCORE-NOTICE.txt), [complete data map](AMR_DATA_LICENSES.md) | accompany source and binary packages containing built-in AMR data |
| License scope and source trail | [font scope](FONT_LICENSES.md), [codec provenance](CODEC_PROVENANCE.md), [table provenance](M4V_TABLE_PROVENANCE.md), [AMR runtime inventory](notes/2026-11-12-amr-independent-implementation.md) | document MIT implementation code with separately licensed AMR/font data |
| System AMR policy and evidence | [SYSTEM_AMR.md](SYSTEM_AMR.md) | document system-first complete decode and portable fallback |

The core identifier is `nano_j2me`; metadata uses `systemid = "j2me"`, matching
existing J2ME metadata. Libraries use `nano_j2me_libretro.so`, `.dll` or `.dylib`.
The Android CI template publishes the `_libretro_android.so` suffix. The source
version is currently 0.5.7; refresh the metadata version whenever that changes.

## Readiness status, checked 2026-11-15

This section records the concrete preparation checks run against the current
tree; the unchecked boxes in [Validation](#validation) remain the external
steps only. Working HEAD for this check: `011247e` (the port now answers the
device properties titles probe before their MIDlet starts, and a load that
fails with the generic handset is retried once with the documented `nokia6230`
profile; this comes on top of the paused-state clock assist, the three corpus
rendering fixes, the bounded state clock assistance and the whole-corpus visual
probe from the previous revisions; the metadata/CI/docs checks below were first
run at `bc49c46`, and the release artifacts were refreshed after each libretro
change as recorded in the hash table).

- **Metadata**. `src/port/libretro/nano_j2me_libretro.info`
  (SHA-256 `c0f30aeae22150208034202986b3db77fd2f423874e2f2bcc1ce7b07c82723c6`)
  is the exact file intended for `libretro-super/dist/info`. Its
  `database = "Mobile - J2ME"` names an existing libretro-database platform
  folder (checked against the live repository), its file name matches the core
  name, `supported_extensions = "jar"` matches JAR-only loading, and
  `needs_fullpath = "true"` matches the core opening the JAR itself. RetroArch
  1.22.2 scanned the packaged `.info` from `libretro_info_path` and generated
  its core-info cache without parse errors (evidence:
  `.local-tools/debug-runs/submission-20261115/retroarch-coreinfo-scan.log`);
  the same file was installed next to the core on an Android RetroArch 1.19.1
  device that loaded and ran it, and it ships byte-identically in every local
  package.
- **CI configuration**. The root `.gitlab-ci.yml` extends the current
  libretro-infrastructure templates (`linux-cmake.yml`,
  `windows-cmake-mingw.yml`, `android-cmake.yml`) and sets the variables those
  templates expect (`CORENAME`, `EXTRA_PATH=libretro`, `CORE_ARGS`). The exact
  template commands were simulated locally at this revision:
  - Linux template command (plain CMake generator):
    `/tmp/ci-sim/build/linux-x86_64/libretro/nano_j2me_libretro.so`,
    3,969,632 bytes.
  - Android template command (NDK 30.0.16248370, `arm64-v8a`):
    `/tmp/ci-sim-ndk/build/android-arm64-v8a/libretro/nano_j2me_libretro.so`,
    renamed to `nano_j2me_libretro_android.so` by the template's `after_script`.
  - Windows template contract (mingw-w64 cross compilers with
    `CMAKE_SYSTEM_NAME=Windows`, as the MXE `*-static-cmake` wrapper provides
    upstream): configure and build of the `nano_j2me_libretro` target succeeded
    and produced `libretro/nano_j2me_libretro.dll` (3,537,295 bytes), i.e. the
    path the template's post-build step expects.
  All three land at `$BUILD_DIR/$EXTRA_PATH/$LIBNAME`, where the templates
  collect them. Re-run at the r17 tree with
  `tools/simulate_ci_builds.sh`: all three contracts pass (Linux 3,973,848 B,
  Windows 3,541,251 B and Android arm64 11,555,520 B at
  `/var/tmp/ci-sim-r17/...` in one run). `tools/simulate_ci_builds.sh` reproduces these template commands
  (and reads `CORE_ARGS` straight out of `.gitlab-ci.yml` so the simulation
  cannot drift from the committed CI file). No upstream pipeline has been
  executed; that requires maintainer registration.
- **Fresh eight-target build**. `tools/build_libretro_cores.sh --jobs 12
  --out /var/tmp/nj2me-submission-dist-r17 all` rebuilt every default-feature
  target from this revision (device properties and generic-handset retry) and
  the tree was copied into `build/libretro-dist/`. Per-core SHA-256:

  | Target | SHA-256 |
  | --- | --- |
  | linux-x86_64 | `a90368876f572b65886102b175ba0f49b60d3d5af211008cf7f43130bebfbbfa` |
  | linux-aarch64 | `8ee5ecd8da49cd42c13800f8d53aedad2abc14df9fb32f42132e152665e1700c` |
  | linux-armv7 | `fb384296703f2159445e48a60587d5b28718595ed7bf02d5b1eea32056c086a8` |
  | android-arm64 | `4ad4e145d09ca459f1790460c5da8022a49b9fc6714fdb79aab77fa748e6370f` |
  | android-armv7 | `e74c994d3845c3cf1f048fec7a9a00af6c7cb47bee643c5d595ecf90d7f4e417` |
  | android-x86_64 | `9d6294cc9749b1ff66df831d41b9f643b22993fc39913ba512ea925f831276f9` |
  | windows-i686 | `a8673238dd035615b00a6d9d76d76c70d2a0f091bcbf3af8b469ec7087c4d2da` |
  | windows-x86_64 | `b787d4abf4e7bf78133ec808631e68053b04fe980c0e4ab074d54ee8acb070c8` |

  Every target directory contains the same `.info`, license/notice set and
  `BUILD_STATUS.txt`; no external test font is bundled.
- **Artifact validation**. The fresh linux-x86_64 core passed the mock-frontend
  matrix (20 passed, 0 failed, 0 skipped), and real RetroArch 1.22.2 loaded and
  ran the core to a clean 600-frame exit. The fresh windows-x86_64 DLL passed
  the Wine fixture suite 4/4. The corpus-level libretro state probe
  (`tools/check_libretro_states.py`, 50 titles, 120 frames) reports
  **50/50 save/restore round trips**. Real RetroArch 1.22.2 also loaded and ran
  four real corpus titles (3D 浴火银河2完整版, 仙剑奇侠传完美版, 拳皇98终极之战,
  雷霆战机2012BT版) for 300 frames each, clean exits and verified core log
  markers. The whole-corpus rendering probe on the
  r17 core (`tools/check_libretro_corpus_visual.py`, 373 sweep-ok titles, 300
  frames, real-time cadence) reported **372 passed, 1 failed**, and a re-run at
  **600 frames** (10 s of play per title) reported the same **372/373**; the
  single remaining failure is not visual: one very slow 3D loader
  (毁灭战士3_10598, which renders its loading sequence 190 times and cannot
  finish within the sampling watchdog). The two titles that refused the generic handset
  (`bubblet.jar`, `1945空战英雄_6105.jar`) now load through the device-property
  answers and the one-time `nokia6230` retry. The paused-save/serving class
  and the six black-frame failures found earlier are gone; details in
  `docs/notes/2026-11-15-libretro-black-frames.md` and
  `docs/notes/2026-11-15-libretro-save-while-paused.md`. The full local CTest
  suite passed **183/183 in 348.03 s** at this revision. Seven M3G titles were
  checked through the mock frontend at the real-time cadence
  (`--pace-us 16666`): 浴火银河2完整版, 蓝牙真3D格斗, 急速狂飙完美破解版,
  浴火银河2瓦尔基里, 极品飞车16：亡命狂飙, 3D激爆拳击 and 3D跳台滑雪2011 all
  pass 120-frame visual checks (two slow starters also at 600 frames). An
  unpaced (`--pace-us 0`) run of the same harness starves the VM worker and
  shows blank frames for 3D激爆拳击; the run-pacing limitation is documented in
  [LIBRETRO.md](LIBRETRO.md#timing-media-and-limitations). Android and
  ARM-Linux cores were rebuilt and ABI/dependency-checked in this pass; their
  runtime behavior was previously proven on the same source line (including a
  real Android RetroArch 1.19.1 device run), but that historical device
  evidence is not re-created by this artifact check. Logs are under
  `.local-tools/debug-runs/submission-20261115/`.
- **Remaining external steps**. Upstream CI pipeline execution and artifact
  collection, the `libretro-super` metadata pull request, the `libretro/docs`
  pull request with a real `mkdocs build`, and optional assets/database entries
  are all upstream decisions and are not performed here.

## Upstream procedure, checked 2026-11-15

Re-verified against the live [Developing Cores guide](https://github.com/libretro/docs/blob/master/docs/development/cores/developing-cores.md);
the steps below are unchanged.

1. Submit the `.info` file to [libretro-super/dist/info](https://github.com/libretro/libretro-super/tree/master/dist/info).
   Test it in RetroArch's configured `libretro_info_path` and inspect *Information
   → Core Information*. Do not assume the Core directory is also the Info
   directory. [libretro-core-info's Makefile](https://github.com/libretro/libretro-core-info/blob/master/Makefile)
   synchronizes metadata from libretro-super; it is not the canonical source for
   this new-core submission.
2. Supply a root `.gitlab-ci.yml` and coordinate core repository/mirror and
   pipeline registration with libretro maintainers. The local configuration
   includes libretro's Linux, Windows MinGW and Android CMake templates. Existing
   YAML alone does not register a core with the buildbot. A maintainer must run
   the pipeline and verify artifact collection. No authenticated upstream
   pipeline has been run for this preparation.
3. Add assets and a content database if needed. Neither a new database nor a
   playlist association is established by writing `database = "Mobile - J2ME"`.
   Do not bundle commercial JARs or test-corpus content in a submission kit.
4. Submit the library documentation using the
   [official core template](https://github.com/libretro/docs/blob/master/docs/meta/core-template.md)
   and [docs contribution instructions](https://github.com/libretro/docs/blob/master/README.md).
   Copy the local draft into `docs/library/nano_j2me.md`, add navigation in
   `mkdocs.yml`, list it in `docs/guides/core-list.md`, update
   `docs/development/licenses.md` with MIT code / Apache-2.0 AMR data / OFL fonts, and add
   `docs/meta/see-also.md` links where relevant. BIOS, achievement and softpatch
   lists need changes only if applicable; this core claims none of those features.

In a separate checkout of libretro/docs, validate the complete documentation
change with its documented commands:

```sh
python3 -m pip install -r docs/requirements.txt
mkdocs build
# Optional local preview:
mkdocs serve
```

These are pending external steps. Acceptance, supported buildbot platforms,
release timing and Core Downloader availability remain upstream decisions.

## Build and platform evidence

The libretro artifact results below record the **2026-11-12 baseline**, before
the subsequent complete AMR runtime/data and system-provider changes. They are
historical evidence. The final 2026-11-13 combined candidate subsequently passed
all eight builds; current results and generated-report paths are in
[system AMR verification](SYSTEM_AMR.md#verification-and-reproduction).

The [cross-build script](../tools/build_libretro_cores.sh) supports Linux x86_64,
Linux aarch64/ARMv7, Android arm64/ARMv7/x86_64 and Windows x86_64/i686. The
[CI draft](../.gitlab-ci.yml) defines seven jobs: Linux x86_64/aarch64, Windows
x86_64/i686 and all three Android ABIs. Linux ARMv7 is a local script target,
not a configured upstream template job. macOS is unverified and has no job.

| Artifact / environment | Evidence obtained | Limits |
| --- | --- | --- |
| Linux x86_64 | final rebuild, 25 ABI exports before/after stripping; native mock frontend matrix passed; RetroArch 1.22.2 LoopDemo smoke cleanly exited after 120 frames | local artifact requires GLIBC_2.43; not a general Linux release baseline or a commercial-title playthrough claim |
| Linux aarch64 | Zig 0.17.1 cross-build; architecture, 25 ABI exports and glibc symbol checks | maximum required GLIBC_2.17; no ARM hardware/QEMU runtime test |
| Linux ARMv7 | Zig 0.17.1 hard-float cross-build; architecture and exports | GLIBC_2.17; current output requires NEON/VFPv3; no runtime test |
| Android arm64-v8a / armeabi-v7a / x86_64 | NDK 30.0.16248370, API 21 builds; architecture and exports | only libc/libm dependencies in inspected outputs; no adb device/emulator runtime test |
| Windows x86_64 / i686 | final MinGW builds, 25 ABI exports before/after stripping; each Wine harness passed 4 checks / 0 failures for LoopDemo states/input/video and AudioDemo PCM | Wine 11.21 only; native Windows untested; KERNEL32/UCRT API-set imports, no libgcc/libwinpthread DLL imports; no minimum Windows version promise |
| Upstream CI | template references and artifact subdirectory reviewed locally | no registered/authenticated GitLab pipeline execution or buildbot artifact validation |

On 2026-11-12, all eight targets were rebuilt with the completed snapshot and
adapter implementation. All passed 25 ABI export checks before and after
stripping; all eight distributed `.info` copies match the source with basic save
states. Default
codecs remained enabled. Build environment: Linux 7.2.14-arch1-1 x86_64, glibc
2.44, GCC 16.2.1 (20260810), MinGW GCC 16.2.0 for both Windows targets, Zig
0.17.1, CMake 4.4.5, Ninja 1.13.2 and Android NDK 30.0.16248370 (r30), API 21.

The final Windows commands were `tools/check_libretro_windows.sh --no-build`
and `ARCH=i686 tools/check_libretro_windows.sh --no-build`. Each passed 4 checks
with 0 failures. LoopDemo produced 140 video/input callbacks, serialized and
restored 140,816,448 bytes, refused a corrupt version and continued running.
AudioDemo produced 120 frames and nonzero PCM (44,294 nonzero samples on x86_64,
42,824 on i686; peak 19,999). Logs are in
`build/libretro-wine-{loopdemo,audiodemo}.jar-{x86_64,i686}.log`. These are Wine
results, not native Windows tests.

Future source changes require the corresponding final-candidate checks again.
Preserve toolchain versions, commands, logs, artifact hashes, dependency lists
and export checks with that candidate. Do not
turn successful cross-compilation into a device compatibility claim. A broadly
distributed Linux x86_64 build needs a deliberate older sysroot/builder and a
recorded glibc floor.

### Native AMR evidence and fallback

The [system AMR guide](SYSTEM_AMR.md) describes the current native backends.
The media layer validates the complete stream before trying the host audio
hook, installed providers and then the built-in fallback. Success requires the
entire expected 8 kHz mono PCM16 clip; errors, wrong formats, truncated results
and stalled providers are discarded before retrying the original input. Bare
AMR, supported audio-only 3GP and movie AMR tracks share this route.

| Backend | Evidence | Limits |
| --- | --- | --- |
| Android | Native API 21 x86_64 app exercised nine AMR cases through the actual service | Emulator app/service evidence; not an ARM hardware or RetroArch-core run. The existing Java audio hook stays first; a VM-less CLI falls back immediately. |
| Apple | API mocks and arm64/x86_64 compile checks | No Apple device or real host AMR decode claim. |
| Windows Media Foundation | SDK fault-path tests and both Wine architectures | Wine's real AMR codec was unavailable; no native Windows AMR decode success claim. |
| Linux | Portable built-in decoder, plus explicit port/custom providers if installed | No automatic native AMR service backend. |

`NJ_ENABLE_SYSTEM_AMR=AUTO` selects the native backend for supported hosted
non-legacy Android, Apple and Windows builds; `0` disables that automatic backend
but leaves explicit port/custom providers available. System PCM may differ
from the built-in decoder. The built-in Apache data remain in the fallback
binary and retain their notice obligations.

## Validation

These commands are runnable from the project root; they are a checklist, not a
claim that every command has already passed on the final candidate:

```sh
cmake -S . -B build/libretro-check \
  -DNJ_BUILD_LIBRETRO=ON -DNJ_BUILD_TESTS=ON \
  -DNJ_BUILD_CLI=ON -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
cmake --build build/libretro-check -j4
ctest --test-dir build/libretro-check --output-on-failure
CORE="$PWD/build/libretro-check/libretro/nano_j2me_libretro.so" \
  tools/check_libretro_frontend.sh
KEEP_LOG=1 tools/check_libretro_retroarch.sh \
  build/libretro-check/libretro/nano_j2me_libretro.so \
  tests/fixtures/alphademo.jar 600
tools/check_no_statics.sh
tools/build_libretro_cores.sh --jobs 4 all
# Windows harness; run for both architectures when toolchains/Wine are present:
ARCH=x86_64 tools/check_libretro_windows.sh
ARCH=i686 tools/check_libretro_windows.sh
```

Inspect [the Windows harness](../tools/check_libretro_windows.sh) for its
supported target arguments and prerequisites. An `all` build requires every
listed toolchain; select individual targets when unavailable toolchains should
not stop independent builds. `ctest` in the separate check directory also
requires its fixture/tool dependencies.

- [x] Basic manual state support established by the snapshot and libretro
  frontend checks: safe-boundary save/load, fresh-VM restore, damaged-state
  rejection, reset/restore and stable serialized size. Metadata advertises
  `savestate = "true"` / `savestate_features = "basic"` only.
- [x] Corpus-level libretro state round trips: 50/50 sampled titles save and
  restore through the mock frontend, including a load into a fresh VM
  after `retro_reset` while the VM is inside a blocking frame
  (`tools/check_libretro_states.py`).
- [x] Decoded AMR heap PCM and playback cursor restore into a fresh codec-free
  VM. Paused and playing cases preserve PCM, resume the remaining samples and
  seek correctly; native handles and device queues are cleared.
- [x] All 44 built-in AMR data definitions reproduce from pinned Apache inputs
  and MIT formulas, with complete license/notice copies and unchanged typed values.
- [x] The bounded AMR implementation inventory and equation/malformed-input
  checks are complete; 1,195 streams / 48,814 frames preserve the local PCM contract.
- [x] The identified MPEG-4 routine/table replacements and complete MP3 table
  reconstruction have their source trails and selected MIT notices recorded.
- [x] Final combined-source CTest/frontend/RetroArch results recorded for the
  actual candidate, including the current AMR provider and snapshot changes.
- [x] Final platform artifacts rebuilt from the same candidate; dependencies,
  exports, hashes, matching metadata and all license/notice files checked.
- [x] Offline simulations of the upstream Linux and Android CMake template
  commands produce the artifact path (`$BUILD_DIR/$EXTRA_PATH/$LIBNAME`) the
  buildbot collects; see the 2026-11-15 readiness section.
- [x] Metadata `database` value verified against libretro-database, and the same
  `.info` ships byte-identically in every local package.
- [x] `.info` parsed by RetroArch 1.22.2 locally (core-info cache written, no
      parse errors); rebuilt metadata same-file bytes in every package.
- [x] Prepared upstream patches apply cleanly to current upstream masters and
      the patched `libretro/docs` checkout passes `mkdocs build`
      (mkdocs 1.6.1, 8.68 s, page rendered; evidence log listed above).
- [ ] Upstream CI runs and collects the correct output library in `libretro/`
      (maintainer registration/execution).

The final combined candidate passed on **2026-11-13**: the serial full CMake
suite passed **182/182**, with no skips, in **317.48 seconds**; focused
ASan/UBSan checks passed **9/9**; the final mock frontend passed **11/11**, with
no skips; and all seven instrumented font cases passed. Actual RetroArch
1.22.2 ran TextDemo with automatic external font selection for 120 frames and
exited cleanly.

All eight default-feature platform cores built with all 25 ABI exports before
and after stripping. Windows Wine usual suites passed 4/4 per architecture,
plus twelve font cases and eighteen actual registered AMR fallback cases
(eight speech modes plus SID on each architecture). Those AMR probes exercise
actual service rejection followed by complete built-in retry; they do not
establish successful native Windows AMR decoding. ARM/Android full-core runtime
and native Apple/Windows codec coverage retain the limits above.

[System AMR verification](SYSTEM_AMR.md#verification-and-reproduction) records
the final log and generated report paths. Binary hashes, dependencies and
source-cohort checks are in `build/libretro-final-report.md` and
`build/libretro-final-artifacts.{json,sha256}`. Final package synchronization
passed after the document freeze: all eight packages contain thirteen frozen
notices, matching `.info` metadata and `BUILD_STATUS`, giving **120 byte-identical
notice/metadata copies**. Each core directory has exactly sixteen regular files,
including its binary. Checksums passed for all eight binaries and all 120 copied
files, with no binary hash changes. The package inventory and checks are in
`build/libretro-final-package-files.{json,sha256}` and the final platform report.

Historical baseline results remain useful for regression comparison. The
2026-11-12 font/codec candidate passed 176/176 CTest tests in 82.02 seconds;
MPEG-4 focused checks passed 5/5 normally and under ASan/UBSan, and MP3/AMR
checks passed 6/6 normally and 3/3 under ASan/UBSan. Font API and seven libretro
font sanitizer cases passed; rendered TextDemo hashes changed between built-in
and external glyphs. Windows x64/i686 Wine checks passed four cases per
architecture plus twelve font cases. These precede the latest complete AMR
and provider changes, so they are not final combined-candidate results.
The latest run and artifact evidence is recorded in
[system AMR verification](SYSTEM_AMR.md#verification-and-reproduction).

The font matrix can be reproduced with:

```sh
FRONTEND="$PWD/build/libretro-check/test_libretro_frontend" \
  tools/check_libretro_fonts.sh \
  "$PWD/build/libretro-check/libretro/nano_j2me_libretro.so"
```

Save states include Java execution state, front/back framebuffers and virtual
clock. They depend on architecture/ABI/endianness/build, matching JAR bytes,
relevant options and the CRC of the external font bytes. Envelope version 2
refuses states from the previous font-unaware version. Decoded heap PCM and
its media time/playback cursor survive restoration into a fresh VM without
opening a codec. Restore drops native audio handles and device queues;
encoded/native-only media can stop. It does not roll back RMS files,
FileConnection contents or external resources. No cross-platform state,
deterministic, rewind, runahead or netplay guarantee is made. Keep these
limitations in all submission descriptions.

## License review

The repository's [root license](../LICENSE) covers the project's MIT runtime.
It does **not** establish a blanket MIT license for every linked font, copied
routine, generated table or optional system library. No directory literally
named `src/third-party` was found; dependencies also occur inside ordinary
source headers. The following is a brief audit, not legal clearance.

| Component | Evidence / attribution | Required resolution |
| --- | --- | --- |
| Liberation Mono Latin tables | [mono](../src/gfx/nj_font_data.h), [AA](../src/gfx/nj_font_data_aa.h); source font version 2.1.5, SIL OFL 1.1 | ship the [full upstream notice](licenses/Liberation-Fonts-OFL.txt), preserve reserved-name conditions for derived tables |
| Source Han Sans CN CJK tables | [mono](../src/gfx/nj_font_cjk.h), [AA](../src/gfx/nj_font_cjk_aa.h); Adobe SIL OFL 1.1 | ship the existing [notice](licenses/source-han-sans-OFL.txt); retain font derivative license and attribution |
| Mascot Capsule parser/animation/raster adaptation | [model](../src/midp/nj_mascot_model.c), [animation](../src/midp/nj_mascot_anim.c), [raster header](../src/midp/nj_mascot_raster.h) identify FreeJ2ME-Plus commit `baca76a4dda6f30abac67fb4a3db009eed05f91c`, Yury Kharchenko / Roman Lahin, MIT | preserve included copyright/permission notices in source and binary notice package |
| MPEG-4 decoder | The identified transcribed transform, coefficient, prediction and packet paths have been replaced by original equation-based code or adaptations of pinned MIT OxideAV material. All seven MPEG-4 input arrays and four escape arrays regenerate without numeric changes. See the [implementation record](notes/2026-11-12-mpeg4-independent-implementation.md) and [table report](M4V_TABLE_PROVENANCE.md). | Preserve OxideAV's complete MIT notice and the reproducible source trail. The former identified-routine/table gates are superseded; this is not a formal whole-repository legal audit. |
| AMR numeric data | All 44 definitions now rebuild from 14 pinned OpenCORE Apache source files and project formulas; every typed value is preserved. No FFmpeg input remains in the [generator](../tools/gen_amr_tables.py). The [complete map](AMR_DATA_LICENSES.md) records all sources, transformations and checks. | Ship the [full Apache license](licenses/AMR-Apache-2.0.txt) and [PacketVideo/3GPP notices](licenses/AMR-OpenCORE-NOTICE.txt), including modification statements. The data retain Apache-2.0. |
| AMR runtime implementation | Spectral, pitch, pulse, gain, phase, synthesis and postfilter paths have independent equation-based replacements. The [complete bounded inventory](notes/2026-11-12-amr-independent-implementation.md) also explains every retained function/type group and records exact PCM and sanitizer checks. | Runtime code uses MIT; preserve source attribution and the separate data notices. The identified implementation review is complete within its scope, without a formal clean-room or whole-repository legal-audit claim. |
| Native AMR bridges | [System AMR](SYSTEM_AMR.md) documents MIT bridge code against host APIs, complete-output validation and original-input fallback. No system codec implementation is imported. | Preserve contributor notices; respect host service terms and record actual codec availability separately from mocks/compilation. |
| MP3 tables | All 33 static table blocks now regenerate from pinned puremp3/NLayer inputs under selected MIT grants or project-written formulas. Every final working value is retained apart from the documented 16 near-zero cosine corrections. | Keep all selected MIT notices in source/binary packages and retain the [source trail](CODEC_PROVENANCE.md#mp3-reproducible-mit-table-source). The [scoped implementation review](CODEC_PROVENANCE.md#scoped-mp3-implementation-review) identified no imported implementation; it is not a whole-repository authorship audit. |
| H.263 coding facts | The [generator](../tools/gen_h263_tables.py) extracts published numeric events directly from hash-pinned ITU H.263 (01/2005); all 20 prior arrays and all 4,096 lookup entries reproduce. | Retain the [provenance record](M4V_TABLE_PROVENANCE.md); this coding-fact reconstruction does not grant a blanket license to unrelated material. |
| Optional zlib / platform CRT | [CMake](../CMakeLists.txt) may discover zlib; toolchains contribute platform runtime dependencies | inspect final linkage and preserve applicable notices; CI/core-only recipe disables zlib discovery |

The default `.info` license field is now `MIT|OFL-1.1|Apache-2.0`. This records
MIT implementation code, OFL font-derived tables and Apache AMR numeric data;
it does not relicense those datasets under MIT. The former unresolved LGPL
warning has been removed after the identified routine/table replacements and
bounded implementation reviews. Historical concerns remain recorded in the
[codec provenance report](CODEC_PROVENANCE.md).

Default media features stay enabled. Selecting a system AMR provider does not
remove built-in fallback data from the binary. Source and binary packages must
include the root MIT license, third-party MIT notices, both font notices, the
full AMR Apache license and its PacketVideo/3GPP notices. Check the actual
linked configuration and optional platform dependencies for every artifact.
These obligations are part of release packaging, independent of upstream
acceptance.

### Liberation Mono provenance

The generated Latin headers identify these source hashes. The installed files
were checked against them and their embedded version/copyright/license records:

```text
LiberationMono-Regular.ttf  version 2.1.5
SHA-256 47ed5b5fcfe6b3c9228937b05de9c769f5fa55b777d539af0f65172f0a24c90b
LiberationMono-Bold.ttf     version 2.1.5
SHA-256 1d8d631105d4cb6c562a0d90f591d6393007d71350c089aec81c37dc8da293ed
Digitized data copyright (c) 2010 Google Corporation.
Copyright (c) 2012 Red Hat, Inc.
Licensed under the SIL Open Font License, Version 1.1
```

The full notice is copied from the official
[Liberation Fonts 2.1.5 LICENSE](https://github.com/liberationfonts/liberation-fonts/blob/2.1.5/LICENSE)
in [Liberation-Fonts-OFL.txt](licenses/Liberation-Fonts-OFL.txt). It retains
upstream wording, including its question-mark punctuation. This is attribution
for the font-derived data; the font authors do not endorse this core. For Source
Han, the generated headers record `SourceHanSansCN-Regular.otf` SHA-256
`e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4`; the existing
notice cites [Adobe's upstream license](https://github.com/adobe-fonts/source-han-sans/blob/release/LICENSE.txt).

## Draft PR descriptions (local only)

Do not submit these without explicit authorization. Replace the evidence
paragraph with the final candidate's commands, results, commit and artifact
hashes; do not turn pending checks into passed ones. The 2026-11-15 readiness
section above supplies the current metadata hash, CI-template simulations and
eight-target artifact hashes.

### libretro-super metadata PR

Suggested title: `Add nano_j2me core information for J2ME MIDlets`

```text
Adds nano_j2me_libretro.info for the native C99 J2ME runtime at
https://github.com/lucinhu/nano_j2me. It loads local JAR MIDlet-1 content without
an external JVM or BIOS. Metadata uses systemid j2me, Core Options v2, one
RetroPad plus keyboard/pointer input, and basic manual save states.

States are architecture/ABI/build dependent. Java state, framebuffer and decoded
heap PCM with its playback cursor are included. Native handles/device queues
are cleared; external RMS/FileConnection state is not rewound. No deterministic,
rewind, runahead or netplay support is claimed.

Implementation code is MIT, built-in AMR numeric data are separately Apache-2.0,
and font-derived tables retain OFL-1.1. The core keeps system-first AMR decoding
with complete built-in fallback; required licenses/notices accompany packages.

The core repository includes a CMake GitLab CI draft for Linux x86_64/aarch64,
Windows x86_64/i686 and Android arm64-v8a/armeabi-v7a/x86_64. Upstream buildbot
registration, pipeline execution and supported platform selection need
maintainer coordination. License review and final validation evidence must be
attached before this draft is submitted.
```

### libretro/docs documentation PR

Suggested title: `Document the nano_j2me J2ME core`

```text
Adds a nano_j2me library page following docs/meta/core-template.md, plus the
navigation, core-list and reviewed license-list entries. It documents JAR-only
loading, no BIOS requirement, input mappings and load-time options, rotation
geometry, per-game storage, and the architecture/build/resource limitations of
manual save states. Platform claims distinguish cross-build checks from device
runtime testing. Core Downloader availability is not asserted.

Validation: run mkdocs build in the upstream docs checkout and attach its
result after the final files and license descriptions are in place.
```

For buildbot coordination, provide the repository URL, core identifier, artifact
subdirectory `libretro`, desired jobs, selected dependency baseline, contact
maintainer and the final license/validation record. Ask maintainers which mirror
or repository integration change they require; the published guide does not
specify a universal self-service registration PR or guarantee acceptance.
