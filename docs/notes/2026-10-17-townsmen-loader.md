# Townsmen 6 French-flag loading stall — 2026-10-17

## Root cause and correction

The original 208x320 Motorola A1000 Townsmen 6 JAR remained on the French flag / 请稍候 for the whole 60-second baseline. `hg.HG.run()` polls its loader `r.isAlive()` at bytecode PC 352; after the loader finishes it advances at PC 370 and releases the loader reference. `r.run()` had already returned, but ordinary Java workers retained `alive=true` forever. The scheduler previously published normal completion only for the internal TimerThread. This is a VM thread-lifecycle defect, rather than a resource or property failure.

The earlier `Vector.elementAt` AIOOBE and `p.a()+34` null `Glu-Demo-Enabled.equals` throw are handled by the original game's exception tables. They still occur in the successful fixed replay and are not the loading blocker.

Changes:

- [nj_thread.c](../../src/core/nj_thread.c): after a started non-main worker returns `NJ_OK` with no Java frames, clear the existing Java `Thread.alive` field once and notify joiners. The scheduler's existing `in_c_call` guard excludes native-owned stacks. Ordinary worker state and scheduler idle timing are retained, preserving Doom's constructor/frame-hook behaviour. TimerThread retains its existing DEAD handling.
- [nj_lib_thread.c](../../src/core/nj_lib_thread.c): `join()` returns immediately if the existing alive field reports completion.
- [test_thread_lifecycle.c](../../tests/test_thread_lifecycle.c), registered in [CMakeLists.txt](../../CMakeLists.txt): embedded Java fixtures cover not-started/alive/completed transitions, an active native bridge with a transient empty Java stack, a completed worker's join, a C-invoked constructor joining a sleeping worker, and a C-invoked constructor waiting for a Runnable child's notification.

No new per-thread field, allocation, dependency, or game-specific workaround is introduced. No original JAR or user RMS was changed. The change does not attempt full DEAD-state reclamation or change finished-program detection. The older Doom timing investigation remains in [the lifecycle note](OPEN-issue-finished-thread.md).

## Replay evidence

All replay artifacts are under [.local-tools/debug-runs/townsmen-loader-20261017](../../.local-tools/debug-runs/townsmen-loader-20261017/). Each replay uses headless mode, the JAR's listed resolution, 30 fps, fresh isolated RMS and a finite 1800-frame budget. Original file SHA-256 before/after matches for all eight JARs; exact original paths, hashes and output mapping are in [variants/manifest.json](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/manifest.json).

Keys by frame: `360:fire 540:soft1 720:fire 1000:fire 1200:fire 1400:down 1500:fire 1650:fire`. Screenshots every 180 frames. They show actual game rendering and input-driven scene/dialogue changes; this is bounded startup/game-entry validation, not a full campaign completion claim.

| Replay | Original platform | Resolution | Inspected result at/before 60s |
|---|---|---|---|
| [v1](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v1/contact.png) | Motorola A1000 | 208x320 | Sound prompt → title → campaign → map → village; female/male/female dialogue advances |
| [v2](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v2/contact.png) | Motorola A760 | 240x320 | Same progression and dialogue; content is rotated 90° within the supplied viewport |
| [v3](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v3/contact.png) | Sony Ericsson K700 | 176x220 | Title, campaign map, village and dialogue advance |
| [v4](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v4/contact.png) | Sony Ericsson K790 | 240x320 | Title, campaign map, village and dialogue advance |
| [v5](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v5/contact.png) | Nokia E60 | 320x240 | Title, campaign map, village and dialogue advance |
| [v6](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v6/contact.png) | Nokia E62 | 320x240 | Title, campaign map, village and dialogue advance |
| [v7](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v7/contact.png) | Nokia N70 | 176x208 | Title, campaign map, village and dialogue advance |
| [v8](../../.local-tools/debug-runs/townsmen-loader-20261017/variants/v8/contact.png) | Nokia N73 | 240x320 | Extra language prompt, then title, campaign map, village and female→male dialogue |

All eight exited 0 on budget completion. Each contact sheet was opened and visually inspected. A1000 reaches the title by frame720/24s, map by1080/36s, village by1260/42s. N73's extra language selection shifts those transitions later, but the same keys still reach and advance village dialogue within60s. Terrain differs between fresh runs because the game generates it.

The independent [patched A1000 contact sheet](../../.local-tools/debug-runs/townsmen-loader-20261017/patched-contact.png) also demonstrates the full progression. The [baseline contact sheet](../../.local-tools/debug-runs/townsmen-loader-20261017/baseline-contact.png) records the original French-flag stall. FreeJ2ME-Plus comparison from corpus-replay independently reached the original A1000 JAR's title at60s with fresh RMS and30fps, confirming that the original archive can progress.

## Regression and limits

`ctest --test-dir build-town -R 'test_thread_lifecycle|test_timer|test_blocked_init' --output-on-failure`: **3/3 passed** (0.98s). The lifecycle test was rerun after adding the transient-native-stack assertion: **passed** (0.06s). Independent `build-town` avoided shared-build competition.

Real [Doom JAR](../../tests/games/毁灭战士3_10598.jar), fresh RMS, 240x320,30fps,360 frames: construction succeeds,356 paints, exit0, no `construction ... blocked`. [Doom screenshots](../../.local-tools/debug-runs/townsmen-loader-20261017/doom-contact.png) show the existing black star field; this confirms startup timing was preserved, not that Doom gameplay is repaired. [Doom runtime log](../../.local-tools/debug-runs/townsmen-loader-20261017/doom/runtime.log) retains caught unsupported-media exceptions.

Remaining limits: normal Java-return lifecycle publication is fixed; uncaught-exception/native-empty-run lifecycle and full scheduler reclamation remain outside this patch. Townsmen WAV media errors remain caught and audio fidelity was not established. A760 orientation was observed but not changed. No save/reload or late campaign coverage was performed. No MIDP LCUI, Android, network or M3G sources were edited. No commit was created.
