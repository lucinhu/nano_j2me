# Audio and video decoders

`nj2me/nj_avcodec.h` holds the provider registry that stands between MIDP's
`Manager.createPlayer()` and whatever actually turns bytes into sound or
pictures.  The image side has had the same shape in `nj_codec.h` for longer;
this file describes the video/audio one.

## Two axes, deliberately separate

**Trimming** is compile-time.  A build can drop a decoder, or parts of one, when
flash matters more than the format:

| Macro | Default | Effect when 0 |
| --- | --- | --- |
| `NJ_ENABLE_MP3` | 1 | no MP3 decoder is linked; `createPlayer` answers `MediaException` |
| `NJ_ENABLE_VIDEO` | 1 | no video decoder is linked |
| `NJ_MP3_LAYER3_ONLY` | 1 | Layer I/II are refused (nobody shipped them) |
| `NJ_MP3_ENABLE_ID3` | 1 | tags are skipped by resync instead of parsed |
| `NJ_MP3_ENABLE_SEEK` | 1 | `Player.setMediaTime` cannot seek inside a song |
| `NJ_MP3_ENABLE_INTENSITY` | 1 | MPEG-1 intensity stereo is treated as plain stereo |
| `NJ_VIDEO_H263_ONLY` | 1 | MPEG-4 Part 2 visual is refused |

**Adapting** is runtime.  Decoders are function tables installed in the VM:

```c
#include "nj2me/nj_avcodec.h"

/* A port with a hardware MP3 decoder installs it after nj_vm_create().  Later
 * installs are tried first, so this one wins over the software decoder. */
nj_avcodec_install_audio(vm, &my_hw_mp3_ops);
```

A provider that returns `NJ_AV_ERR_UNSUPPORTED` from `open()` disables that
format just as effectively as trimming it out, without a rebuild.  The registry
itself lives in `nj_vm` (72 bytes: two arrays of four providers plus two
counts), which is why the runtime still has no writable globals.

## Providers in the tree

| Provider | Format | State |
| --- | --- | --- |
| `nj_av_audio_mp3` | MPEG-1/2/2.5 Layer III | done — within 1 LSB of ffmpeg |
| `nj_av_video_h263` | H.263 baseline (raw or 3GP/MP4) | done |

### Video (round 21)

`src/midp/nj_h263.c` decodes H.263 baseline I and P frames and
`src/midp/nj_mp4.c` is an allocation-free ISO-BMFF demultiplexer that hands it
samples:

- **Demuxer**: `ftyp`, `moov/mvhd`, `trak/tkhd`, `mdia/mdhd/hdlr`, `minf/stbl`,
  `stsd`, `stts`, `stsc`, `stsz`, `stco/co64`, `stss`, up to eight `mdat`
  ranges.  It refuses rather than guesses: fragmented files, several sample
  descriptions, `co64` above 4 GiB, tables whose sums disagree.
- **Decoder**: picture formats 1–5 (128×96 … 1408×1152), optional FFmpeg-style
  GOB headers, MCBPC/CBPY, dquant, DC/AC TCOEF with escape, H.263
  dequantisation, integer 12-bit two-pass IDCT, 4:2:0 to RGB565 through integer
  BT.601, one-vector half-pel motion compensation, skipped and intra
  macroblocks in P frames.
- **Refused cleanly**: MPEG-4 Part 2, H.263+ annexes (plus/PLUSPTYPE), PB
  frames, long vectors, advanced prediction/OBMC, 4MV, custom picture sizes,
  standard MBA-bearing GOB headers.

Pixel output was checked against FFmpeg's RGB565 decode on six frames at 13
positions: worst per-channel delta **1** (the test allows 4).  Cost per frame
on this host at `-O3`, measured with `CLOCK_PROCESS_CPUTIME_ID`: 124 µs for a
128×96 3GP sample, 285 µs at QCIF, 724 µs at CIF.  Peak allocator use is
37 KB / 77 KB / 306 KB for those sizes; worst static stack frame is 1,816 B.

## What the runtime does with it

`Manager.createPlayer()` will sniff a clip with `nj_avcodec_find_video()`,
open it through the provider's allocator hooks, keep the caller's buffer alive,
and ask for one RGB565 frame per emulated frame; the player closes the decoder
when it is deallocated.  Video descendants of JSR-135 (`VideoControl`) present
those frames through the same painting path the rest of LCDUI uses.

## Tests

- `tests/test_h263.c` — 86 checks over embedded fixtures: demuxer sample
  offsets/timestamps for 3GP, `stco`/`co64`, an `mp4v` track that must be
  refused, decoder pixel spots, provider `decode()` contract, truncated and
  mutated input, and leak checks.  It runs in `ctest`.

## MP3 inside a Player

`Manager.createPlayer()` now hands an MP3 stream to the runtime's decoder
instead of refusing it.  The shape is the same one MIDI uses, and for the same
reason: a three minute track at 44.1 kHz stereo is twenty-one megabytes of PCM,
so the samples cannot live in the Java heap.  The Player keeps

- `pSrc` - the file's own bytes, which the decoder reads from;
- `pAvK` - the flag that says a `nj_avcodec` audio provider renders this
  stream;
