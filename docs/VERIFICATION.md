# Verification ledger

One authoritative list of what has already been verified, with the evidence and
the commit that carries it, so the same ground is not walked twice. The
detailed reasoning stays where it was written: `docs/notes/*.md` for
investigations, `docs/COMPATIBILITY.md` for behaviour and known gaps,
`docs/PERFORMANCE.md` for measurements, and each entry below for the verdict
plus where to look.

Read this file before starting a verification pass on a title or a subsystem.
The corpus gate itself (`tools/run_game_corpus.sh`) is re-run whenever its
answer is needed, but a title listed as verified here does not need that pass
repeated unless the code changed the path the verification used.

## How a row is written

* **VERIFIED** means the behaviour was observed on the real JAR, or by an
  assertion test, with the command or evidence path recorded here. A note that
  says "should work" is not verification; a screenshot plus a reproducible
  command is.
* **GAME-SIDE** means the title is the cause, and the row says how that was
  proved (usually: the reference emulator does the same, or its own bytecode
  contains the bug). A game-side row is not a reason to change the runtime.
* **OPEN** means nobody has finished the observation; the row says what is
  missing.
* **SUPERSEDED** rows stay, with a pointer to the newer row. Verification that
  quietly disappears is how a stale claim gets re-checked a month later.
* Evidence under `.local-tools/debug-runs/` is deliberately not committed
  (frames, logs and stitched screenshots are bulk); the row names the
  directory, and the commit carries the text that quotes it.

## Corpus snapshot

The gate over all collected titles, most recent full sweep:

* build `/tmp/nj-r5` — Release, `-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF`,
  working tree at commit `239ccec`; run through `NJ_BIN=/tmp/nj-r5/nj2me
  NJ_CORPUS_WORK=/tmp/nj-r5-corpus tools/run_game_corpus.sh` with the same
  passes as before (uncapped, paced, wall-clock, device re-run, heap re-runs).
* Result: **393 passed, 2 failed** of 395 titles. Both failures are the
  title's, not the runtime's:

| title | verdict | why |
| --- | --- | --- |
| `鲁宾逊漂流记汉化BT版_4677` | GAME ASSET | the JAR is truncated — 286,534 bytes with no end-of-central-directory record; `unzip -l` fails and python's `zipfile` refuses it too. Nothing to load. |
| `乱世Q三国_1649` | GAME-SIDE | the title's own licence check expires: its boot prints `授权过期` ("licence expired") and calls `notifyDestroyed()`. The reference emulator prints the same and exits the same way (`.local-tools/debug-runs/corpus-r3/q3/ref/ref.log`). The sweep supplied a fresh RMS directory, so this is the first-install path. |

History: the sweep before this one (`/tmp/nj-r3`) counted **392 passed,
3 failed**, and its third row was `实况足球经理2009_10609`, written up as a
flaky game-side title whose loader thread raced its own `paint()`.  That was
wrong: its loader died at a `DataInputStream.reset()` the runtime had bound to
an empty slot, so it read its packed container 44 bytes off.  The fix
(`239ccec`, `docs/notes/2026-10-21-datainput-rewind.md`) makes the boot
deterministic and the title passes.  The 393/2 sweep older than that one is
also history — it predates `乱世Q三国_1649`'s licence row being reproduced on a
fresh RMS.  All older sweeps are in `docs/COMPATIBILITY.md`; treat this
snapshot as the measured one.

## Real-game verifications

