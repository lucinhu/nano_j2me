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
