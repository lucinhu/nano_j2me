# The last two MPEG-4 Part 2 boundaries: video packets and AC prediction

Two entries in `docs/CODECS.md`'s "still not implemented" list were decoder
features rather than products: **video packet (resync) headers** and **AC
prediction**.  The corpus movies do not need either (they are single-slice and
use no AC-predicted macroblocks), which is exactly why they were deferred: no
real title could prove them.  Both are now implemented against
FFmpeg-generated fixtures, pixel-verified, and pinned in `tests/test_m4v.c`.

Commits: `e0c069f` (video packets), `0201ac6` (AC prediction).

## Why fixtures had to be encoded for this

The three corpus movies set `resync_marker_disable = 1` - one slice per VOP -
and FFmpeg's macroblock-type dump shows no `ac_pred` macroblocks in the frames
checked.  Both features therefore need a stream that carries them:

    # video packets: -threads > 1 turns on FFmpeg's sliced encoder
    ffmpeg -f lavfi -i color=black:s=96x64:r=10:d=0.6 \
           -f lavfi -i color=white:s=16x16:r=10:d=0.6 \
           -filter_complex "[0][1]overlay=x='t*120':y='10+t*20'" \
           -c:v mpeg4 -threads 3 -q:v 4 -bf 0 -g 100 -f 3gp m4v_slice.3gp
    # AC prediction: -flags +aic makes the encoder emit ac_pred macroblocks
    ffmpeg -f lavfi -i "testsrc=size=96x64:rate=10:duration=0.4" \
           -c:v mpeg4 -flags +aic -threads 1 -q:v 6 -bf 0 -g 100 \
           -f 3gp m4v_acpred.3gp

The 96x64 sliced movie has 24 macroblocks and three slices (rows 0 / 1 / 2-3),
so it writes two video packet headers per VOP, starting at macroblock 0, 6 and
18.  The 96x64 AC movie has 13 of its 24 I-VOP macroblocks with `ac_pred = 1`,
which between them exercise both prediction directions, the left-column and
top-row stores and the alternate scans.

Both are embedded in the tree (`tests/m4v_slice_clip.h` and the
`m4v_acpred_3gp` entry in `tests/m4v_clip.h`) with reference plane hashes.

## Video packets, as the reference parses them

`mpeg4videodec.c` detects a packet marker **at the bit after a macroblock**
(`mpeg4_is_resync()`), then `ff_h263_resync()` skips the stuffing and
byte-aligns, and `ff_mpeg4_decode_video_packet_header()` reads the header.  Our
port follows the same split:

- detection uses the reference's own trick: the 16-bit window right after the
  macroblock is compared with one of eight patterns depending on the bit
  offset (`0x7F00, 0x7E00, 0x7C00, ...`), and the announced macroblock number is
  validated before the position is accepted;
- the header is: stuffing bit then ones to the byte boundary, a zero prefix of
  16 bits (I-VOP) or `f_code + 15` bits (P-VOP), a one bit, the absolute
  macroblock number, `quant_precision` quantiser bits and a header-extension
  flag.  The extension is refused (`-2`) rather than skipped;
- the drivers walk macroblocks by absolute index, so a packet may begin
  anywhere; the slice's first row and start column feed the DC predictor's
  outside-slice overrides and the motion-vector predictor's
  `first_slice_line`/`resync_mb_x` cases.  The DC/MV grids are not reset: the
  reference keeps decoded neighbour values readable and neutralises only the
  slots the slice rules name.

Result: the sliced fixture decodes to FFmpeg's planes exactly - 6 frames, Y, U
and V, max-abs-diff 0.  Synthetic unit tests also pin the exact header bit
layout for an I-VOP and a P-VOP with `f_code = 2`, plus the refusals (wrong
prefix length, macroblock number zero).

## AC prediction, as the reference decodes it

`ff_mpeg4_pred_ac()` is the whole story: when a block's DC predictor chose the
left neighbour, its **first column** of raw coefficients is added to the block;
when it chose the top neighbour, its **first row** is added.  The prediction is
in the quantised domain (it happens before `dct_unquantize_h263_intra` scales
the coefficients) and is rescaled by `ROUNDED_DIV(neighbour_level *
neighbour_qscale, qscale)` when the neighbour macroblock used a different
quantiser.  Blocks 1/3 (left direction) and 2/3 (top direction) are inside the
same macroblock and never rescale.  The reconstruction then stores the block's
own first column/row for the next block.

Two things around it matter as much as the prediction itself:

- the coefficient scans are not the plain zigzag: with `ac_pred` the reference
  selects `permutated_intra_v_scantable` (from `ff_alternate_vertical_scan`)
  when predicting from the left and the horizontal one when predicting from
  above.  `nj_m4v_scan_index_mode()` now carries those exact tables;
- a non-intra macroblock clears the AC slots a later intra block could read
  (`ff_clean_intra_table_entries`), and a video packet start clears the band of
  slots outside the new slice (`ff_mpeg4_clean_buffers`).  Without the clears an
  intra block would predict from coefficients belonging to another slice or an
  inter block.

Result: the AC fixture decodes to the reference exactly - 4 frames, Y, U and V,
max-abs-diff 0.

## A caveat about the ffmpeg oracle, and how the fixtures were verified

While checking the AC fixture one chroma sample differed by one level from the
**system** ffmpeg.  It was not our decoder: FFmpeg's x86 SIMD Simple IDCT is not
bit-exact with its own C reference on every input.  The same clip decoded with
FFmpeg 9.0.2 built with `--disable-x86asm` gives the value our decoder produces;
the system SIMD build differs on one sample of frame 3.  The decoder follows the
C reference, so the fixture hashes were produced with the C-only build:

    # from the FFmpeg 9.0.2 source tree
    ../configure --disable-x86asm --disable-everything --disable-autodetect \
                 --disable-network --disable-programs --disable-doc \
                 --disable-shared --enable-static --enable-avcodec \
                 --enable-avutil --enable-decoder=mpeg4 --enable-parser=mpeg4video
    make -j
    # feed it the container's samples with a small harness, or use the tool in
    # tools/refdecode_m4v.c against a system ffmpeg when SIMD is not suspected

The three corpus movies match both builds, so the earlier "pixel-identical to
FFmpeg" claims are unaffected, and `docs/CODECS.md` records the caveat for
fixture work.

## Evidence and tests

`.local-tools/debug-runs/m4v-boundaries-20261027/`

| Path | What it holds |
| --- | --- |
| `resync/sliced.3gp` | the `-threads 3` fixture |
| `resync/sliced.compare.txt` | per-frame Y/U/V max-abs-diff 0, 6/6 frames |
| `acpred/acpred.3gp` | the `-flags +aic` fixture |
| `acpred/ca-*.yuv` | the C-only FFmpeg reference planes |
| `acpred/acpred.compare.txt` | per-frame Y/U/V max-abs-diff 0, 4/4 frames |

`tests/test_m4v.c` is 118 checks: it now includes the packet-header bit layout
and refusals, the alternate scan tables, `ROUNDED_DIV`, the sliced-movie decode
and the AC-prediction decode, alongside the earlier chain tests.  The other
suites stay green (test_h263 97, test_avcodec 102, test_lib 912), ASan/UBSan are
clean and `tools/check_no_statics.sh` still finds no mutable globals.

## What is still open (playback, not decoding)

The decoder boundaries are closed; the remaining limits are in the Player:
the pump decodes one sample per emulated frame instead of following the movie's
DTS/fps (a 15 fps clip plays about twice as fast at 30 fps), and a video Player
does not play the movie's own AMR audio track.  Both are recorded in
`docs/CODECS.md`.
