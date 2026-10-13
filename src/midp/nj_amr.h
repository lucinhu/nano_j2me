/*
 * nano_j2me - nj_amr.h
 *
 * The AMR narrowband speech decoder behind Manager.createPlayer() for the
 * "#!AMR" assets in the collection.  Private to src/midp: nj_lib_media.c is
 * the only caller.
 */
#ifndef NJ_AMR_H
#define NJ_AMR_H

#include "nj2me/nj_types.h"

/* AMR-NB is one shape: 8 kHz mono, the telephone band.  There is no other. */
#define NJ_AMR_RATE 8000u

/*
 * Frame geometry, from 3GPP TS 26.090: a 20 ms frame of four 40 sample
 * subframes, a tenth order short term filter.  The generated tables
 * (nj_amr_tables.h) are cut to these, so they are part of this decoder's
 * interface with itself.
 */
#define NJ_AMR_LPC_ORDER 10
#define NJ_AMR_SUBFRAME  40
#define NJ_AMR_BLOCK     (4 * NJ_AMR_SUBFRAME)
#define NJ_AMR_MODES     9

/*
 * How many frames an RFC 4867 AMR narrowband file holds - the "#!AMR\n" magic
 * followed by concatenated frames - or zero when the data is not narrowband
 * AMR at all (AMR-WB, or one of the three assets in the collection that is
 * named .amr but holds an MP4 container) or holds no complete frame.
 *
 * This is also the size of the PCM a decode produces, which is why it is a
 * call of its own: the caller gets to allocate the answer once, at the size it
 * will actually be.
 */
nj_u4 nj_amr_frames(const void *data, nj_u4 size);

/*
 * Decode the file into PCM the caller owns: nj_amr_frames() * NJ_AMR_BLOCK
 * samples of 16-bit signed little-endian mono at NJ_AMR_RATE samples per
 * second, i.e. nj_amr_frames() * NJ_AMR_BLOCK * 2 bytes.  Returns the frames
 * decoded, or zero when there is nothing to decode.
 *
 * Nothing is allocated and nothing is read back: the decoder's entire state
 * sits on its own stack, so a caller with room for the samples has room to
 * decode them - the largest clip in the collection is 310 KB of samples
 * against 4 KB of stack (measured, -fstack-usage).
 */
nj_u4 nj_amr_decode(const void *data, nj_u4 size, nj_u1 *pcm);

#endif /* NJ_AMR_H */