| title / jar | what was verified | evidence | commit |
| --- | --- | --- | --- |
| 实况足球经理2009_10609 | the packed-resource loader now rewinds its `DataInputStream`: sound confirmation dialog at frame ~300 (same dialog as the reference at 9 s), main menu (NEW CAREER / SHARE TEAM / ABOUT / OPTIONS / EXIT) at frame 900, player-creation form at frame 1900, on a fresh RMS at 176x208 and at 240x320; 2,100 framed run at 29.9 fps, 914.9 M bytecodes, heap 5.33/5.39 MiB of 16; SDL2 smoke run 600 frames exit 0 | `docs/notes/2026-10-21-datainput-rewind.md`; `.local-tools/debug-runs/rfmanager-r4/` | `239ccec` |
| Doom 3 (`毁灭战士3_10598.jar`) | boots past the 12 s sleep, menu navigation with held keys, New Game enters the level; corridor, dark room and the EXIT door render with textures and the HUD; forward/backward movement and left/right turning each change the picture (symmetric turns within 36 px after the same hold); 900 framed run at 26.7 fps with only the title's own caught `NullPointerException` in `Doom.video()` and a missing menu image left | `.local-tools/debug-runs/doom-gameplay-round18/`, `doom-heldkeys/` | `2e7f1f1`, `7b7a117` |
| 极品飞车16：亡命狂飙_1519.jar (The Run) | the race renders: menus and car selection as before, then the World through its own active camera - yellow Camaro in the tunnel, road markings, minimap and positions (P3 376M / P2 357M / P1 317M), 1008 distinct colours in frame 03150 where the old capture had two. A 3,300-frame headless run reaches the race at 29.8 fps with 588,645 triangles and 55,991 renders. Behind it are three JSR 184 semantics fixes: `render(World)` prefers the World's active camera over `setCamera()`, `Transform.setTransform(null)` means identity, and `Node.getTransformTo()` works between siblings | `docs/notes/2026-10-21-m3g-camera-semantics.md`, `docs/notes/2026-10-21-nfs-the-run-m3g.md`; `.local-tools/debug-runs/nfs-prod-r10o/` | `1e47d9a` |
| Doom 3 audio | the MP4 container's `samr` track decodes: the theme is 6001 frames, the run reports `pcm_frames=960160` (= 6001×160); pistol cues 35/30 frames and `doom.3gp` 624 frames all open | `docs/notes/2026-10-21-doom-mp4-amr.md`; `8805928` | `8805928` |
| Doom 3 lighting | each `World` is lit by its own `Light` nodes instead of reusing the first collected set; the level is no longer black, and the two-World regression test fails on the old code (`tests/test_m3g.c`) | `docs/notes/2026-10-21-m3g-world-lights.md`; `.local-tools/debug-runs/doom-lights-fix/` | `cbe8633` |
| Ancient Empires 2 (`远古帝国2汉化版.jar`) | cold RMS load in a fresh process, commander move (4,6)→(4,4), AI turn, one attack exchange (100→89 and 100→44 HP) and return to the map, Chinese dialogue rendered, Motorola `-21`/`-22` raw softkeys; both headless and SDL2 ports | `.local-tools/debug-runs/ancient-rms-round17/`; `docs/notes/2026-10-20-motorola-softkeys.md` | `9106b79`, `de6d5c8`, `c6b1e9a` |
| Stranded 2 (`孤岛余生2时间之谜汉化版_7371.jar`) | boot chain (language, music prompt, title, main menu, new game) into the opening room; right-taps move the character and pan the camera (33,085 px changed over 150 frames, then blocked by furniture); 8,100 framed run at 30.2 fps with 1,923,010 triangles. A 6,200-frame boot now snapshots (`room.njs`, 15.5 MB, after the M3G `Image2D` root fix) and a fresh process resumes it at 29.9 fps. Saving still writes store 0 = 528 B and stores 1/2 = 18 B each, and "load and continue" is **OPEN**: neither 80 key taps in each of the four directions nor touch taps at the green door / lower-right floor / left door produced a room transition or an auto-save | `.local-tools/debug-runs/stranded-round2/`, `stranded-save/`, `stranded-r5/` | `2ce09a3`, `7b7a117`, `46d75fc` |
| 3D Rally (`3D拉力赛-进化汉化版_6366.jar`) | race renders through the software rasteriser (car, road, snow, HUD; 120,397 triangles over 2,700 frames); its `TimerTask` NPE is the title leaking timer tasks — the reference prints `Cannot invoke "a.repaint()" because "Rally.f" is null` and dies the same way | `docs/notes/OPEN-issue-m3g-render-unreached.md`, `docs/notes/M3G-sprite3d-repair.md`; `docs/images/rally3d-race.png` | `cbe8633`, `2ce09a3` |
| Tumbleweed (`jump.jar`) | plays at 176x208 and 176x220 (countdown, sprite, score); at 128x160/240x320 it shows its own "Screen too small/large" alert, byte-for-byte the same behaviour as the reference (verified both sizes) | `.local-tools/debug-runs/jump-sizes/`, `jump-ref/`; `tests/test_lcdui.c` Alert wrap test | `35eb1f2` |
| `1945空战英雄_6105.jar` | its platform check demands a Nokia prefix; `--device nokia6230` boots to the mission menu (default profile intentionally does not spoof it; the reference does so by rewriting the literal) | `docs/notes/2026-10-20-corpus-game002-uwan-platform.md` | `4f5e3c1` |
| `蹦球传说_7801.jar` | its `q.<clinit>` reads the JAR's own `META-INF/MANIFEST.MF`, extracts the `Nokia-Platform:` value (`Nokia*`) and calls `System.exit(0)` unless `microedition.platform` matches it; with `--device nokia6230` at 176x208 it passes the story screen into gameplay (0/30 eggs, 0:05 timer, 30.2 fps, exit 0) — the default profile exits by the title's own check, same class as 1945 and consistent with the no-spoof policy | `docs/notes/2026-10-21-bounce-platform-gate.md`; `.local-tools/debug-runs/bounce-r8/` | this commit |
| 僵尸危机2 (`game-087`) | the `callSerially` drain no longer starves the frame loop: one snapshot of the queue per paint, self-requeued runnables defer to the next frame; regression test fails on the old drain (64 runs on one paint) | `docs/notes/2026-10-20-callserially-drain.md`; `.local-tools/debug-runs/` | `134499f` |
| HighSpeed3D (`RMS`) | an empty record (`addRecord(null,0,0)`) is returned as a zero-length array instead of `null`, restoring the menu; covered by the RMS tests | `docs/notes/2026-10-20-*` RMS notes | shipped before `cbe8633` |

