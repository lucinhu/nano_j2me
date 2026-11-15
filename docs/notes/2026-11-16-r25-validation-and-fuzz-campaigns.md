# r25 validation refresh and hardening fuzz campaigns (round 53)

The canonical release package is r25 in `build/libretro-dist/` (built from
revision `43219e6`, see `docs/LIBRETRO_SUBMISSION.md`). Round 53 re-ran the
whole verification set against that package and pushed the fuzz campaigns
further, because the previous long campaign had found two real crashes.

Nothing in this round changed the core sources, so r25 stays canonical. The
point of the round is evidence: a denser validation matrix for the submission
bundle and a much longer adversarial run over the new hardening.

## Validation refresh

| check | command (essentials) | result |
|-------|----------------------|--------|
| full test suite | `ctest --test-dir /var/tmp/nj2me-lrstate-build -j1` | **189/189 passed, 349.38 s** |
| option matrix (mock frontend) | `tools/check_libretro_option_matrix.py <r52 rows> --count 8 --seed 5` | **88/88 profiles passed, 0 failed** |
| real RetroArch corpus | `tools/check_libretro_retroarch_corpus.sh <r25 core> 600` | **8/8 titles passed** (serial rerun; see flake note) |
| whole-corpus visual, 600 frames | `tools/check_libretro_corpus_visual.py <r52 rows> --frames 600 --jobs 8` | **370 passed, 1 known limitation, 0 failed** of 371 |
| corpus headless sweep | `NJ_BIN=... tools/sweep_compat.py .local-tools/debug-runs/compat-20261116-r53` | **370 ok / 29 title-exit / 2 title-exception / 2 no-frame / 0 timeout / 1 bad-jar** |
| long-run deep probe | `tools/check_corpus_deep.py <r52 rows> --count 24 --seed 61 --frames 1800` | **22 passed / 2 early exit / 0 failed** |
| class fuzz, all classes | `tools/fuzz_classfile.py <cli> 5000 2 201 all` | **5000 clean, 0 crashed, 0 hung** |
| JAR container fuzz | `tools/fuzz_jar.py <cli> 5000 2 202` | **5000 clean, 0 crashed, 0 hung** |
| media decoder fuzz | `tools/fuzz_media.py <cli> 5000 2 203` | **5000 clean, 0 crashed, 0 hung** |

The 600-frame whole-corpus visual probe used a private copy of the r25 core
(the shared `build/libretro-dist` is rebuilt by another workstream), so the
"core crashed" mmap hazard from
`docs/notes/2026-11-15-libretro-submission-readiness.md` does not apply.

The deep probe's two early exits are optional-service titles that stop
themselves (`NBA 10（EA正版）` at frame 353, `地下城与勇士-鬼剑士` at frame
433); both exit cleanly with no uncaught exception, and both are `ok` in the
short sweep.

## Two oracle lessons from this round

**A real-RetroArch batch can lose a marker under load.** The first serial
8-title batch ran while the 88-run option matrix was active. Seven titles
passed; `雷霆战机2012BT版_3101.jar` exited 0 after 600 frames but its
`[nano_j2me] run ended: ...` line was missing. The same title passed the
single-title check immediately afterwards, and a second serial batch run
**8/8 passed**. Do not treat a missing end marker from a concurrent batch as a
defect: re-run the title (or the batch) without other probes.

**The parallel headless sweep is not a stable oracle for wall-clock-sensitive
titles.** Against the r52 sweep it changed three verdicts:

| title | r52 | r53 | standalone, 3 runs |
|-------|-----|-----|--------------------|
| `西伯利亚大反击_11466.jar` | title-exit (291 f) | ok (600 f) | pass 3/3 |
| `穿越火线-韩国正版_6874.jar` | ok (600 f) | title-exit (294 f) | pass 3/3 |
| `美男狂想曲_3876.jar` | ok (600 f) | title-exit (58 f, 26 paints) | pass 3/3 |

All three titles pass 600 frames / 20 s when run alone with the same binary
(`--fps 30 --run-frames 600 --keys`, 3 repetitions each). The sweep runs ten
titles in parallel, so a title that branches on wall-clock time can see a
different timeline than it does alone. Treat a single parallel-sweep verdict
change as a candidate until it reproduces standalone; the two fuzz-found
crashes in round 52 are the reason this note insists on reproduction.

## What is covered now

The malformed-input suite exercises: class files (single-locus, multi-locus,
and any class in the JAR), the ZIP container itself (byte/truncate/append
mutations of local headers, central directory, EOCD, manifest and resources),
and every media decoder the fixtures can reach (AMR-NB, WAV PCM, IMA ADPCM,
SMF MIDI, PNG through both `Image` entry points, JPEG, GIF through both entry
points). All gates run in CTest.

Still not covered, unchanged from the round-52 note: the MPEG-4/video
containers, cross-`Player` decoder state, and several damaged resources in one
JAR. Those are the next targets, not current defects.
