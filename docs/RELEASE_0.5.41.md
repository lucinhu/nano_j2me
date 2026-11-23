# nano_j2me 0.5.41

This release carries the timer-task change that fixed the last corpus title
whose host loop starved the runtime's frame pump.

## Highlights

- **Timer tasks run as Java threads and can be preempted** (`src/midp/nj_lib_util.c`,
  `src/midp/nj_lib_lcdui.c`, `src/core/nj_internal.h`, `src/core/nj_thread.c`).
  `java.util.Timer` used to execute each due `TimerTask.run()` to completion
  inside one C→Java call.  A title whose task hosts its game loop — which is
  how `TheMummy_K700F` is written — therefore owned the VM thread forever:
  the frame pump never got control back and the 20 ms boot-handshake timer was
  starved, leaving the title black after frame 8.
  The timer helper is now a Java class (`nj2me.util.TimerThread`) that loops
  over `pump()`/`afterRun()`, so `TimerTask.run()` executes as ordinary
  bytecode and the scheduler preempts it at quantum boundaries.  Each task
  still gets its own scheduler turn (periodic tasks no longer loop inside a
  quantum), a throwing task cancels its timer, and `repaint()` inside a timer
  callback keeps the handset semantics: it paints inline, and
  `serviceRepaints()` paces the loop to one step per displayed frame.
- **`TheMummy_K700F` now renders through the CLI and the libretro core**, and
  its state requests succeed.  CLI (176x208, heap 16, 30 fps, 600 frames):
  `frames=600 paints=609 gfx=11,610`; frame 150 shows the splash
  (14,970/36,608 non-black pixels) and frames 300-600 the menu/game screen
  (34,897/36,608).  Through the core: 47/47 harness checks, 297/317 content
  frames, 3 distinct frame hashes, save/restore passes.
- Timing-sensitive titles keep their pacing: `test_timer`,
  `test_timer_fairness` and the rest of the timer/repaint regression group
  pass unchanged.
- Whole-corpus libretro visual probe on the release core: **1,031 passed,
  1 known limitation, 2 failed** over the 1,034 titles whose JARs are still on
  disk.  The two failures are the documented ones (`PAC-MAN (176x220)` is
  title-side, `Asphalt4EliteRacing` refuses the state service because one
  frame exceeds the 4 s window); `毁灭战士3_10598` is the known slow-loader
  limitation.  `TheMummy_K700F` is no longer in the failure list.
- No compatibility regressions: a 100-title CLI A/B against the previous
  build shows 0 verdict changes; a 30-title stats sample has 25 identical
  frame/paint pairs and five ±1-paint timing deltas.

## Artifacts

Cores (`.info` SHA-256 `43aaeda8357b8bdaf11f0769d31c48603426894bd0b4d179cc9f3543d49ad0e8`):

| Target | Bytes | SHA-256 |
|---|---:|---|
| linux-x86_64 | 4,048,840 | `71b2cb1f337b17e6f5ac878372562034b2a68599ed3408d1fdea52af9f78a5e5` |
| linux-aarch64 | 3,524,736 | `b6f99ce603eb1e2671478be8842bbaa5b93b161b8b8abb2889cf685705e1f835` |
| linux-armv7 | 3,177,756 | `58f4139eb97bc21c75022a180bb8627b17e4a09edb085eb8cfe63f4cf752ca22` |
| android-arm64 | 3,770,560 | `74455ed533c4c2ba42b9b8d5f1f7b1a5bb22b91a89002838d6804972817dfceb` |
| android-armv7 | 2,866,288 | `63894825a9cf0c19c287fd921ae6758bb0fd2d0fba35ad99e1f206c170d8cdaf` |
| android-x86_64 | 3,796,488 | `c2b15a092260fc86e056fd6c4d668bce5c911f48aedb090cd7ffdbf89f04a0f8` |
| windows-i686 | 3,602,944 | `5b1dab3dc237e500442fade4ff4c5e88e9b9c1f3ff2eb477e19f14467eb783d7` |
| windows-x86_64 | 3,372,544 | `27aa1776dfdf07c744f5c7db3add5ab48bd9e9f90e6884dd2587affeea8ea044` |

`nj2me-0.5.41-libretro-cores.zip`: 12,181,765 B, 128 entries, SHA-256
`cd4aa8f0c2c7933846a854d2c81b68805b80e8ccd45286970d662652392ea005`.

APKs (versionCode 541, minSdk 21):

| APK | Bytes | SHA-256 |
|---|---:|---|
| arm64-v8a | 1,058,972 | `24596bd09f6ab844ee4c1f834e1fc911c3bbc9bf6ea5cf6ad69cbe8c86e92eee` |
| armeabi-v7a | 1,067,166 | `3f4ddddbf8cbcd47561276a023c37ec40fc8660d1c1bb0e9135b8509d11a9e8d` |
| x86 | 1,046,678 | `1ff084cc64a8b5650335b2bcb4a73b4e0b700ce81e4f07be3ccfff98b3a23d04` |
| x86_64 | 1,063,065 | `08c808f54554184d1c470688523f19240054a71055d4efb68cf4edb34b1b9f5e` |
| legacy (ARMv5) | 741,932 | `395bf712336c369d718e77ee030a0b310c6b6bc3e7ad0eb835f9021ce8ee8a4d` |

## Verification

- Full serial CTest: 210/210 passed.
- Mock frontend conformance matrix: 20/20.
- Real RetroArch 1.22.2 LoopDemo, 120 frames: PASS.
- Windows x86_64 core under Wine, audio demo, 300 frames: 4/4 PASS.
- Submission bundle validator: 32/32.
- Whole-corpus libretro visual probe: 1,031 passed / 1 known / 2 failed
  (1,034 measured titles; the remaining rows point at JARs that are no longer
  on disk).

## Upgrade notes

- Save states from 0.5.8-0.5.40 are refused by the version CRC; 0.5.41 states
  are compatible with later 0.5.x releases as usual.
- Rewind stays sized for small heaps; runahead and netplay are still not
  advertised (virtual-clock pacing makes them unsuitable).
- The libretro port keeps the `nano_j2me_deterministic` option, the cheat
  engine, the `SYSTEM_RAM` memory map and multi-touch from earlier releases.

## Upstream

- libretro-super PR (<https://github.com/libretro/libretro-super/pull/2204>)
  head updated to the 0.5.41 `.info` (fork commit
  `a807173da913eb7b20a48d80742e7674969380a7` on top of upstream
  `55e50a81195eaf8dd1b7720a601282459832ced1`).
- libretro/docs PR (<https://github.com/libretro/libretro-docs/pull/1239>)
  unchanged, open and mergeable.

Full history: `docs/LIBRETRO.md` and `docs/CORPUS.md`.
