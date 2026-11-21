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
second invocation only fetches what is still missing.

### Round 160: the fetched corpus lives in a persistent cache

Earlier rounds downloaded into `/tmp/vaultN`, which the system cleared between
work sessions - probes could then not replay fetched rows and the data had to
be downloaded again.  `tools/fetch_corpus.py` now defaults to a persistent
cache:

    python3 tools/fetch_corpus.py                       # default items -> cache
    python3 tools/fetch_corpus.py my-top-95-mobile-java-games
    python3 tools/fetch_corpus.py -d /tmp/somewhere item-name
    NJ_SWEEP_DIRS="$HOME/.cache/nano_j2me/corpus" tools/sweep_compat.py OUT

The destination is `$NJ_CORPUS_CACHE` when set, otherwise
`$XDG_CACHE_HOME/nano_j2me/corpus` (default `~/.cache/nano_j2me/corpus`).  A
bare word argument is an archive.org item; a path-looking argument is still
accepted as the destination for backwards compatibility.  The cache is never
committed (it is downloaded content); the content-hash duplicate check makes
re-runs and interrupted runs cheap, and the cache also counts as already-known
so a repeated run skips everything it has.
  The first run of this
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

## Round 126: external set re-fetched, 412-row sweep (2026-11-20)

The round-11/12 out-of-tree vault (`/tmp/vault`, `/tmp/vault2`) is gone, so the
published 969-row matrix can no longer be rebuilt from this machine as-is.
Repairing the fetcher (previous section) and running it into `/tmp/vault4`
recovered **eight Gameloft/retail titles** that are not in `tests/games`; a
fresh sweep with those directories added
(`NJ_SWEEP_DIRS=/tmp/vault4 NJ_KEYS_DIR=tools/corpus_keys`,
`.local-tools/debug-runs/compat-20261120-r126`, 10 jobs, 0.5.18 CLI) reads:

| rows | ok | title-exception | no-frame | title-exit | bad-jar | timeout |
|---|---|---|---|---|---|---|
| 412 | **407** | 1 | 2 | 1 | 1 | 0 |

All eight newly fetched titles run 600 frames normally (284–601 paints):
`Gameloft Brother In Arms Earned In Bloods 1.2.4`, `Gameloft Christmas Block
Breaker`, `Gameloft Midnight Bowling`, `Gameloft.Mystery.Mansion.Pinball
1.2.1-Retail`, `New York Nights v1.00`, `Nightmare Creatures`,
`TotallySpies S60`, `gameloft.medieval.combat.v1.04.s60`.  The five non-ok rows
are exactly the round-124 set (game-side NPE, two self-exits confirmed against
the reference oracle, one genuine title exit, one truncated ZIP).

`docs/COMPATIBILITY_MATRIX.md` was refreshed in round 127 by merging the
2026-10-25 external snapshot with this sweep: every in-repo title (and the three
fetched titles that already had a historical row) now carries its 0.5.18
measurement, the five genuinely new titles were appended, and the table states
this mixed provenance explicitly instead of pretending all rows were measured
at once.

## Round 127: the compatibility matrix merges history with a fresh sweep (2026-11-20)

The published matrix mixed two impossible-to-repeat things: 969 rows measured
in October on an older binary and a vault that no longer exists.  Round 127
rebuilds it without dropping either half:

- `tools/make_compat_matrix.py` now reads an optional `meta.json` provenance
  block and, when it carries a `sources` paragraph, prints that paragraph plus
  an explicit "rows were measured in different batches, compare within a batch"
  note.  It also reads each row's optional absolute `jar_path` before falling
  back to `tests/games`, so fetched titles can report their 3D API usage.
- A scratch rows directory (`.local-tools/debug-runs/compat-matrix-r127`)
  contains the 969 historical rows parsed back out of the Markdown table, with
  407 of them replaced by their round-126 measurements (404 in-repo titles +
  3 fetched titles that already existed in the old vault snapshot), plus the
  five new fetched titles as `extra_rows.jsonl`.
