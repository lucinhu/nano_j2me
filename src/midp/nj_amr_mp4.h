/*
 * nano_j2me - nj_amr_mp4.h
 *
 * The AMR narrowband sample iterator for files that carry the track inside an
 * ISO base media (MP4/3GP) container instead of the RFC 4867 "#!AMR\n" layout.
 * Private to src/midp: nj_amr.c is the only caller.
 *
 * Three assets in the collection are named ".amr" while holding an MP4 file
 * (Doom's three sound effects and its 3gp video), and the sniffer in
 * nj_lib_media.c now names that container instead of calling it unknown.  What
 * is needed to play one is the reverse of muxing: find the single 'samr' track,
 * walk its sample tables, and hand the decoder each sample's bytes.
 *
 * Nothing here allocates and nothing keeps state between calls except the
 * iterator itself, which the caller puts on its stack.  Every table read is
 * bounds checked against the file and every sample is required to lie inside
 * an 'mdat' payload, so a truncated or hand-edited file stops the walk instead
 * of reading bytes belonging to another box.
 */
#ifndef NJ_AMR_MP4_H
#define NJ_AMR_MP4_H

#include "nj2me/nj_types.h"

/*
 * Room for four 'mdat' boxes.  One is what every real muxer writes; a file with
 * more is refused rather than silently dropping samples, because the caller
 * cannot tell the difference between "no more data" and "we stopped looking".
 */
#define NJ_AMR_MP4_MDAT 4

typedef struct nj_amr_mp4 {
    const nj_u1 *data;
    nj_u4        size;

    /* Sample size table: either one constant size, or one entry per sample. */
    nj_u4        stsz_at;        /* first entry byte, 0 when constant */
    nj_u4        stsz_count;     /* samples in the track */
    nj_u4        stsz_size;      /* constant sample size, 0 when a table */

    /* Sample-to-chunk and chunk offset tables. */
    nj_u4        stsc_at;
    nj_u4        stsc_count;
    nj_u4        stco_at;
    nj_u4        stco_count;
    int          stco64;         /* 'co64' instead of 'stco' */

    /* 'mdat' payload ranges, [start, end). */
    nj_u4        mdat_at[NJ_AMR_MP4_MDAT];
    nj_u4        mdat_end[NJ_AMR_MP4_MDAT];
    int          mdat_count;

    /* Walk state: sample 'index' is next; it sits in chunk 'chunk', at byte
     * 'used' from its start, with 'in_chunk' samples already handed out. */
    nj_u4        index;
    nj_u4        entry;
    nj_u4        chunk;
    nj_u4        in_chunk;
    nj_u4        used;
    nj_u4        spc;             /* samples in the current chunk */
    nj_u4        chunk_at;        /* offset of the current chunk in the file */

    /* 'damr' framesPerSample: how many AMR frames one sample may hold.  The
     * decoder still counts the frames in the bytes it is handed - this is the
     * declaration, and a sample whose bytes disagree ends the walk. */
    nj_u4        frames_per_sample;
} nj_amr_mp4;

/*
 * Parse the container and the one 'samr' track.  Returns 1 when the file holds
 * exactly one AMR narrowband track whose tables are complete and consistent,
 * zero otherwise.  A file with no AMR track, two of them, or a table that
 * disagrees with another table is refused: playing half of an ambiguous track
 * is worse than refusing.
 */
int nj_amr_mp4_open(nj_amr_mp4 *it, const void *data, nj_u4 size);

/*
 * Next sample's bytes.  Returns 1 and sets *sample and *bytes, 0 at the end of the
 * track, -1 when the file contradicts itself (a sample outside its chunk or
 * outside every 'mdat') - at which point the caller must discard the track
 * rather than decode the frames it has already seen.
 */
int nj_amr_mp4_next(nj_amr_mp4 *it, const nj_u1 **sample, nj_u4 *bytes);

#endif /* NJ_AMR_MP4_H */
