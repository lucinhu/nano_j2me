# The movie's sound track through a port's AMR decoder

A video Player never had a provider seam for its sound: `nj_media_movie_amr_decode()`
decoded the `samr` track with the built-in `nj_amr.c` and nothing else.  A
phone whose AMR lives in firmware (or a port that routes audio through
libavcodec) therefore played `.amr` effects with its own decoder and the same
codec inside an MP4/3GP movie with the software one - two paths for one
format, and the movie path paid the software decoder's whole-track buffer even
where the port has a better one.

## What changed

`nj_media_movie_amr_decode()` now asks the registry first.  The track reaches
the lookup as an elementary `#!AMR` stream, which is exactly what
`nj_av_audio_ops.accepts()` takes, so no new interface was needed:

* the *installed* provider is used when it is not the built-in one.  The
  software provider is skipped on purpose - it decodes the whole track into
  its own buffer, so routing the movie through it would hold the PCM twice -
  and the direct call below stays the default path;
* its frame count is derived from `step.frames / NJ_AMR_BLOCK` (a hardware
  decoder is not required to agree with the table walk on the exact count) and
  clamped to `pcm_cap / 320`, so a lying count cannot make the Player read
  past the PCM array;
* a provider that claims the stream and then produces nothing usable - an
  `open()` that fails, or a decoder that answers "more input needed" for the
  whole track - falls through to `nj_amr.c` instead of leaving the intro
  silent.  This is the part that keeps the seam additive: a port's decoder
  can only add a path, never take one away.

## Regressions

`tests/test_avcodec.c` gained a fake AMR provider whose PCM is a sentinel
(`0x2b2b`) and a "deaf" variant that opens and then never decodes:

* baseline decode with the stock registry is captured first;
* with the sentinel provider installed, the movie's PCM is the sentinel and
  the frame count reaches the Player;
* with the deaf provider installed, the result must equal the baseline frame
  count **and** match its PCM byte for byte - that is the fallback;
* `nj_avcodec_reset()` puts the software provider back.

With the pre-fix code (provider result returned unconditionally, including
zero frames) exactly the two fallback checks fail; with the fix,
`test_avcodec` is 205 checks / 0 failures.

## Validation

* `test_avcodec` 205 checks / 0 failures; `test_lib`'s movie-audio check
  (`test_movie_amr_track`) still passes, so the default path is unchanged.
* Full `ctest` 124/124; the sanitized suite is clean (ASan + UBSan + LSan).
* Real-game sanity: `1942空战_10403` reaches its intro at 240x320 with
  `--keys '90:soft1'` and reports `pcm_frames=2764684` at frame 1200, i.e. the
  default movie-audio path still feeds the port.  No port in this tree
  installs a hardware AMR provider, so the new branch has no real-game A/B;
  its evidence is the fake-provider test above.

## Limits

The seam is per-Player and whole-track: a port decoder is handed the entire
elementary stream once, not incrementally.  A decoder that can only consume
fragments falls back to `nj_amr.c`.  `VideoControl` still does not expose the
track as a separate controllable Player.
