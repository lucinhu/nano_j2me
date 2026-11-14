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
