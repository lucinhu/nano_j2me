# MPEG-4 Part 2: P-VOPs, 4MV and the three corpus movies

Until this round the MPEG-4 Part 2 decoder in this runtime could only decode the
first (intra) frame of a movie.  The intra picture and the P-VOP macroblock
header were verified against FFmpeg, but the layer stopped at the first
macroblock that carried eight-by-eight motion vectors: inter4v, half-pel motion
compensation and the inter residual path were missing, and `createPlayer()`
refused `mp4v` by name.

This round lands all three pieces and turns the "first frame only" decoder into
one that reproduces FFmpeg pixel for pixel on the corpus's own movies.

## What was added

- **The P-VOP macroblock layer, ported from the reference decoder**
  (`.local-tools/debug-runs/m4v-round25/mpeg4videodec.c`):
  `ff_h263_pred_motion`'s median predictor for all four 8x8 luma blocks,
  the 16x16 and 4MV motion-vector syntax, `ff_h263_round_chroma` for the 4MV
  chroma vector, the reference's 4:2:0 chroma mapping (`mv >> 2` with
  `(mv & 3) != 0`, not a second shift of a rounded vector), and the inter
  residual reconstruction.
- **The exact-ish bugs the I-VOP could not reveal.**  The AC loop that rounds
  224/227 made the first frame green happened to be exercised only through
  zero coefficients in the I-VOP.  Real P-VOP intra macroblocks found three
  more:
  1. the first escape form is selected by a `0` bit, but the code mapped it to
     `form = 0` and then compared `form == 1`, so first escapes fell into the
     third escape branch and read a marker bit too many;
  2. the third escape carries its own sign inside the 12-bit level and must then
     not read another sign bit;
  3. intra AC levels are stored raw by the reference entropy decoder and get
     `dct_unquantize_h263_intra`'s `qmul/qadd` in `put_dct` - without that the
     blocks were almost flat.
- **`vop_rounding_type`.**  A P-VOP header carries one bit that selects FFmpeg's
  `put_no_rnd_pixels_tab`: half-pel interpolation truncates instead of rounding
  up.  FFmpeg's own debug prints this inverted (`rnd:0` means no-rounding = 1),
  which is a good way to misread it.  Ignoring the bit produced a one-level
  drift on a few percent of samples that then compounded frame after frame.
- **The reference Simple IDCT, transcribed.**  `src/midp/nj_idct.h` is a generic
  integer transform accurate to about one level; MPEG-4's reference decoder uses
  FFmpeg's Simple IDCT with its own 11-bit rows and 20-bit column rounding, and
  the residual difference showed up as +-1 on a few percent of samples.  The
  MPEG-4 path now uses a direct transcription of
  `simple_idct_template.c` (`nj_m4v_idct` for the inter residual,
  `nj_m4v_idct_put` for the intra picture); H.263 keeps the shared transform.
- **The provider.**  `nj_av_video_mpeg4` registers next to H.263
  (`nj_avcodec_reset()` installs it second, so it is offered first and its
  `accepts()` identifies `mp4v` by container + VOL).  `createPlayer()` no
  longer refuses `mp4v` by name.  The provider decodes at the coded macroblock
  size and converts only the visible rectangle, which matters for the HAWX
  movie (240x180, i.e. 192 coded rows).
- **The empty `mdat` fix.**  Tom Clancy's H.A.W.X. ships a `movie.3gp` with a
  zero-payload `mdat` placeholder box before the real one; `nj_mp4_open()` now
  skips such a box instead of refusing the file, while still refusing a file
  whose `mdat` boxes are all empty.

## Verification

