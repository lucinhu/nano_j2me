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