Titles and platforms verified in the same repository before this ledger existed
are summarised in `docs/COMPATIBILITY.md` and the dated notes; recent ones are
commits `7146c42` (Townsmen 6 variants), `1870218` (Android HTTP against a
controlled local server), `8e9c2c7` (Android Pinyin IME), `7399362` (Asphalt 6
and NFS The Run), `d44f5fa` (Android default handset layout), `41c584e`
(Asphalt 4), `bd98e54`/`80e9494` (desktop text input). Their detailed evidence
lives in the commit message and body, not in this table.

## Subsystem verifications

| area | verdict | evidence |
| --- | --- | --- |
| Unit/assertion tests | **87/87** under ctest on a clean Release build (threaded-dispatch check included) | `/tmp/nj-def` build; `ctest` log |
| Computed-goto interpreter | **fixed**: every dispatch-table entry reached `L_haveop` after a single fetch; converted and switch interpreters produce identical bytecode counts and byte-identical frames on `1945空战英雄_6105` | `tools/check_threaded_dispatch.sh`; `docs/notes/OPEN-issue-threaded-dispatch.md`; commit `3f0dd46` |
| Key-script holds | `--keys` supports `frame:key:down` / `:up`; a held key stays down across frames, an explicit up edge releases once; mutation of the release pass fails two assertions | `tests/test_poke.c` (24 checks); `docs/notes/2026-10-21-key-script-holds.md`; commit `7b7a117` |
| `DataInputStream` mark/reset | `mark`, `reset` and `markSupported` forward to the wrapped stream (FilterInputStream semantics); base `InputStream.reset` raises `IOException`, `mark` stays a no-op; binding `reset` back to the empty slot fails the tests | `tests/test_ioapi.c` (166 checks); `docs/notes/2026-10-21-datainput-rewind.md`; commit `239ccec` |
| M3G World lights | each World renders with its own Light nodes; regression test rejects the old shared-light behaviour | `tests/test_m3g.c`; `docs/notes/2026-10-21-m3g-world-lights.md`; commit `cbe8633` |
| M3G camera and node transforms | `render(World)` uses the World's active camera even when `Graphics3D.setCamera()` is also set (the reverse left The Run's race camera at its menu pose); `Transform.setTransform(null)` resets to the identity; `Node.getTransformTo()` computes `inv(Wb) * Wa` for any two nodes in one graph, not just ancestors. Each fix has a regression block; reverting the camera precedence fails exactly its assertion | `tests/test_m3g.c` (321 checks); `docs/notes/2026-10-21-m3g-camera-semantics.md` |
| M3G rasteriser | textured/lit geometry for 3D Rally and Doom; depth, culling, perspective-correct texture and per-vertex lighting covered by `test_m3g`, `test_m3g_file`, `check_m3g_reals`, `check_m3g_api`, `check_m3gdemo` | `docs/COMPATIBILITY.md` JSR 184 section |
| M3G `Image2D` ref roots | `Image2D(int, Object)` releases the ref root it takes while converting an LCDUI image to RGBA bytes; the leak pinned the array for the VM's life and made every snapshot of a title using the constructor refuse with `temp root held` (`NJ_ERR_STATE`); a regression assertion fails if the release is dropped | `tests/test_m3g.c` (309 checks); `docs/notes/2026-10-21-m3g-image2d-root-leak.md`; commit `46d75fc` |
| M3G node-light roots | `render(Node, Transform)` on a scene whose Lights are nodes builds a temporary `Light[]` + matrix array and releases their roots once the Graphics3D fields hold them; an assertion fails if the release is dropped (the old code was the only failure: `render(Node) with scene lights leaves no ref root behind`, 312 checks / 1 failure). Preventive: a root scan of all 26 M3G JARs stays at zero | `tests/test_m3g.c` (312 checks); `docs/notes/2026-10-21-m3g-node-light-roots.md`; commit `c270bfb` |
| CJK + Latin antialiased fonts | 4× supersample + Lanczos + packed 2bpp, mono build bit-identical text, `+438880 B` constants and zero heap; both fonts included only under `NJ_ENABLE_FONT_CJK_AA` | `docs/notes/2026-10-20-latin-font-aa.md`; commit `96cfb44` |
| `callSerially` drain | snapshot of the queue per paint; self-requeue deferred to the next frame | `tests/test_lcdui.c`; commit `134499f` |
| VM snapshots | a snapshot can only be written at a frame boundary; a run that never reaches one (a title blocked in a native for its whole budget, e.g. Doom 3's constructor `join`) now reports **why** instead of silently writing nothing, and a boundary beyond the run is reported too. Regression: `waitdemo.jar` blocked constructor and a 5-frame run asking for boundary 50 | `tests/savestate/run.sh` (5 sections); `docs/notes/2026-10-21-snapshot-frame-boundary.md`; commit `8133cc4` |
| Alert body wrapping | `ui_text()` wraps at panel width; regression test fails if wrapping is disabled | `tests/test_lcdui.c`; commit `35eb1f2` |
| JSR 82 `LocalDevice.getProperty` | answers the three properties the specification defines (`bluetooth.sd.attr.retrievable.max` = `64`, `bluetooth.connected.devices.max` = `7`, `bluetooth.sd.trans.max` = `1`); the name buffer had been one byte shorter than the longest name, so the first property was silently always null, which the trimmed `-Werror` build reported as `-Wstring-compare`; shortening the buffer again fails exactly that assertion | `tests/test_wireless.c` (18 checks, fake radio on the headless port); `docs/notes/2026-10-21-trim-builds.md` |
| Trimmed builds | every `NJ_ENABLE_*` switch `nj_config.h` documents is a CMake cache option now, and trimmed configurations build with zero warnings under `-DNJ_WERROR=ON`; a lean 2D build (54% of the default's text) runs `1945空战英雄_6105` to its title screen at 30.2 fps.  Before this, `-DNJ_ENABLE_FONT_CJK=0` was silently ignored and the mono CJK tables stayed linked | `docs/notes/2026-10-21-trim-builds.md` (matrix and `size` table) |
| `System.out` text | output is UTF-8, so a title's own Chinese diagnostics are readable in the log (previously one `?` per character); lone surrogates and 4-byte code points covered by `test_ioapi` | `tests/test_ioapi.c` |

## Known game-side defects — do not "fix" these in the runtime

| title | behaviour | proof |
| --- | --- | --- |
| 3D Rally | leaks `java.util.Timer` tasks across its lifecycle; a stale task calls `repaint()` on a null field, kills its `Timer`, and later `schedule()`s get `Timer cancelled` | reference emulator throws the same `NullPointerException` from `TimerThread.mainLoop` |
| Doom 3 | `FIRE` has no visible effect before a weapon/ammo exists (HUD shows `0 0`); `Doom.video()` NPEs on the missing `VideoControl`; one menu image is absent from the JAR | caught exceptions in the run log; bytecode of `Doom/m.ig()`/`video()` |
| Ancient Empires 2 | `e.b(11,1)`/`e.e(10)` index its own 9-entry sound table; the game catches the `ArrayIndexOutOfBoundsException` it causes | `docs/notes/2026-10-20-motorola-softkeys.md` |
| Tumbleweed | rejects screens outside 150..250 px; renders its own alert | reference emulator identical |
| 1945空战英雄_6105 | demands `microedition.platform` start with `Nokia` | its MIDlet constructor; reference emulator's literal rewrite |
| 乱世Q三国_1649 | licence-time check prints `授权过期` and exits | reference emulator identical |
| 魂斗罗 / 斩妖伏魔录 / etc. | the corpus triage's remaining rows; all reproduced with the reference or in their own bytecode | `docs/notes/2026-10-20-corpus-triage.md` |

## Open verification gaps

* **Stranded 2** — loading a checkpoint and continuing gameplay is not
  verified: the save slots are written (store 0 = 528 B, stores 1/2 = 18 B), no
  checkpoint payload has been produced yet, so the "continue" path has nothing
  to read. The opening room was explored from the snapshot - 80 key taps in
  each of the four directions, and touch taps at the green door, the lower-right
  floor and the left door - with no room transition and no auto-save. The N97
  variant of the same title did auto-save when leaving the interior, so the
  trigger exists in the game; where this 240x320 build puts its exit is the open
  question.
* **Doom 3** — a completed mission, or combat with an actual weapon, is not
  verified; the level and movement are. Mid-run snapshots are not possible:
  the MIDlet constructor joins the game thread, so the first native call
  consumes the whole run; the runner reports that instead of writing nothing
  (`docs/notes/2026-10-21-snapshot-frame-boundary.md`).
* **Ancient Empires 2** — mission completion and a full campaign are not
  verified; movement, one AI turn and one combat exchange are.
* **Mascot Capsule** — no collected title reaches the rasteriser, so it is
  exercised only by its unit test; a real title using it is still missing.
* **Host text input** — the Android side is verified against a real IME; other
  ports are not.
* **Finished-thread `DEAD` reclamation** — deferred: the shortcut regressed the
  Doom constructor wait. Ordinary worker join is verified.
