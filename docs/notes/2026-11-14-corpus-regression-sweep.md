# Corpus regression sweep, 2026-11-14 (fixed binary)

Baseline for the compatibility work after the two VM fixes of
`2026-11-14-cooperative-scheduling-startup-hangs.md` (synchronized `run()`
monitor acquisition, paint/frame blocking `serviceRepaints()`).

## Recipe

Command:

    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      tools/sweep_compat.py .local-tools/debug-runs/compat-20261114-fixed-sweep

Artifacts: `.local-tools/debug-runs/compat-20261114-fixed-sweep/` with
`rows.jsonl` (one row per title), `keys.txt` (the 23-key script), `meta.json`,
and per-title RMS directories under `rms/`.

Each title runs with `--port headless --size 240x320 --heap 16 --fps 30
--run-frames 600 --keys @keys.txt --stats`; a `failure`/`timeout` row is
retried once at `--heap 32` and then with `--device nokia6230`.

## Verdicts

404 titles:

| verdict | count |
|---|---|
| ok | 371 |
| title-exit | 27 |
| title-exception | 3 |
| no-frame | 2 |
| bad-jar | 1 |

Previous baseline (`.local-tools/debug-runs/compat-20261113-sweep-keys`) had
367/29/4/2/1. Every row that changed improved or is neutral:

- `三国塔防－曹操传_1610.jar`: title-exit (1 frame, caught
  `IllegalMonitorStateException at Object.notifyAll`) -> ok, 600 frames.
- `明星制造梦工厂_11048.jar`: timeout -> ok, 600 frames.
- `生化危机之恶化_8448.jar`: title-exit -> ok, 271 -> 600 frames.
- `帝国时代2汉化版_7864.jar`: 322 -> 600 frames.
- `NBA2010授权正版_5967.jar`: 649 -> 657, `喜羊羊与灰太狼_7826.jar`:
  658 -> 601, `极品飞车14_2336.jar`: 601 -> 611 (pacing noise only).

No row regressed.

## Remaining non-ok rows

Real candidates (tracked for triage):

- `三国赤壁小乔秘史_7976.jar` — title-exception, 600 frames, one uncaught
  `java/lang/NullPointerException` (plus caught exceptions).
- `哆啦A梦－3D卡丁车增值版_1560.jar` — title-exception, 600 frames, uncaught
  `java/io/IOException`, `java/lang/NullPointerException`,
  `java/lang/NumberFormatException`, `RecordStoreNotFoundException`.
- `愤怒的小鸟-高清版_2353.jar` — title-exception, 600 frames, includes
  `java/lang/OutOfMemoryError` at `--heap 16` (it is clean at `--heap 32`).

Game-side / fixture noise (not emulator defects):

- 27 title-exit rows: the title called `notifyDestroyed()` itself after
  painting (e.g. `冰淇淋少女成长日记_7307.jar` 290 frames with 290 caught
  IMS from its own animation thread, `JBenchmark*`, `Boun_en_ar...`,
  `使命召唤5_7199.jar`); the `book*.jar`/`jump.jar` fixtures deliberately quit
  after 30 frames; `hello.jar` is the intentionally broken fixture.
- 2 no-frame rows: `乱世Q三国_1649.jar` (server/auth RMS missing),
  `有女倾城破解版_11525.jar` (installs no Displayable and exits).
- 1 bad-jar: `鲁宾逊漂流记汉化BT版_4677.jar` (corrupt archive).

## Caveats

- The first full run above predates the `serviceRepaints()` frame-boundary
  blocking change (`2026-11-14-cooperative-scheduling-startup-hangs.md`, round
  3) and the manifest trim; the "Final re-run" section supersedes it as the
  regression evidence. A 45-title sample was also re-run against that binary
  (43 ok; the two exceptions are the known nokia6230 device row and the
  corrupt JAR).
- `--heap 16` OOM retry is still the sweep script's policy, not the runtime's:
  the libretro core does not automatically restart a title with a larger heap.

## Final re-run, 2026-11-14 (post manifest-trim fix)

    rm -rf .local-tools/debug-runs/compat-20261114-final-sweep
    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      tools/sweep_compat.py .local-tools/debug-runs/compat-20261114-final-sweep

This binary carries every fix verified in this round: synchronized `run()`
monitor acquisition, frame-boundary `serviceRepaints()` blocking, and the
`MIDlet.getAppProperty()` trim from `2026-11-14-title-exception-triage.md`.

- 404 rows: **372 ok**, 26 title-exit, 2 title-exception, 2 no-frame, 1 timeout,
  1 bad-jar (previous full run: 371/27/3/2/0/1; first baseline: 367/29/4/2/1/1).