- Regenerated `docs/COMPATIBILITY_MATRIX.md`: **974 titles — 968 `运行正常`,
  2 `标题自行退出`, 2 `未呈现画面`, 1 `标题自身异常`, 1 `非可用包`**.  Each
  re-measured row now records its current frame/paint/bytecode numbers, the
  run's note (e.g. `runs without input; the generic key script made it exit`)
  and its heap (`愤怒的小鸟-高清版` now runs at 32 MB instead of failing).
- Historical rows that could not be re-run keep their October numbers, so the
  matrix remains the only record of that external set while it is offline.

`COMPATIBILITY_MATRIX.md` is generated; the scratch rows directory stays in
`.local-tools` (untracked) like every other sweep output.

### Round 128 (2026-11-20): the corpus fetcher can ingest loose JARs

The default archive.org items no longer carry `.zip` archives (the fetch
path had quietly stopped adding titles), so `tools/fetch_corpus.py` grew a
second ingestion mode: loose `.jar` files listed directly on an item are
downloaded one by one, filtered by `MIN_BYTES`, deduplicated by SHA-256 and
flattened into the vault.  `.7z`/`.rar` members are reported as skipped
(there is no 7z reader in the standard library).  The item
`my-top-95-mobile-java-games` (95 loose JARs, 61 MB) was added to
`DEFAULT_ITEMS`; fetching it into `/tmp/vault5` added **108 unique JARs**
(68 MB), since the same item also ships a redundant ZIP.

The expanded sweep (`NJ_SWEEP_DIRS=/tmp/vault4:/tmp/vault5`, 10 jobs,
0.5.18 CLI) covered **520 rows**:

| verdict | count |
| --- | --- |
| ok | 510 |
| timeout | 2 |
| bad-jar | 2 |
| title-exit | 2 |
| failure | 1 |
| title-exception | 1 |
| no-frame | 2 |

100 rows were new; 95 ran normally.  The five new non-ok rows were triaged
by hand:

* `vault/assassins_creed_260615.jar` — runs 600 frames normally but needs
  **246.8 s** (2.4 fps) of game logic, far over the 90 s sweep budget; slow
  title, not a hang.
* `vault/gangstar_2_kings_of_260766.jar` — prints `DONE`, creates its MIDI
  players and then idles in `hrtimer_nanosleep` after repeatedly catching
  `NullPointerException at e.b()V`; the same happens with the
  `--device nokia6230 --heap 32` recipe and the FreeJ2ME-Plus reference
  shows a blank white screen, so this is a game-side startup stall.
* `vault/bounce_tale_qy5sf1si.jar` — not a ZIP file at all (bad download or
  a JAD renamed to `.jar`), classified `bad-jar` correctly.
* `vault/bouncetale_9s1otkmz.jar` — valid JAR (107 entries) that calls
  `notifyDestroyed()` after one frame, with and without keys; genuine
  `title-exit`.
* `vault/snake_2.jar` — runs 60 frames/paints without installing a
  `Displayable` and exits with code 1 ("installed no Displayable").

The compatibility matrix was regenerated from the merged rows directory
(`.local-tools/debug-runs/compat-matrix-r128`): **1074 titles — 1063
`运行正常`, 3 `标题自行退出`, 2 `未呈现画面`, 2 `非可用包`, 1
`标题自身异常`, 1 `失败`, 2 `超时`**.  Regenerating again reproduces the
file byte for byte.

### Round 129 (2026-11-20): the last libretro visual failure was a runtime bug

The 510-title libretro corpus probe on the expanded corpus finished with
**508 passed, 1 known limitation, 1 failed**.  The single failure was a new
fetched title, `the_sims_3_winter_ed_259820.jar` (EA Mobile, The Sims 3
Winter Edition): every frame pure black.  Root cause: the title calls
`setFullScreenMode(true)` from its paint loop, the runtime rejected changes on
a *visible* Canvas with `IllegalStateException`, the title swallowed the
exception and never drew.  See
`docs/notes/2026-11-20-fullscreen-visible-toggle.md`; after relaxing the rule
the game renders its main menu (600 frames, 817 paints, 83 259 gfx calls).
A scan of all 108 fetched titles with the pre-fix binary found no other title
affected by this rule.  Full serial CTest: 199/199.

