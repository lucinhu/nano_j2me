# nano_j2me 0.5.30

A compatibility-release: two member-level API bugs found by the new linkage
scanner are fixed, and the release ships the usual libretro core package plus
per-ABI Android APKs with GLES2 enabled by default.

## Highlights

- **Inherited Canvas key methods now resolve through interfaces.**
  `Canvas.getGameAction/getKeyCode/getKeyName` are declared by MIDP as instance
  methods, but the runtime only registered static entries (for obfuscated
  games that emit `invokevirtual` against a static method).  A Canvas subclass
  that implements an interface through the inherited method
  (`Pro Snowboard`: `v extends FullCanvas implements s`) failed with
  `AbstractMethodError`.  The tables now keep the static entries and add
  instance entries after them, so CP resolution still links the static form
  for obfuscated bytecode while `invokeinterface` finds the instance one.
  `snowboardr` now renders: 300 frames / 300 paints / gfx=6719 / tris=14990.
- **Mascot Capsule `AffineTrans.set(IIIIIIIIIIII)V`.**  `MINI 3D` passes the
  twelve matrix entries as arguments instead of an `int[]`; the overload was
  missing, so every frame threw `NoSuchMethodError` and nothing 3D was drawn.
  Added the 12-int native (row-major `m00..m23`).  `mini3draci` now runs
  300 frames / 216 paints / gfx=2477 / tris=3436 with zero linkage errors.
- **New tool `tools/check_corpus_linkage.py`** replays every sweep row briefly
  and reports `NoClassDefFoundError` / `NoSuchMethodError` / `NoSuchFieldError`
  / `AbstractMethodError` / `IllegalAccessError` /
  `IncompatibleClassChangeError` / `UnsatisfiedLinkError` / `ClassFormatError`
  / `ExceptionInInitializerError`.  Over the full 1046-title corpus it reports
  exactly one remaining hit — `ArcadePark2`'s self-missing `GlomoCountry`
  class (title-side packaging bug, fails the same way on a handset).

## Artifacts

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| `nj2me-0.5.30-libretro-cores.zip` | 12,087,815 B | `84cdd2741ed4119a19ef48fdb738d7976dfd80f57471505042efaf8fe077977e` |
| `nj2me-0.5.30-arm64-v8a.apk` | 1,042,588 B | `6a3ac34255546f91f00cd06b0fc7353d5d283638efdd39ea637ee51d10a63c1d` |
| `nj2me-0.5.30-armeabi-v7a.apk` | 1,054,878 B | `8dffc314caa135741747db2ad18dffabd7f078b53b382a76b71787dcd18c9242` |
| `nj2me-0.5.30-x86.apk` | 1,034,390 B | `6594957b9cc94c0cc537bae8ce13d5cf0ebca78e2ee4ad0fc6372b8c40f87ee7` |
| `nj2me-0.5.30-x86_64.apk` | 1,050,777 B | `c3b07109c556cd17b2a398f15f0240155d6b0920742c6b6156e84695090c1ad0` |
| `nj2me-0.5.30-legacy.apk` | 732,778 B | `d45a09fb0720df845b164cce12c38145f9fbaccefb797b169092d170c4faa64a` |

All APKs carry `versionCode 530` / `versionName 0.5.30`; the four
GLES2-capable APKs (arm64-v8a, armeabi-v7a, x86, x86_64) enable the OpenGL ES
2 backend by default (panel toggle switches back to software), and the
`-legacy` ARMv5/API 5 APK stays software-rendered.

### libretro cores (r49)

| Platform | Size | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,003,760 B | `64eca3c3687ca097e506f224ba013369705f7e4df803518d79b718abfede1e04` |
| linux-aarch64 | 3,479,680 B | `1a19f72fde2c289eac7a5e6f193a93de67ca56cc8bf893fe6a619fd551eff439` |
| linux-armv7 | 3,143,428 B | `0b7df65cc33c5936b2892ef2730afd464b8b2f31ba82f7818576432126ed943f` |
| android-arm64 | 3,721,672 B | `dd373a889b91948d7c1502cf7c8cd99b90c02dca13a07f4c19c04b3e56f1f6fc` |
| android-armv7 | 2,837,516 B | `c9b36bdfaf757bab6ffc7403386934ff5feab84f98c4464de6036284e80c8d3c` |
| android-x86_64 | 3,750,576 B | `ad55623044e5d7168a5f6a4f147a69be521c2f40790e9aaed35ab22af57fba02` |
| windows-i686 | 3,567,616 B | `24da1e4629e902bf50518ad0aa86fa694e172f19fad4ab76d7e35a63015f8593` |
| windows-x86_64 | 3,339,776 B | `e243f37430a61f8e404edde405fb0f004a1b68dfe66fd97a936b72d319c439e1` |

The core `.info` is identical to the tracked source copy
(`SHA-256 b3c7426efa61c640f57a753cf0b2a3d73ff0c79dbe8fddabf98c8341a45691ee`).

## Verification

- full serial CTest: **207/207 passed** (362.16 s);
- libretro mock frontend matrix: **20/20 passed**;
- real RetroArch 1.22.2 loopdemo (120 frames): **PASS** with core log markers;
- Windows x86_64 Wine audiodemo: **4/4 passed** at 300 frames;
- submission bundle validator: **32/32 passed**;
- full 1046-title linkage scan: **1 hit** (`ArcadePark2`, title-side);
- corpus sweep on the 0.5.30 CLI: **1050 rows → 1036 ok / 4 title-exception /
  4 bad-jar / 2 timeout / 2 title-exit / 2 no-frame**, verdict-identical to
  0.5.29 (0 changes); matrix refreshed (1817 titles / 1799 ok);
- both fixed titles pass the libretro mock harness through the release core
  (`--keys tools/corpus_keys/generic.txt --frames 300 --visual-required`,
  47 checks / 0 failed each), with `snowboardr` gfx 17,405 -> 22,319 and
  `mini3draci` tris 0 -> 69,016 in the sweep.

## Upgrade notes

- Save states are version-CRC gated: states from 0.5.8–0.5.29 are refused by
  the 0.5.30 build (start a fresh state).  Android in-app states same rule.
- The GLES2 default applies to newly built APKs only; the panel toggle
  “硬件加速（GLES2）” still forces software for a session.
- No core-option or memory-map changes in this release.

## Upstream

- libretro-super PR: https://github.com/libretro/libretro-super/pull/2204
- libretro/docs PR: https://github.com/libretro/docs/pull/1239

Both PRs are open and mergeable; the fork branch is `add-nano_j2me` at
`97bf1f4` (parent `55e50a8`), tracking `nano_j2me_libretro.info` 0.5.30.
