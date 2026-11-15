# Media-decoder fuzzing

`tools/fuzz_classfile.py` fuzzes class files and `tools/fuzz_jar.py` fuzzes the
ZIP container.  Both stop before the thing that actually consumes most untrusted
bytes after loading: the media decoders.  A damaged AMR frame, WAV header or SMF
track inside an otherwise valid JAR must come back as a Java exception or
silence — never a crash and never a hang.

## Tool

`tools/fuzz_media.py [BIN [CASES [MUTATIONS [SEED]]]]`

Fixtures and targets:

| JAR | entry | decoder |
| --- | --- | --- |
| `tests/fixtures/audiodemo.jar` | `res/tone.amr` | AMR-NB |
| `tests/fixtures/audiodemo.jar` | `res/beep.wav` | RIFF/WAVE PCM |
| `tests/fixtures/audiodemo.jar` | `res/adpcm.wav` | IMA ADPCM |
| `tests/fixtures/mididemo.jar` | `song.mid` | SMF → built-in MIDI renderer |
| `tests/fixtures/gamedemo.jar` | `res/player.png` | PNG → `Image.createImage(String)` |
| `tests/fixtures/gamedemo.jar` | `res/tiles.png` | PNG → `Image.createImage(InputStream)` |
| `tests/fixtures/imagesdemo.jar` | `res/sample.jpg` | baseline JPEG → `Image.createImage(String)` |
| `tests/fixtures/imagesdemo.jar` | `res/sample.gif` | GIF → `Image.createImage(byte[],int,int)` |

One target entry is mutated per case and the JAR is rebuilt around it with the
untouched entries, so the ZIP stays valid and only the decoder sees the damage.
Mutation kinds: `byte` (flip one bit), `run` (four random bytes), `truncate`,
`append`.  Each case runs the fixture for 240 frames through the headless CLI.

CRASH = the process dies from a signal; HANG = it exceeds the budget (unmutated
fixture time × 40, at least two seconds).  A clean exit counts as handled even
if the fixture prints a caught Java exception — rejecting damaged media is the
correct behaviour.

## Evidence, 2026-11-15

Against `/var/tmp/nj2me-goal13-build/nj2me`:

- 120 cases, two mutations, seed 3 → **clean 120 / crashed 0 / HUNG 0**;
- 400 cases, two mutations, seed 17 → **clean 400 / crashed 0 / HUNG 0**
  (log `.local-tools/debug-runs/fuzz-media-r49.log`, gitignored);
- 300 cases over all six targets including the PNG decoder, two mutations,
  seed 29 → **clean 300 / crashed 0 / HUNG 0**
  (log `.local-tools/debug-runs/fuzz-media-images-r50.log`, gitignored);
- 300 cases over eight targets including the JPEG and GIF decoders, two
  mutations, seed 31 → **clean 300 / crashed 0 / HUNG 0**
  (log `.local-tools/debug-runs/fuzz-media-images-r51.log`, gitignored).

The ctest gate `fuzz_media` (`python3 tools/fuzz_media.py <cli> 60 2`,
TIMEOUT 300) covers the six targets in about 0.7 s; the full serial suite is
now **188/188 passed in 338.75 s** (`/tmp/r49-ctest.log`).

## What is still not covered

- video containers: no fixture JAR ships a `.3gp`/`.mp4`/`.h263` resource, so
  the MPEG-4/H.263 demux and video decoders are not fuzzed here (their unit
  tests use constructed streams);
- JPEG and GIF image decoders (PNG is covered through `gamedemo.jar`; the
  other two only have their constructed-stream unit tests);
- decoder state carried across `Player` lifetime (the fixture opens/closes one
  player per resource).
