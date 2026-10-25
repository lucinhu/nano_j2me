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
