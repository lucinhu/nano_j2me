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
now **189/189 passed in 347.31 s** (`/tmp/r51-ctest2.log`).

## Long campaign, round 52: an oversized GIF canvas

The 3000-case campaign (`tools/fuzz_media.py <cli> 3000 2 103`) found three
HUNG cases, all in `imagesdemo.jar:res/sample.gif`.  They were not hangs in the
watchdog sense: each reproduces as a standalone SIGSEGV, and the cause is
structural.  The mutations rewrote the GIF screen size to 38027x50324,
40708x49041 and 38109x51747 (roughly 7.6-8.0 GB as ARGB8888).  `nj_gif_decode`
only checked that both dimensions were non-zero, then allocated
`width*height*4` bytes and cleared the canvas pixel by pixel; a lazily
satisfied multi-gigabyte allocation is either a very long clear (the HUNG
classification) or a wild write when the allocator/fault path disagrees
(SIGSEGV in libc from the clearing loop).

Fix: `GIF_MAX_PIXELS` = 4096x4096 (the same budget the PNG decoder applies,
16.7 M pixels) is enforced in two places - `nj_gif_decode` right after the
header dimensions are read (before any allocation, with the message
"GIF image is larger than 16.7 million pixels") and `gif_info` (so a caller
probing the image before decoding gets `NJ_ERR_FORMAT`).

Evidence:

- `tools/fuzz_media.py <cli> 3000 2 103` after the fix ->
  **clean: 3000, crashed: 0, HUNG: 0**; the three saved reproducers now print
  `IMAGE gif refused: java.lang.IllegalArgumentException: ... GIF image is
  larger than 16.7 million pixels` and the fixture exits 0.
- `tests/test_gif.c` gained `test_oversized_canvas()`: a 13-byte header
  claiming 60000x60000 is refused with that message and allocates nothing
  (the counting allocator proves it), while a header exactly at 4096x4096
  still reports its size through the info probe.
- The failure artifacts stay at `/tmp/fzkeep/media-hung-{79,1855,2423}.jar`
  for manual replay; `tools/fuzz_media.py` now saves any crash/hang when
  `NJ_FUZZ_KEEP` is set.

## Round 55: several damaged media entries in one JAR

A real damaged download usually breaks more than one resource, so every case can
now damage several *different* entries of the same archive: pass a fifth
argument `multi` to `tools/fuzz_media.py`; it picks `MUTATIONS` entries (at
least two) from the same JAR, rebuilds the ZIP with all of them replaced, and
runs the fixture once.  Archives with a single media entry (the MIDI fixture)
fall back to the single-target path.  The CTest gate `fuzz_media_multi`
(`60 2 7 multi`) keeps this covered.

Evidence: `python3 tools/fuzz_media.py /var/tmp/nj2me-goal13-build/nj2me 3000 2 77 multi`
→ **clean 3000 / crashed 0 / HUNG 0** (log
`.local-tools/debug-runs/fuzz-r55/media-multi-3000.log`), on top of the earlier
single-target 3000-case run.  No new decoder defect found: the two crashes the
long campaign did find (oversized GIF canvas, array operand not checked as an
object) are fixed and covered by `tests/test_gif.c` and the hardening fixture.

## Round 56: the video container is covered too

The gap above is closed for MPEG-4 Part 2 in a 3GP container.  The new
fixture `tests/fixtures/moviedemo.jar` carries `res/movie.3gp` — a synthetic
6-frame 64x48 testsrc clip (`tests/fixtures/3gp/movie.3gp`, 1728 bytes,
generated by ffmpeg with `-c:v mpeg4 -profile:v 0` and checked in because it
cannot be produced without an encoder).  `tests/moviedemo/MovieDemo.java`
opens it the way a game does:

```
Manager.createPlayer(getResourceAsStream("/res/movie.3gp"), "video/3gpp")
realize(); prefetch();
VideoControl vc = (VideoControl) getControl("VideoControl");
vc.initDisplayMode(VideoControl.USE_DIRECT_VIDEO, canvas);
vc.setDisplayLocation(4, 4); vc.setVisible(true); start();
```

so the path under fuzz covers the `ftyp`/`moov`/`mdat` demux, the `mp4v`
sample entry, the MPEG-4 Part 2 decoder, the frame-boundary direct-video
overlay and the Java control natives.  A run prints
`media: mpeg4-part2 movie 64x48, 6 samples`, `media: started Player kind=2 ...`
and `VIDEO started duration=600000`; the shell gate
`tools/check_movie_decode.sh` (CTest `check_movie_decode`) asserts those lines,
and a manual frame dump shows the video frames (633-667 colours inside the
64x48 overlay region for frames 1-18, then the canvas-only backdrop).

The fixture was added to `tools/fuzz_media.py` as a ninth target
(`moviedemo.jar:res/movie.3gp`, 120 frames), so the existing single-target and
`multi` campaigns cover it automatically.

The H.263 provider is covered the same way: `tests/fixtures/3gp/movie_h263.3gp`
is a synthetic 6-frame 128x96 H.263 (s263) clip (3513 bytes; 128x96 is the
smallest size the reference encoder accepts), `MovieDemo` plays it second
through the same VideoControl path, and it is the tenth fuzz target
(`moviedemo.jar:res/movie_h263.3gp`).  The `check_movie_decode` gate asserts
both `media: h263 movie 128x96` and `VIDEO2 started`, so both video providers
are exercised end to end on every test run.

Evidence: `python3 tools/fuzz_media.py <cli> 1000 2 401` →
**clean 1000 / crashed 0 / HUNG 0** (log
`.local-tools/debug-runs/fuzz-r56-media-video.log`); `... 500 2 402 multi` →
**clean 500 / 0 / 0**.  No new decoder defect found.

## What is still not covered

- video bitstreams other than the two synthetic clips (e.g. a full-size H.263
  stream with advanced prediction modes, or MPEG-4 with B-frames);
- decoder state carried across `Player` lifetime (the fixture opens/closes one
  player per resource).
