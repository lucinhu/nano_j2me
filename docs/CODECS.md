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
| `nj_av_audio_mp3` | MPEG-1/2/2.5 Layer III | decoder core being written |
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
