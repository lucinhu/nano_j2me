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

* build `/tmp/nj-r3` — Release, `-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF`,
  commit `7146c42`; run through `NJ_BIN=/tmp/nj-r3/nj2me
  NJ_CORPUS_WORK=/tmp/nj-r3-corpus tools/run_game_corpus.sh` with its own
  default passes (uncapped, paced, wall-clock, device re-run, heap re-runs).
* Result: **392 passed, 3 failed** of 395 titles. The three are accounted for
  and none is a runtime defect:

| title | verdict | why |
| --- | --- | --- |
| `鲁宾逊漂流记汉化BT版_4677` | GAME ASSET | the JAR is truncated — 286,534 bytes with no end-of-central-directory record; `unzip -l` fails and python's `zipfile` refuses it too. Nothing to load. |
| `乱世Q三国_1649` | GAME-SIDE | the title's own licence check expires: its boot prints `授权过期` ("licence expired") and calls `notifyDestroyed()`. The reference emulator prints the same and exits the same way (`.local-tools/debug-runs/corpus-r3/q3/ref/ref.log`). The current sweep supplied a fresh RMS directory, so this is the first-install path. |
| `实况足球经理2009_10609` | GAME-SIDE (flaky) | its initialisation races its own loader thread: a flat frame and an NPE in `d.T(int)` (its string cache) appear only when the loader has not finished first. `COMPATIBILITY.md` records that two runs of the same binary can execute 208 million or 15 million bytecodes; the row is accurate per run, not across runs. |

Earlier sweeps are recorded in `docs/COMPATIBILITY.md` (374/21 → 393/2). The
393/2 claim does **not** reproduce on today's build: `乱世Q三国_1649` fails
deterministically, its licence check is bytecode-level and the reference
emulator behaves identically, so the earlier number is stale rather than the
runtime having regressed. Treat 392/3 above as the measured snapshot and the
older paragraph as history; the next sweep updates both.

## Real-game verifications