## Round 132: fetcher robustness and motion of the fetched titles (2026-11-20)

The archive.org corpus cannot be rebuilt in one pass: the twenty-year-old items
are served slowly, hand out `SSL: UNEXPECTED_EOF_WHILE_READING` on some files
and keep several collections in `.rar` archives that the standard library
cannot open.  `tools/fetch_corpus.py` was hardened so a resume is cheap:

* `fetch_bytes()` retries transient failures with backoff and falls back to
  `curl -sL --fail --retry 3` when urllib keeps failing (archive.org answers
  some handshakes with a plain EOF from Python's ssl module while curl gets the
  same URL fine);
* archive.org metadata requests go through the same helper (a whole collection
  no longer disappears because one metadata fetch blipped);
* an output file that already exists with a plausible size is skipped, so a
  second run only downloads what is missing.

`tools/check_corpus_motion.py` now appends `NJ_SWEEP_DIRS` directories to its
search path (same override as the sweep), so fetched titles can be probed; and
a subset rows directory must contain the sweep's `keys.txt` — without it every
title runs with no input and menus look frozen (the first pass reported 54 of
111 static for that reason, the corrected pass 26).

Fetched-corpus motion at 300/900/1500 frames (111 ok titles that live under
`/tmp/vault4` and `/tmp/vault5`):

* 300 frames: 57 animate / 54 static;
* 900 frames with the real key script: **85 animate / 26 static**;
* the 23 static rows matched by name were re-probed at 1500 frames: 6 animate
  (slow loaders), **14 remain static title/menu screens** (Asphalt 6, Sonic
  Advance, Midnight Pool 3, Castlevania/... style splash menus) — titles that
  wait for a specific key rather than a frame count.

A further fetch run was started for five game collections with loose JARs
(`j2me_game_collection_vol1`, `tomb_raider_j2me_collection`,
`j2me_20230404_202304`, `j2me-horror-games-collection`,
`nokia35103g2017gamepack`) into `/tmp/vault6`; the transfer is slow under the
current archive.org conditions (~200 of ~600 files after 25 minutes) and a
sweep over the new titles follows when it finishes.

## Round 133: the expanded corpus, and a real library gap it found (2026-11-20)

`/tmp/vault6` finished at 238 new JARs from five archive.org collections, so
the sweep now covers **862 titles** (404 in-repo + 458 fetched):

    ok 827 | title-exception 20 | failure 6 | timeout 3 | bad-jar 2
    title-exit 2 | no-frame 2

The twenty `title-exception` rows are almost all one family: seventeen
`Tomb Raider Anniversary` device builds.  Re-running each row alone (same heap,
same key script) gives `frames=600`, rc=0 and **no uncaught exception**, so
those are timing-sensitive game-code threads under ten-way sweep load, not
runtime defects; a solo recheck before counting a `title-exception` row is the
rule from round 124 applied again.

The six `failure` rows exposed two useful things:

* `Alco Bench.jar` died at startup with
  `NoSuchFieldError: java/lang/Boolean.TRUE`.  CLDC does not declare the
  `Boolean.TRUE`/`FALSE` constants, but ports built against the J2SE shape read
  them with `getstatic`; the class library now materialises the two boxed
  singletons in `nj_boxing_init()` (`src/core/nj_lib_boxing.c`, direct object
  construction because the native call machinery is not available during
  builtin setup).  The title now runs 600 frames / 448 paints.  `tests/test_lib.c`
  gained a regression case: the statics are non-null boxed singletons whose
  `booleanValue()` is true/false (1795 checks, 0 failures).
