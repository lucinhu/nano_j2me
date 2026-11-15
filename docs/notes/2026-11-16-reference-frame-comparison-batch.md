# Reference-frame comparison batch: 14 2D titles against FreeJ2ME-Plus

`tools/compare_reference_frame.sh` renders one frame in our CLI and one in the
local FreeJ2ME-Plus headless runner, then reports the mean absolute error
(MAE).  Earlier runs used it on a few titles (`雷霆战机2012BT版_3101` MAE 2.74,
`灵异村庄_11169` MAE 4.24 with pacing).  This batch ran it over more 2D titles
from the r54b sweep to look for rendering defects.

Command per title (paced 30 fps on our side, threshold 20/255):

```
KEEP=<dir> tools/compare_reference_frame.sh <nj2me> tests/games/<jar> \
    .local-tools/run-freej2me-headless.sh 300 240 320 20 30
```

`KEEP=<dir>` is new: on failure it keeps our frame, the reference screenshot
and a side-by-side composite, so a high MAE can be adjudicated visually
instead of guessed at.

## Results

| title | MAE /255 | verdict |
| --- | --- | --- |
| 俄罗斯方块中文版_2832 | 0.46 | match |
| CCTV贺岁游戏-三国演义_6527 | 1.23 | match |
| 真武三国BT版_3953 | 1.57 | match |
| 闪电战机－雷霆出击_118 | 2.14 | match |
| 速度与激情6_9 | 2.23 | match |
| 实况足球2012_2122 | 2.94 | match |
| 倚天屠龙记_4827 | 4.59 | match |
| 使命召唤4现代战争_9388 | 5.44 | match |
| 家园6之革命_7692 | 10.09 | match (slight animation phase) |
| 冠军足球经理2010汉化版_5198 | 26.80 | scene phase: ours main menu, reference name-input screen |
| 众神殿之王子归来_8404 | 50.99 | scene phase: ours story page, reference main menu |
| 文明3_10522 | 107.47 | scene phase: ours language menu, reference title splash |
| 黑暗破坏神II_9009 | 201.63 | same splash, different canvas adaptation: reference composited 240x160 content in the 240x320 shot, ours painted the forced full size |
| 拳皇98终极之战_7534 | 245.69 | scene phase: ours Enorbus splash, reference sound-settings screen |

## What this says

- Nine titles match the reference closely (0.46-10.09 MAE), which is a useful
  independent signal that the 2D path — image decode, font metrics, clipping,
  transparency, color — agrees with a different implementation on real titles.
- Every high-MAE case was a **different scene**, not different pixels of the
  same scene: the fixed frame number maps to a different point in each
  runtime's load/animation timeline, and the reference got an extra key press
  at 3 s.  This is the known limitation of a single-frame comparison; a
  meaningful failure needs both sides on a static screen (menu, title, high
  score) or a matched input script.
- The 黑暗破坏神II case shows the other trap: both sides are on the same
  splash, but the reference composited its 240x160 canvas into the 240x320
  shot while ours painted the forced 240x320 screen, so the MAE is dominated by
  canvas geometry, not by drawing quality.
- No rendering defect was found in this batch.  The next useful step for this
  tool would be sampling several of our frames around the reference shot and
  reporting the best match, so scene phase stops dominating the number.

Evidence: `.local-tools/debug-runs/r59-refcmp.log` (batch 1),
`r59-refcmp2.log` (batch 2), kept composites under
`.local-tools/debug-runs/r59-cmp/` (both gitignored).
