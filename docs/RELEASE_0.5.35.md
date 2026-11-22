# nano_j2me 0.5.35

Released 2026-11-23, from metadata commit `7cc9d8f`.

This is a runtime compatibility release: the corpus grew by 673 JARs (the Sony
Ericsson K700/K800 collection) and the new titles found three real bugs that are
fixed here.  The libretro core, the Android APKs and the CLI all carry the same
fixes; software rendering remains the default and GLES2 stays opt-in.

## Highlights

- **`Class.newInstance()` follows the JVM same-package rule.**  A package-private
  class is accessible when the caller is in the same runtime package; only
  cross-package access raises `IllegalAccessException`.  `3D_Tetris` builds its
  scene object this way and now boots (600 frames, 191 paints, 71,647 draw
  calls).  The `tests/excdemo` fixture covers both sides of the rule.
- **Sprint PCS `PlayerListener` constants** are real static fields on the
  interface class instead of declarations missing from the runtime.
- **Vendor member coverage**: `Integer.longValue()`,
  `CustomItem.invalidate()`, `ServiceRecord.set/getDeviceServiceClasses()`,
  `AffineTrans(IIIIIIIIIIII)V`, `Effect3D.set/getSphereTexture()` and
  `Graphics3D.dispose()` are implemented (no handset resources exist to free, so
  `dispose()` is a no-op and the service-class bits are stored on the record).
  `check_vendor_members` now only accepts the three title-side patched-ABI
  classes listed in `tools/api_member_allowlist.txt`.
- **Corpus**: the persistent cache is 1316 unique JARs (439 MB); the
  compatibility matrix is 2462 titles, 2420 ok, regenerated deterministically.
  The new batch's other non-ok rows are bad archives, titles that exit without
  painting, or titles throwing their own exceptions while continuing to paint.

## Artifacts

libretro cores (`nj2me-0.5.35-libretro-cores.zip`, 128 entries, 12,171,959 B,
SHA-256 `286faeb1457fe3870cc50e850b31ff847b399c5b3f061205d9de2b753aa19eee`):

| target | bytes | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,048,832 | `356b1eea8fd934af4ab3bd573eaf3469ed97abafeefa7be0f632c9fe0eebb238` |
| linux-aarch64 | 3,522,008 | `a6ebc35085602c7397dc5c3f580106217af37f761de1f2fd81e5ba28df662c1b` |
| linux-armv7 | 3,174,880 | `8bedc7889a84cf43b323dfc6753cf28e7c438c2170b3ea5aac9b2ab10d191852` |
| android-arm64 | 3,767,720 | `f39bca8bfa17706bfb58a2f9edeb31ebadcea237ad2a79acfefd406ef080f43a` |
| android-armv7 | 2,863,972 | `943f9ca22a2a0a232235a414b8d83e1bbbdb200a38ba3f75e46ef353e01d11e7` |
| android-x86_64 | 3,793,568 | `bfc05d746a620fad8d7e404d6d7a700b4f1ae4a20195801f9553cd0e20587fa9` |
| windows-i686 | 3,599,872 | `f8145dba91144a58fff1eb39e7357f07e3c34198f30303a11941cd97853ac30c` |
| windows-x86_64 | 3,369,984 | `b4fa6f7b91de03996be9eaf9ff0fdb00fba426c50c79db23953a18ac169cf71e` |

The `.info` metadata has SHA-256
`633b6ab16e21c02c3e93f4ceb1e483a7f741fffe7d8ff7ea8ae6c8d225773091`
(`display_version = "0.5.35"`); the tracked `libretro-super` patch applies to
master `55e50a8` as fork commit `a198cc8`.

Android APKs (versionCode 535, one ABI each; the legacy build is ARMv5/API 5):

| file | bytes | SHA-256 |
| --- | --- | --- |
| nj2me-0.5.35-arm64-v8a.apk | 1,054,876 | `c7929c6f2e7287256d84e9f4cb45537220eec2f1dfe9d3f4d4e46080bee45c3e` |
| nj2me-0.5.35-armeabi-v7a.apk | 1,063,070 | `9d31ad35e1b2b7e116a78dd3900fd798637903117da0a3c9a41411bbefe82374` |
| nj2me-0.5.35-x86.apk | 1,042,582 | `46e1c7d729c5ddc95f418238ecf8f971c35100b012b83feae749d6792422fef9` |
| nj2me-0.5.35-x86_64.apk | 1,058,969 | `d3f652cc5e5355fbd6cd82a9f22e80f47783c7f094bb9641f7278ec92fba4cb7` |
| nj2me-0.5.35-legacy.apk | 741,124 | `bc5ef509920e2d20ff420ec8b8881e9a079551652cb7addd0201536d03492598` |

`SHA256SUMS-0.5.35.txt` in the release carries the zip and the five APKs.

## Verification

- Full serial CTest: **210/210 passed** (377.06 s).
- Libretro mock frontend matrix: **20/20**.
- Real RetroArch 1.22.2 LoopDemo, 120 frames: PASS.
- Windows x86_64 under Wine, 300 frames: **4/4**.
- Submission bundle validator: **32/32**.
- Whole-corpus libretro visual probe (r54 core, 300 frames, per-title canvas
  sizes): see `docs/CORPUS.md` round-209 section.

## Upgrade notes

- Save states are versioned by the runtime version CRC, so 0.5.34 states are
  refused by 0.5.35 (and vice versa) even though the state format itself did not
  change.
- The desktop/Android builds still default to the software renderer; start with
  `--graphics gles2` (SDL2 builds) or the libretro core option to try the
  hardware path.

## Upstream submission

- `libretro/libretro-super#2204`: info-only, branch `add-nano_j2me` at fork
  commit `a198cc8` (parent master `55e50a8`), OPEN/MERGEABLE.
- `libretro/libretro-docs#1239`: docs page, head `95a1d0a` (baseline
  `96ee874`), OPEN/MERGEABLE.

Neither is merged; both are ready for review.
