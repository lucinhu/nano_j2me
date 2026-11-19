# Corpus

`tests/games/` holds the JARs the compatibility work is measured against.  The
directory is deliberately **not** tracked by git (`.gitignore:42`): the files are
third-party games of mixed provenance and the tree would carry ~280 MB of them.

    $ ls tests/games/*.jar | wc -l
    398

Most titles were collected by hand over the rounds; the ones taken from
archive.org are listed here so a later reader can tell a download from a
hand-collected file.

| file in tests/games | archive.org item | title |
| --- | --- | --- |
| Flippy跳跃大冒险_archive.jar | `FlippysFlyingFrenzyRAZRV3` | J2ME Flippy's Flying Frenzy EN (patched for Motorola RAZR V3) |
| 3D武装直升机2_archive.jar | `3d-attack-chopper-2-j2me-1-01` | 3D Attack Chopper 2 v1.01 |
| 太空冲击303_archive.jar | `spaceimpact-303-en` | Space Impact 303 |

`docs/COMPATIBILITY_MATRIX.md` lists every one of them with what happened when
it was replayed: size, MIDlet name, vendor, version, MIDP profile, 3D API in
use, class count, heap, frames, bytecodes, the exceptions it threw, the
verdict and the reason behind a non-ok verdict.  Two tracked tools reproduce it:

* `tools/sweep_compat.py [dir]` replays every JAR with one fixed recipe (headless,
  240x320, `--fps 30`, 600 frames, 16 MB heap retried at 32 MB, one shared input
  script, a fresh RMS directory per title) and writes `rows.jsonl`.  A title that
  cannot start on the generic handset is retried once with `--device nokia6230`,
  so a device-profile requirement is measured rather than reported as a failure.
* `tools/make_compat_matrix.py [dir]` turns `rows.jsonl` into the table.

A row is `ok` when the title ran its frames, `未呈现画面` when it exited cleanly
without ever presenting a frame, `失败` when it crashed or was rejected, and
`非可用包` when the JAR is not a usable MIDlet suite.  The three states are kept
apart on purpose: a title that runs and paints nothing is not a crash, and
lumping them together used to hide which rows were actually broken.

## Where the table's provenance comes from (round 18)

A rendered matrix must say which build produced the rows, not which commit
happens to be checked out when it is rendered.  `tools/sweep_compat.py` now
writes `meta.json` next to `rows.jsonl`:

```json
{"commit": "2606d63", "date": "2026-10-25"}
```

`tools/make_compat_matrix.py` reads that file first and prints
"Built from `2606d63`, 2026-10-25 (recorded by `tools/sweep_compat.py`)"; only
when there is no meta file does it fall back to the current `HEAD`.  Rows that
are swept in later runs keep the meta of the run that wrote them, so a table
grown over several sessions cannot silently claim a single build.

## The archive.org vault sweep (round 2)

`tools/sweep_compat.py` also reads extra corpus directories from the
`NJ_SWEEP_DIRS` environment variable (colon separated), so a large collection can
be measured without committing it:

```sh
NJ_SWEEP_DIRS=/path/to/vault/Roms tools/sweep_compat.py .local-tools/debug-runs/compat-matrix-20261025
```

Rows from those directories are labelled `vault/<name>.jar`.  The 455-title
archive.org item `j2me-vault` ("J2ME FULL ROMS & Screenshots Archive") is one such
collection and is deliberately kept out of this repository.

## The Tomb Raider collection (round 4)

A second archive.org item, `tomb_raider_j2me_collection` (66 MB, one file per
JAR, no aggregate ZIP), contributes titles from the Tomb Raider series.  The
sweep script hashes every candidate against the corpora it already knows, so the
106 JARs this project had not seen are added while 86 duplicates are skipped:

```sh
NJ_SWEEP_DIRS=/path/to/vault/Roms:/path/to/vault2 tools/sweep_compat.py .local-tools/debug-runs/compat-matrix-20261025
```

As with the first collection, the JARs stay out of the repository; only the
verdict rows are committed.


### Verdicts

`ok` (ran to the frame budget), `title-exit` (the title called
`notifyDestroyed()` after painting), `no-frame` (the title never painted a
frame), `failure`, `timeout`, `oom`, `bad-jar`.  A row that paints and then
quits by design is `title-exit`, not `no-frame`: the latter is what sends us
looking for a runtime defect.

## Growing the corpus from archive.org (round 11)

`tools/fetch_corpus.py` rebuilds the out-of-tree corpus reproducibly: it walks
the named archive.org items, unpacks every `.jar` inside their zips, drops
files smaller than 20 kB and anything whose SHA-256 is already on disk
(`tests/games`, `/tmp/vault/Roms`, `/tmp/vault2`), and writes the rest to the
output directory:

    python3 tools/fetch_corpus.py /tmp/vault3
    NJ_SWEEP_DIRS=/tmp/vault/Roms:/tmp/vault2:/tmp/vault3 \
        python3 tools/sweep_compat.py .local-tools/debug-runs/compat-matrix-20261025

A run can be interrupted and resumed: duplicates are detected by content, so a
second invocation only fetches what is still missing.  The first run of this
round added seven Gameloft titles (Brother in Arms, Midnight Bowling, Mystery
Mansion Pinball, Medieval Combat, New York Nights, and two more) - one zip in
the collection failed with an SSL EOF and was skipped, which the script reports
rather than aborting.  The sweep grew from 961 to **968 rows** with the new
titles all `ok`, so the matrix now reads 964 ok / 2 never-painted / 1
title-exit / 1 unusable package.

The corpus stays out of the repository (size and provenance); only the tools
that fetch and measure it are tracked.

### A second expansion run and what stalled it (round 12)

Re-running `tools/fetch_corpus.py` added one more Gameloft title, bringing the
swept corpus to **969 rows (965 ok / 2 never-painted / 1 title-exit / 1 unusable
package)**.  The run also showed where the fetcher can stall: one archive.org
zip answered the metadata request and then kept the payload request open, so the
run sat on that item for minutes.  The download and metadata requests now carry
explicit timeouts (120 s / 60 s), so a single slow item is skipped instead of
holding the whole run; because duplicates are dropped by content hash, the next
run resumes exactly where this one stopped.


### Fetcher regression and repair (round 126, 2026-11-20)

A later edit of the metadata/download timeout arguments repeated a keyword
(`timeout=120, timeout=60`), which is a Python `SyntaxError`: the reproducible
corpus-fetch path was broken from that commit until round 126, where the
duplicate was removed (metadata request 60 s, payload request 120 s).  The
script compiles cleanly again and a fresh run into `/tmp/vault4` downloaded
eight titles that are not in `tests/games`.

## Does an ok row actually move? (round 13)

The sweep records that a title painted something, not that the frame ever
changed: a title can paint the same pixels for 900 frames.  `tools/check_corpus_motion.py`
dumps three frames per title (same recipe as the sweep, `--dump-every` a third
of the budget) and counts how many of them differ byte for byte, so the two
cases stay apart:

* a static title/menu screen under the shared key script - expected, the script
  is generic and most titles wait for a tap we do not send;
* the runtime painting a stale frame - a real defect.

Sample of 12 titles drawn deterministically from the 969 `ok` rows (seed 7,
900 frames): **7 change frames, 5 paint one frame**.  The five are all
title/menu screens (Elven Chronicles and 疯狂农场汉化版 show 3 distinct frames,
Medal of Honor 2010 and Prince of Persia 1-2), so no stale-frame case showed up
in this sample.  Treat it as a starting measurement, not a verdict: the tool
exists so a future round can grow the sample, and a frozen row is only a lead
until it is compared with the reference emulator.

### Reading a frozen frame (round 14)

`tools/check_corpus_motion.py <rows dir> --count N --frames F` dumps three frames
per title and prints `unique_frames`.  Two samples so far: 7 of 12 and 3 of 6
titles change their frames, the rest paint one frame for the whole run.

That second group is not automatically a defect, and one was traced to the end:

* `slimevolley.jar` - 600 frames, 600 paints, but only **15,038 bytecodes in
  total** (25 per frame) and 4 gfx calls per paint.  The title paints its idle
  screen and waits for a pointer it never gets from a key script; there is
  nothing for the emulator to render differently.  Adding ten `fire` presses
  changes nothing, which fits a game that only starts on a tap.

So the reading rule is: a frozen row means *the shared input script did not
reach a state that animates*, and only a per-title key or a reference-emulator
comparison can turn it into evidence.  The tool exists to produce the candidate
list; it does not classify.

### One frozen frame, read to the prompt (round 15)

`细胞分裂4混沌法则中文版_11671` was one of the "painted one frame" rows, so it
was compared against the reference renderer before anything was changed:

- Ours, 600 paced frames: `frames=600 paints=600 gfx=15,021 bytecodes=256,696,121
  natives=102,358,098`; dumped frames 300/450/600 are byte-identical.
- Reference renderer, same title, two screenshots three seconds apart: both are
  the same "您想打开声音进行这个游戏吗? 是 / 否" prompt (different bytes because
  the cursor blinks).

So the frame is not stale: the title paints its sound prompt and then polls for
an answer.  The 13 M bytecodes/s and 170k native calls per frame are the title's
own busy-wait for the soft-key answer, and the shared sweep script never presses
it.  Nothing to fix in the runtime; the row is `ok` and the frozen frame is the
title waiting for input, exactly as the previous round's rule says.

### Frozen frames are mostly prompts (round 16)

Third motion sample, plus a follow-up that answered the prompt:

| title | shared script | script that answers the sound prompt |
| --- | --- | --- |
| 细胞分裂4混沌法则中文版_11671 | 600 frames, gfx 15,021, 256.7M bytecodes, one frame | 900 frames, **gfx 57,443**, 411k bytecodes |
| slimevolley.jar | 600 frames, 15,038 bytecodes | exits after 40 frames: the soft key it gets is "quit" on that menu |

So the title was not stuck: it was holding a modal "turn sound on?" prompt and burning its own poll
loop until someone answered.  The shared corpus script never presses a soft key at the right moment,
which is why a row can be `ok`, paint 900 identical frames and still be healthy.  A frozen row becomes
a candidate only after either a per-title script or a reference comparison; otherwise the finding is
about our input recipe, not the runtime.

### A generic soft-key clause does not unfreeze anything (round 17)

`check_corpus_motion.py` now takes `--keys` so a run can be repeated with a different script.  The
same 12 titles, same seed, with a clause that presses a soft key at frames 8/12/160/400/700, gave the
identical split: **7 titles change frames, 5 paint one frame in 900**.

So blanket scripting is not the answer to a frozen row.  Those five sit in menus whose buttons are
reached by pointer taps on title-specific coordinates (or they wait on a timer longer than the run).
Turning a frozen candidate into evidence needs either a per-title script or a reference comparison;
this is now the documented rule, and the tool can take that script without being edited.


### A carrier billing page is not a frozen frame (round 66)

`终极雷电_4125.jar` looked like a menu that ignored input: a fire key at frame 10 did nothing,
while the same key at frame 100 (after the title menu had settled) opened a **carrier billing
dialog** — "游戏支付 / 您将选择北京全天通公司提供的游戏风云榜业务 / 信息费1元/条 … 确认 / 返回".
The title's content sits behind that SMS subscription page, so a probe that presses fire either
stays on the title menu or lands on the payment page; 1800 frames then paint the same screen and
no audio Player is ever created.

The rule for adjudication is the one above: a frozen or silent row is a statement about the input
recipe until a per-title script or a reference comparison says otherwise.  The SMS page is a
game-side gate (there is no SIM and no Wireless Messaging counterpart on the emulator side to
complete it), not a rendering, input or audio defect.  The same reasoning applies to the other
menu-bound rows in the audio sample: `侠盗飞车_6246` and `机甲风暴_7888` sit on their title menus
in the sampled window.

Reading the screenshot is what settled it, so keep `--dump-frames` + `--dump-every` in the probe
recipe when a row is about to be called broken.

### Per-title key scripts in the sweep (round 69)

The rule above ("a frozen row is about the input recipe until a per-title script
says otherwise") now has tooling: `tools/sweep_compat.py` honours the
`NJ_KEYS_DIR` environment variable, a directory of per-title key scripts named
after the JAR (`浴火银河2完整版_7689.jar` -> `浴火银河2完整版_7689.txt`, same
syntax as `--keys`).  The curated files live in `tools/corpus_keys/` with a
README that documents the format, the naming rule and the curation rules: only
add a script when the generic recipe cannot reach representative content and the
script was verified from a frame dump.  A row that used a curated script carries
an extra `"keys": "<file name>"` field, so a reader can tell a verdict with
title-specific input from a generic one.

The first two scripts are the ones this document already discusses:

* `浴火银河2完整版_7689` — the title opens on a static "Music On / Music Off"
  screen; the script (fire/fire/soft1/fire/soft2/fire at frames 30..180)
  dismisses it and the same 600-frame probe then records `m3g=174,831`,
  `tris=1,189,565` instead of the previous `m3g=0` frozen menu.
* `终极雷电_4125` — fire at frames 10 and 100 keeps the run on the title menu
  and then on the carrier billing page, documenting the gate from round 66
  instead of leaving the row at a black menu.

Neither script changes a verdict by pressing through a game-side gate: the SMS
page stays a gate, and the sweep reports it as `ok` with the billing screen
painted, exactly like the round-66 adjudication.

### Motion and heap probes on 0.5.17 (round 108)

`tools/check_corpus_motion.py` grew from three samples of ~10 titles to two
targeted runs: 24 `ok` 2D rows (seed 5, 300-frame recipe, three dumps) and 10 3D
rows (`--min-m3g 1000`).  The 2D sample
gave **23 titles changing frames, 1 painting one frame**; the 3D sample gave
**10 of 10 changing frames**.  No stale-frame or black-frame row surfaced, so
the earlier reading rule ("a frozen row is a statement about the input recipe
until a reference comparison says otherwise") held again.

The single static 2D row was read to the end.  `法拉利GT3－世界赛道BT版_776.jar`
at the default 16 MB heap paints a blue screen with the title's own text
`WOOP'S! OUT OF MEMORY!` / `Total=16384` and burns 4.15 billion bytecodes in
900 frames while it waits.  With `--heap 32` (or 64) the same run starts the
game's Ferrari F430 splash and settles at ~300k bytecodes: the frame is still
static (it is a splash screen), but the game is no longer in its error path.
So this is a **heap-sensitive title**, not a rendering defect: the game checks
the total Java heap itself, catches the failure and paints its own error
screen, which is why the sweep records it as `ok` at 16 MB.  `--heap 32` makes
it start; the APK's in-game tools panel can raise the same setting.

`--heap 16 --heap-grow` is the general workaround: the arena grew to a
22,078,880-byte peak and the game left the error path, so growth is what a
title like this needs when the static default is too small.  The client keeps
`heap_auto_grow = 0` by default on purpose (fixed heaps keep state sizes and
GC behaviour predictable; the launcher/APK offer an explicit switch), so the
probe documents the case rather than changing the default.

To find rows like this automatically, the new
`tools/check_corpus_heap.py <rows dir> --count N --frames F --seed S` runs each
sampled `ok` title twice (16 MB and 32 MB) and flags a row `heap-sensitive`
when the low-heap run burns at least 8x the high-heap bytecode count - the
signature of a game-side error screen plus a busy wait.  A 40-title sample
(seed 7, 300 frames) found **0 heap-sensitive rows**: the 16 MB default is
adequate for the typical corpus title, and the heap-hungry ones are rare
outliers (this title; 愤怒小鸟 needs more than 16 MB as recorded earlier).
The tool is kept for regressions in the default heap or in error handling.

## Reference cross-check of leftover non-ok rows (round 116, 2026-11-19)

The remaining non-`ok` corpus rows were historically adjudicated by inspecting
the titles themselves.  This round re-checked a sample against an independent
runtime, FreeJ2ME-Plus, using the tracked headless driver
`.local-tools/run-freej2me-headless.sh JAR RUNDIR [W H]` (commands on stdin:
`wait MS`, `shot PATH.png`, `quit`).

| title | our verdict | FreeJ2ME-Plus reference | conclusion |
|---|---|---|---|
| `有女倾城破解版_11525.jar` | immediate exit, 0 frames, 44,358 bytecodes; `ZWFRZMIDlet.startApp()` sets a Canvas whose `run()` switch calls `ZWFRZMIDlet.a()` → `notifyDestroyed()` when static state `l == 6` | same immediate exit; screenshot is FreeJ2ME's own "APP TERMINATED! Open a new one through Drag-Drop or 'File->Open'." placeholder | game-side self-exit (cracked build), not a runtime defect |
| `乱世Q三国_1649.jar` | no frame, `RecordStoreNotFoundException` on first frame | prints `授权过期` (authorization expired) and terminates immediately (screenshot: same APP TERMINATED placeholder) | game-side authorization expiry; runtime failure is the expected consequence |
| `愤怒的小鸟-高清版_2353.jar` | runs but needs more than the 16 MB default heap (OutOfMemoryError surfaced by the game) | cannot load at all in the reference: `java.lang.NullPointerException … this.suitename is null` in `MIDletLoader.loadManifest` | not comparable — our runtime gets further; documented under the known >16 MB heap requirement |
| `三国赤壁小乔秘史_7976.jar` | NPE thrown twice inside game code, keeps running 300 frames / 1.7 B bytecodes | 4.5 s screenshot is effectively black (6×10⁻⁴ lit pixels, small gray text block) and no exception is logged | both runtimes fail to bring up the game; the title depends on resources that are missing/stale in this dump, not on runtime behaviour |

Method notes:

- `printf` inside a single-quoted format does not expand shell variables; pass
  the absolute screenshot path as an argument (`printf 'wait 4500\nshot %s\nquit\n' "$shot"`).
- FreeJ2ME's headless driver writes screenshots as `shot PATH.png` and exits
  non-zero if the target exists; always use a fresh path per run.
- Run each title in its own `RUN_DIRECTORY` — the reference keeps its own
  `rms/`, `config/` and `freej2me_system/` state there, so re-using a directory
  would mix runs.

Conclusion: every row checked shows the same failure class in both runtimes,
strengthening the existing adjudications (the remaining non-`ok` rows are
title-side: authorization/SMS menus, cracked self-exit, missing resources,
insufficient heap, or intentionally malformed fixtures).

## Full-corpus motion probe: 384 titles at 300 / 900 / 1500 frames (round 122, 2026-11-19)

The earlier motion checks sampled 24 2D and 10 3D titles.  This round ran every
`ok` row from the 0.5.18 sweep (`compat-20261119-r105`) through
`tools/check_corpus_motion.py` with `--count 400 --frames 300 --jobs 8`
(parallel jobs and the sweep's per-row `heap`/`device` recipe were added to the
tool in this round; a device retry also bumps the heap, and replaying with the
generic profile reports a false freeze).

- 384 titles: **332 paint different frames, 52 paint one frame** at 300 frames.
- Re-running the 52 static rows with `--frames 900` (dumps at 300/600/900):
  **36 more animate** (they were on slow loading screens), 16 stay static.
- Re-running those 16 with `--frames 1500` and the sweep's per-row recipe
  (`--heap`/`--device`): **15 animate**; the remaining 13 are static menus /
  title screens awaiting input (mine-sweeper menu, pool title, taxi title,
  angry-birds OOM notice at 16 MB, Ferrari GT3 splash, …), and one
  (`贪吃蛇经典版_4515.jar`) exits after 55 frames, which is the title's own
  behaviour.  No stale/black-frame renderer defect surfaced.
- The "black" flag is horizon-sensitive: several rows are pure black at frame
  200 (loading) and 100 % lit by frame 400 (e.g. `世界足球2012中文版_1724`,
  `魔兽塔防_12480`, `超级坦克大战2汉化版_531`), so a 300-frame sample reads
  them as black screens when they are only slow loaders.  Judge with 900+
  frames before calling a title black.
- `1945空战英雄_6105.jar` needs the Nokia device profile: without
  `--device nokia6230` the MIDlet constructor fails
  (`cannot instantiate ExtendTorpedoMidlet: java/lang/IllegalAccessException`)
  and the run paints nothing; with it the title animates.  This is a recipe
  issue, not a loader defect — the sweep already retries such titles with that
  profile and records `needs --device nokia6230` in the row.

## Round 123: the generic key script can make a title exit

A `title-exit` verdict only means "the MIDlet called `notifyDestroyed()`
inside the frame budget"; it does not mean a player would have quit there.  The
generic recipe presses `30:fire` and `55:soft1` to get past start screens, and
on some titles the default selection on that first screen is "exit"/"quit", or
the key itself is "close this page".  Re-running all 14 `title-exit` rows of
the 0.5.18 sweep (`compat-20261119-r105`) **with no input at all** showed that
13 of them run the complete 600-frame budget; only `蹦球传说_7801.jar` really
exits by itself (91 frames, 91 paints):

| row | with generic keys | with no input |
| --- | --- | --- |
| `Boun_en_ar_de_he-IL_ru-RU.jar` | exit at 55 | 600 frames / 592 paints |
| `Vime_en_ar_de_he-IL_ru-RU.jar` | exit at 55 | 600 frames / 524 paints |
| `JBenchmark HD.jar` | exit at 140 | 600 frames / 2 paints |
| `book4/5/6/7.jar` | exit at 30 | 600 frames / 600 paints |
| `jump.jar` | exit at 30 | 600 frames / 600 paints |
| `三国志吞食天地_8144.jar` | exit at 57 | 600 frames / 296 paints |
| `冰淇淋少女成长日记_7307.jar` | exit at 290 | 600 frames / 600 paints |
| `杀鬼子-敌后武工队_2185.jar` | exit at 293 | 600 frames (playable) |
| `百鬼商店破解版_10526.jar` | exit at 72 | 600 frames / 600 paints |
| `美男狂想曲_3876.jar` | exit at 58 | 600 frames (menu) |
| `蹦球传说_7801.jar` | exit at 90 | exit at 91 (self-exit) |

`tools/sweep_compat.py` now retries a `title-exit` row once without keys (same
`--heap`/`--device` recipe, 600 frames, a temporary empty script
`<rows>/keys-none.txt`).  If it reaches at least half the budget with rc=0 and
no uncaught exception, the row becomes `ok`, records `keys: "none"` and the
note "runs without input; the generic key script made it exit"; a title that
also quits without input keeps `title-exit` and the note says so.  The other
probes replay the row recipe: `check_corpus_motion.py`,
`check_corpus_deep.py` and `check_corpus_audio.py` skip `--keys` for a
`keys: "none"` row and use `tools/corpus_keys/<name>.txt` when the row names a
per-title script.

The lesson: the generic recipe must stay conservative (no pointer taps, see
above), and a recipe-caused exit is a property of the *recipe*, not of the
title.  Per-title scripts in `tools/corpus_keys/` remain the right way to
drive a screen that needs real input; `keys: "none"` is for titles whose
attract mode runs on its own.

Full sweep after the fix (`compat-20261120-r123`, the same 0.5.18 CLI and key
map as r105): **398 ok / 1 title-exit / 2 title-exception / 2 no-frame /
1 bad-jar, 0 timeout** — up from 386 ok / 13 title-exit and, more importantly,
with every remaining `title-exit` row now proven to exit without input.  The
only difference beyond the 13 recipe-caused exits is `GT赛车2_36.jar`
returning to `ok` (600 frames, 478 paints) after its usual load-sensitive
timeout in the previous run.  No new crash, hang or uncaught exception
appeared.

## Round 124: an uncaught `OutOfMemoryError` is a heap verdict, not a title verdict

`愤怒的小鸟-高清版_2353.jar` was the last `title-exception` row that looked
like a runtime defect.  At the default 16 MB heap its media loader throws
`OutOfMemoryError` several times and the game thread finally dies with
`uncaught exception in thread 2: java.lang.OutOfMemoryError @ luma.k.c+209`,
yet the MIDlet keeps painting (600 frames, 525 paints).  The sweep marked that
`title-exception` because the thread died, even though the cause is the heap:
at 32 MB the same recipe runs all 600 frames with **zero uncaught exceptions**.

The sweep now routes that case through the existing heap retry: when a run
reaches the frame budget with rc=0 but has an uncaught exception *and* the log
contains `OutOfMemoryError` at heap 16, it retries at 32 MB instead of
reporting `title-exception`.  Genuine game-code exceptions keep the verdict and
now carry a readable note naming the exception, e.g. `三国赤壁小乔秘史_7976.jar`
under heap 16: "game code threw java.lang.NullPointerException in a thread
while the MIDlet kept painting" — a bug inside the title's own thread, not a
runtime failure.

The lesson is the same as for the boot path: an OOM always means "retry with
more heap" first, and only a title that still fails at 32 MB is a compatibility
row.

The round-124 full sweep (`.local-tools/debug-runs/compat-20261120-r124`, 10
jobs, 0.5.18 CLI) therefore reports **399 ok / 1 title-exception / 2 no-frame /
1 title-exit / 1 bad-jar, 0 timeout** — 404 rows, 387 of them fully rendering
without any exception.  The single `title-exception` is the game-side
`NullPointerException` above; the two `no-frame` rows are an expired activation
and a self-exit confirmed against the FreeJ2ME-Plus reference; `蹦球传说`
exits by itself after 91 painted frames; `鲁宾逊` is a truncated ZIP.

## Round 125: 399-title deep probe at 900 frames (2026-11-20)

The round-124 `ok` set re-run through `tools/check_corpus_deep.py` at 900
frames (30 s per title at 30 fps), 8 jobs, with every row's own heap, device
profile and input recipe replayed:

| titles | passed | early exit (title ends itself) | failed |
|---|---|---|---|
| 399 | **390** | 8 | **1** |

The one failure was a parallel-load timeout, not a regression:
`时光樱花册－遗失之心_1009.jar` hit the 240 s per-title budget with zero
frames while seven other titles loaded concurrently; stand-alone (same
binary, same recipe) the same 900-frame run completes in **29.8 s** with
`frames=900 paints=900 gfx=4500` and no exceptions.  It is a heavy loader
that loses badly under load, not a hang.

The eight early exits are titles whose program ends by itself during a longer
run (`Lab Hamster`, `SlimeVolleyBallMidlet`, `Snake` at 470 frames; `都市摩天楼`
479; `龙珠Z传说` 321; `明星麻将馆(美女真人版)` 325; `杀鬼子-敌后武工队` 293;
`Hotguys Rhapsody` 58) — all `notifyDestroyed()` paths, and the probe treats a
clean early exit as acceptable rather than a failure.

Tooling: `tools/check_corpus_deep.py` now replays the row's `heap` and
`device` profile instead of a uniform `--heap 16`.  The first probe run
flagged `1945空战英雄_6105.jar` with `rc=1 frames=0`; the row records
`"device": "nokia6230"` and "needs --device nokia6230", and the unmodified
uniform run cannot start that title's constructor.  Replaying the row profile
removes that false failure.
