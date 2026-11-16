# Per-title corpus keys and the round-70 sweep, 2026-11-16

## Per-title key scripts

The corpus probes replay one generic key script for every title, which is why a
menu-bound row used to read as "static" or "silent". Round 69 added a way for a
single title to get its own script without editing any tool:

* `tools/corpus_keymap.py` — shared resolver: `<keys dir>/<jar name without
  .jar>.txt` when it exists, else the generic script.
* `NJ_KEYS_DIR` — environment variable naming such a directory. Honoured by
  `tools/sweep_compat.py`, `tools/check_corpus_deep.py`,
  `tools/check_corpus_audio.py` and `tools/check_corpus_states.py`.
* `tools/corpus_keys/` — curated scripts, with a README that documents the
  naming rule, the `--keys` syntax, the curation rules (only when the generic
  recipe cannot reach representative content and the script was verified from
  a frame dump; never to press through a game-side gate) and the two current
  scripts.
* A sweep row that used a curated script carries `"keys": "<file name>"`, so a
  reader can tell it from a generic row.

Verified effect on the two curated titles:

| Title | Before (generic script) | After (curated script) |
| --- | --- | --- |
| `浴火银河2完整版_7689.jar` | 600 frames, `paints=2`, `m3g=0` — static "Music On / Music Off" screen | 600 frames, `paints=2`, `m3g=211,725`, `tris=1,241,757` — the 3D scene behind the selection screen |
| `终极雷电_4125.jar` | fire at frame 10 does nothing; row stays on a black menu | fire at 10 and 100 documents the carrier billing page from round 66; row `ok` with 585 painted frames |

## Round-70 sweep

Full 404-title sweep with the curated keys enabled:

```sh
NJ_KEYS_DIR=$PWD/tools/corpus_keys \
NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
    tools/sweep_compat.py .local-tools/debug-runs/compat-20261116-r70
```

Result (`.local-tools/debug-runs/compat-20261116-r70/rows.jsonl`):
**371 ok / 28 title-exit / 2 title-exception / 2 no-frame / 1 bad-jar**.

Compared with the round-63 sweep (372 ok / 27 title-exit / 2 / 2 / 1) the only
verdict change is `跑跑卡丁车-经典中秋版_2117.jar` ok -> title-exit. That row is
inherently flaky, not a new regression: the same binary and the same keys give
either 697 frames (frame budget) or a self-exit at 293 frames with
`notifyDestroyed()`, depending on the run — six repeats gave one `ok` and five
`title-exit` runs, and the caught `RecordStoreNotFoundException` is present in
both variants. The title's own save-state branch appears to decide whether it
quits early; nothing in the runtime changed between the sweeps except the M3G
span-state hoist (which cannot touch a 2D title with `m3g=0`).

All other non-ok rows are the previously adjudicated ones (three benchmark /
fixture families, game-side billing/auth gates, the 愤怒的小鸟 heap case, the
三国赤壁 stale pack, 鲁宾逊 bad JAR, 有女倾城 self-exit).

The `docs/COMPATIBILITY_MATRIX.md` table was built from the October 969-title
corpus at `2606d63`; it is a historical document, not the current 404-title
sweep, and was not regenerated here.