| title / jar | what was verified | evidence | commit |
| --- | --- | --- | --- |
| Doom 3 (`毁灭战士3_10598.jar`) | boots past the 12 s sleep, menu navigation with held keys, New Game enters the level; corridor, dark room and the EXIT door render with textures and the HUD; forward/backward movement and left/right turning each change the picture (symmetric turns within 36 px after the same hold); 900 framed run at 26.7 fps with only the title's own caught `NullPointerException` in `Doom.video()` and a missing menu image left | `.local-tools/debug-runs/doom-gameplay-round18/`, `doom-heldkeys/` | `2e7f1f1`, `7b7a117` |
| Doom 3 audio | the MP4 container's `samr` track decodes: the theme is 6001 frames, the run reports `pcm_frames=960160` (= 6001×160); pistol cues 35/30 frames and `doom.3gp` 624 frames all open | `docs/notes/2026-10-21-doom-mp4-amr.md`; `8805928` | `8805928` |
| Doom 3 lighting | each `World` is lit by its own `Light` nodes instead of reusing the first collected set; the level is no longer black, and the two-World regression test fails on the old code (`tests/test_m3g.c`) | `docs/notes/2026-10-21-m3g-world-lights.md`; `.local-tools/debug-runs/doom-lights-fix/` | `cbe8633` |
| Ancient Empires 2 (`远古帝国2汉化版.jar`) | cold RMS load in a fresh process, commander move (4,6)→(4,4), AI turn, one attack exchange (100→89 and 100→44 HP) and return to the map, Chinese dialogue rendered, Motorola `-21`/`-22` raw softkeys; both headless and SDL2 ports | `.local-tools/debug-runs/ancient-rms-round17/`; `docs/notes/2026-10-20-motorola-softkeys.md` | `9106b79`, `de6d5c8`, `c6b1e9a` |
| Stranded 2 (`孤岛余生2时间之谜汉化版_7371.jar`) | boot chain (language, music prompt, title, main menu, new game) into the opening room; right-taps move the character and pan the camera (33,085 px changed over 150 frames, then blocked by furniture); 8,100 framed run at 30.2 fps with 1,923,010 triangles. Saving writes store 0 = 528 B and stores 1/2 = 18 B each, but "load and continue" is **OPEN** | `.local-tools/debug-runs/stranded-round2/`, `stranded-save/` | `2ce09a3`, `7b7a117` |
| 3D Rally (`3D拉力赛-进化汉化版_6366.jar`) | race renders through the software rasteriser (car, road, snow, HUD; 120,397 triangles over 2,700 frames); its `TimerTask` NPE is the title leaking timer tasks — the reference prints `Cannot invoke "a.repaint()" because "Rally.f" is null` and dies the same way | `docs/notes/OPEN-issue-m3g-render-unreached.md`, `docs/notes/M3G-sprite3d-repair.md`; `docs/images/rally3d-race.png` | `cbe8633`, `2ce09a3` |
| Tumbleweed (`jump.jar`) | plays at 176x208 and 176x220 (countdown, sprite, score); at 128x160/240x320 it shows its own "Screen too small/large" alert, byte-for-byte the same behaviour as the reference (verified both sizes) | `.local-tools/debug-runs/jump-sizes/`, `jump-ref/`; `tests/test_lcdui.c` Alert wrap test | `35eb1f2` |
| `1945空战英雄_6105.jar` | its platform check demands a Nokia prefix; `--device nokia6230` boots to the mission menu (default profile intentionally does not spoof it; the reference does so by rewriting the literal) | `docs/notes/2026-10-20-corpus-game002-uwan-platform.md` | `4f5e3c1` |
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
| M3G World lights | each World renders with its own Light nodes; regression test rejects the old shared-light behaviour | `tests/test_m3g.c`; `docs/notes/2026-10-21-m3g-world-lights.md`; commit `cbe8633` |
| M3G rasteriser | textured/lit geometry for 3D Rally and Doom; depth, culling, perspective-correct texture and per-vertex lighting covered by `test_m3g`, `test_m3g_file`, `check_m3g_reals`, `check_m3g_api`, `check_m3gdemo` | `docs/COMPATIBILITY.md` JSR 184 section |
| CJK + Latin antialiased fonts | 4× supersample + Lanczos + packed 2bpp, mono build bit-identical text, `+438880 B` constants and zero heap; both fonts included only under `NJ_ENABLE_FONT_CJK_AA` | `docs/notes/2026-10-20-latin-font-aa.md`; commit `96cfb44` |
| `callSerially` drain | snapshot of the queue per paint; self-requeue deferred to the next frame | `tests/test_lcdui.c`; commit `134499f` |
| Alert body wrapping | `ui_text()` wraps at panel width; regression test fails if wrapping is disabled | `tests/test_lcdui.c`; commit `35eb1f2` |
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
| 实况足球经理2009_10609 | loader-thread race decides whether it paints | two runs of the same binary, `COMPATIBILITY.md` |
| 魂斗罗 / 斩妖伏魔录 / etc. | the corpus triage's remaining rows; all reproduced with the reference or in their own bytecode | `docs/notes/2026-10-20-corpus-triage.md` |

## Open verification gaps

* **Stranded 2** — loading a checkpoint and continuing gameplay is not verified;
  the save slots are written, the "continue" entry point is not mapped. Also
  whether a walk crosses a room boundary and triggers a checkpoint.
* **Doom 3** — a completed mission, or combat with an actual weapon, is not
  verified; the level and movement are.
* **Ancient Empires 2** — mission completion and a full campaign are not
  verified; movement, one AI turn and one combat exchange are.
* **Mascot Capsule** — no collected title reaches the rasteriser, so it is
  exercised only by its unit test; a real title using it is still missing.
* **Host text input** — the Android side is verified against a real IME; other
  ports are not.
* **Finished-thread `DEAD` reclamation** — deferred: the shortcut regressed the
  Doom constructor wait. Ordinary worker join is verified.
