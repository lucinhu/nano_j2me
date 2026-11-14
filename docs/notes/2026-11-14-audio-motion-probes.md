# Corpus audio and frame-motion probes, 2026-11-14

Two probes that look past the sweep's "ran 600 frames" metric: whether titles
with audio assets actually deliver PCM to the platform, and whether a sample of
titles paints more than one distinct frame (a "black screen" that keeps
painting the same pixels is invisible to the sweep's counters).

Rows source: `.local-tools/debug-runs/compat-20261114-fixed-sweep/rows.jsonl`
(371 ok rows of 404). Binary: `/var/tmp/nj2me-goal13-build/nj2me`, the headless
build with the monitor and `serviceRepaints()` fixes.

## Audio probe

New tool: `tools/check_corpus_audio.py` (takes `NJ_BIN`; `--require .mid` etc.
filters by asset extension). It samples `ok` titles whose JAR carries
`.mid/.mp3/.amr/.wav/.imy/.mld`, runs 180 frames headless with the sweep key
script, and reads `observed: … tones=N pcm_frames=M`.

    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      tools/check_corpus_audio.py .local-tools/debug-runs/compat-20261114-fixed-sweep \
      --count 8 --frames 180 --require .mid

Result — 5 delivered audio, 3 silent:

| title | assets | pcm_frames |
|---|---|---|
| 雷霆战机2012BT版_3101 | 2 | 512000 |
| 蓝白五子棋_9790 | 2 | 409128 |
| 仙魔飞行棋_417 | 3 | 252455 |
| 诛神－斗破苍穹_1402 | 5 | 438272 |
| 街头霸王2冠军典藏版_1812 | 13 | 86238 |
| 美女滑雪胜地_2 | 2 | 0 |
| 开心餐厅2012_745 | 3 | 0 |
| 3D拉力赛-进化汉化版_6366 | 2 | 0 |

The mixer path works for real titles (PCM frames, no `MediaException`). The
three silent rows are follow-ups, not yet adjudicated: they may only start
audio after a menu action the key script does not reach, or their sound may be
opt-in; a title-side explanation must be checked before calling them bugs.
`tones` stays 0 on all sampled rows (they use MIDI/MP3 players, not Nokia tone
sequences).

## Frame-motion probe

Existing tool `tools/check_corpus_motion.py` now takes `NJ_BIN` like the sweep.
It dumps three frames per title (`--dump-every frames/3`) and counts distinct
PPM digests.

    NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
      tools/check_corpus_motion.py .local-tools/debug-runs/compat-20261114-fixed-sweep \
      --count 8 --frames 240

Result — 8 titles, 8 change frames, 0 paint one frame (all three dumps
distinct): 男摄影师_11003, 柯楠之密室杀人事件_3821, 模拟人生3中文版_6162,
使命召唤4现代战争_9388, 血战轩辕之魔神蚩尤_11172, 俄罗斯方块中文版_2832,
杀鬼子-敌后武工队_2185, 实况足球2012_2122.

No stale-frame case in this sample; extend the sample before drawing a corpus
wide conclusion.

## Silent rows resolved, and a way to name the decoder

The three "silent" rows from the first probe were run again with a longer
window and per-title input:

| title | why the first probe saw no audio | result |
| --- | --- | --- |
| 美女滑雪胜地_2 | the 180-frame window ended on a location-select map the sweep keys never picked | pointer taps on a marker then fire: `pcm_frames=1145384`, MIDI BGM plays |
| 3D拉力赛-进化汉化版_6366 | window too short (music starts after the intro) | 300 frames: `pcm_frames=45056` |
| 开心餐厅2012_745 | the story intro's 跳过 button leads to a dialog scene, but the title creates no Player at all in 900 frames | `pcm_frames=0`, no `media:` log lines |

So the rule for this probe is: a fixed 180-frame window reports "no audio
within the window", not "silent title"; reach gameplay first with a per-title
script.

`nano_j2me` now has a CLI switch for exactly this question:

    build/nj2me --port headless --verbose 3 --run-frames 240 --stats game.jar

Level 3 is `debug` and prints the runtime's own decisions, including the
decoder that was chosen. The MIDI and WAV built-in paths log their frame
count now; MP3, AMR, IMA ADPCM and the MP4/3GP video parser already did.
Examples from the corpus:

    media: mpeg4-part2 movie 176x144, 275 samples          (1942空战_10403)
    media: built-in MIDI renderer, 355880 frames, 1 channels (蓝白五子棋_9790)
    media: built-in MIDI renderer, 756990 frames, 2 channels (雷霆战机2012BT版_3101)

`apps/nj2me_main.c` passes it through `nj_run_opts.verbose` to
`nj_vm_set_verbose()`, so the level is raised before the application opens and
the class library's first lines are included too. Evidence log:
`.local-tools/debug-runs/goal13-verify/codec-media-probe.log`.

## Motion probe, second sample (3D and a measured black-screen ratio)

`tools/check_corpus_motion.py` gained `--min-m3g N` and a PPM `non_black`
ratio (the P6 header was being parsed as an integer, so the ratio was always
0%). 3D sample, 8 titles, 900 frames: all animate (2–3 distinct dumps) and
`non_black` is 37–100%:

    浴火银河2完整版_7689 3/100%   蓝牙真3D格斗_3424 2/98%
    急速狂飙完美破解版_4964 2/100% 浴火银河2瓦尔基里_3093 3/88%
    浴火银河2汉化版_6950 3/100%   极品飞车16：亡命狂飙_1519 3/37%
    浴火银河2瓦尔基里(汉化)_3067 3/89%  3D跳台滑雪2011_4507 3/100%

2D sample (12 titles, seed 11): 11 change frames; one static row,
`口袋仓鼠_11936` (unique_frames=1, non_black 93%). It turned out to be a PET
INFO menu waiting for a soft key: an aggressive script with fire/soft1/soft2
and a pointer tap advanced it to the hamster cage and the MIDlet then exited
from the pointer hitting Exit. So the frozen row is the documented "menu
awaits input" case, not a rendering defect.
