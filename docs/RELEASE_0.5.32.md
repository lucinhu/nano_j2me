# nano_j2me 0.5.32 — libretro r51

Release date: 2026-11-22. Tag `v0.5.32`.

This release implements the Nokia UI **TextEditor** overlay family (the last
API-coverage item from the KEmulator reference roadmap) and includes the
field-level extension of the builtin-API audit plus the whole-corpus state
probe and ASan/UBSan tooling evidence gathered since 0.5.31.

## Highlights

* **Nokia `TextEditor` + `TextEditorListener`.** `com.nokia.mid.ui.TextEditor`
  is now a real `CanvasItem` subclass: both static factories, content
  get/set/insert/delete, `getMaxSize`/`setMaxSize` (with truncation),
  constraints and initial input mode, caret and selection (only meaningful
  while focused), font and background/foreground/highlight colours,
  multiline flag, and the internal `_invokePaint`/`_contentChanged`/
  `_inputAction` hooks game code uses. `TextEditorListener` exposes the twelve
  `ACTION_*` constants and receives `inputAction(TextEditor,int)` callbacks
  (content changes and explicit actions are forwarded).
* **Overlay compositing fix.** The CanvasItem painter used to dispatch
  `_invokePaint` with `CanvasGraphicsItem` as the owner; it now dispatches on
  the item's real class, so TextEditor (and any future CanvasItem subclass)
  paints on top of the canvas after its paint.
* **Field-level member audit.** `nj2me --dump-builtin-api` also dumps field
  constants/instance fields (with flags), and `tools/check_vendor_members.py`
  audits every `Fieldref` in the corpus through the superclass chain. Result
  on the measured corpus: 296,486 member refs over 195 referenced classes,
  zero missing runtime fields, and only the three title-side method gaps
  already allowlisted.
* **Corpus evidence refreshed.** `tools/check_libretro_states.py` now runs the
  full corpus in parallel (`--jobs`), replays fetched rows through their
  recorded `jar_path` and per-title canvas size, and the full-corpus probe is
  1033/1036 state round trips (the three known long-frame titles stay
  documented). `tools/check_corpus_asan.py` adds a whole-corpus ASan+UBSan
  campaign that is clean over all 1050 measured titles.
* TextEditor limitations are documented: single-line layout (no scrolling),
  no touch-control panel (`TextEditorTouchControl`/`TextEditorExtensionAccess`
  exist as classes only), and the S40/S60 subclasses remain stubs.

## Artifacts

Core package `nj2me-0.5.32-libretro-cores.zip` (128 files, top directory
`nano_j2me-0.5.32-libretro`):

| target | library | size | sha256 |
| --- | --- | --- | --- |
| linux-x86_64 | `nano_j2me_libretro.so` | 4,044,720 | `0fd33c30e63dc644bd27d86371d56bfd8c5e4e574abec02cf2dc79cd7ed07657` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,516,672 | `54679527a6398d92eea6c0ec1cda5896287a6755cc8b75bcb99f360538a5a64b` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,170,984 | `82e22a7bf773b438188975a6d9bffcbff733e702191a1ddbae0a6befc3e5ee29` |
| android-arm64 | `nano_j2me_libretro.so` | 3,762,224 | `f85479aaf23c78b5d887ad2a37b63dc9c76a48fe0b56d126199c4bb1d46a9c7d` |
| android-armv7 | `nano_j2me_libretro.so` | 2,860,876 | `8e949beebfde515388e67e51b30d5cc8581b7c10616254b5a62ce5558cc88056` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,788,352 | `8a0f26309be3efbf9159a76b10987355517b210ea149d61c9be31ce3f430256a` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,595,776 | `067ceb75380d6b20f753323c772945e1e0cc5a3178b86180af008c01c769f309` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,366,400 | `215fc703952a895c74b25131c5824f04b1362959ca8277740599fd53659fb834` |

The `.info` file ships in every target directory (`display_version = "0.5.32"`,
SHA-256 `411161e48b3bb07c455719a4aedc38b2207030f9d21a26e8cc6aaf1c2445baf2`).

Android APKs (`versionCode 532`; four phone ABIs build the GLES2 backend by
default, legacy stays software):

| APK | size | sha256 |
| --- | --- | --- |
| `nj2me-0.5.32-arm64-v8a.apk` | 1,054,876 | `c5081bb14d33f48e41d79b56c008faa7706433ecf8e51c74b1f82ecc1ff04450` |
| `nj2me-0.5.32-armeabi-v7a.apk` | 1,063,070 | `50665e3707ad2d9f90e6b98d9dde363f6bd3ec1dff00373a182a14519d5fe8b0` |
| `nj2me-0.5.32-x86.apk` | 1,042,582 | `0750b6705383f987aec65057f67444fe3be0347ead894f4e0900cebf7e725a84` |
| `nj2me-0.5.32-x86_64.apk` | 1,058,969 | `5ec30a65dc8d26e3912b873c8bf6230c431a85700be76e0a720a62d798a3645d` |
| `nj2me-0.5.32-legacy.apk` | 739,954 | `8590eedaf69b88cac487922a0d083739f82e7c1d98a3a77d4a7d808cab0a935a` |

Release asset `nj2me-0.5.32-libretro-cores.zip` is 12,161,494 bytes, SHA-256
`653cb10fac1b8c19f32581b2132daddef9841382eb816b6aecef6dbd457d88a4`;
`SHA256SUMS-0.5.32.txt` lists the zip and the five APKs.

## Verification

* Full serial CTest on the release tree: **209/209 passed in 364.01 s**.
* Mock frontend matrix: **20/20 passed**.
* Real RetroArch 1.22.2 LoopDemo, 120 frames: **PASS** with core log markers.
* Windows x86_64 under Wine: **4/4 passed** at 300 frames.
* Submission bundle self-check: **32/32 passed**.

## Upgrade notes

Core option names and save-state semantics are unchanged since 0.5.31; the
Nokia TextEditor additions are pure API surface (no new core options). Save
states exported by 0.5.8–0.5.31 are refused by the version CRC as before.
TextEditor overlays are composited during paint, so they appear in save-state
screenshots exactly as on the canvas.

## Upstream

* Super PR: https://github.com/libretro/libretro-super/pull/2204 — branch
  `add-nano_j2me` at commit `a4859b5` on `libretro-super` master
  `55e50a8`; OPEN/MERGEABLE, no merge claimed.
* Docs PR: https://github.com/libretro/docs/pull/1239 — branch at
  `95a1d0a` against docs master `96ee874`; OPEN/MERGEABLE, no merge claimed.
