# nano_j2me 0.5.12

Release date: 2026-11-16

This release is about input: the runtime, the Android frontends and the
libretro core now keep every finger the frontend reports, so J2ME titles that
ask for multiple pointers (the Nokia `MultipointTouch` API, used by J2ME
Minecraft PE and other touch games) see the whole hand instead of a single
averaged point. The Android APK also ships the same day.

## Highlights

* **Multi-touch everywhere.** `nj_event` carries a pointer id. The MIDP
  `Canvas` pointer stays with the first finger that lands (that is what a
  handset did), while every finger is fed to the Nokia MultipointTouch
  listener. libretro reads `RETRO_DEVICE_ID_POINTER_COUNT` and emits per-finger
  down/move/up; frontends that only report a mouse behave exactly as before.
  Android forwards every `MotionEvent` pointer through `PadPress` with its
  pointer id, so the on-screen keypad and multi-touch titles both see all
  fingers. `--keys` scripts learned `pressed#N@X,Y`, `dragged#N@X,Y` and
  `released#N@X,Y`.
* **New fixture and gate.** `tests/multitouchdemo` drives two fingers through
  the Nokia listener and the Canvas API; `check_multitouch` asserts the
  listener ids/coordinates, the single-finger Canvas contract and that the
  second finger never becomes the Canvas pointer. `test_nokia_touch` covers
  two independent fingers and id clamping; the Android Java `PadPressTest`
  gained a two-finger-on-the-picture case.
* **Cores rebuilt** from tree `21a79f7` (0.5.12 metadata on top of the
  multi-touch commit `12d3fa4`). `memory_descriptors = "true"` is kept, so the
  cheat-search memory map and `SYSTEM_RAM` window still work.

## Artifacts

| Target | Size (bytes) | SHA-256 |
|--------|--------------|---------|
| linux-x86_64 | 3,843,912 | `a5e7b8ecaa1a2351dbc6ec970145f3c5cad2008d58b9bd1c28df1e2eb8108e8c` |
| linux-aarch64 | 3,326,360 | `abe1f9bdc605b6ad269d4c93f1e23df2aec0db141644523343f61be226d02406` |
| linux-armv7 | 3,034,308 | `d5a4d96964db1f4c08cf084afa2745cb301319253a06f270b56d1bf77b826724` |
| android-arm64 | 3,560,824 | `5ad0e5eb698af213f96b59c29f911938761fdf3652d9b142cdb04b87b3507a01` |
| android-armv7 | 2,747,844 | `29d207de0487a1d4e765cf7942fd1d858d552a2ff53bc788253ea7dd8a30d607` |
| android-x86_64 | 3,601,040 | `f265550868f15bea1c5aae7744d138c2612295229d226fd15b0e29e301e7393d` |
| windows-i686 | 3,454,976 | `b8a7a488ceb86ef94f32d84683afc6a3eb418d549bdc8af171e7563cb5d95423` |
| windows-x86_64 | 3,230,208 | `86a925c4a3241c8c1a8cc8513da451842409151612673799c9279512df6bd835` |

Each core directory contains the core, `nano_j2me_libretro.info`
(SHA-256 `288a1aa423b16e46f4f397d9b86959b31bd6e01a45af22b71216d2d70850db73`,
`display_version = "0.5.12"`), `LICENSE`, `BUILD_STATUS.txt` and the license
tree.

Android APKs (debug-signed, no game bundled):

* `nj2me.apk` — 3,275,114 bytes, `arm64-v8a armeabi-v7a x86 x86_64`,
  versionCode 512, versionName 0.5.12
* `nj2me-legacy.apk` — 701,372 bytes, API 5 / ARMv5 edition,
  versionName 0.5.12-legacy

## Verification

* `check_multitouch` 13/13; `test_nokia_touch`, `test_save`, `test_vm`,
  `test_libretro_frontend` and `check_pointer` green.
* Full CTest at the multi-touch tree: **193/193 passed in 382.07 s**.
* libretro conformance matrix on the r31 Linux core: **20/20 passed, 0
  failed**; option matrix 66/66 (6 titles × 11 profiles).
* Real RetroArch 1.22.2: loopdemo 120 frames and 600 frames exit 0 with the
  core log markers verified.
* Windows x86_64 core under Wine: **4/4 fixture checks passed** at 300 frames.
* Android Java suite (including the new two-finger PadPressTest) passes.
* Corpus: full 404-title sweep with the per-title key scripts and the
  pointer-free generic recipe — 387 ok / 12 title-exit / 2 title-exception /
  2 no-frame / 1 bad-jar (no timeout). The 12 title-exit rows are game-side
  ends or probe-gated titles; the recipe change is documented in
  `docs/notes/2026-11-16-generic-input-recipe.md`.

## Upgrade notes

* Save states: the snapshot envelope carries a version CRC, so states saved
  by 0.5.8–0.5.11 are refused, not misread. Start new states after upgrading.
* libretro cheats: the arena is exposed as `SYSTEM_RAM` and as a memory map;
  frontend cheat search works, `retro_cheat_set` remains a no-op (libretro
  cheat codes cannot describe a managed heap).
* Multi-touch through libretro needs a frontend that reports more than one
  `RETRO_DEVICE_POINTER`; RetroArch on Android does. Desktop mice still send
  one pointer and behave as before.

## Upstream

Ready-to-apply submission materials live in `docs/libretro/upstream/`
(libretro-super `.info` patch, libretro/docs page patch, PR bodies). Two PRs
are open against upstream and await review; no merge is claimed here:
`libretro/libretro-super#2203` and `libretro/docs#1239`.
