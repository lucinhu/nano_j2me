# Corpus triage: all 12 non-clean results accounted for

Corpus: `.local-tools/debug-runs/corpus-20261017/results.jsonl` (395 JARs,
180f each, 35 s timeout). 12 rows were not clean. Re-tested against the
current build; every one now has an explicit disposition.

## Fixed by this line of work (4)

| # | Game | Original symptom | Fix |
|---|------|-------------------|-----|
| 87 | 僵尸危机2_4349 | timeout (0.4 fps) | `134499f` callSerially drain re-queued runnables *during* the drain (64×40 ms per paint) |
| 188 | 扫雷行动_10904 | timeout (586 paints/frame, 4.7 fps) | `0b12620` coalesce `serviceRepaints` to one synchronous paint per frame |
| 179 | 急速狂飙完美破解版_4964 | `ArrayIndexOutOfBoundsException @ j.b+463` | RMS empty-record `NULL` return (rs_getRecord); now 600f 30.2 fps exit 0 |
| 379 | 魂斗罗_5652 | timeout (14.3 fps now) | recovered by the two paint-loop fixes above |

## Fixed as a false fault (1)

* **263 火影忍者汉化版_10735** — "exception escaped: RuntimeException"
  with no message. Root cause `f7f8f7c`: paint interrupted by run stop
  returned `NJ_ERR_HALT` (-3) and the no-exception fallback fabricated a
  fault at teardown. Now 0 escapes over 10 runs.

## Verified working now (2)

* **287 疯狂农场汉化版_5998** — caught exceptions only (save-file EOF,
  missing `/prov.png`); renders splash → ALAWAR → Farm Frenzy title with
  玩 button (`.local-tools/debug-runs/corpus-20261017/f287-strip.png`).
* **218 极品飞车之无间风云FX中文版_4518** — 600f 600/600 paints, no
  exceptions (the recorded M3G ClassCastException is gone).

## Game bugs — identical failure on the reference emulator (3)

* **197 斩妖伏魔录_6231** — `NullPointerException @ c.showNotify+6`
  (field `e` null). FreeJ2ME-Plus throws the exact same NPE. Nano still
  reports it (`f7f8f7c` kept genuine escapes intact).
* **367 阿尔卑斯牧场物语_7043** — identical NPEs on FreeJ2ME
  (sound/Loop, Graphics.setColor).
* **259 灌篮高手-爆发!樱木_7947** — `ArithmeticException /by zero` in
  the game's own `paint`; FreeJ2ME same.

## By design (1)

* **2 1945空战英雄_6105** — `IllegalAccessException`; the game gates on
  `microedition.platform` starting with "Nokia". Nano's generic profile
  correctly fails; `--device nokia6230` boots it (exit 0). FreeJ2ME only
  passes with `compatoverrideplatchecks:on` bytecode rewriting. Opt-in
  device profile is the answer — `4f5e3c1`.

## Corrupt corpus artifact (1)

* **385 鲁宾逊漂流记汉化BT版_4677** — SHA `0d52c309…` is a *truncated*
  ZIP: 38 local file headers, **0** central-directory records, no EOCD.
  FreeJ2ME fails on it too (`suitename is null` from a missing
  manifest). Not an emulator bug; "not a usable MIDlet suite" is the
  correct rejection.

## Net

12 non-clean = 4 fixed + 1 false fault fixed + 2 verified working
+ 3 reference-identical game bugs + 1 by design + 1 corrupt download.
No unexplained failures remain; no emulator-side defect is outstanding
from this corpus.
