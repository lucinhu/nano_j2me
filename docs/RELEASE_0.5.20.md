# nano_j2me 0.5.20

0.5.20 is a compatibility release.  The class library now materialises
`java.lang.Boolean.TRUE` / `Boolean.FALSE`, which CLDC does not require but
J2SE-shaped ports read with `getstatic`; titles that died on
`NoSuchFieldError: java/lang/Boolean.TRUE` now start and run.

## Highlights

* **`Boolean.TRUE` / `Boolean.FALSE` materialised** (`src/core/nj_lib_boxing.c`):
  the two boxed singletons are created during builtin setup (direct object
  construction - the native-call machinery is not available yet at that
  point), so `getstatic java/lang/Boolean.TRUE` returns a real object with
  `booleanValue()` true/false.  Found by the expanded corpus: `Alco Bench.jar`
  went from a startup `NoSuchFieldError` to 600 frames / 448 paints.
* **Corrupt JAR handling in the sweep** (`tools/sweep_compat.py`): a pre-flight
  ZIP integrity check reports a damaged archive as `bad-jar` with the offending
  entry, instead of blaming compatibility for a later `NoClassDefFoundError`;
  `tools/make_compat_matrix.py` skips damaged entries when reading constant
  pools.  The compat matrix was regenerated from the merged history:
  **1414 titles, 1398 运行正常**.
* **Draw-call counters recorded per sweep row** (`gfx`, `natives`): a title that
  paints frames but issues no draw call is now visible without a visual probe.

The libretro core is otherwise feature-identical to r38: full-screen mode
changes on a visible Canvas, multi-touch, cheat codes/value search,
SYSTEM_RAM memory map and the measured rewind limits are unchanged.

## Artifacts

| target | size | SHA-256 |
|---|---:|---|
| linux-x86_64 | 3,852,160 | `50195aa1398fe2d27dc768098f681c4ac5803a5306ce94a5da8c1c6bb39ab86f` |
| linux-aarch64 | 3,332,336 | `f425068a7a53ef3344984764b1f49a3db0e107fac4bf2d706a6718b3d8514d27` |
| linux-armv7 | 3,039,720 | `88aebbcc0cd28e3c2df1ec61fa55e1af0c67187ee630018f8b6a36ff1ccc4e50` |
| android-arm64 | 3,567,104 | `84046d9355435d05bdbcb97ba5ccd44d228f8a783acccc9eee74c14def4b4db5` |
| android-armv7 | 2,752,120 | `2668696884191fa7669cefe1de41bc5bc9a0f470ad1aada8f35cd0d049b0f6f4` |
| android-x86_64 | 3,607,480 | `3052d7526d1785d181c19dd7a6257489816553eb53b73cac3b59f33c7f99b56d` |
| windows-i686 | 3,460,096 | `160e7bea2ad3cccd75a092b99b6cb6ed0f973b18181c574f8151e99ce49e0c80` |
| windows-x86_64 | 3,236,352 | `7cce95959680492632d771d87a3ede3ea1c82fc9465526f22897bf7a781dfa65` |

Release bundle: `nj2me-0.5.20-libretro-cores.zip` (11,352,213 bytes,
SHA-256 `1ec812c6a7f067a79403ca89fae9f07eecafb612452c27d804c5ce85da5cbb61`),
containing all eight targets with `.info`, licenses and notices.  Metadata:
`.info` SHA-256 `f1323bd73c5a798e845fda550d8d0ab837991e70e3473f5bddcd22135115c20e`.

### Android APKs

| file | version | size | SHA-256 |
|---|---|---:|---|
| `nj2me.apk` | 0.5.20 (versionCode 520), 4 ABIs, minSdk 21 | 3,299,690 | `74d1bae20ca39d00547fa7a096a3d41981725ea0bcb507b618fb5d4d252107f6` |
| `nj2me-legacy.apk` | 0.5.20-legacy, minSdk 5 | 704,318 | `0553315f171bc213bc085603d66bb489420f8299aecb7577f34521698f8164ec` |

## Verification

* full CTest: 199/199
* libretro mock conformance matrix: 20 / 20
* real RetroArch 1.22.2: LoopDemo 120 frames PASS
* Windows x86_64 under Wine: 4 / 4
* submission bundle: 32 / 32
* whole-corpus libretro visual probe (HEAD core, 845 renderable titles):
  840 passed / 2 known limitations / 3 fetched-title black screens
  (documented in `docs/notes/2026-11-21-fetched-black-screen-titles.md`)

## Upgrade notes

* Save states from 0.5.8-0.5.19 are refused by the version CRC; start a fresh
  state after upgrading.
* Android: install over the previous APK; existing games, settings and states
  are kept.

## Upstream

Ready-to-post material lives in `docs/libretro/upstream/`; PRs
libretro/libretro-super#2204 and libretro/docs#1239 track the current metadata
and library page.  No merge is claimed.
