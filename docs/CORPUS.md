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

