# nano_j2me 0.5.28

Release date: 2026-11-22.  This is **r47** of the libretro core.

## Highlights

### JSR-177 `pki`: the last class-linkage gap in the corpus

The full-corpus API audit (1048 JARs: `tests/games` plus the 646-file fetched
cache) now reports **641 implemented classes and a single JAR with a missing
API reference** — `毁灭战士3_10598.jar`, whose JSR-226 `SVGAnimator` symbols
are unreachable code (the title runs and never throws a linkage error).

The last *reachable* gap was `IstanBUL.jar`: its HTTPS bootstrap guards a
region with `catch (javax.microedition.pki.CertificateException e)`.  A catch
clause is resolved while an exception is dispatched, so without the class an
unrelated `IOException` would have surfaced as `NoClassDefFoundError`.

`src/midp/nj_lib_pki.c` adds the JSR-177 package surface only:

* `javax.microedition.pki.Certificate` — the interface and all eight accessors;
* `javax.microedition.pki.CertificateException extends java.lang.Exception` —
  constructors `()V`, `(String)V`, `(int, Certificate)V`,
  `(String, int, Certificate)V`, plus `getReason()` and `getCertificate()`;
  the message is stored in Throwable's `detailMessage`.

This is a **linkage surface, not a SATSA implementation**: no TLS, no keystore,
no certificate parsing; `https://` URLs are still answered by `Connector` as
unsupported, and no code path can obtain a real certificate.  Regression gate:
`tests/test_pki.c` (21 checks).

### Diagnostics: opt-in frame dumps in the libretro harness

The mock frontend (`tests/test_libretro_frontend.c`) gained `--dump-dir DIR`
and `--dump-every N`: RGB565 frames are written as P6 PPM so a renderer
difference can be inspected through the real core instead of the CLI.  The
round-178 MidnightPool investigation used it to show the title really is
black through the core while the CLI renders it.

### Corpus probes replay the per-title canvas size

Round 177 added per-title screen profiles (landscape 320x240 builds were being
measured on the default 240x320 canvas).  This release propagates that to all
remaining corpus probes — deep, motion, audio, states, cpu, heap and media
checks now resolve the size from the JAR name (or `tools/corpus_screens/`) and
prefer each row's absolute `jar_path`, so fetched titles outside `tests/games`
replay correctly.

### Test infrastructure

* Full CTest: **207/207** tests (includes `test_pki`).
* libretro mock frontend matrix: **20/20**.
* Real RetroArch 1.22.2, LoopDemo 120 frames: **PASS**.
* Windows x86_64 under Wine, 4 scenarios at 300 frames: **4/4**.
* Submission bundle validator: **32/32**.
* Whole-corpus libretro visual probe on the previous release (r46, 1034 titles):
  **1028 passed / 2 known limitations / 4 failed**; the failures are the two
  documented long-frame save-timeout titles, MidnightPool's core-only black
  screen (documented limitation) and the title-side PAC-MAN network stall.

## Download

| File | Size (bytes) | SHA-256 |
| --- | --- | --- |
| `nj2me-0.5.28-libretro-cores.zip` | 12,075,697 | `83fb972fef3846991d5ecd9e04d851f9c2897ff9167690115448970629effc95` |
| `nj2me-0.5.28-arm64-v8a.apk` | 1,009,820 | `11afa37a97e8b9cba00681489f11511e79d8603a459c6104cc86270feb2f6083` |
| `nj2me-0.5.28-armeabi-v7a.apk` | 1,013,918 | `e7c2955ce42b03bd971c23a6f19eb09551fe84275e62244a65b28fae768135d5` |
| `nj2me-0.5.28-x86.apk` | 993,430 | `290e72ce3b93c639ddccf0eb30fd31c141424b948d150e90c725b097e0b26977` |
| `nj2me-0.5.28-x86_64.apk` | 1,009,817 | `0bcc564c78694e7c58fcfded849319d6d606f5de3db265eadaa7dc1f39c1eae1` |
| `nj2me-0.5.28-legacy.apk` | 731,396 | `1d5669547557faf3fcc46cbc4d3ba3ed69c624929099187d57bd49d051fe9da4` |

