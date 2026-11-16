# Per-title key scripts for the corpus probes

The corpus probes replay one generic key script (`keys.txt`) for every title:
`tools/sweep_compat.py`, `tools/check_corpus_deep.py`,
`tools/check_corpus_audio.py` and `tools/check_corpus_states.py` all honour
`NJ_KEYS_DIR` through the shared `tools/corpus_keymap.py` helper. Some titles need different input to reach representative content: a
first screen that waits for a key, a menu that needs a soft key, a prompt whose
default button is "back". This directory lets one title get its own script.

## Naming and format

* Name the file after the JAR, without the `.jar` suffix:
  `浴火银河2完整版_7689.jar` -> `浴火银河2完整版_7689.txt`.
* The format is exactly what `--keys` accepts on the command line: entries
  separated by commas or newlines, `frame:key`, pointer presses as
  `123:pressed@x,y` and `124:released@x,y`, and a trailing `@` list for pointer
  positions (`110:pressed@120,160`). No comments: the CLI parses the whole file.
* Frames are presented frames of the paced run, the same counter `--stats`
  prints.

## Usage

```sh
NJ_BIN=/path/to/nj2me NJ_KEYS_DIR=tools/corpus_keys \
    tools/sweep_compat.py .local-tools/debug-runs/compat-custom-keys
```

When a title uses a file here, its `rows.jsonl` row gets an extra
`"keys": "<file name>"` field so a reader can see the verdict used curated
input. Titles without a file here keep the generic script.

## Curation rules

Only add a script when (a) the generic script cannot reach representative
content, and (b) the script was verified by a frame dump or screenshot. Record
the evidence in the commit message or in a note under `docs/`. Do not add a
script to make a verdict look better: a billing page or a login wall is a
game-side gate, and pressing through it is not the same as fixing a title.

## Current scripts

| Title | Script | Why |
| --- | --- | --- |
| `浴火银河2完整版_7689.jar` | fire/fire/soft1/fire/soft2/fire at frames 30..180 | The title opens on a "Music On / Music Off" selection screen with static frames; the script dismisses it and the 240x320 canvas then shows the 3D scene (`--dump-frames`: 28 colours at frame 100 -> 948 at frame 400). |
| `终极雷电_4125.jar` | fire at 10 and 100 | Fire at frame 100 opens the carrier billing dialog ("游戏支付 / ... 信息费1元/条 ... 确认/返回"), which is the content gate this title reaches without a SIM; the script documents that screen instead of leaving the title on a black menu. |
