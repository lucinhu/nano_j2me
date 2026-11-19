# Audio regression probe on 0.5.17, 2026-11-19 (round 107)

The last corpus audio sample (`docs/notes/2026-11-14-audio-motion-probes.md`)
ran on an early build. Between then and 0.5.17 the runtime gained the cheat
engine, the Android cheat editor and multi-touch, none of which should touch
the media path - this probe re-establishes that with a larger sample so a
silent-title regression cannot hide behind "no recent audio evidence".

Rows: `.local-tools/debug-runs/compat-20261119-r105` (the round-105 sweep of
the 0.5.17 CLI, 384 ok rows). Binary: `/var/tmp/nj2me-goal13-build/nj2me`
(commit `cfe7126`). The probe samples `ok` titles that carry
`.mid/.mp3/.amr/.wav/.imy/.mld` assets, runs `--run-frames/--fps 30` headless
with the sweep key script, and reads `observed: tones=N pcm_frames=M` at
`--verbose 3`.

## Main sample (40 titles, 1200 frames, seed 7)

    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      python3 tools/check_corpus_audio.py \
        .local-tools/debug-runs/compat-20261119-r105 --count 40 --frames 1200 --seed 7

**40 titles: 33 delivered audio, 7 silent** (full log `/tmp/r107-audio.log`).
Every silent row is `silent-nostart`, i.e. no `Player` was started at all, and
none is `silent-started` (Player started but no PCM reached the mixer) - that
is the class that would indicate a decoder/mixer break.

| silent title | assets | frames | adjudication |
|---|---:|---:|---|
| 生化危机-险境求生_3290 | 12 | 1200 | title/menu phase; the generic key script never starts a Player |
| 火影忍者格斗2010_4601 | 2 | 1200 | same (menu needs a different key than the generic one) |
| 口袋仓鼠_11936 | 11 | 470 | title exits at frame 470 before any audio is requested |
| 仙剑奇侠传(2008)_12406 | 1 | 1200 | title/menu phase, no Player started |
| 模拟人生2_10762 | 43 | 1200 | known OPEN issue `docs/notes/OPEN-issue-finished-thread.md` (game thread finished, black screen) |
| 火焰-纹章传说第二章_12131 | 7 | 1200 | title/menu phase, no Player started |
| 终极雷电_4125 | 1 | 1200 | known game-side SMS payment dialog before audio |

Delivered rows include heavy PCM carriers (e.g. 斩妖伏魔录4 with 259 audio
assets, 3,074,786 PCM frames; 夜半鬼吹灯 1,478,207 frames; 极品飞车16
640,000 frames), and `tones=0` throughout is expected: these titles use
MIDI/MP3/AMR Players, not Nokia tone sequences.

## MIDI subset (10 titles, 600 frames, seed 11)

    ... --count 10 --frames 600 --seed 11 --require .mid

**10 titles: 6 delivered audio, 4 silent** (`/tmp/r107-audio-mid.log`); the
four silent rows are `silent-nostart` (疯狂农场汉化版_5998, 宠物王国3金_4892,
荒岛余生(GLU正版)_12478, 机甲风暴_7888). Delivered rows include
梦幻西游_4223 (1,711,921 PCM frames), 开心农场BT版_6075, 纯爱物语3之梦想之翼_5112,
植物大战僵尸(新角色加强版)_982, 快打旋风_10825, 拳皇98终极之战_7534.

## AMR subset (10 titles, 600 frames, seed 12)

    ... --count 10 --frames 600 --seed 12 --require .amr

**10 titles: 7 delivered audio, 3 silent** (`/tmp/r107-audio-amr.log`); the
three silent rows are `silent-nostart` (扫雷行动_10904, Flippy跳跃大冒险_archive,
生化危机5核变_866). Delivered rows include 毁灭战士3_10598 (1,060,000 PCM
frames from its music thread even while the loader is still busy - the slow
loader is a construction-time issue, not an audio one), 真西游记_6848,
午夜台球圣诞中文版_8677, FIFA09足球(EA中文版)_7962, 极品飞车16,
明星麻将馆美女_9760, NBA篮球2007_10887.

## Conclusion

No audio regression on 0.5.17: across 60 sampled title runs the mixer
delivered PCM for every title that started a Player, and every silent row was
a menu/first-screen phase, an early exit, or one of the two already-documented
issues. The decoder set exercised includes MIDI, MP3, AMR, WAV and mixed-asset
titles. Re-run the same three commands whenever the media or scheduler code
changes; the meaningful invariant is **0 `silent-started` rows**.

## Also in this round

* New CTest gate `check_multitouch_draw` (`tools/check_multitouch_draw.sh`):
  the multitouch fixture paints one fixed colour per finger, and the check
  parses a dumped PPM to prove finger 1 is at (40,50) green and finger 2 at
  (150,120) blue - independent coordinates, not a merged pointer. 7/7 checks;
  committed as `5fd5f3b`.
* Full serial CTest after the gate: **198/198 passed in 351.76 s**
  (`/tmp/r107-ctest.log`).