* `DiamondTwister.jar` fails with `NoClassDefFoundError: e`, but the JAR on
  disk is byte-identical to the archive.org md5 and `unzip -t` itself reports
  "invalid compressed data to inflate e.class": the archived JAR is corrupt, so
  the sweep's `failure` is honest - the loader cannot extract a class that the
  archive stored damaged.  (`AMF_Xtreme_Bowling.jar` has no MIDlet class in its
  manifest at all, and `biohazard-...jar` names `bingdu.CBMID` while shipping
  `bingdu/CBMIDlet.class`: both game-package defects.)

Audio re-validated on the released 0.5.19 core (same recipe as round 107):
40-title sample 30 delivered / 9 silent, MIDI and AMR subsets ran normally, the
only non-silent anomaly was `时光樱花册－遗失之心_1009` exceeding the per-title
budget (known slow loader).  **0 `silent-started` rows**: the invariant from
round 107 still holds.

Fetcher helpers (`tools/fetch_corpus.py`) now retry with backoff, fall back to
`curl` for archive.org's flaky TLS and skip files already on disk; the motion,
audio and deep probes all accept `NJ_SWEEP_DIRS` so fetched titles replay.

## Round 134: re-sweep with the Boolean fix, and corrupt archives are now `bad-jar` (2026-11-20)

Full re-sweep on the fixed binary (`/tmp/vault4:/tmp/vault5:/tmp/vault6`, 862
rows, 600 frames @ 30 fps, 10 jobs):

    ok 845 | bad-jar 3 | failure 4 | timeout 3 | title-exception 3
    title-exit 2 | no-frame 2

Compared with round 133 (827 ok / 6 failure / 20 title-exception):

* `vault/Alco Bench.jar` is `ok` (600 frames, 872,875 bytecodes): the
  `Boolean.TRUE`/`FALSE` fix unblocked it.
* The seventeen `Tomb Raider Anniversary` device builds and
  `JBenchmark3D.jar` are `ok` in this run; the round-133 `title-exception`
  verdicts really were parallel-load timing flakes, matching the solo rechecks.
* `vault/DiamondTwister.jar` is now classified **`bad-jar`** with the note
  "archive entry is corrupt (Error -3 while decompressing data: invalid block
  type)". `tools/sweep_compat.py` gained a pre-flight `jar_integrity()` check
  (ZIP open + `testzip()`), so a damaged archive is reported as an unusable
  package instead of a compatibility failure; non-ZIP files get "not a zip
  file". `tools/make_compat_matrix.py`'s constant-pool `detect()` also skips
  damaged entries instead of aborting the whole table.

`docs/COMPATIBILITY_MATRIX.md` was regenerated from the merged history
(2026-10-25 snapshot + round-134 measurements): **1414 titles, 1398 运行正常**;
regeneration is byte-deterministic.  `docs/CORPUS.md` provenance sentence and
the matrix header now name 0.5.19 and the round-134 batch.

### Round 136: the sweep flags a blank canvas as a note

A row can reach `ok` while the title never draws anything: `paints` counts
presentations, not draw calls, and a title whose renderer thread died before
installing its canvas state presents an untouched screen forever.  The sweep
now appends the note **`painted <paints> frames but made no draw calls (blank
canvas)`** whenever a row finishes `ok` with `gfx == 0` and `paints > 0`.
Verified on the two fetched titles found by the round-135 visual probe
(`NeonRunner.jar`, `GoldenSolitaire.jar` -> both `ok`, `gfx=0`, note present)
and on two normal rows (`loopdemo.jar` `gfx=2400`, `3D城市桌球_8299.jar`
`gfx=1156` -> no note).  The visual probe keeps reporting these titles as
failures; the note makes them visible from a plain sweep as well, which is
what a future batch of fetched titles needs (`tools/run-freej2me-headless.sh`
shows `NeonRunner`/`GoldenSolitaire` reaching a language-select screen, so
their blank canvas is a title/renderer init gap, not a core crash).

### Round 139: motion at a fixed horizon is not a verdict

`tools/check_corpus_motion.py` used to dump three frames spaced `frames // 3`
apart, so a 900-frame run only looked at frames 300/600/900.  That schedule
misses a title's intro: a game that animates its logo for two seconds and then
settles on a menu looks frozen at 900 frames but animated at 300.  Measured on
the 335 `ok` fetched rows under `/tmp/vault6` (sample of 250, 8 jobs):

