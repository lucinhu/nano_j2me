# Long-run soak probe: `--stats-every` and `tools/check_corpus_soak.py`

Functional probes run a few hundred frames.  A leak of a few KB per frame is
invisible there and still passes every test, so the emulator's long-run
behaviour had no instrument at all: the `--stats` line only described the last
frame of the run.

## CLI: `--stats-every <n>`

`nj_run_opts.stats_every` (`include/nj2me/nj_port.h`) makes the runner print the
same counter line `--stats` prints, every `n` presented frames, without waiting
for teardown:

```
stats: frames=100 paints=100 bytecodes=3614 natives=710 heap=310464/310464 ...
```

The sample is emitted from both frame pumps: the explicit frame loop and the
frame hook that serves a MIDlet running its own loop inside `startApp()`
(仙剑奇侠传完美版 and the loader titles), so long-running constructors are
covered too.  `--stats` itself is unchanged; the final line is still printed
before teardown.

The fields that matter for a soak are `heap=used/peak`: a linear rise is a
leak, a one-step rise is a cache.

## `tools/check_corpus_soak.py`

```
NJ_BIN=build/nj2me python3 tools/check_corpus_soak.py <sweep rows dir> \
    --count 8 --frames 6000 --every 1000 --seed 3
```

It samples `ok` rows (with `--min-frames`, default 240, so there is something
to run), runs each title for `--frames` at `--fps 0` with `--stats-every`,
prints the heap samples, and fails only on a crash, timeout or uncaught
exception.  Heap growth between the last two samples above
`max(4 MB, mid/2)` is reported as a warning, or as a failure with
`--strict-heap`, because a game may legitimately cache a level.

## First run

`compat-20261116-r54b`, 8 titles x 6000 frames (log
`.local-tools/debug-runs/r58-soak.log`): **8 passed, 0 failed**.

| title | heap used samples (frames 1000..6000) | reading |
| --- | --- | --- |
| 麻将象棋五子棋3合1_12014 | 1.72M 1.73M 1.74M 1.75M 1.76M 1.77M | ~11 KB / 1000 frames — tiny steady rise, not a leak at this rate |
| 网球大满贯_11950 | 0.53M 0.53M 1.45M 1.45M 1.45M 1.45M | one step at frame ~3000 (level data), flat afterwards |
| 波斯王子之时之沙中文版_11315 | 0.46M flat | flat |
| 浴火银河2瓦尔基里(汉化)_3067 | 0.31M flat | flat |
| FIFA09足球(EA中文版)_7962 | 348K 348K 348K 348K 348K 349K | flat (small boundaries noise) |
| 中华麻将馆－极限胡牌版_244 | 10.75M flat | large static allocation, flat |
| 穿越火线-韩国正版_6874 | 1.02M 0.75M 0.75M 0.75M 0.75M 0.75M | drops after load (temporary load buffers), flat |
| 地下城与勇士之鬼剑士_9408 | 1.11M 1.11M 1.77M 1.77M 1.84M 1.84M | two steps (areas/levels), then flat |

No emulator-side leak surfaced in this sample.  The probe is cheap enough
(`--fps 0`, 8 titles in a few minutes) to run after memory-related changes; a
future regression would show as a straight line rising to the end of the run
instead of the step-then-flat shapes above.

## CTest gate

`check_stats_every` (`tools/check_stats_every.sh`, loopdemo.jar, 300 frames /
100 per sample) keeps the option wired: it asserts four counter lines
(100/200/300 periodic + the final one), the heap field, and that the last line
is the final `frames=300` line.