- `pData` - a small holder byte array (state + one frame's scratch, 76 B), the
  same arrangement the MIDI path uses;

and `nj_media_frame()` pulls one frame of PCM per emulated frame by calling the
provider's `decode()` and handing the samples to the port.  `setLoopCount()`
works through the provider's `seek()`, and closing the Player calls the
provider's `close()`, so a decoder that allocated through `nj_av_mem` gives it
all back.

`tests/test_lib.c` pins the wiring with a 1.4 KB MP3 fixture: it asserts that
`createPlayer(stream, "audio/mpeg")` succeeds, that `pAvK` is set, that the
decoder reported a frame count, and that pumping the media loop moves PCM into
the port (`pPushed > 0`).  Against the previous tree the same test fails at
`createPlayer` and at both holder assertions.

RAM: two Player fields (a reference and a byte, 16 B with padding) and the
holder array per streamed Player.  No writable static data; ROM grows by the
decoder plus the branch (~18 KB of decoder text).

## The MP3 decoder, and how it was verified

The first version of `src/midp/nj_mp3.c` parsed and streamed correctly - frame
boundaries, bit reservoir, frame counts, END semantics - and yet every sample
was wrong: it assigned Huffman codewords canonically by code length, while ISO
Table B.7 lists them in codeword order.  Symbol lengths matched, so nothing
about the frame stream looked wrong, but each coefficient pair decoded to the
wrong symbol.  A self-consistent round-trip test cannot see that; only a
reference decode can.

The decoder now uses the 1378 table entries in ISO codeword order and a binary
search over them, and fixes two short-block defects that the Huffman bug had
hidden (requantisation indexed before reorder, and the short IMDCT both read
contiguous coefficients instead of stride-3 and overwrote the overlapping
windows instead of accumulating them).

`tests/test_mp3.c` embeds four LAME fixtures and the last audio frame of
ffmpeg's decode of each, asserting the worst per-sample delta is at most 1:

| Stream | Correlation before | Worst delta before | Correlation after | Worst delta after |
| --- | --- | --- | --- | --- |
| MPEG-1 mono | 0.012 | 17,180 | **1.000000** | **1** |
| MPEG-1 stereo | 0.042 | 22,679 | **1.000000** | **1** |
| MPEG-2.5 mono | -0.098 | 36,586 | **1.000000** | **1** |

Mean absolute error after the fix is 0.003-0.005 LSB.  Decode cost is about
340x realtime; `.text` is 18,363 B and there is no writable static data.

Known gaps: the LAME Info frame is emitted as one silent frame and there is no
gapless trimming (so raw totals differ from `ffmpeg -f s16le` by the encoder
delay); CRC is skipped rather than verified; Layer I/II are not implemented;
free-format bitrate is refused.

## MP3 in the corpus (round 2)

The decoder and the player path are one thing; whether titles actually get
sound is another.  Two corpus titles that ship MP3 assets play them through
`Manager.createPlayer()` with no changes on their side:

| Title | Assets | `pcm_frames` after 400 paced frames |
| --- | --- | --- |
| 恋爱物语－我的机器女友_6219 | `sound/n_good.mp3`, `n_happy.mp3`, ... | 50,927 |
| 星辰变S60V5完整版_3364 | `quantou.mp3` (title music) | 1,131,934 |

Both run to their frame budget with no exception from the media API, which
means the holder path (decoder + one frame of scratch in a 76 B array, file
bytes in `pSrc`) is exercised end to end, not just by the unit test.  The
`IOException` a few titles print for images such as `/logo.png` and
`/fengmian.png` are their own probes for files that are not in their JARs
(checked: neither entry exists in 星辰变's 96 entries), not a loader defect.

What is still missing for audio: an AMR provider behind the same seam (today
AMR goes through the port hook and the buffered whole-file path), and reference
fixtures for MPEG-2 rates and linbits-heavy streams.

## Replacing a decoder at run time

`nj_avcodec_install_audio()` / `nj_avcodec_install_video()` prepend a provider to
the VM's table, so a port with a hardware codec overrides the built-in software
one; passing `NULL` removes every provider of that kind (a port tears its codec
server down that way).  The tables live in `nj_vm` (72 B in total), so nothing is
static and two VMs in one process can disagree about which decoder to use.

The seam is only worth having if the replacement is exercised, so
`tests/test_avcodec.c` does exactly that: it installs a fake MP3 provider that
emits a sentinel sample, checks the lookup prefers it over the software decoder,
drives `Manager.createPlayer()` over a real MP3 stream, pumps the media frame
hook and asserts the port receives the sentinel - i.e. the port's decoder, not
ours, produced the sound.

## AMR behind the seam

AMR narrowband is now a provider too (`amr-nb`, registered by
`nj_avcodec_reset()` next to MP3 and H.263).  `nj_amr.c` decodes a whole file in
one call, so the provider keeps that shape: `open()` decodes once, `decode()`
hands out the samples in slices, `seek()` rounds to the nearest 20 ms block.
There is no RAM win in re-shaping the decoder to per-frame state - the samples
exist in memory either way, and the Player decides where - but a port with a
hardware AMR codec can now install its own provider and shadow this one, and
`-DNJ_ENABLE_AMR=0` removes both.

`tests/test_amr.c` pins it: the provider accepts `#!AMR` streams, reports 8 kHz
mono and two 20 ms frames, and hands out PCM that is **byte-identical** to
`nj_amr_decode()`'s output (127 checks in that test now, 8 of them new).

Note: `Manager.createPlayer()` still decodes AMR through the media library's own
path rather than through the provider.  The provider exists so ports can replace
it and so the registry is uniform; switching the Player over is the next step.

### Where each piece is wired (round 7)

| Piece | Provider | `Manager.createPlayer` | Media pump | Evidence |
| --- | --- | --- | --- | --- |
| MP3 | `nj_av_audio_mp3` | `audio/mpeg`, streamed through a holder | pulls PCM per frame | `tests/test_lib.c` (real samples reach the port), `tests/test_avcodec.c` (a port's decoder wins) |
| AMR-NB | `nj_av_audio_amr` | `audio/amr` through the provider (an installed decoder wins); the `samr` track inside MP4 keeps the direct path | one-shot PCM | `tests/test_amr.c` (byte-identical to `nj_amr_decode()`), `tests/test_lib.c` (a port-installed decoder produces the Player's samples) |
| H.263 | `nj_av_video_h263` | `video/3gpp`: Player is built with the demuxer, decoder and one RGB565 frame | **next round**: decode a sample per emulated frame | decoder verified against ffmpeg in `tests/test_h263.c`; Player creation checked on a real 176x144 clip (kind=2, 10 samples) |
| MP4/3GP | `nj_mp4` demultiplexer | `video/3gpp` opens the movie track | - | sample iterator API tested in `tests/test_h263.c` |

Trim costs, one switch at a time against a matched base (hosted CLI `.text`,
Release, tests off): AMR 49,536 B, MP3 33,600 B, VIDEO 31,552 B; see
`docs/FOOTPRINT.md` for the full matrix.  Every switch keeps `.data`/`.bss`
unchanged - the registry lives in the VM (72 B) and decoder state in the
provider's allocation.

### Remaining work: video inside a Player

The pieces exist and are verified individually - `nj_mp4` walks samples
(`tests/test_h263.c`), `nj_av_video_h263` decodes them against ffmpeg's pixels -
but nothing assembles them into a `Player`.  The missing assembly, in order:

1. `sniff_media()` already reports `MP4`; `createPlayer()` needs a branch next to
   the MP3 one that opens `nj_mp4`, asks `nj_avcodec_find_video()` for a decoder
   and stores `{nj_mp4, decoder}` plus an RGB565 frame buffer on the Player
   (`pVid`/`pVbuf`, with `pVw`/`pVh`);
2. `nj_media_frame()` needs a video branch: one sample per emulated frame into
   the frame buffer, `pPushed`/`pTime` advanced from the sample's timestamp, and
   `endOfMedia` when `nj_mp4_next()` runs out;
3. `VideoControl.getSnapshot()` then turns the frame buffer into an `Image`
   (and `initDisplayMode` may stay minimal - most titles only snapshot).

Until step 1 lands, createPlayer() says so instead of claiming there is no MPEG
audio decoder in a file that contains video and no audio at all.

### Video: what exists after round 10

`Manager.createPlayer()` now builds a video Player for a 3GP/MP4 movie: `nj_mp4`
selects the video track, `nj_avcodec_find_video()` picks the provider (so a
port's hardware H.263 decoder wins), and the Player carries the demuxer, the
decoder, the geometry and one RGB565 frame buffer.  A real 176x144 clip builds a
Player reporting `kind=2`, 176x144 and 10 samples; the frame buffer is allocated
once, at creation, so the pump will not allocate while a title paints.

Still to do, in this order: decode one sample per emulated frame in
`nj_media_frame()`, then `VideoControl.getSnapshot()`/`initDisplayMode()`.

## Movie frames reach the Player (round 12)

`nj_media_frame()` now has a video branch: a Player with `pKind == 2` demultiplexes
one sample per emulated frame, decodes it into its RGB565 buffer and advances
`pPushed`/`pTime`; when `nj_mp4_next()` runs out it stops the Player and fires
`endOfMedia`, the same ending the audio path has.  Video Players need no audio
device, so the pump's entry guard no longer returns early when the port has no
`pcm_write`.

Evidence with a 176x144 H.263 3GP (10 samples, ffmpeg's `testsrc`): after
`createPlayer("video/3gpp")`, realize/prefetch/start and 40 pump calls,
`pPushed == 10` (every sample) and the 50,688-byte frame buffer holds a real
picture (byte checksum 6,343,036, not a zero buffer).

Still open: `VideoControl.getSnapshot()`/`initDisplayMode`, and an in-tree test
that pins the decoded frame against ffmpeg's pixels (the scratch probe above is
not committed; `tests/test_h263.c` already has the reference frames to reuse).

The wiring is now pinned in the tree: `tests/test_lib.c` drives the same
`clip_3gp` fixture that `tests/test_h263.c` verifies against ffmpeg through
`Manager.createPlayer(stream, "video/3gpp")`, starts it, pumps one media frame
and asserts the Player reports the clip's size (128x96), counts four samples,
advanced `pPushed` by one and filled a 24,576-byte RGB565 frame buffer with
something other than zeros.  The clip itself moved to `tests/h263_clip.h` so the
two tests cannot drift apart.

### What the corpus actually ships (round 16)

`tools/check_corpus_video.py` walks every JAR in `tests/games` and the
out-of-tree vaults and reports each video container with the codec named in its
sample description.  Across all corpora there are three video assets:

| Title | Asset | Codec |
| --- | --- | --- |
| 毁灭战士3_10598 | `vid/doom.3gp` | **MPEG-4 Part 2** (`mp4v`) |
| 1942空战_10403 | `1942.3gp` | **MPEG-4 Part 2** (`mp4v`) |
| Tom Clancy's H.A.W.X. | `movie.3gp` | **MPEG-4 Part 2** (`mp4v`) |

So the H.263 path built so far is exercised only by our own ffmpeg fixture, and
**the decoder real titles need is MPEG-4 Part 2 (Simple Profile)**.  That
reorders the remaining work: M4V (VLC tables, IDCT, half-pel motion
compensation, P-frames) is the milestone, and H.263 stays as the smaller
sibling it already covers.

### The demultiplexer on the corpus's own movies (round 17)

`nj_mp4` had only ever been exercised by our own ffmpeg fixture, so it was run
against the two real 3GP files the corpus ships (extracted from
`tests/games/毁灭战士3_10598.jar:vid/doom.3gp` and
`tests/games/1942空战_10403.jar:1942.3gp`) and the result compared with
`ffprobe`:

| File | `nj_mp4` | `ffprobe` |
| --- | --- | --- |
| 1942.3gp | 176x144, 275 samples, 25 fps, codec `mp4v` | `mpeg4 176x144 25/1 275` |
| doom.3gp | 320x240, 189 samples, 15 fps, codec `mp4v` | `mpeg4 320x240 15/1 189` |

Geometry, sample count and frame rate agree exactly, and the sample description
is identified as `mp4v`.  Both files also carry an AMR-NB audio track, which is
the path `test_amr` and the MP4 audio branch already cover - that is why the
毁灭战士3 intro has sound today while its picture stays refused: the container
and the audio track work, the MPEG-4 Part 2 video decoder does not exist yet.

### Handoff: what an MPEG-4 Part 2 decoder would reuse (round 18)

The corpus's three movies are `mp4v`, so this is the one decoder left.  The
good news is that most of `src/midp/nj_h263.c` applies unchanged:

- **Reusable as-is**: the 8x8 integer IDCT, the 4:2:0 -> RGB565 conversion, the
  half-pel bilinear motion-compensation kernel, the macroblock/block loop, and
  the whole `nj_avcodec` provider + Player + pump path already built around it.
- **Different in M4V**: start-code framing (VOS `0x000001B0`, VOL `0x00000120`,
  VOP `0x000001B6`) instead of H.263's picture header, different VLC tables
  (Table B-16 coefficients, B-17 motion vectors), per-macroblock `cbpc`-driven
  chroma step instead of H.263's fixed plane layout, and the VOL header carries
  the geometry (`width x height`) that `nj_mp4` currently takes from the sample
  description.
- **Not needed for Simple Profile level 0-2**: B-VOPs, quarter-pel, reversible
  VLC, AC/DC prediction and shape coding - so the scope is smaller than "MPEG-4
  video" sounds.

`tests/h263_clip.h` cannot cover M4V; the fixtures would be the two real 3GP
files above, compared frame by frame against `ffmpeg -f rawvideo -pix_fmt
rgb565` decodes of the same samples - the same shape as the H.263 test.

### A first slice that did not survive contact with real data (round 19)

An initial `nj_m4v` VOL parser was written from the field order remembered from
ISO/IEC 14496-2 6.2.3.1 (id 4, shape 2, marker 1, time_res 16, ...) and passed a
hand-built synthetic header.  It then read nonsense from the decoder
configuration of the two real movies:

| File | VOL payload begins | Parser said |
| --- | --- | --- |
| `1942.3gp` | `20 00 84 ...` | 2824x2314, time_res 16928 |
| `doom.3gp` | `20 00 c4 8d 88` | 8002x65, time_res 25158 |

The synthetic test was self-consistent by construction, so it could not catch a
wrong grammar - the real payload starts with `0x00`, i.e. the marker bit that
the assumed layout puts in that position reads 0.  The slice was reverted rather
than committed: the field order and widths must be taken from an authoritative
source (an MPEG-4 Visual decoder implementation, not memory), and the acceptance
test must parse the real `esds` decoder configuration of the two corpus movies
before any macroblock work starts.

### The VOL header, parsed from the reference decoder (round 20)

The first attempt read the layer id before anything else; the reference decoder
(ffmpeg `libavcodec/mpeg4videodec.c`, `decode_vol_header`) shows that a VOL
starts with a random-access flag and an 8-bit video object type, then an
optional object-layer identifier, the aspect ratio and optional VOL control
parameters, and only then the shape.  The parser now follows that order, and
`tests/test_m4v.c` checks it against the `esds` decoder configuration attached
to the corpus's two real movies - not against a header we wrote ourselves:

| Config | Expected | Parsed |
| --- | --- | --- |
| `1942.3gp` (32 B) | 176x144, time_res 1000 | same |
| `doom.3gp` (48 B) | 320x240 | same |

The test also refuses a truncated config and random bytes.  Next: VOP headers
and the B-16 macroblock layer, reusing the H.263 inverse transform and motion
compensation that already pass their own reference tests.

### VOP headers, checked against the corpus's own first frames (round 21)

`nj_m4v_parse_vop()` reads what the reference decoder reads: the two-bit coding
type, the unary modulo-time-base, a marker, the time increment (whose width
comes from the VOL's resolution), a marker and `vop_coded`.  B-VOPs are refused
by design - Simple Profile has none.

Two real first samples pin it down:

| Sample | Bytes | Parsed |
| --- | --- | --- |
| `1942.3gp` sample 0 | `00 00 01 b6 10 03 09 8c ...` | I-VOP, time increment 0, coded |
| `doom.3gp` sample 0 | GOP, then `00 00 01 b6 10 03 09` | I-VOP after the GOP, coded |

`nj_m4v_find_vop()` skips the group-of-VOPs header that one of them starts with.
Next: the macroblock layer - reuse the H.263 inverse transform and half-pel
motion compensation, add the B-16 VLC tables, then register the decoder as the
`NJ_AV_VIDEO_MPEG4` provider.

### Refusing a codec by name (round 23)

`Manager.createPlayer()` used to hand any `video/*` MP4/3GP to the H.263
provider, which accepts a container it can demultiplex and then decodes noise
from an `mp4v` sample entry.  The track's fourcc is now checked first: `s263`,
`H263` and `h263` go to the provider, and everything else is refused with a
MediaException that names the reason - for the corpus's three movies, that is
"MPEG-4 Part 2 video has no decoder in this runtime".  The existing H.263 clip
still builds a Player (test_lib 881/0, test_h263 86/0).

### Where the decoder goal stands (end of the 24-round run)

| Requirement | State | Evidence |
| --- | --- | --- |
| MP3 decoder | done | mirrors ffmpeg's PCM to <= 1 LSB (81 checks) |
| MP3 behind a provider, in `Manager.createPlayer` | done | streamed holder path; sentinel test proves the port's decoder wins |
| AMR-NB behind a provider | done | byte-identical to the runtime's own decoder (127 checks), Player routed through it |
| Provider replaced at run time | done | audio and video replacement/removal tests |
| MP4/3GP demultiplexing | done | parses the corpus's real movies with the same geometry as ffprobe |
| H.263 decoding | done | pixels match ffmpeg's decode (86 checks) |
| H.263 movies inside a `Player` | done | creation, sample pump, in-tree test on a shared clip |
| MPEG-4 Visual framing (VOL, VOP) | done | validated against both corpus decoder configurations and first samples |
| **MPEG-4 Part 2 macroblocks** | **not done** | no VLC tables, no reconstruction; the Player refuses `mp4v` by name |
| Trim macros | done | AMR 87 KB, MP3 62 KB, VIDEO 45 KB, MIDI 33 KB; each switch builds |
| RAM/ROM documented | done | `docs/FOOTPRINT.md`, 72 B registry in the VM, no writable statics |

The one open item is the `mp4v` macroblock layer: entropy decoding with the
B-16 tables, inverse scan/prediction and reconstruction, reusing the H.263
inverse transform and half-pel motion compensation.  `nj_m4v` already hands it
the layer geometry and per-picture timing, and `tests/test_m4v.c` pins the
framing so that work starts from a checked base rather than from a guess.

### The MPEG-4 macroblock layer, piece by piece (rounds 25-35)

The decoder is being built bottom-up so each piece is tested before the next
one leans on it:

| Piece | State | Test |
| --- | --- | --- |
| intra AC tables | done, generated from the reference data | every one of 102 codewords round-trips |
| intra DC size + signed delta | done | all 26 size codewords, both sign cases |
| coefficient scan / store | done | table is a permutation, odd positions land at natural indices |
| AC walk to the last-flagged symbol | done | synthetic stream stops at the right cursor |
| single intra block (DC prediction + empty AC) | done | predicted DC survives, other 63 coefficients are zero |
| escape form, DC prediction across blocks, P-VOP/B-16, IDCT | **not done** | - |

`nj_m4v_decode_intra_block()` already returns distinct codes for "corrupt" (-1)
and "escape not decoded yet" (-3), so the wiring cannot confuse the two.

### M4V building blocks after round 43

Everything below is in `src/midp/nj_m4v.c` and pinned by `tests/test_m4v.c`
(48 checks): AC lookup table (generated), AC decode and run/level/last
expansion, DC size code and signed delta, DC quantiser scale (reference
tables), coefficient scan and placement, AC coefficient walk, single intra
block and its pixel reconstruction, macroblock grid, block origins inside a
macroblock, and 4:2:0 chroma plane sizes.

What the I-frame assembly still needs: the cross-block DC prediction rule and
the AC escape form.  Until those land, `nj_m4v_decode_intra_block()` reports
-3 for an escape rather than pretending the coefficient was zero, and nothing
in the tree claims `mp4v` can be played.

### Handoff: finishing MPEG-4 Part 2 (state after round 47)

`src/midp/nj_m4v.c` already decodes one intra block end to end. What an I-frame
still needs, in dependency order:

1. **DC prediction semantics.** `nj_m4v_dc_predict()` does the arithmetic; the
   missing part is choosing the neighbour block inside a macroblock (top-left
   block looks at the left macroblock's top-right block, falling back to the
   block above, and so on).  Take the rule from `mpeg4videodec.c`'s
   `decode_intra_block` / `dc_pred` context in
   `.local-tools/debug-runs/m4v-round25/`, not from memory.
2. **AC escape form.** The reference path is around lines 1471 and 1594-1602 of
   that file ("first escape" / "second escape"); it needs the per-quantiser
   `max_level` / `max_run` tables, which are not in `nj_m4v_tables.h` yet.
3. **Macroblock raster.** `nj_m4v_frame_macroblocks()` and
   `nj_m4v_block_origin()` give the loop bounds and destinations;
   `nj_m4v_decode_intra_pixels()` fills one block.
4. **Provider.** Register an `nj_av_video_ops` with format
   `NJ_AV_VIDEO_MPEG4` whose `open()` parses VOL/VOP (both done) and decode()
   drives steps 1-3; `createPlayer()` already prefers a provider for video and
   currently refuses `mp4v` by name, so that refusal becomes the fallback.
5. **Acceptance.** The corpus's `1942.3gp` (176x144, 275 samples) and
   `doom.3gp` (320x240, 189 samples) are mp4v; decode their first frame and
   compare against ffmpeg's, the way `tests/test_h263.c` does for the H.263 clip.

Nothing in the tree claims `mp4v` playback today: `createPlayer()` raises a
MediaException naming the missing decoder, and the M4V decoder reports -3 for
an escape it cannot yet read.

### Gate status (round 54)

The full suite is green on `74f2277` (118/118), `no_statics` included: the two
decoder tables are `const` data in `.rodata`, the provider registry lives in
`nj_vm` and decoder state lives in the provider's own allocation.  Earlier
rounds reported `-E no_statics` because another workstream had uncommitted
edits that put an `on.1` symbol in `.data`; that change is not part of this
goal's commits and has since been resolved.

### Escape tables, derived the reference way (round 57)

`tools/gen_m4v_escape.py` writes `include/nj2me/nj_m4v_escape.h`: for each half
of the intra RL table (`i < last` and `i >= last`) it computes `max_level[run]`
and `max_run[level]` with the rule copied from libavcodec/rl.c - the largest
level seen at a run, and the largest run seen at a level.  The header is data
only; it is not used yet.  Numbers from the reference tables: runs up to 20,
levels up to 27, 21 and 28 entries per half.

### The escape grammar, as the reference implements it (round 60)

Read from `mpeg4videodec.c:1465-1602` (the non-RVLC path that Simple Profile uses).
Right after the escape symbol the next two bits choose the form:

| Bits | Form | Layout |
| --- | --- | --- |
| `0x` | first escape | skip the 0, then run/level come from the **escape half** of the same VLC table, `level += max_level[last][run-1] * qmul`, then one sign bit |
| `10` | second escape | skip 2, run/level from the same escape table, `run += max_run[last][level/qmul] + 1`, then one sign bit |
| `11` | third escape | skip 2, `last`(1) `run`(6) marker, level as **signed 12 bits**, marker; `level = level*qmul ± qadd` with a [-2048, 2047] clamp |

The consequence for our tables: the escape forms need the same code words decoded to
`(run, level)` pairs, not to symbols.  `nj_m4v_tables.h` currently maps codes to
symbols only, so the generator has to emit a second LUT with `(run, level)` packed
in it before `nj_m4v_ac_escape()` can be written.  `nj_m4v_escape.h` (max_level /
max_run) is already in place and pinned by a test.

### The escape is a separate code word, not symbol 0 (found round 73)

Reading ff_mpeg4_rl_intra carefully: `ff_mpeg4_intra_vlc` has **103** entries while
the run/level arrays have **102**.  The last entry, `{0x3, 7}` (bits `0000011`),
is the escape code; it is *not* one of the 102 symbols.  Symbol 0 is
(run 0, level 1), per `ff_mpeg4_intra_level[0] == 1`.

`nj_m4v_ac_sym()` currently treats symbol 0 as the escape, which means a real
bitstream escape is decoded as "run 0, level 1" and a genuine 0/1 coefficient is
mistaken for an escape.  The AC round-trip tests did not catch this because
both sides used the same table.  Fix: build the LUT from all 103 code words,
mark the escape with a sentinel (e.g. 0xFFFFFE), and return -2 only for it.

### What an I-VOP still needs (round 94)

The entropy layer is complete and tested (`test_m4v`, 62 checks): AC symbols,
all three escape forms, DC size/delta/prediction arithmetic, the DC scaler
tables, scan order, block geometry, the 4:2:0 colour matrix.  Turning those
into a picture needs three more steps, in this order:

1. **DC dequantisation into the coefficient domain.**  `nj_m4v_decode_intra_block()`
   currently stores `dc_pred + delta` at `coef[0]`; MPEG-4 scales the intra DC
   term by the DC scaler before the transform, so the frame driver must multiply
   by `nj_m4v_dc_scale(qscale, luma)` (tables already in the tree) rather than
   leaning on the AC path's `qmul/qadd`.
2. **Grid-backed prediction across the macroblock raster.**  `nj_m4v_dc_block()`
   already reads left/top/top-left from a picture-wide grid, applies
   `nj_m4v_dc_predict_q()` and writes back `level * scale`; the driver has to
   allocate three such grids (two luma 8x8 grids and one each for Cb/Cr) and walk
   `nj_m4v_frame_macroblocks()` in raster order.
3. **Wiring.**  after planes exist, `nj_m4v_yuv420_to_rgb565()` produces the
   frame; registering it as `NJ_AV_VIDEO_MPEG4` reuses the demuxer/Player/pump
   path that already works for the 3GP container (rounds 12-13), and the
   acceptance is the first frame of `1942.3gp` against ffmpeg's own decode.

Nothing here is wired yet: `createPlayer` still refuses `mp4v` by name, which is
the honest answer until the planes exist.

### Codec gate state (round 95)

| Check | Result |
| --- | --- |
| `test_m4v` | 62 checks / 0 failures |
| `test_m4v` under ASan+UBSan (Debug) | 62 / 0, no reports |
| `test_h263` (vs ffmpeg pixels) | 86 / 0 |
| `test_lib` (MP3/AMR player paths) | 881 passed / 0 failed |
| `BENCH sum` | `-136042548 s=5` unchanged |
| trim switches | AMR/MP3/VIDEO/MIDI all build when off (round 22) |

The sanitizer run matters here: the M4V code walks bitstreams with explicit
bounds checks, so ASan/UBSan over the whole test binary is the check that the
"corrupt stream" paths really stay inside their buffers.

### Goal status at the end of the 96-round budget (round 96)

Delivered with tests:

| Item | Evidence |
| --- | --- |
| MP3 provider and streaming Player | `test_mp3` (LAME fixtures vs ffmpeg, worst delta 1 LSB), `test_lib` real samples reach the port, `test_avcodec` a port's decoder wins |
| AMR provider and Player path | `test_amr` provider output byte-identical to `nj_amr_decode()`, `test_lib` player-level replacement |
| H.263 decoder | `test_h263` 86 checks against ffmpeg pixels |
| MP4/3GP demultiplexer | track geometry/timebase matches ffprobe on the corpus's own movies |
| Video Player and media pump | 3GP clip decodes through `Manager.createPlayer` in `test_lib` |
| Runtime provider replacement | audio and video replacement/removal covered in `test_avcodec` |
| Trim macros | AMR/MP3/VIDEO/MIDI each build with the feature off; costs in `docs/FOOTPRINT.md` |
| RAM/ROM | every item above has its numbers in `docs/FOOTPRINT.md` |

Not delivered: **MPEG-4 Part 2 frame decoding**. The entropy layer is complete and
tested (`test_m4v`, 62 checks: AC symbols, three escape forms, DC size/delta and
prediction arithmetic, DC scaler tables, scan order, block geometry, the 4:2:0
matrix), but the frame driver - DC dequantisation into the coefficient domain,
grid-backed prediction across the macroblock raster, provider registration and
pixel acceptance on `1942.3gp` - is not written. `createPlayer` therefore still
refuses `mp4v` by name, which is the truthful behaviour.

### The frame driver, and where it diverges (work in progress)

`nj_m4v_decode_ivop()` now walks the macroblock raster: three DC prediction
grids, six blocks per macroblock, `nj_m4v_dc_scale()`/`nj_m4v_dc_coef()` into the
transform domain, `nj_idct8x8()` and clipped writes to the Y/Cb/Cr planes.  It is
**not wired into any player and not claimed to decode correctly**.

Against the first sample of `1942.3gp` it decodes every block of macroblock (0,0)
and two blocks of (1,0), then the bitstream no longer matches the intra VLC
table (the 12-bit window at the failure point is `1110 0011 0110`, which is not a
codeword).  That is a real bitstream-alignment bug, not a tolerance issue, so the
driver must not be enabled until it is found.  Diagnostics are behind
`NJ_M4VDBG` (per-block DC size, level, AC result and bit offsets).

Two hypotheses were already eliminated with evidence: `ac_pred_flag` (skipping a
bit per macroblock makes it fail earlier, so the stream does not carry it) and
`intra_dc_vlc_thr` (`ff_mpeg4_dc_threshold[0] = 99` and `qscale = 9`, so the DC
*is* coded with its own VLC, which is what the driver does).

### A working harness for the frame driver (round 103)

Reaching `nj_m4v_decode_ivop()` with correct inputs takes three steps, and the
first one was the reason earlier probes "succeeded" doing nothing:

1. the VOL comes from the mp4v `esds` configuration, **not** from the sample
   (`nj_m4v_parse_vol(cfg, len, 22, &vol)`; 0 = success, and the 1942 config
   gives 176x144 at `time_res` 1000);
2. the VOP header starts at **bit 32** of the sample (the sample opens with the
   `00 00 01 b6` start code); `nj_m4v_parse_vop_bits()` returns 1 on success and
   ends at bit 56, reporting `type=0 (I), coded=1, qscale=9`;
3. `nj_m4v_decode_ivop()` then fails at bit **588** of the sample, i.e. inside
   the first macroblock row, not after eight blocks as the earlier (offset-
   confused) probe suggested.  That is the number to bisect from next.

### What is now ruled out in the M4V alignment bug (round 106)

Three suspects were checked against the reference's own tables and code:

* the AC lookup table: expanded to all 4096 twelwe-bit windows and compared
  entry by entry with `ff_mpeg4_intra_vlc[103][2]`, **0 mismatches** (and the
  103rd entry is indeed `{3, 7}`, the seven-bit escape);
* the `last` split: `ff_mpeg4_rl_intra` says `n = 102, last = 67`, exactly what
  `NJ_M4V_AC_LAST` uses, and `ff_mpeg4_intra_level[]` contains no zero level, so
  there is no separate EOB symbol to mis-handle;
* the escape grammar: the reference reads, for the third form, two mode bits,
  `last`, six-bit `run`, a marker, twelve signed `level` bits and another
  marker - which is what `nj_m4v_escape3()` does; the second and first forms
  match as well.

So the remaining candidates are the DC delta's sign-bit order and the
block-end convention (the reference tracks `i` with a 192 offset so a `last`
symbol whose run lands at 63 ends the block, rather than treating the run as a
plain offset).  Both are cheap to test with the trace harness described above.

### The DC delta sign rule was inverted (round 109)

`nj_m4v_dc_delta()` read `size` bits and then subtracted `2^size - 1` when the
leading bit was **set**, which makes the value negative for a set sign bit.  MPEG-4
is symbol-first (ffmpeg's `get_xbits`): a set leading bit is positive, a clear one
means `value - (2^size - 1)`.  With the old code `size == 1` always produced zero.

Fixed, with the tests rewritten to the specification's values (`size 2`:
`10 -> +2`, `11 -> +3`, `01 -> -2`, `00 -> -3`; `size 1`: `1 -> +1`, `0 -> -1`).
This is a reconstruction bug only - the bit count is unchanged, so it is **not**
the cause of the alignment failure at bit 588.  `test_m4v` 63 checks, 0 failures.

### A reference decoder for the last mile (round 111)

`tools/refdecode_frame.c` decodes the first video frame of a container with the
system ffmpeg (libavformat 63.1.102 + libavcodec are installed here) and prints
its geometry; run it with `pkg-config --cflags --libs libavformat libavcodec
libavutil`.  It reports `codec=mpeg4 176x144` and one decoded frame for
`1942.3gp`.  That makes the remaining MPEG-4 work checkable end to end: once the
frame driver gets past the alignment failure, its luminance can be compared with
this decoder's output instead of eyeballing dumps.

#### What the three real movies actually are (round 112)

`tools/refdecode_frame.c` also prints the decoder's `extradata`, its frame count
and its frame rate, so the runtime's demultiplexer and VOL parser can be checked
against ffmpeg on the corpus's own files:

| File | ffmpeg says | runtime already parsed |
| --- | --- | --- |
| `1942.3gp` (1942空战) | mpeg4 176x144, 32-byte config, 275 frames, 25 fps | 176x144, 275 samples, 25 fps |
| `doom.3gp` (毁灭战士3) | mpeg4 320x240, 48-byte config, 189 frames, 15 fps | 320x240, 189 samples, 15 fps |

The 1942 config is byte-identical to the 32 bytes `tests/test_m4v.c` embeds, and
the frames/geometry/fps all agree - the demuxer and the VOL parser are right, the
remaining work is the frame driver's alignment bug and then pixel comparison
against this oracle.

#### Round 113: the DC tables match too, and libavcodec's DCT dump is off

The intra DC VLC tables were diffed against `ff_mpeg4_DCtab_lum`/`_chrom` in
`mpeg4data.h`: both match entry for entry (our packed `(code << 4) | len` form,
0 mismatches), so the DC size codes are not a source of drift either.

Trying to make ffmpeg print its decoded coefficients hit a wall: setting
`AVCodecContext.debug = FF_DEBUG_DCT_COEFF` produces nothing because the distro
build compiles `ff_tlog` out (it is behind `#ifdef TRACE`).  So the coefficient
comparison has to come from our side: dump our per-block (run, level, bits)
sequence and compare it against a **reference implementation of the block parser
that is known correct** - either a small standalone table-driven parser written
from the spec, or ffmpeg's `mpeg4videodec.c` compiled with `TRACE` defined by
hand (it is ~5k lines plus dependencies, so the standalone parser is the cheaper
option).

### Where MPEG-4 Part 2 stands (round 195)

Decoded and tested: VOS/VOL/VOP headers (both real films' configurations), the
macroblock header (MCBPC symbol, ac_pred, CBPY, dquant), the DC code (size VLC,
long-code marker, prediction round trip), the AC VLC with all three escape
forms, zigzag placement and the shared integer IDCT.

Not decoded yet: the first I frame stops after a few macroblocks.  Three facts
from the reference are recorded here because they are the remaining suspects,
with the exact lines they came from (`mpeg4videodec.c` in
`.local-tools/debug-runs/m4v-round25/`):

| Reference lines | Fact | We do |
| --- | --- | --- |
| 2049-2055 | `ac_pred` is read before `cbpy`, `cbp = (cbpc & 3) \| (cbpy << 2)` | aligned in 2aad164 |
| 1426-1432 | when `ac_pred` is set the scan order is swapped to the horizontal or vertical permutated table, chosen by `dc_pred_dir` | still one zigzag table |
| 1424-1436 | inside the intra branch `qmul = 1, qadd = 0` - the scaling happens elsewhere | we scale every level by `qmul/qadd` |

`tests/test_m4v.c` pins the parts that are done (79 checks); the frame driver and
`tools/refdecode_frame.c` show how far the first frame gets.

### The oracle says the first frame is black (round 199)

`tools/refdecode_frame.c <file> <out.ppm>` decodes the first video frame with
the system ffmpeg and writes it as a PPM.  For `1942.3gp` the result is
176x144 and **every sample is zero** (max 0, mean 0.0), which makes the
acceptance test for the MPEG-4 intra path concrete: a correct decoder must
produce an all-zero frame for this sample, so the comparison needs no
tolerance and no perceptual metrics.

Compile it with libavformat/libavcodec/libavutil/libswscale; it is not part of
the test suite because those libraries are not build dependencies.

### The decoder configuration now comes out of the container (round 121)

The VOL that the MPEG-4 video decoder needs is not part of the sample: it lives
in the `esds` box of the sample description, inside a nest of MPEG-4 systems
descriptors (`ES_Descriptor` → `DecoderConfigDescriptor` →
`DecoderSpecificInfo`).  `nj_mp4_open()` now walks that nest and reports the
config as `cfg_at` / `cfg_size` (absolute offset in the caller's buffer, no
allocation, no copy), so a provider can do

    nj_mp4_parse_vol(file + mp4.cfg_at, mp4.cfg_size, ...) -> VOL

instead of hunting for a start code in the first sample.  Descriptor lengths use
the expandable seven-bit encoding and every one of them is bounds checked
against the enclosing box, so a truncated `esds` ends the walk instead of
reading outside the file.

The walk itself is `nj_mp4_esds_find()`, exposed in `nj_mp4.h` and unit tested
in `tests/test_h263.c` on a hand-built descriptor nest (found, exact length,
exact payload, truncated nest refused, nest without a config reported as none).
Against the two corpus movies it reports the real configurations:

| Clip | Sample entry | Config | VOL |
| --- | --- | --- | --- |
| `1942.3gp` (vault) | `mp4v` | 47 bytes at 1641 | 176x144, time_res 1000, 25 fps |
| `doom.3gp` (vid) | `mp4v` | 63 bytes at 221276 | 320x240, time_res 15, 15 fps |

Both configurations have complexity estimation disabled, which matters for the
still-open alignment bug: the VOP header then carries **no** complexity bits, so
the eight bits that the header is short by come from somewhere else and the
"complexity estimation" hypothesis for it is now ruled out with evidence.

### The VOP header carries no change_CR/alpha bits when the shape is rectangular (round 122)

`decode_vop_header()` reads `change_CR_disable` and the `constant_alpha` flag
inside `if (ctx->shape != RECT_SHAPE)`.  Our parser read them for every VOP, two
bits that a 3GP movie never carries, so everything after them was shifted:
`intra_dc_vlc_thr` and `qscale` both came out wrong (qscale 6 instead of the 9
the reference decoder reports, and the first macroblock then started two bits
late).

With the condition added, the first sample of `1942.3gp` now lines up:

| Field | Before | Now | Reference |
| --- | --- | --- | --- |
| VOP header end | bit 58 | **bit 56** | `nj_m4v_parse_vop_bits()` |
| qscale | 6 | **9** | `ffmpeg -debug qp` prints 9 |
| `intra_dc_vlc_thr` | shifted | **0** | 3-bit field right after `vop_coded` |

The macroblock data that follows confirms the alignment on the first block
instead of just not crashing: at bit 56 the stream reads MCBPC `1` (symbol 0),
`ac_pred` 0 and CBPY `0011` (symbol 0), i.e. `cbp = 0` - exactly what an
all-black first frame must carry, and matches the oracle's all-zero frame.

## The first I-VOP decodes to the reference pixels (round 224)

Three defects sat between "the header lines up" and "the picture is right"; all
three were found by diffing against the FFmpeg decoder run on the same file
(`tools/refdecode_frame.c` plus a per-macroblock dump of `block[0]`, then a
whole-frame YUV comparison).

1. **The MCBPC and CBPY tables were in the wrong order.**  The intra tables were
   written with the symbols complemented, so every macroblock read a plausible
   but incorrect CBP.  Both now carry the symbols from H.263 Annex A
   (`/tmp/h263data.c` in the reference tree) and match
   `tests/test_m4v.c`'s derived expectations.
2. **`dquant` has to persist for the rest of the frame.**  The quantiser was
   re-read from the VOP header for every macroblock, so the frame's first
   `dquant` was forgotten on the next one.  It now walks the macroblock loop.
3. **The intra DC coefficient was never dequantised.**  `nj_m4v_decode_intra_block()`
   stores the *level*, and the reference's `dct_unquantize_h263_intra()` then
   multiplies it by the DC scaler before the IDCT (`block[0] *= y_dc_scale`).
   We passed the raw level to `nj_m4v_idct()`, which produced mid-grey for every
   block.  The reconstruction now uses `nj_m4v_dc_coef()` and subtracts
   `NJ_M4V_DC_GREY` (1024), which is the bias the reference IDCT carries: it
   writes the coefficient straight out, so 1024 means sample 128.

With those in place `nj_m4v_decode_ivop()` consumes the whole sample - 2284 of
2288 bits, the rest being the trailing zero padding - and the first frames of
`1942.3gp` (176x144) and `doom.3gp` (320x240) match the reference decoder
**pixel for pixel on the luma plane** (100% of samples equal, maximum absolute
difference 0).  Chroma differs by at most 1 on 3% of samples, the expected
residue of two different integer IDCT roundings.

Still missing for playback: P-VOPs, so only the first frame of a clip decodes,
and the data-partitioned and advanced-intra variants some corpus titles use.
`tests/test_m4v.c` (90 checks) pins the DC dequantisation: level 8 at a DC
scaler of 16 reconstructs the limited-range black sample, and level 0 always
reconstructs black.

Also pinned in `tests/test_m4v.c` (88 checks): the rectangular header ends at
bit 56 with qscale 9 / thr 0, and the 1942 VOL reports complexity estimation
disabled with zero complexity bits promised for I/P/B VOPs.

## The AC coefficient loop, ported from the reference (round 227)

The I-VOP's first frame is all black, so its AC coefficients happen to be zero
and an incorrect AC loop still produced the right picture.  P-VOP intra
macroblocks do carry AC data, and the old loop diverged from the reference
stream after the first coefficient.  `src/midp/nj_m4v.c` now carries the
coefficient loop transcribed from the reference decoder's
`mpeg4_decode_block()` (non-RVLC, non partitioned):

- the 12-bit lookup `nj_m4v_ac_lut[]` yields `len << 8 | symbol`; the run goes
  through the `+1` skip of the DC slot, and a "last" symbol adds 192 so that
  the index wraps past 62 and ends the block exactly where the reference does;
- the sign is a separate bit read after the symbol (or after the escape's
  level), as `(level ^ sign) - sign`;
- the escape code word `0000011` is followed by `0` (first form), `10` (second
  form) or `11` (third form); the first form adds
  `max_level[last][run] * qmul` to the level, the second adds
  `max_run[last][level]` to the run, and the third carries explicit
  last/run/level fields with marker bits;
- the same loop serves the inter residual, with `qmul = qscale << 1` and
  `qadd = (qscale - 1) | 1`.  Its run-level and lookup tables come from
  `src/midp/nj_vlc_inter.h`, generated from the reference's
  `ff_h263_rl_inter`, so no writable global state is needed.

`tools/gen_m4v_escape.py` still emits the intra escape maxima
(`include/nj2me/nj_m4v_escape.h`), and the parity tests in `tests/test_m4v.c`
compare the loop against the same synthetic bit streams as before.

With this in place the P-VOP macroblock layer walks the reference positions
for the encoded macroblocks (71, 95, 199, 285, 353, ... in the second frame of
`1942.3gp`), and stops cleanly on the first eight-by-eight motion vector
macroblock, which is not implemented yet.  The frame is therefore still not
complete; the next step is inter4v prediction and motion compensation.