| run frames | dump schedule | change frames | one frame only |
|-----------:|---------------|--------------:|---------------:|
| 300        | every 100 (100/200/300) | 226 | 24 |
| 900        | every 300 (300/600/900) | 217 | 33 |
| 900        | every 100 (100..900) | **233** | **17** |

The old 900-frame schedule reclassified 16 titles as static even though they
animate in their first second; the corrected schedule keeps the early frames
in every run (`_dump_every()` in the tool: 100-frame steps, thinned to at most
12 dumps for very long runs).  The remaining 17 static titles are mostly
Tomb Raider device builds and menu screens (non-black 53-100 %), plus the
three blank-canvas titles already tracked in
`docs/notes/2026-11-21-fetched-black-screen-titles.md` (`NeonRunner.jar`,
`GoldenSolitaire.jar`, `Tomb Raider Anniversary v0.3.2 (s40v2) (128x128)`).
Lesson: "same pixels at three sample points" is not the same question as
"does this title ever draw two different frames" - put the early frames in
the window before calling a title static.

### Round 145: 862-row sweep on 0.5.21

`NJ_SWEEP_DIRS=/tmp/vault4:/tmp/vault5:/tmp/vault6`, `compat-20261121-r145`,
ten workers, 600 frames at 30 fps:

| verdict | rows |
|---|---:|
| ok | 844 |
| title-exception | 3 |
| failure | 4 |
| bad-jar | 3 |
| timeout | 4 |
| title-exit | 2 |
| no-frame | 2 |

Compared with the 845 ok of round 134 the only verdict change is
`丧尸危机2-自由之翼BT版_997.jar` (ok -> timeout).  It is not a regression: ten
standalone runs with the curated script `tools/corpus_keys/丧尸危机2-自由之翼BT版_997.txt`
(and the generic recipe, and fresh RMS dirs) all finish 600 frames rc=0, so the
sweep hit a parallel-load stall.  Every other non-ok row is a documented slow,
stalled or defective fetched title from rounds 133-134.

### Round 146: the NeonRunner fix was reverted (Townsmen5 regression)

`07a0a5c` delivered `sizeChanged()` from `Display.setCurrent()` (before the
first paint) and fixed the NeonRunner black screen on the CLI and through the
libretro core (8,312 non-black pixels, matching FreeJ2ME-Plus).  The round-145
libretro probe then showed it broke `家园5汉化版_10376.jar` (Townsmen5): the
core went black with `startApp() failed: java.lang.ArrayIndexOutOfBoundsException
@ Townsmen5.LA+8`, because the game's own `Canvas.sizeChanged()` runs game logic
that assumes `startApp()` has returned, and the new delivery point is inside
`startApp()`.

A/B isolation: the released r40 core (`build/libretro-dist/linux-x86_64`) renders
Townsmen5 with 72,592 non-black pixels 3/3 times; the fixed core failed 3/3.
Trying to keep both orderings failed too: a zero-sized-surface guard did not
help, and deferring the delivery to the next frame boundary fixed Townsmen5 but
made NeonRunner black again (its MIDlet thread installs its renderer before the
frame boundary, so the later `sizeChanged()` overwrote it).  No delivery point
in the current runtime satisfies both patterns, so the fix was reverted in
`c6a823d` ("Revert \"fix(lcdui): deliver sizeChanged() from setCurrent() before
the first paint\"") rather than ship a regression in an in-repo title.

After the revert, a rebuilt core renders Townsmen5 again (PASS, 72,592 px) and
NeonRunner returns to the documented black state (0 px, `gfx=0`).  A safe fix
needs a delivery point that is after `startApp()` returns but before any game
thread can install UI state - i.e. runtime work outside the Canvas class, and
it is tracked in `docs/notes/2026-11-21-fetched-black-screen-titles.md`.

### Round 149: five more archive.org collections fetched (vault7)