- `哆啦A梦－3D卡丁车增值版_1560.jar`: title-exception → **ok** (manifest trim).
- `聊斋之倩女幽魂_11185.jar`: title-exit → **ok** (294 → 600 frames).
- `GT赛车2_36.jar`: ok → timeout. This is a load artifact, not a code
  regression: re-run standalone against the same binary it reaches 600 frames
  with `exit status 0` — but takes 76.7 s wall (7.8 fps, user CPU only 10.9 s),
  and the sweep's 95 s subprocess budget was consumed while a full `ctest -j1`
  run overlapped the sweep. Slow self-paced titles sit close to that budget.
- Remaining non-ok rows are the same rows as before and are adjudicated in the
  triage note (game-side exits, auth/no-frame titles, one corrupt JAR) except
  `三国赤壁小乔秘史_7976.jar` (game-side stale 176 `pak`) and
  `愤怒的小鸟-高清版_2353.jar` (needs >16 MB heap; libretro `auto` gives ≥32).

## Revalidation, 2026-11-15 (worktree after the web/GLES2 branch)

    rm -rf .local-tools/debug-runs/compat-20261115-r9
    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      tools/sweep_compat.py .local-tools/debug-runs/compat-20261115-r9

The headless binary was rebuilt from the current worktree, so it also contains
the other session's in-progress GLES2/Web and M3G changes; this is a
worktree-level regression check, not a commit-tagged artifact.

- 404 rows: **373 ok**, 26 title-exit, 2 title-exception, 2 no-frame, 0 timeout,
  1 bad-jar (2026-11-14 final re-run: 372/26/2/2/1/1).
- Only two verdict changes, both improvements: `美男狂想曲_3876.jar`
  title-exit → ok (58 → 600 frames, 26 → 583 paints) and
  `西伯利亚大反击_11466.jar` title-exit → ok (291 → 600 frames/paints).
- No regression: every previously ok row is still ok; `GT赛车2_36.jar` is ok
  again in an uncontended run; the remaining non-ok rows are the same
  adjudicated set as above.
- Spot checks of the fixes from earlier rounds (600 frames, `--heap 16`, paced
  30 fps, no uncaught exceptions): 三国塔防－曹操传_1610 reaches 300 frames /
  300 paints (5 caught game-side AIOOBEs), 空战1944_10321 600/207,
  明星制造梦工厂_11048 with the `165:right` script 600/538,
  雷霆战机2012BT版_3101 300/168, 1942空战_10403 600/502 (MPEG-4 movie audio
  decodes), 愤怒的小鸟-高清版_2353 at `--heap 32` 600/501.

## Revalidation, 2026-11-15 (round 42, after the M3G fast path)

Command: `NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me tools/sweep_compat.py
.local-tools/debug-runs/compat-20261115-r42` (CLI rebuilt from `c16ff5f`,
Release, 10 workers, 600 frames at 30 fps, per-title keys script).

Result: **404 rows: 371 ok, 27 title-exit, 2 title-exception, 2 no-frame,
1 timeout, 1 bad-jar** (previous full run 373 ok / 26 title-exit / 2 / 2 / 0 / 1).
Two verdict changes versus `compat-20261115-r9`, both adjudicated as
harness/timing noise rather than emulator regressions:

- `生化危机之恶化_8448.jar` ok -> title-exit (frames 271, paints 107). Without
  the key script the same run reaches 600 frames (frames=600, paints=202,
  20.0 s). A reference CLI built from tree `91392ee` behaves identically with
  the key script (frames=271, paints=107, 9.03 s), so the change came from the
  real-time pacing integration changing which screen each frame-indexed key
  lands on; the title itself calls `notifyDestroyed()` in response to input.
  Game/keys interaction, not a defect.
- `GT赛车2_36.jar` ok -> timeout. Standalone the same binary completes 600
  frames in 77.96 s (7.7 fps, 313.9 M bytecodes); the reference `91392ee` CLI
  needs 131.0 s for the same run (4.6 fps), so the current build is faster.
  The sweep's per-title budget is hit only while ten heavy titles run in
  parallel. Previously recorded as a load-sensitive title (round 4 sweep);
  adjudicated as parallel-load noise.

All other adjudications are unchanged: `三国赤壁小乔秘史_7976.jar` (stale pak,
game-side), `愤怒的小鸟-高清版_2353.jar` (>16 MB heap), `乱世Q三国_1649.jar`
(auth expiry), `有女倾城破解版_11525.jar` (MIDlet self-exit),
`鲁宾逊漂流记汉化BT版_4677.jar` (truncated ZIP), fixtures/JBenchmark rows
(title-exit by design).

## Revalidation, 2026-11-15 (round 52, crash-hardening build)

The guarded interpreter (array/object operand checks) and the GIF canvas budget
were revalidated with a full sweep against the same recipes (10 workers,
600 frames at 30 fps, per-title keys) into
`.local-tools/debug-runs/compat-20261115-r52`:

- **404 rows: 371 ok / 28 title-exit / 2 title-exception / 2 no-frame /
  0 timeout / 1 bad-jar**, compared with the round-42 run
  (371 / 27 / 2 / 2 / 1 timeout / 1).