The oracle is FFmpeg 9.0.2 (libavcodec 63.1.102) via
`tools/refdecode_m4v.c`, which decodes video frames and writes planar Y/U/V;
`tools/m4v_dump.c` writes the same layout with the runtime decoder and
`tools/compare_m4v.py` prints per-plane max-abs-diff and percentage of equal
samples.  For the Player path, `tools/m4v_player_probe.sh` builds
`tools/m4v_player_probe.c`, which calls `Manager.createPlayer()` exactly like a
MIDlet, pumps `nj_media_frame()` and dumps the Player's RGB565 frame buffer as
PPM; `tools/m4v_dump.c --convert-yuv` converts FFmpeg's YUV planes with the
runtime's own colour matrix for the comparison, and
`tools/m4v_dump.c --provider` does the same through the provider handle.

    tools/refdecode_m4v tests-relative/1942.3gp 1942.ref 16
    tools/m4v_dump         tests-relative/1942.3gp 1942.run 16
    python3 tools/compare_m4v.py 1942.ref 1942.run --frames 16 \
            --manifest 1942.ref.manifest
    tools/m4v_dump --provider 1942.3gp 1942.prov 16
    tools/m4v_dump --convert-yuv 176 144 1942.ref 1942.rgb 16
    tools/m4v_player_probe.sh /tmp/nj-m4v-build 1942.3gp 1942.pl 16 16

Results (16 frames per movie, every frame, every plane):

| Movie | Size | Y max / equal | U max / equal | V max / equal |
| --- | --- | --- | --- | --- |
| `1942.3gp` | 176x144 | 0 / 100.00% | 0 / 100.00% | 0 / 100.00% |
| `vid/doom.3gp` | 320x240 | 0 / 100.00% | 0 / 100.00% | 0 / 100.00% |
| `movie.3gp` (HAWX) | 240x180 | 0 / 100.00% | 0 / 100.00% | 0 / 100.00% |

The provider path and the Java-level Player path are byte-identical to the same
reference frames after the runtime's own YUV-to-RGB565 conversion: all 48 frames
of each path match the reference's RGB565 hashes exactly.  Evidence, including
YUV dumps, PPM/PNG frames, the comparison CSV and the player probe output, is
under `.local-tools/debug-runs/m4v-movies-20261027/` (the Lead reproduced the
earlier eight-frame comparison independently in
`.local-tools/debug-runs/m4v-pvop-2026-10-27/lead-repro/`).

`tests/test_m4v.c` embeds the first four samples of `1942.3gp` (I/P/I/P) and
FNV-1a hashes of the reference planes of frames 1 and 3, so a regression in the
motion-vector predictor, the MC kernel, the rounding type or the IDCT fails the
normal test run without any external files; the provider is exercised against a
real 64x48 four-frame movie in `tests/m4v_clip.h`, through `test_m4v`'s provider
checks, `test_avcodec`'s `createPlayer` path and `test_lib`'s Player pump.

## Scope boundaries (documented, not blockers)

- **Resynchronisation markers.**  All three corpus movies have
  `resync_marker_disable = 1` (one slice per VOP).  FFmpeg's encoder enables
  sliced encoding when its automatic thread count exceeds one, so a movie
  encoded with default settings can carry video packet headers between
  macroblock rows; this decoder does not parse them yet.  The test fixture in
  `tests/m4v_clip.h` is encoded with `-threads 1` for the same reason, and the
  header records it.
- **AC prediction.**  `ac_pred` is decoded but the prediction itself is not
  applied to the coefficients; none of the corpus movies in the first 16 frames
  uses it (FFmpeg's macroblock-type dump shows no `A` entries), and the
  reference stores AC levels in the quantised domain, so implementing it is a
  self-contained next step.
- **VideoControl is still a stub.**  `Player.getControl("VideoControl")` returns
  null (its class exists so checkcasts resolve), so titles skip their videos
  instead of painting them; the three corpus titles run but do not show their
  movies.  The frames themselves decode and land in the Player's RGB565 buffer,
  which is what the `player/` evidence above proves; wiring `getSnapshot()` /
  `initDisplayMode()` is the remaining step to put them on screen.