The hardened fetcher (`tools/fetch_corpus.py`, curl fallback + skip-saved) was
pointed at five more loose-JAR/archive items: `j2me_game_collection_vol1`,
`j2me_20230404_202304`, `j2me-horror-games-collection`, `pvz-j2me` and
`nokia35103g2017gamepack`.  It added **280 JARs** to `/tmp/vault7` (many are
variant builds of titles already in vault4-6 - the collector keeps them as
separate rows because each archive entry is a different file).

Sweep of just the new directory (`NJ_SWEEP_DIRS=/tmp/vault7`,
`compat-20261121-r149-vault7`, 10 jobs, 600 frames):

| verdict | rows |
|---|---:|
| ok | 276 |
| title-exception | 3 |
| failure | 3 |
| bad-jar | 1 |

All seven non-ok rows are already-known titles/variants: `4 in 1` and
`Jewel Quest Deluxe` and `Plants vs Zombies` throw a game-side NPE in a thread
while the MIDlet keeps painting; `AMF_Xtreme_Bowling` (manifest without a
MIDlet class) and `Plant vs Zombies by ayarde09` fail to install a Displayable;
`ArcadePark2` installs no Displayable; `Diamond Twister (mobilegamesarena.net)`
is corrupt in the archive (`Error -3 while decompressing data: invalid block
type`).  No new crash, hang or uncaught-exception class appeared, so the new
batch is compatible with the current reverted tree.

### Round 149: audio revalidation on 0.5.21

40-title sample from the round-145 sweep rows (`--frames 1200 --seed 7
--timeout 240`): **27 delivered audio, 12 silent (`silent-nostart`), 1 timeout**
(`时光樱花册－遗失之心_1009`, the documented heavy loader) and **0
`silent-started` rows** - the round-107 invariant still holds: no title starts
a Player and then fails to deliver PCM to the mixer.

### Round 150: combined sweep of tests/games + vault4-7 (1141 rows)

Full sweep with `NJ_SWEEP_DIRS=/tmp/vault4:/tmp/vault5:/tmp/vault6:/tmp/vault7`
(`compat-20261121-r150`, 10 jobs, 600 frames): 1141 rows measured, **899
unique files** (each extra corpus dir is flattened into the same `vault/`
namespace, so 242 identical basenames from different collections were measured
twice; the counts below are per unique file).

| verdict | unique rows |
|---|---:|
| ok | 878 |
| title-exception | 5 |
| failure | 5 |
| bad-jar | 3 |
| timeout | 4 |
| title-exit | 2 |
| no-frame | 2 |

Compared with the round-145 rows only 45 files are new (all from vault7); 43
of them are ok and the two new non-ok rows are Plants vs Zombies variants
(below).  Triage of the new rows:

* `JBenchmark3D.jar` (title-exception in the sweep) runs 600 frames, 0
  uncaught exceptions and 29.3 s standalone (49.1 M triangles) - the parallel
  sweep caught a thread exception window that does not reproduce alone.
* `SoulOfDarkness_HTC_P3450_EN_IGPL_EU_101_S8000.jar` times out in the sweep
  but completes 600 frames standalone in 142 s (4.2 fps, 829 M bytecodes):
  a genuinely slow title over the 90 s sweep budget, not a hang.
* `Plants vs Zombies (240x320, ayarde09 v4.2.61).jar` has a manifest with no
  `MIDlet-1` entry (only `MIDlet-Name`/`MIDlet-Icon`), so the loader rejects
  it: package defect.
* `Plants vs Zombies (240x320).jar` runs 600 frames and keeps painting while
  a thread throws `java.lang.NoClassDefFoundError: Layer @ Graphic.<init>` -
  see the static-field-area fix below.

### Round 150: NJ_MAX_STATICS was too small for class-heavy titles

The Plants vs Zombies triage found a real runtime limit: `Graphic.<init>`
calls `Layer.getLayerProperty()`, which forces Layer's `<clinit>`, whose
static fields push the VM past **4096 static slots**.  Class linking then
returned `NJ_ERR_NOMEM` without recording a reason, so callers reported a bare
`NoClassDefFoundError: Layer` (naming a class that is demonstrably present)
and the game silently half-ran: `gfx=132` for 300 frames, most layers missing.