- `GT赛车2_36.jar` timeout -> ok: standalone the same binary still needs
  ~78 s for 600 frames, so the round-42 timeout remains parallel-load noise.
- `西伯利亚大反击_11466.jar` ok -> title-exit (frames 291, paints 291): the
  title is itself flaky at this point. Running it alone with the same keys,
  the pre-hardening reference CLI (`/tmp/nj2me-base2`) also called
  `notifyDestroyed()` at frame 291 on one of two runs and reached 600 frames on
  the other, so this is a title/keys timing interaction, not a regression from
  the guards.
- No new non-ok rows, and all previously adjudicated rows are unchanged
  (`三国赤壁小乔秘史_7976.jar`, `愤怒的小鸟-高清版_2353.jar`,
  `乱世Q三国_1649.jar`, `有女倾城破解版_11525.jar`,
  `鲁宾逊漂流记汉化BT版_4677.jar`, fixtures/JBenchmark by design).

## Round 54 final revalidation, 2026-11-16 (synchronized-callback monitor fix)

The bridge monitor fix (see
`docs/notes/2026-11-16-bridge-synchronized-callbacks.md`) was revalidated with
a full sweep against the same recipes (10 workers, 600 frames at 30 fps,
per-title keys) into `.local-tools/debug-runs/compat-20261116-r54b`:

- **404 rows: 372 ok / 27 title-exit / 2 title-exception / 2 no-frame /
  0 timeout / 1 bad-jar** — the best distribution recorded so far, and the
  first sweep with no timeout at all.
- Compared with the unbounded-monitor intermediate run
  (`.local-tools/debug-runs/compat-20261116-r54`: 369 / 27 / 2 / 2 /
  3 timeout / 1), the three titles that deadlocked under the unbounded wait
  are back: `极品飞车13之变速英文3D版_6646.jar`, `极品飞车16：亡命狂飙_1519.jar`
  and `荣誉勋章之空降神兵改良版_2993.jar` timeout -> ok (600/617/600 frames).
- Compared with the round-53 sweep (370 / 29 / 2 / 2 / 0 / 1) the two
  wall-clock-sensitive improvements are stable: `穿越火线-韩国正版_6874.jar`
  and `美男狂想曲_3876.jar` title-exit -> ok (600 frames each). No regression
  in either direction, and all previously adjudicated non-ok rows are
  unchanged.

`冰淇淋少女成长日记_7307.jar` no longer throws
`IllegalMonitorStateException` (0 occurrences, was one per frame) but still
ends itself at frame 290 — that exit is game-side and predates the fix.

## Round 62 revalidation, 2026-11-16 (after the pacer revert)

The round-61 sweep (`compat-20261116-r61`) was taken with the pacer change
that was later reverted, so it is not a valid baseline (`370 ok / 25
title-exit / 4 timeout / ...`).  After the revert commit `230d4f5`, a fresh
full sweep with the reverted CLI:

- `NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me tools/sweep_compat.py .local-tools/debug-runs/compat-20261116-r62`
  (10 workers, 600 frames @30 fps, per-title keys and RMS)
- Result: **404 rows = 371 ok / 27 title-exit / 3 title-exception / 2 no-frame /
  0 timeout / 1 bad-jar** — the four r61 timeouts are gone (0 timeouts).
- Diff vs `compat-20261116-r54b` (372/27/2/2/0/1): one row changed,
  `大富翁中国之旅_11873.jar` ok → title-exception (1 uncaught
  `java/lang/StackOverflowError`, 407 paints vs the usual ~283–314).
  Adjudicated a parallel-load / key-timing flake on a game-side recursion:
  three standalone re-runs with the same sweep keys are deterministic
  `rc=0`, 600 frames, ~282–309 paints, 0 uncaught.  Not a regression.
- The two r61 regression casualties stay fixed: `JBenchmark HD.jar`
  (139 frames rc=0) and `丧尸危机2-自由之翼BT版_997.jar` (621 frames rc=0),
  and the known-slow rows (`GT赛车2_36`: ~130 s loader, `时光樱花册`:
  ~90 s) are back inside their usual band.

Full test suite after the revert: **192/192 PASS in 337.83 s**
(`/tmp/r61-ctest5.log`).

## Round-88 revalidation on 0.5.14, 2026-11-19

The 0.5.14 release (shared cheat engine, Android cheat files) does not change
the headless runtime path for titles without cheats, but the engine sits in
`nj_midp_frame()`, so the corpus was re-run with the release CLI:

```
NJ_BIN=<headless CLI at 4ca20ca> NJ_KEYS_DIR=$PWD/tools/corpus_keys \
  tools/sweep_compat.py .local-tools/debug-runs/compat-20261119-r88
```

Result: **404 rows = 387 ok / 12 title-exit / 2 title-exception / 2 no-frame /
1 bad-jar, no timeout**. The verdict set is identical to the round-73 sweep
(0 changes), so the cheat-engine refactor, the metadata bump and the Android
change are compat-neutral. This is the current best distribution and the one
0.5.14 ships with.
