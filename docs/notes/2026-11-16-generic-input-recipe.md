# The generic corpus key recipe was producing false title-exit rows

Date: 2026-11-16 (round 71)

## Symptom

After the round-70 sweep, 28 rows were `title-exit`. Reading them one by one
showed that most of them were not the game ending: `hello.jar`, `maze.jar`,
`JBenchmark*.jar`, `史前部落`, `都市摩天楼` and friends simply stopped a few
seconds in, with `notifyDestroyed()` called from a button handler.

## Cause

The generic key script used by `tools/sweep_compat.py` contained three
arbitrary pointer taps

```
60:pressed@60,300
120:pressed@120,160
180:pressed@180,300
```

plus `soft2`. On a title whose menu has a Back/Quit/Exit button, one of those
fixed coordinates lands on that button and the MIDlet exits. The verdict then
records `title-exit` even though the game runs fine when it is actually played.

Evidence: replaying all 28 r70 title-exit rows with a pointer-free script
(fires, soft1, arrows, digits only, no `pressed@`/`released@`, no soft2) made
**16 of them run the full 600 frames** — among them `hello.jar` (600/423),
`maze.jar` (600/253), `CS反恐精英(蓝牙对战)` (600/600), `JBenchmark` (600/600),
`PC正版-使命召唤5` (600/575), `史前部落` (600/600), `是男人就下100层`
(600/600), `比基尼美女海底影相` (600/633), `生化危机之恶化` (601/223),
`都市摩天楼` (600/601). The remaining 11 still end themselves (`Boun`/`Vime`
55 frames, book4–7/jump 30, `JBenchmark HD` 140, `冰淇淋少女成长日记` 290,
`百鬼商店` 73, `蹦球传说` 91 via `System.exit`) — those are genuine
game-side ends for a corpus probe.

## Change

`tools/sweep_compat.py` now uses a pointer-free generic recipe:

```python
KEYS = ["30:fire","55:soft1","80:53","140:left","165:right","190:48",
        "215:up","240:down","290:5","320:fire","375:star","400:6",
        "430:fire","470:soft1","545:fire","575:left"]
```

Titles that genuinely need a tap to leave a menu now get a per-title script in
`tools/corpus_keys/` (resolved by `NJ_KEYS_DIR` through
`tools/corpus_keymap.py`), which is the mechanism added in round 69. Two were
needed after this switch:

- `丧尸危机2-自由之翼BT版_997.txt`: the game waits on its loading/dialogue
  screen and ignores keys; a pointer tap at 90/120/150 makes it start. 600
  frames / 601 paints with the per-title script.
- `凡人修仙传之七派六宗_4145.txt`: same situation, the intro only advances on
  a tap. 600 frames / 407 paints with the per-title script.

Both were verified standalone before being added; the generic recipe never
presses through a game-side gate, it just avoids tapping unknown buttons.

## Result

Full sweep with the pointer-free recipe + the two per-title scripts
(`.local-tools/debug-runs/compat-20261116-r71b`): distribution recorded in the
round-71 report. The round-70 comparison was 371 ok / 28 title-exit; the first
r71 run already moved 15 rows title-exit → ok
(`CS反恐精英(蓝牙对战)`, `JBenchmark`, `JBenchmark2`, `JBenchmark3D`,
`PC正版-使命召唤5`, `hello`, `maze`, `史前部落`, `学姐的秘密`,
`是男人就下100层`, `比基尼美女海底影相`, `生化危机之恶化`, `贪吃蛇经典版`,
`都市摩天楼`, `鲁宾逊漂流记汉化版`) and lost one (`凡人修仙传`, restored by
its per-title script). `跑跑卡丁车-经典中秋版` stays flaky (293-frame
self-exit vs 693-frame run, same caught `RecordStoreNotFoundException` both
ways) and is recorded as such.

## Notes for future probes

- A `title-exit` verdict only means "the MIDlet called `notifyDestroyed()`
  within the frame budget"; it says nothing about *why*. Always read the last
  screens before treating it as a compatibility failure.
- A fixed coordinate tap is title-specific input, not generic input. Keep the
  generic recipe keyboard-only and move taps to `tools/corpus_keys/`.
- `鲁宾逊漂流记汉化版_702` now runs 600 frames but paints once — it is an
  "ok by budget" row, not evidence of correct rendering; its renderer needs a
  separate look.