Fix:

* `include/nj2me/nj_config.h`: default `NJ_MAX_STATICS` **4096 -> 16384**
  (128 KB of `nj_value`, harmless on desktop and on the 16 MB default heap).
* `src/core/nj_class.c` + `src/core/nj_internal.h`: a new `vm->link_oom`
  flag is set when the static area is exhausted during linking, and
  `nj_class_link()` then throws
  `java/lang/OutOfMemoryError: static field area exhausted: increase
  NJ_MAX_STATICS in nj_config.h` instead of the misleading link error.

Verification: PvZ CLI 300 frames now `paints=227 gfx=13,479` with zero
`NoClassDefFoundError` lines (was `gfx=132`); the libretro core renders it
(harness `PASS visual_nonblack: max non-black pixels=779`); full serial CTest
**199/199 in 376.34 s**; libretro mock matrix 20/20; real RetroArch LoopDemo
120 frames PASS.

### Round 151: the fixed binary recovers PvZ and JBenchmark3D (899 unique files)

Re-ran the combined sweep (`NJ_SWEEP_DIRS` vault4-7, `compat-20261121-r151`,
10 jobs) with the NJ_MAX_STATICS fix and the rebuilt CLI.  1141 rows measured,
**899 unique files**:

| verdict | unique rows |
|---|---:|
| ok | 879 |
| timeout | 5 |
| failure | 5 |
| title-exception | 3 |
| bad-jar | 3 |
| title-exit | 2 |
| no-frame | 2 |

Changes versus round 150: `Plants vs Zombies (240x320).jar` and
`JBenchmark3D.jar` moved from `title-exception` to `ok` (the static-field-area
fix and the parallel-load flake both resolved), and `GT赛车2_36.jar` flaked to
`timeout` under the 10-way load - the documented ~130 s loader, which completes
600 frames when measured alone.  All other non-ok rows are the known
slow/self-exiting/defective titles listed in the round-150 section.

The compatibility table was regenerated from this batch (899 fresh rows plus
560 historical rows not re-measured): **1459 titles, 1438 `运行正常`**
(`docs/COMPATIBILITY_MATRIX.md`, deterministic across two runs).

## Round 158 (2026-11-21): whole in-repo corpus on the 0.5.23 libretro core

The released r42 core (`build/libretro-dist/linux-x86_64/nano_j2me_libretro.so`,
0.5.23, the same bytes as the GitHub release zip) was replayed over every
in-repo title the latest sweep marked `ok` (404 rows, their recorded per-title
key scripts): **397 titles rendered through the mock libretro frontend, 396
passed, 1 known limitation, 0 failed**.  The known limitation is the
documented `毁灭战士3_10598.jar` slow loader (the frontend budget cannot wait
out the 190-pass loading animation).  No `visual_nonblack` failure, no crash,
no timeout - the 0.5.23 API-surface additions did not disturb any rendered
title.

The external vault rows (`/tmp/vault4` … `/tmp/vault7`) were no longer on disk, so
this pass covers the committed corpus only; fetched titles keep their round-150
evidence in `docs/COMPATIBILITY_MATRIX.md`.

### Round 158 also: 900-frame deep probe on 0.5.23

The same in-repo `ok` set was replayed for 900 paced frames (30 fps, 240 s
budget, per-title key scripts, heap/device replayed from the row): **397
titles, 397 passed, 0 early exit, 0 failed** - every title reached the full
frame budget without a late crash, hang, or new uncaught exception.  This is
the first deep pass that covers the 0.5.23 tree (the round-125 pass ran on
0.5.18).

Audio sample on the same tree (40 titles, 240 frames, recorded key scripts):
**19 delivered audio, 21 `silent-nostart`, 0 uncaught/0 timeout**.  The silent
rows are menu-bound or have no started Player at 240 frames (the same class of
row as the round-149 sample); no row regressed to a crash, and the audio
invariant (no `silent-started`) still holds.
