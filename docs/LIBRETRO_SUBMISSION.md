# libretro submission kit

This is a **submission kit**, not an announcement of upstream acceptance.  The
project repository is committed and pushed as part of routine work; on
2026-11-16 the project author explicitly authorized upstream submission and the
two prepared pull requests were opened from forks:
[`libretro/libretro-super#2204`](https://github.com/libretro/libretro-super/pull/2204)
and [`libretro/docs#1239`](https://github.com/libretro/docs/pull/1239).  No
merge or acceptance is claimed. Runtime code is MIT licensed, built-in
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
| `libretro-super` patch | [libretro-super-nano_j2me-info.patch](libretro/upstream/libretro-super-nano_j2me-info.patch) | one-file diff adding `dist/info/nano_j2me_libretro.info`; verified against `libretro-super` master `1b988c49` (and the earlier `fb15e63`) |
| `libretro/docs` patch | [libretro-docs-nano_j2me.patch](libretro/upstream/libretro-docs-nano_j2me.patch) | 5-file diff: library page with upstream front matter, `mkdocs.yml` nav, `core-list.md`, `licenses.md`, `see-also.md`; verified against `libretro/docs` master `d83b34b`, `mkdocs build` passes |
| Ready-to-post PR: `libretro-super` | [PR-libretro-super.md](libretro/upstream/PR-libretro-super.md) | title, patch commands, body and checklist for the `.info` PR |
| Ready-to-post PR: `libretro/docs` | [PR-libretro-docs.md](libretro/upstream/PR-libretro-docs.md) | title, patch commands, body and checklist for the library-page PR |
| CI template simulation | [tools/simulate_ci_builds.sh](../tools/simulate_ci_builds.sh) | local pre-submission check that the Linux/Windows/Android template command shapes build and place artifacts where the templates collect them |
| Bundle validator | [tools/check_libretro_submission_bundle.sh](../tools/check_libretro_submission_bundle.sh) | one command that re-checks the whole local bundle offline: `.info` byte-identity (source/bundle/release tree), the two exact-index patches, the docs page front matter, and optionally clean-clone apply + `mkdocs build`; also registered as the `check_libretro_submission_bundle` CTest case |
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
version is currently 0.5.8; refresh the metadata version whenever that changes.

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
  (SHA-256 `8cd968c9bcab94c798b62e6ac3467d5f1d0c1117a10b8fd6113347bc45c33c97`, version 0.5.9), with `memory_descriptors = "true"` because the core publishes the arena memory map
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
  | windows-i686 | `ee490673d218829ec8e8ea7ab85c48d70dd4d43cfbc21e35dd5df58f5335e47b` (r19, deterministic link) |
  | windows-x86_64 | `8bcdbd2ac402808c0325304175f1eeb2e69bcca401a832a52f9872d886543158` (r19, deterministic link) |

  Linux and Android hashes are unchanged from r17. The two Windows rows were
  regenerated in the r19 build (after the deterministic-PE change below); the
  earlier r17 PE DLLs differed only in COFF timestamp/checksum bytes.
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
  markers. The batch checker `tools/check_libretro_retroarch_corpus.sh` then ran
  the same end-to-end contract over **eight** corpus titles (3D, 2D, audio and
  save-state users) for 600 frames each, and over four of them for 1800 frames
  each — **14/14 runs passed** with clean exits and both core log markers
  (the runs are unpaced under RetroArch's null video driver, so they are an
  integration check, not a wall-clock frame-rate measurement; cadence is
  covered by the mock-frontend probes). The whole-corpus rendering probe on the
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
  suite passed **184/184 in 339.51 s** at this revision (the new `check_libretro_submission_bundle` case validates this offline bundle). Seven M3G titles were
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

## Update, 2026-11-15 (requirements audit, r18 revalidation and r19 deterministic Windows PE)

Rebuilt all eight release targets at HEAD `d10fe50` (`tools/build_libretro_cores.sh
--jobs 12 --out /var/tmp/nj2me-submission-dist-r18 all`, rc=0; 8 dirs x 16 files,
`.info` `c0f30aeae22150208034202986b3db77fd2f423874e2f2bcc1ce7b07c82723c6`
identical). Since `011247e` the tree changes are documentation, web assets and a
CLI profiler no-op; the rebuilt Linux and Android cores are byte-identical to the
r17 table above (same SHA-256), while the two Windows DLLs differed only in PE
timestamp/checksum bytes.

That remaining Windows difference is now fixed: GNU `strip` on PE rewrites the
COFF header with the build time, and the linker also embedded a build timestamp,
so `tools/toolchain-mingw-w64.cmake`, `tools/toolchain-mingw-w64-i686.cmake` and
`tools/build_libretro_cores.sh` now link Windows cores with
`-Wl,--no-insert-timestamp -Wl,--build-id=none` plus link-time `-s` and skip the
post-link `strip` for PE targets. Two independent clean builds of both Windows
targets are now byte-identical (COFF `Time/Date` is the Unix epoch), and Wine
still reports 4 passed / 0 failed. All eight targets were then rebuilt into the
**r19** package and that tree is now the canonical `build/libretro-dist`
(the r17 tree was backed up to `/var/tmp/libretro-dist-20261115-r17-backup.tgz`).
On r19 the linux-x86_64 mock matrix reports 20/20, real RetroArch 1.22.2 still
runs LoopDemo 120 frames to a clean exit, and the Windows DLL passes Wine 4/4.
See the reproducibility notes in
[the readiness note](notes/2026-11-15-libretro-submission-readiness.md).

Full revalidation on that build (logs under
`.local-tools/debug-runs/submission-20261115/`, gitignored):

- mock frontend matrix: 20 passed, 0 failed (`r18-mock.log`)
- real RetroArch 1.22.2: LoopDemo 120 frames PASS, corpus batch 8/8 titles x
  600 frames PASS (`r18-ra-loop.log`, `r18-ra-corpus.log`)
- Windows x86_64 DLL under Wine: 4 passed, 0 failed (`r18-wine.log`)
- libretro state round trips: 50/50 (`r18-state50.log`)
- whole-corpus rendering probe at 300 frames: 372/373, only
  `毁灭战士3_10598.jar` (slow loader, documented) times out
  (`goal16-libretro-corpus/r18-full.log`)
- full CTest: 184/184 in 369.41 s (`/tmp/r18-ctest.log`)
- CI template simulation: 3/3 PASS (`/tmp/r18-ci-sim.log`; Linux 3,973,848,
  Windows 3,541,251, Android arm64 11,555,520 bytes)

### Upstream requirement audit

| Upstream requirement | Local evidence | Remaining external action |
| --- | --- | --- |
| `.info` in `libretro-super/dist/info` | byte-identical file in the bundle; patch applies to clean `fb15e63`; validator checks payload identity | open the `libretro-super` PR |
| Root `.gitlab-ci.yml` using libretro CI templates | `.gitlab-ci.yml` selects the Linux/Windows/Android templates; all three template command shapes simulated at HEAD and artifact paths match | core repository/mirror and pipeline registration by libretro maintainers; first upstream CI run |
| `libretro/docs` library page and indices | 5-file patch applies to clean `d83b34b`; `mkdocs build` passes and renders the page | open the `libretro/docs` PR; upstream docs CI |
| Optional assets / content database | `database = "Mobile - J2ME"` names an existing libretro-database platform, so no new `.dat` is needed; no icon artwork was prepared (optional, not required) | none required for first submission |
| Libretro API obligations | video callback exactly once per `retro_run`, input polled at least once, no libretro API use outside the permitted contexts, fixed `retro_serialize_size` computed at load, `needs_fullpath = "true"`; checked by the mock-frontend conformance matrix | none |
| Core/artifact naming | `<name>_libretro.so` / `.dll`; the Android template renames to `_libretro_android.so` | none |

Conclusion: no open local implementation item remains for the submission kit.
The only remaining steps are the two upstream PRs plus maintainer-run CI and
pipeline registration, all of which require explicit authorization and are not
performed by this preparation.

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

## Update, 2026-11-15 (round 32): M3G span fast path and the r20 package

A measured rendering optimisation landed this round (details and methodology
in `docs/PERFORMANCE.md`, section "M3G Gouraud textured span fast path"):
`sw_span_gouraud_texargb()` handles the Gouraud + nearest-textured + MODULATE +
opaque-blend span class that the general loop served for roughly half of a
textured 3D title's spans. It was verified with a deterministic-prefix
comparison (the first 120 presented frames of the profiled title are
run-to-run reproducible; all dumped prefix frames are byte-identical between
the previous build and the new one) and measures **-4.6% user CPU** on the
20k-frame profile run.

The **r20** candidate package (`build/libretro-dist/`, built from a clean
worktree at `91392ee` plus that patch with
`tools/build_libretro_cores.sh --jobs 12 --out /var/tmp/nj2me-submission-dist-r20 all`,
8 targets x 16 files, `.info` `c0f30aea…`) is the package to submit:

| Target | Size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,823,384 | `ab709a3b7995767b2d567042be8f8d0918db4cb5f429923df3c9f231312fb760` |
| linux-aarch64 | 3,307,176 | `107c2a2af6875a72e4d8b236af85a1c2e06ccc5ae99ca3075eb491d7fc7f7502` |
| linux-armv7 | 3,015,464 | `f7cae5d456c9dfddbfb97968349c9381284c8891bb5db8eba46e8db817750491` |
| android-arm64 | 3,536,112 | `1b45f8f8b3d8027436f105c352879e302ca2169ccb25992add05a5d97c722343` |
| android-armv7 | 2,725,160 | `13d4f77ac38911d62c0d8f5c5da26ba174318429660aa628b8c912588c689b00` |
| android-x86_64 | 3,582,088 | `8d4746ea1cc24bbbde2bfe888a0e0d32585c455efd29efc0cb16718134722fb7` |
| windows-i686 | 3,432,448 | `be76b2fe234d7e32753db1089a068c9b7a1d7bfd8c8194e817f1336a00ac2da8` |
| windows-x86_64 | 3,211,264 | `e9109375e9209f199cf408b784e759099c5669e5cfb8fdb56b7f42a76237c088` |

Validation of r20: mock-frontend matrix **20/20** (a first r20 matrix run
flaked one audiodemo serialize check while the corpus probe and the full test
suite were saturating the machine; the immediate rerun passed 20/20), whole-
corpus rendering probe **372/373** (only the documented slow loader fails;
same number as r19, so the optimisation introduces no rendering regression),
full CTest **184/184 in 458.09 s**, real RetroArch 1.22.2 LoopDemo 120 frames
PASS, Windows x86_64 under Wine **4/4**. The libretro state probe was not
re-run this round; the change is rasteriser-only and CTest covers the
serialize paths.

## Update, 2026-11-15 (round 32b): r21 package (real-time pacing + M3G span fast path)

A second candidate was packaged after the real-time virtual-clock/audio pacing
change (`4ae7790`) landed on top of the M3G span fast path. The **r21** tree is
in `build/libretro-dist/` (r20 backed up to
`/var/tmp/libretro-dist-20261115-r20-backup.tgz`), built from a clean worktree
at `a90831e` with `tools/build_libretro_cores.sh --jobs 12
--out /var/tmp/nj2me-submission-dist-r21 all` (8 targets x 16 files, `.info`
`c0f30aea…` unchanged):

| Target | Size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,827,480 | `87ed2fa913fba5bbfed0d7be8218d95c2d326b7ff299852eccaa55bebbec4f57` |
| linux-aarch64 | 3,308,328 | `e754bfdde5f2f61a5545354a8bb7920e8ce8e012ccf549d1b43b608eddada915` |
| linux-armv7 | 3,016,728 | `4ce97996a8b1bec5b289d6a3840ae52c04d92da3505b4c183eeda91db67b2c30` |
| android-arm64 | 3,537,360 | `fd3c0160bcd7b3c7851e7c735ca80c54f278aad828970820cee6bc46d103cde5` |
| android-armv7 | 2,726,136 | `edfe6a8f8e06fff15afe6b674d523f814bba6781f236597e76233841462fbd90` |
| android-x86_64 | 3,583,512 | `b77c15328f30a61a19513933b1da9f7356fc768acb006c9e6182fb3924de4490` |
| windows-i686 | 3,434,496 | `4b03d6c042a87345f820caa6fca1e2fef0a658fcea28fc00a6bf262870cdda03` |
| windows-x86_64 | 3,213,312 | `6352ab126998fa3d1a67d75974ea456c5e452a03d298aee5d3f4dc3483af948d` |

Validation of r21: mock-frontend matrix **20/20**, whole-corpus rendering probe
**372/373** (only the documented slow loader fails, same as r19/r20 → no
rendering regression from either the pacing change or the fast path), real
RetroArch 1.22.2 LoopDemo 120 frames PASS, Windows x86_64 under Wine **4/4**;
the clean-worktree test suite passes **181/181 in 364.63 s** (this tree
carries the generated `tests/fixtures` set; the committed CI configuration
generates it with the `nj_fixtures` target).

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

## Update, 2026-11-15 (round 43): public-clone reproducibility

A fresh `git clone` of origin/main at `3684016` was used as the only input:
`tools/simulate_ci_builds.sh` satisfied all three upstream CI template command
shapes from the clone (linux 3,978,184 B, windows 3,546,661 B, android arm64
11,588,344 B), the submission-bundle validator reported 16/16 by itself and
32/32 with the r24 release tree passed via `--dist`, and the clone-built linux
core ran `loopdemo` in real RetroArch 1.22.2 plus a whole-corpus visual probe of
370/371 (only the documented slow Doom3 loader fails). Nothing outside the
public tree is needed to apply the two upstream patches or build the targets.

## Update, 2026-11-15 (round 48): real-frontend save-state round trip

The mock frontend proves serialize/unserialize at the API level; the user path
also needs the frontend's own save/load plumbing. New tracked check
`tools/check_libretro_retroarch_state.sh` launches RetroArch 1.22.2 headless
twice against the same content: run 1 plays 150 frames with
`savestate_auto_save = "true"` (RetroArch writes the state when the content
unloads), run 2 plays 60 frames with `savestate_auto_load = "true"` and must log
`[nano_j2me] resumed from a save state`.

Result on the canonical r24 linux-x86_64 core (sha256 `d9feff13...`): PASS.
The raw loopdemo snapshot is 140,816,472 bytes with the auto heap, and
RetroArch's `.state.auto` file is 170,087 bytes -- the frontend's state
compression makes even auto-heap states practical on disk, while rewind buffers
still hold raw snapshots (hence the rewind sizing guidance above).

## Update, 2026-11-15 (round 51): ready-to-post PR bodies and a clean-clone rerun

The two upstream changes now have ready-to-post PR descriptions in the
bundle, so opening them needs no rewriting:

- [`docs/libretro/upstream/PR-libretro-super.md`](libretro/upstream/PR-libretro-super.md)
  — target repo/branch, baseline `fb15e630655375a50317be2a7953477ef9a5d12a`,
  exact patch/commit commands, body text and checklist.
- [`docs/libretro/upstream/PR-libretro-docs.md`](libretro/upstream/PR-libretro-docs.md)
  — target repo/branch, baseline `d83b34b21b8a2c14db573aea66d876e343d316ca`,
  the five changed files, body text and checklist.

Re-ran the end-to-end rehearsal on a fresh `git clone --depth 1` of
`origin/main` at `52752b0` (the current HEAD, after the round 44–50 test
tooling commits; no core sources changed since the r24 package):

- `tools/simulate_ci_builds.sh --out /var/tmp/r51-ci --jobs 8` → **3/3
  contracts PASS**: linux `.so` 3,978,184 B, windows `.dll` 3,546,661 B,
  android `.so` 11,588,344 B (template renames it to `_android.so`).
- `tools/check_libretro_submission_bundle.sh` inside the clean clone →
  **16 passed, 0 failed** (bundle-only mode; the gitignored fixtures and
  release tree are absent by design), and **32 passed, 0 failed** from the
  main tree with `--dist /var/tmp/nj2me-submission-dist-r24`.
- The clone-built Linux core passed the mock-frontend matrix **20/20** and a
  real RetroArch 1.22.2 load/run/exit check (120 frames).

The r24 hash table above remains the canonical release artifact table.

## Update, 2026-11-16 (round 52b): r25 package

r25 replaces r24 as the canonical package in `build/libretro-dist/`. It is
built from revision `43219e6` (the only later commit, `dd91bfc`, is
documentation/tooling only, so the binaries match the core sources). It adds:

* the malformed-bytecode hardening from `a296ce8` (array-operand ownership
  guards that turn corrupt operand-stack references into
  `java/lang/VerifyError`, plus the GIF canvas pixel budget from the long fuzz
  campaign), and
* the async constructor/load path from `a2abdee` and `68bffb9` (a MIDlet
  loading screen can no longer block `retro_load_game()`, and a constructor
  that runs the whole game loop is waited out instead of killed), and the
  `SYSTEM_RAM` memory map/cheat-search window from r24.

| target | size | sha256 |
|--------|------|--------|
| linux-x86_64 | 3,843,912 | `117a992ff2195d63e64a58a034f1e38dfbab5124d80057e4e2d4527a9a89cce4` |
| linux-aarch64 | 3,324,104 | `4e62066a4f0d2fa151e4f44c4b2d6d7f95d3bc5ad24ab345043f389374014917` |
| linux-armv7 | 3,031,988 | `707c413d5f40e2457941ebd165a1d2de21cf848a1b50933edaa150e7e975ed23` |
| android-arm64 | 3,556,856 | `5c9aed7e98dca2b82201171aca184c737d3b27360639d90cd2faf9f8a7f19400` |
| android-armv7 | 2,744,772 | `6da97d9d6fa914f9af1a5c36b2b0d9d1b96336685cc7aade9088b0a3b25ca5a8` |
| android-x86_64 | 3,597,168 | `a17d45e3787d4b8c375af0d584dc55aebe92e653da5533e8ea130a2ef1e15c64` |
| windows-i686 | 3,451,392 | `3dffa1b0875716ce23093e7386657ab57d2eb30b41ad750b604ebb2c8de19c19` |
| windows-x86_64 | 3,226,624 | `4704da8b51dfdcf1ea2334b8c945e611aa719797ef450ab58af0a1efa97bb1a2` |

`.info` is unchanged: `740df7353c181bd76955f04a64508c4c1d1994509c1b4290b3b420b04da424e1`.

The release-verified tree is archived at `/var/tmp/nj2me-submission-dist-r25b/`
(and the previous canonical r24 at `/var/tmp/nj2me-submission-dist-r24/`).
The shared `build/libretro-dist/` working directory is rebuilt by the
concurrent web/GLES2 workstream, so compare hashes with the table above before
treating it as canonical (round 53 found it replaced by a different build).

Validation on r25: mock frontend matrix **20/20**; real RetroArch 1.22.2
LoopDemo 120 frames PASS; Windows x86_64 under Wine **4/4**; whole-corpus
rendering probe **371 titles: 370 passed, 1 known limitation (the documented
slow `毁灭战士3_10598` loader), 0 failed**; libretro state-corpus probe
**50/50 round trips** at the 60 Hz cadence a frontend uses; full serial CTest
on the same tree **189/189 in 349.38 s**.

One documented behaviour change: a frontend that requests a state *before the
asynchronous MIDlet construction finishes* can no longer be trusted with that
state. Real frontends call `retro_run()` at 60 Hz and are unaffected, and the
state probe now paces frames for that reason; the note
`docs/notes/2026-11-15-libretro-async-load-early-state.md` records the
reproduction and the recommended `load_ready` gate for the load/state path.

## Update, 2026-11-16 (round 53): validation refresh on r25

r25 remains canonical; no core sources changed this round. The verification set
was re-run against `build/libretro-dist/` and extended:

* full serial CTest **189/189 passed** (349.38 s) on the r25 tree;
* libretro option matrix **88/88** profiles passed (8 corpus titles × 11
  non-default profiles through the mock frontend);
* real RetroArch 1.22.2 corpus batch **8/8** titles passed at 600 frames
  (serial rerun; one earlier batch lost an end-of-run marker while another
  probe saturated the machine — re-running alone passed);
* whole-corpus rendering probe at **600 frames**: 371 titles, **370 passed,
  1 known limitation (the documented slow `毁灭战士3_10598` loader), 0
  failed**, using a private copy of the r25 core;
* libretro state-corpus probe **50/50** at the 60 Hz cadence;
* long-run deep probe: 24 titles × 1800 frames, **0 failures** (2 optional
  titles stop themselves by design).

The hardening campaigns were extended to **5000 cases each** — class files
(any class in the JAR), JAR containers and media decoders — with **zero
crashes and zero hangs**. Details and the oracle caveats (concurrent real-
frontend batches and the parallel sweep can flake; reproduce standalone
before calling a defect) are in
`docs/notes/2026-11-16-r25-validation-and-fuzz-campaigns.md`.

## Release 0.5.8 (r26), 2026-11-16 — synchronized callbacks + version bump

Engine and metadata version moved to **0.5.8** (`.info`
SHA-256 `48ceccf6fc7110a605809c90877a4e16d57ff5e0275503c88ac1f6385790b791`,
`display_version = "0.5.8"`). This release also carries the round-54
`fix(core): acquire monitors for synchronized JNI-bridge callbacks`.

**Artifacts** built from clean tree `52c04a0` with
`tools/build_libretro_cores.sh --jobs 12 --out /var/tmp/nj2me-submission-dist-r26 all`
(eight targets, 16 files each):

| Target | Core size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,843,912 | `2960da47b3d4cd56dd36c5f4c6d6312d40dc779323c8143f28c815269e959e8f` |
| linux-aarch64 | 3,324,488 | `192216818e8e2124382fee0a5f36e9539d756db7d0b54b1590ce4b34da80f270` |
| linux-armv7 | 3,032,420 | `6d17fad81f6b5a38eff61aec08e77defced4fd4abf738daeb38350b02e837c31` |
| android-arm64 | 3,557,272 | `1db919222f4fca4dac5cff1b9f452f6bb88b166415504b18674514de8cd86684` |
| android-armv7 | 2,745,044 | `29919a1c54df50e44dd9a6d4893dfc5f93a5e940fbfefe6891f1511a0b92befe` |
| android-x86_64 | 3,597,520 | `b837ae505de5bf90e711d9b1a132b3abb87ffe49301e7abd65efee3603e65286` |
| windows-x86_64 | 3,227,136 | `6efd4ca692542dc83f4cda09fb0bc1d204c9d5e665feba3c10187752d9f9ef8e` |
| windows-i686 | 3,451,904 | `5f0c8f667c2e6e0f925dc4d9d9fc0bcae056bdc07e1d62ca0f3a901106f84982` |

**Validation**: mock-frontend matrix 20/20 on the linux core; real RetroArch
1.22.2 alphademo 300 frames PASS; Windows x86_64 Wine 4/4; full 404-title
corpus sweep **372 ok / 27 title-exit / 2 title-exception / 2 no-frame /
0 timeout / 1 bad-jar** (`compat-20261116-r54b`, all non-ok rows previously
adjudicated); full serial CTest **189/189 PASS in 357.49 s**.
`build/libretro-dist/` now holds this package, and the GitHub release
`v0.5.8` carries `nj2me-0.5.8-libretro-cores.zip` (SHA-256
`729cbcf21bf5c9db8ce669297d420ce3b599f77bb1ee120b9a62da4c39e4eafa`).

**Upstream**: with the author's explicit authorization (2026-11-16) the two
prepared pull requests were opened — `libretro/libretro-super#2204` (#2203 was
superseded after the branch history was rebuilt with an upstream parent) and
`libretro/docs#1239`. The remaining external steps are the upstream CI run and
review; nothing is merged or accepted yet.

### Update, 2026-11-16: `libretro/docs#1239` rebased

Upstream `libretro/docs` master advanced to
`177c669990b659e7cfd00add4564b85c2a692179` (2026-11-16) after the first PR
revision, which made the PR show as conflicting.  The fork branch
`add-nano_j2me` was rebased onto that commit, the docs patch regenerated
(`docs/libretro/upstream/libretro-docs-nano_j2me.patch`, still 5 files,
+229 lines), `mkdocs build` re-run successfully (page 98,129 bytes), and the
branch force-pushed.  `libretro/libretro-super#2204` remains mergeable against
its unchanged baseline.

## Release 0.5.9 (r27), 2026-11-16 — state buffer bound fix

Patch release over 0.5.8. Version moved to **0.5.9** (`.info`
SHA-256 `8cd968c9bcab94c798b62e6ac3467d5f1d0c1117a10b8fd6113347bc45c33c97`,
`display_version = "0.5.9"`); the super patch was regenerated from the new
file. The release carries the save-state buffer bound fix for object-dense
titles (`docs/notes/2026-11-16-state-buffer-bound.md`) and the multi-target
media fuzz gate.

**Artifacts** built from clean tree `2efd2c7` with
`tools/build_libretro_cores.sh --jobs 12 --out /var/tmp/nj2me-submission-dist-r27 all`
(eight targets, 16 files each):

| Target | Core size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,843,912 | `3e167228644c68bc64293fb86db371f9b096f256fc4f32c7ece4fc9b6fe12d70` |
| linux-aarch64 | 3,324,712 | `daa219235b2aa1c86be2d75c1a2c496dc969278b0b25ab366ffa3a7c7a961bdb` |
| linux-armv7 | 3,032,580 | `c44a0437a17d7a3b02b61034cffa10cfb51eaa4a656f9aea60a180f6ff1e34f8` |
| android-arm64 | 3,557,496 | `7852d996a4051d2e6fe6ef16e8105f2068a7547dd9e7039858403aff0d9ebae1` |
| android-armv7 | 2,745,172 | `d6355503a67c703808c2f50bf1551e0600c26fb0d67196678ffbf3d49aa55e33` |
| android-x86_64 | 3,597,648 | `3ee448916d7d5efafb25af3d076d54706aa2950e070848abf2665518874bfcb9` |
| windows-i686 | 3,452,416 | `4122ecbece527bdf1a07ee288eca33dbf1ec8f58b7d0627fa1f3d897f6fa88e4` |
| windows-x86_64 | 3,227,648 | `95abe1323ce7d3f5f3c1ad2b755e0ee5eb4d1f6d8be3d68dd6be6ec7f9f9685b` |

Validation: mock frontend matrix **20/20**; `MinecraftJ2ME_PE_0.6.1_V2.2.jar`
save-state round trip 45/45 checks (`serialize_size=157,593,664`); real
RetroArch 1.22.2 alphademo 300 frames PASS; Windows x86_64 under Wine 4/4; full
serial CTest **190/190 in 357.09 s**; 60-title state probe 60/60. Canonical
copy installed in `build/libretro-dist/` (previous tree backed up at
`/var/tmp/libretro-dist-20261116-pre-r27-backup.tgz`); GitHub release `v0.5.9`
carries `nj2me-0.5.9-libretro-cores.zip` (SHA-256
`a60e3cec993dda6e8aa833d1741ec73a76a8e5b3874a355bd518454be3f9a1a9`) and the
checksums file. The two upstream PR branches were updated to the 0.5.9
`.info`.

## Update, 2026-11-16 (round 64): 0.5.11 release for the pacer-owned nap

The frame pacer now owns the idle nap: the cooperative scheduler reports
whether an idle pass did real (non-main-thread) work, returns early when it did
not, and the pacer sleeps for a bounded slice of the remaining frame budget
(`docs/PERFORMANCE.md`, `docs/notes/2026-11-16-paced-cpu-burn-investigation.md`).
Paced 2D titles dropped from ~9-10 s of CPU per 10 s of wall time to 0.05-0.09 s
at the same 30 fps, with no change to unpaced throughput, loader-heavy titles,
the 404-title sweep distribution (372/27/2/2/0/1) or the libretro results.

**Artifacts** built from clean tree `ca0e43d` with
`tools/build_libretro_cores.sh --jobs 12 --out /var/tmp/nj2me-submission-dist-r30 all`
(eight targets, 16 files each, `.info`
`67048b11bdefad3448aab34b83d42fe32ec2a1d3bd149ac7b617e38356c46d0e`,
`display_version = "0.5.11"`):

| Target | Core size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 3,843,912 | `e29593a0e59e56da252855047f65b3bdf331997db6e8168e5098c058bb6b01af` |
| linux-aarch64 | 3,325,256 | `e2c90af45b7d4cf5e8fb145441da83dff30306bca5d9d54571879681f4cf162f` |
| linux-armv7 | 3,032,868 | `fc3dc72a7357cd30c64378330ae5c2f76536b8064f9587961a77293a44ba3058` |
| android-arm64 | 3,559,672 | `6de50818786605a493ae6e9a6d6790631b271a3e1cc66a397ebfd3d2f83ea054` |
| android-armv7 | 2,747,076 | `35ee559ba38f98b25e338ed7123b7fd6e4ecbd7214537053e1ae20edd0e17cd1` |
| android-x86_64 | 3,599,920 | `f17761e4003d36f0888df1be25c8432772e43e317b2fbbc09dbc66f4e27a8013` |
| windows-i686 | 3,453,440 | `a8f4e1d84653a7fc4a2f8c5ac60b9b256b3ccc6df9c02496cc7638d8bffb05b1` |
| windows-x86_64 | 3,228,160 | `e2c1d1196db39aec564d19d27a1bd35089fbe03efd2fbd1b7de62ee841c21652` |

Validation: mock frontend matrix **20/20**; real RetroArch 1.22.2 loopdemo
120 frames PASS; Windows x86_64 DLL under Wine 4/4 at 120 and 300 frames (the
120-frame audiodemo audio check is Wine-timing sensitive: 6/7 passes, one lone
flake on a loaded host); `MinecraftJ2ME_PE_0.6.1_V2.2.jar` state round trip
45/45 (`serialize_size=157,593,664`); option matrix 88/88; libretro state probe
12/12; full serial CTest **192/192**. Canonical copy installed in
`build/libretro-dist/` (previous tree backed up at
`/var/tmp/libretro-dist-20261116-pre-r30-backup.tgz`); GitHub release `v0.5.11`
carries `nj2me-0.5.11-libretro-cores.zip` (SHA-256
`86babcd96fc0e66203aa7516a714bada81dec995840d74c2f733397197f07992`) and the
checksums file. The two upstream PR branches are updated to the 0.5.11 `.info`.

## Update, 2026-11-16 (round 71): 0.5.12 release with multi-touch

Tree `21a79f7` (multi-touch commit `12d3fa4` plus 0.5.12 metadata). `.info`
SHA-256 `288a1aa423b16e46f4f397d9b86959b31bd6e01a45af22b71216d2d70850db73`,
`display_version = "0.5.12"`; the libretro-super patch applies to `fb15e63`.

| target | size (bytes) | SHA-256 |
|--------|--------------|---------|
| linux-x86_64 | 3,843,912 | `a5e7b8ecaa1a2351dbc6ec970145f3c5cad2008d58b9bd1c28df1e2eb8108e8c` |
| linux-aarch64 | 3,326,360 | `abe1f9bdc605b6ad269d4c93f1e23df2aec0db141644523343f61be226d02406` |
| linux-armv7 | 3,034,308 | `d5a4d96964db1f4c08cf084afa2745cb301319253a06f270b56d1bf77b826724` |
| android-arm64 | 3,560,824 | `5ad0e5eb698af213f96b59c29f911938761fdf3652d9b142cdb04b87b3507a01` |
| android-armv7 | 2,747,844 | `29d207de0487a1d4e765cf7942fd1d858d552a2ff53bc788253ea7dd8a30d607` |
| android-x86_64 | 3,601,040 | `f265550868f15bea1c5aae7744d138c2612295229d226fd15b0e29e301e7393d` |
| windows-i686 | 3,454,976 | `b8a7a488ceb86ef94f32d84683afc6a3eb418d549bdc8af171e7563cb5d95423` |
| windows-x86_64 | 3,230,208 | `86a925c4a3241c8c1a8cc8513da451842409151612673799c9279512df6bd835` |

Validation: mock matrix 20/20; option matrix 66/66; real RetroArch loopdemo
120 frames PASS; Wine 4/4 at 300 frames; full CTest 193/193; corpus sweep
387 ok / 12 title-exit / 2 title-exception / 2 no-frame / 1 bad-jar. Release
archive SHA-256 `7467092702a939b7c0dc16c3d3a025fab5d8f41337c5eb48f59eb0d451d7d58b`;
GitHub release `v0.5.12` also ships `nj2me.apk` (3,275,114 B) and
`nj2me-legacy.apk` (701,372 B). Both PRs remain open and mergeable; no merge
claimed.

## Update, 2026-11-17 (round 81): 0.5.13 with cheat codes

The libretro core now implements `retro_cheat_set()`/`retro_cheat_reset()` in
one documented core-specific form, `<arena offset>:<hex bytes>`, re-applied at
the start of every `retro_run()` over the `SYSTEM_RAM` arena window; `.info`
sets `cheats = "true"`, and the mock frontend also parses `pressed@X,Y` entries
in its key scripts (the old parser split on every comma and dropped the tap).
Metadata is 0.5.13, `.info` SHA-256
`cd52e1a0f260aeb818a51c1af7f09dcd62ab85db7595b0b88753a41f77131530`; the r32
package is installed in `build/libretro-dist` and an eleven-target release
bundle is published with tag `v0.5.13`.

Validation on the r32 package: mock matrix 20/20; real RetroArch 1.22.2
LoopDemo 120 frames PASS; Windows x86_64 under Wine 300 frames 4/4; CTest
`check_libretro_cheat` PASS (apply, per-frame re-apply, disable, malformed,
reset); submission-bundle validator 32/32; full CTest 196/196 at the release
tree. The upstream PRs (libretro/libretro-super#2204 and libretro/docs#1239)
now carry the 0.5.13 `.info`; no merge or acceptance is claimed.

## Update, 2026-11-19 (round 87): 0.5.14 with the shared cheat engine

The runtime now owns the cheat engine (`src/core/nj_cheat.c`): the libretro
port, the headless CLI (`--cheat`, `--cheat-file`) and the Android build share
one per-VM table, and the APK reads `<jar dir>/cheats/<stem>.txt` (plus the
files/RMS dirs) at start. Metadata is 0.5.14, `.info` SHA-256
`497b24a6eff931e6cf109ba1f582d8a5c4c477b906ee5b99326cbcdb50ab2fd8`.

The r33 package was built from a clean worktree at revision `51c892c`
(eight targets) and installed as the canonical `build/libretro-dist`. Core
hashes are listed in `docs/RELEASE_0.5.14.md`. Validation: mock matrix 20/20,
real RetroArch LoopDemo 120 frames PASS, Windows x86_64 Wine 4/4 at 300
frames, option matrix 66/66, state round trips 12/12, full serial CTest
197/197. Both APKs were rebuilt at 0.5.14 and published with the release
bundle (tag `v0.5.14`).

Upstream: the `libretro-super` branch was rebuilt on the current master with
the 0.5.14 `.info`; the docs page already documents the cheat format and needs
no version change. Both pull requests stay open; no merge or acceptance is
claimed.

## Round-90 end-to-end rehearsal (2026-11-19)

Re-ran the whole rehearsal on the 0.5.14 tree (`6a478a4`) so the "ready to
submit" evidence stays current:

- `tools/check_libretro_submission_bundle.sh` → **32 passed, 0 failed**.
- The same check with a clean `libretro-super` / `libretro/docs` clone pair
  reset to the recorded upstream baselines (`1b988c49…`, `177c6699…`), both
  patches applied and the docs site built with real `mkdocs` →
  **41 passed, 0 failed**.
- Fresh `git clone --depth 1` of origin/main at `6a478a4`:
  `tools/simulate_ci_builds.sh --jobs 8` satisfied **all three upstream CI
  template contracts** (linux `.so` 3,999,920 B; windows `.dll` 3,565,867 B;
  android `.so` 11,700,936 B after the template rename to
  `nano_j2me_libretro_android.so`).
- In that clone, `tools/build_libretro_cores.sh linux-x86_64` produced a core
  (3,848,056 B, sha256 `5f6ac53b059a288215d1fd780252659bd4bd48f904e223ec8fce826ad3b0874c`)
  that passed the mock conformance matrix **20/20** and real RetroArch 1.22.2
  LoopDemo for 120 frames.  Clone-built binaries differ from the release r33
  hashes by design (different build directory/flags); the rehearsal verifies
  behaviour, not byte identity.
- Conclusion: the kit still builds and applies from a pristine checkout, and
  no local state blocks the external submission steps.

## Release 0.5.15 / r34 (2026-11-19)

The metadata moves to 0.5.15 with the Android in-game cheat editor
(`b98062c`, `docs/notes/2026-11-19-android-cheat-editor.md`); the core
implementation is identical to r33.  `.info` SHA-256
`d9f107094647cbbd206dbbc3148bfdf032d1341d18e53590939820ebef59f6c4`; tracked
super patch regenerated against master `1b988c491e31f1cc768572f3d68f45e69d194e5b`
(37 lines, one added file, `git apply --check` passes).

r34 built from a clean worktree at `f18ed5c` (eight targets, installed as the
canonical `build/libretro-dist`).  Hashes and validation: mock 20/20, real
RetroArch LoopDemo 120 frames PASS, Wine 4/4 at 300 frames, bundle 32/32, full
CTest 197/197 — see `docs/RELEASE_0.5.15.md`.

## Round-94 end-to-end rehearsal (2026-11-19, 0.5.15)

Re-ran the whole rehearsal on the 0.5.15 release tree (`8b20d50`) so the
"ready to submit" evidence matches the published release:

- `tools/check_libretro_submission_bundle.sh` → **32 passed, 0 failed** on the
  canonical `build/libretro-dist` (r34).
- The same check with clean `libretro-super` / `libretro/docs` clones reset to
  the recorded baselines (`1b988c49…`, `177c6699…`), both patches applied and
  the docs site built with real `mkdocs` → **41 passed, 0 failed**.
- Fresh `git clone --depth 1` of origin/main at `8b20d50`:
  `tools/simulate_ci_builds.sh --jobs 8` satisfied **all three upstream CI
  template contracts** (linux `.so` 3,999,920 B; windows `.dll` 3,565,867 B;
  android `.so` 11,700,936 B after the template rename).
- In that clone (with the test fixtures copied in), the CI-template-built
  Linux core passed the mock conformance matrix **20/20** and real RetroArch
  1.22.2 LoopDemo for 120 frames.
- Upstream state: `libretro/libretro-super#2204` OPEN/MERGEABLE at head
  `576d189` (0.5.15 `.info`), `libretro/docs#1239` OPEN/MERGEABLE at
  `5917f7d`; both 0 comments / 0 reviews — awaiting upstream review, no merge
  claimed.


### 0.5.16 / r35 (2026-11-19)

- Version bumped to `0.5.16`; both `.info` copies are byte-identical with
  SHA-256 `e372f211ed6c26e55bd23160e14d5e55422a54452209a7ed65032bf55c3c57da`
  (`display_version = "0.5.16"`). The super patch was regenerated from a fresh
  `libretro/libretro-super` clone at master
  `1b988c491e31f1cc768572f3d68f45e69d194e5b` (37 lines, one added file,
  `git apply --check` passes).
- r35 package built from clean worktree at `64ad3cf`
  (`/var/tmp/nj2me-r35-tree`) into `/var/tmp/nj2me-submission-dist-r35`; the
  canonical `build/libretro-dist` was swapped (previous tree backed up
  `/var/tmp/libretro-dist-20261119-pre-r35-backup.tgz`). Hashes:

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3,848,064 | `3c993241be743fc7a82395cfd7f4dd420cda43a09e21029ba641a80b9d8a3305` |
| linux-aarch64 | 3,331,296 | `8844bc88e15213cd218ac7b1e88978949bf5a545938a09ee2cd03762fcab586a` |
| linux-armv7 | 3,038,512 | `b782c0d16dd89f2e9fce17b7f29ae8160786f5e8380436dc54ac3b42030e8324` |
| android-arm64 | 3,565,904 | `424e41db70cf6aaddd794889a39acd8dcbc2759d5afc47b1b3907e2a37262b07` |
| android-armv7 | 2,751,136 | `a234b145abfe857038451d90b39874c47d8ae67fae2bf2daeca7eb5acc2465fe` |
| android-x86_64 | 3,606,040 | `df12b5e210b7cc0d07f878e5be1a510e87599030708a695d4962c19d1cf573da` |
| windows-i686 | 3,459,584 | `119d1f4021c4097c14f37e50a0f301377b1bbe455763d84482379194626eaa3f` |
| windows-x86_64 | 3,235,328 | `1e0a8479e19acf46bbd2f7c30aff6146962c3314433553c6e6cb2c7d877fe701` |

- Validation: mock conformance matrix **20/20**; real RetroArch 1.22.2
  LoopDemo 120 frames **PASS**; Windows x86_64 under Wine **4/4** at 300
  frames; submission bundle check **32/32**; full serial CTest **197/197 in
  347.93 s**.
- APKs rebuilt for 0.5.16 (`versionCode 516`, `versionName 0.5.16`):
  `nj2me.apk` 3,295,594 B
  `763ed720e7c3650a04be2303e7b48e6418842c7a27bee6e81178e0e607d3f931`;
  `nj2me-legacy.apk` 703,979 B
  `c594b9e6241144bc7b0a1747a20e76ce12255b1083725cbebe4f53fd0efd4fad`.
- Release package `nj2me-0.5.16-libretro-cores.zip` 11,346,011 B, SHA-256
  `bfeb3738a9b45251ea4a5726e275c6ed3c5de9bfddee863a56a3ea2e26a4cb3e`.
- GitHub release: https://github.com/lucinhu/nano_j2me/releases/tag/v0.5.16
  (tag `v0.5.16`). Upstream PR bodies updated to 0.5.16; super branch rebuilt
  on the current master and force-pushed; both PRs remain OPEN/MERGEABLE and
  await review — no merge claimed.

### Round-99 verification of the released 0.5.16 tree (2026-11-19)

A fresh `git clone --depth 1` of origin/main at `de58803` (the 0.5.16 release
commit) was used to re-run the whole submission pipeline from scratch:

- `tools/simulate_ci_builds.sh --jobs 8` satisfied **all three upstream CI
  template contracts** (linux `.so` 4,000,096 B; windows `.dll` 3,567,227 B;
  android `.so` 11,707,944 B after the template rename).
- In that clone (with `tests/fixtures` copied in), the CI-template-built Linux
  core passed the mock conformance matrix **20/20** and real RetroArch 1.22.2
  LoopDemo for 120 frames (**PASS**).
- `tools/check_libretro_submission_bundle.sh --super-clone … --docs-clone …
  --apply-clones --mkdocs …` with fresh baseline clones
  (`libretro/libretro-super` `1b988c49…`, `libretro/docs` `177c6699…`):
  **41 passed, 0 failed** (both patches apply and the docs site builds).
- Whole-corpus libretro visual probe on the r35 core (300 frames, 6 parallel
  jobs): 387 titles → **383 passed, 3 known limitations, 1 failed**. The
  single failure, `杀鬼子-敌后武工队_2185` (a save-state restore check), passed
  all 45 harness checks in three standalone runs with an immutable private core
  copy (`/tmp/r99-core/core.so`), so it is the same load-sensitive state flake
  documented for rounds 78/95 (six instances each holding a 128 MB arena), not
  a regression.
- Upstream PRs #2204 (super, `d933b05` with the 0.5.16 `.info`) and #1239
  (docs) remain OPEN/MERGEABLE with 0 comments / 0 reviews; no merge claimed.

### 0.5.17 / r36 (2026-11-19)

- Version bumped to `0.5.17`; both `.info` copies are byte-identical with
  SHA-256 `93c4862a4974e134aaf3311b0e1be811dd1c8f1d8962881ed5c794b043525b85`
  (`display_version = "0.5.17"`). The super patch was regenerated from a fresh
  `libretro/libretro-super` clone at master
  `1b988c491e31f1cc768572f3d68f45e69d194e5b` (37 lines, one added file,
  `git apply --check` passes). Commit `d6eef33`.
- r36 package built into `/var/tmp/nj2me-submission-dist-r36`; the canonical
  `build/libretro-dist` was swapped (previous tree backed up
  `/var/tmp/libretro-dist-20261119-pre-r36-backup.tgz`). Hashes:

| target | bytes | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 3,848,064 | `1bf54556c63a37d07e122f6a4cbd3713874a19b20caeedede506bdd48dc04b62` |
| linux-aarch64 | 3,331,296 | `615a6096b92e53ba07c2260753d33cf3c9935919ecc74a9ea04b9fcce3636435` |
| linux-armv7 | 3,038,512 | `03ddbbff442074726b021080f1bfb9101500e269bd3aa7fd5ce1cc652fcc4813` |
| android-arm64 | 3,565,904 | `321a3cf14fac7a9e9c64d065cd6b9e3a43a14b6ffb4d14f37da4893a6316b356` |
| android-armv7 | 2,751,136 | `d31cbb80a6b70868cfe35000f65c69fa189ff57b36551c926b401c223e20a711` |
| android-x86_64 | 3,606,040 | `5fbd02a5f648286d5937e542f1cea6316e2d2aee5aa14581cbbfc65bddf50c52` |
| windows-i686 | 3,459,584 | `561ad8726dfed1c6f4f3eef438411db2814ed0273950b0e8866f208d1d8aea43` |
| windows-x86_64 | 3,235,328 | `e31236e1eb7784b2a343bf9dda5627033473e9703dc4e2f766895a9a2f1ad91e` |

- Validation: mock conformance matrix **20/20**; real RetroArch 1.22.2
  LoopDemo 120 frames **PASS**; Windows x86_64 under Wine **4/4** at 300
  frames; submission bundle check **32/32**; full serial CTest **197/197 in
  347.29 s**.
- APKs rebuilt for 0.5.17 (`versionCode 517`, `versionName 0.5.17`):
  `nj2me.apk` 3,295,594 B
  `b5e8dc8190d2a9924c996e020dc9a7de86212566966ec8696d8c291ff1efc081`;
  `nj2me-legacy.apk` 704,229 B
  `e4158a2f6c7c2baa329b40693fd4d42a295ac75433a847770746d4cabe4f71e8`.
- Release package `nj2me-0.5.17-libretro-cores.zip` 11,347,105 B, SHA-256
  `5876c9f162aa528d0186fca3fcc70d2015f61b637bc99422da84f82c6d122651`.
- GitHub release: https://github.com/lucinhu/nano_j2me/releases/tag/v0.5.17
  (tag `v0.5.17`). Upstream PR bodies updated to 0.5.17; super branch rebuilt
  on the current master (`1b988c49…`) as commit `bf88d89` with the 0.5.17
  `.info` (raw GitHub content confirms `display_version = "0.5.17"`) and
  force-pushed; PR #2204 is OPEN/MERGEABLE at head `bf88d89`, PR #1239 at head
  `5917f7d`; both await review with 0 comments / 0 reviews — no merge
  claimed.

### Round-102 verification of the released 0.5.17 tree (2026-11-19)

Same end-to-end rehearsal as round 99, now from a fresh `git clone --depth 1`
of origin/main at `084c29a` (the 0.5.17 release tree):

- `tools/simulate_ci_builds.sh --jobs 8` satisfied **all three upstream CI
  template contracts** (linux `.so` 4,000,096 B; windows `.dll` 3,567,227 B;
  android `.so` 11,708,520 B after the template rename).
- In that clone (with `tests/fixtures` copied in), the CI-template-built Linux
  core passed the mock conformance matrix **20/20** and real RetroArch 1.22.2
  LoopDemo for 120 frames (**PASS**).
- `tools/check_libretro_submission_bundle.sh --super-clone … --docs-clone …
  --apply-clones --mkdocs …` against the recorded baselines
  (`libretro/libretro-super` `1b988c49…`, `libretro/docs` `177c6699…`):
  **41 passed, 0 failed** (both patches apply, the docs site builds).
- Whole-corpus libretro visual probe on the r36 core (300 frames, 6 parallel
  jobs): 387 titles → **383 passed, 3 known limitations, 1 failed**. The
  single failure, `杀鬼子-敌后武工队_2185` (`serialize_writes_state:
  retro_serialize(157593664) returned 0`), passed all 45 harness checks in
  three standalone runs with an immutable private core copy
  (`/tmp/r102-core/core.so`), so it is the same load-sensitive state flake
  documented for rounds 78/95/99 (six probes each holding a 128 MB arena; the
  direction, save vs restore, varies), not a regression.
- Upstream PRs #2204 (super, head `bf88d89` with the 0.5.17 `.info`) and #1239
  (docs, head `5917f7d`) remain OPEN/MERGEABLE with 0 comments / 0 reviews; no
  merge claimed.