Android APKs are split per ABI (`versionCode 528`); use the one matching the
device (`arm64-v8a` for current phones, `armeabi-v7a` for older 32-bit phones,
`x86`/`x86_64` for emulators).  The legacy APK is the ARMv5 / API 5 build.

## Core binaries

All eight libretro cores in the zip identify as 0.5.28 (`.info`
`display_version = "0.5.28"`, SHA-256
`d0e83540d26fb8e0e26bb2fb35b00259029f319168db369c645f7ba6f577289e`).

| Target | Size (bytes) | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,003,760 | `e9438e0ad321eccdb98ff267eb6d6d1561c01b26a8d339a03c04afd2f8609a58` |
| linux-aarch64 | 3,474,640 | `5fdcad96e690c53e75c6305d49c1b808031f7a1b288ab10453ffd72cdae0bddd` |
| linux-armv7 | 3,140,964 | `1d00e553dcbe0390f9dd5ce9f3e8f5e1392fcde55a9f90acbbbce95d13f134d6` |
| android-arm64 | 3,716,264 | `b450fb6ca2ecefce3bbb5edcdd58fc96485186428239b45cb6cc445ddb0858c5` |
| android-armv7 | 2,835,276 | `f27c85149cd2c64a7857683788fd9457d38bb4c4dfd30208a0b466aae7826d67` |
| android-x86_64 | 3,747,136 | `41f8c728c079a5fa97bb341cdd79446dab532ea80d330998eb5e2d6cfd19ba30` |
| windows-i686 | 3,564,544 | `3d075fa3ebfc9b9210a4ee2ba6f236ae5864c968ec247c53bf2992378adef27b` |
| windows-x86_64 | 3,336,704 | `0fa116c428dc7607f48774d440ccdee6908d310157d7c720a4fa5ea317281dc3` |

## Verification

| Check | Result |
| --- | --- |
| Full serial CTest | 207/207 passed |
| libretro mock matrix | 20/20 |
| Real RetroArch 1.22.2, LoopDemo 120 frames | PASS |
| Windows x86_64 Wine, 4 scenarios @ 300 frames | 4/4 |
| Submission bundle validator | 32/32 |
| `check_field_descs.py` | 284 lookups, 0 problems |

## Known limitations

* **Save-state service timeout for long-frame titles.** A frontend
  `retro_serialize()` issued while the VM is inside a frame that runs longer
  than `LR_STATE_TIMEOUT_MS` (4 s) fails with `NJ_ERR_STATE` (core logs
  "save-state request refused").  Affected probes: MidnightPool1, 杀鬼子,
  Asphalt4.  Serialize after the first `retro_run` calls have returned.
* **MidnightPool1 renders in the CLI but is black through the core.**  Its
  `paint()` busy-waits for 50 ms of `System.currentTimeMillis()`; the virtual
  clock advances only on the frontend thread, so the loop never completes.
  Documented in `docs/notes/2026-11-21-fetched-black-screen-titles.md`.
* **PAC-MAN (176x220)** waits for Sprint carrier network state (its
  `Muglet.getURI()` is empty); this is title-side, and the FreeJ2ME reference
  hangs on it as well.
* **Doom3's JSR-226 SVG symbols** are not implemented (unreachable code).
* **Rewind** works only with small heaps (a 25 MB state is ~120 ms per slot;
  the auto heap makes it impractical).  Runahead is not advertised.
* **Netplay** is not implemented.
* **States from 0.5.8–0.5.27** are refused by the state version CRC, as usual
  for this series.

## Upstream

Tracked submission kit: PR **libretro/libretro-super#2204** (adds
`dist/info/nano_j2me_libretro.info`) and PR **libretro/docs#1239** (core page,
core list row, license row, see-also group, mkdocs nav).  Both are open and
mergeable; nothing has been merged.
