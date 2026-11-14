/* MIT. A playable AMR adaptation of the immutable demuxer-only 3GP fixture. */
#ifndef NJ_TEST_AMR_MOVIE_FIXTURE_H
#define NJ_TEST_AMR_MOVIE_FIXTURE_H

#include "../m4v_audio_clip.h"
#include <string.h>

#define NJ_TEST_AMR_MOVIE_BYTES (sizeof(m4v_av_3gp) + 2u)

/* These tag searches apply only to the fixed synthetic fixture below. */
static nj_u4 nj_test_amr_movie_tag(const nj_u1 *clip, nj_u4 len, const char *tag)
{
    nj_u4 i, at = 0u;
    for (i = 4u; i + 4u <= len; ++i) if (!memcmp(clip + i, tag, 4u)) at = i;
    return at;
}
static nj_u4 nj_test_amr_movie_be32(const nj_u1 *p)
{ return (nj_u4)p[0] << 24 | (nj_u4)p[1] << 16 | (nj_u4)p[2] << 8 | p[3]; }
static void nj_test_amr_movie_put32(nj_u1 *p, nj_u4 n)
{ p[0] = (nj_u1)(n >> 24); p[1] = (nj_u1)(n >> 16); p[2] = (nj_u1)(n >> 8); p[3] = (nj_u1)n; }

/* out has NJ_TEST_AMR_MOVIE_BYTES bytes; mdat and the original fixture stay put.
 * Five 13-byte mode-0 blocks occupy its audio area in the existing 3+2 chunks.
 * The old demuxer fixture has partial AMR blocks and an incomplete damr. Add
 * mode_change_period and frames_per_sample, growing all eight containing boxes.
 * This yields actual playable AMR without weakening validation of the original.
 */
static int nj_test_amr_movie_fixture(nj_u1 *out, int audio_only)
{
    static const char *parents[] = {
        "moov", "trak", "mdia", "minf", "stbl", "stsd", "samr", "damr"
    };
    nj_u4 i, j, at, insert;
    memcpy(out, m4v_av_3gp, sizeof(m4v_av_3gp));
    at = nj_test_amr_movie_tag(out, sizeof(m4v_av_3gp), "damr");
    if (!at || nj_test_amr_movie_be32(out + at - 4u) != 15u) return 0;
    insert = at + 11u;
    memmove(out + insert + 2u, out + insert, sizeof(m4v_av_3gp) - insert);
    out[insert] = 0u; out[insert + 1u] = 1u;
    for (i = 0; i < sizeof(parents) / sizeof(parents[0]); ++i) {
        at = nj_test_amr_movie_tag(out, NJ_TEST_AMR_MOVIE_BYTES, parents[i]);
        if (!at) return 0;
        nj_test_amr_movie_put32(out + at - 4u, nj_test_amr_movie_be32(out + at - 4u) + 2u);
    }
    for (i = 0; i < M4V_AV_AUDIO_FRAMES; ++i) {
        out[40u + i * 13u] = 0x04u;
        for (j = 0; j < 12u; ++j) out[41u + i * 13u + j] = (nj_u1)(11u + j * 37u + i * 17u);
    }
    at = nj_test_amr_movie_tag(out, NJ_TEST_AMR_MOVIE_BYTES, "stsz");
    if (!at || nj_test_amr_movie_be32(out + at + 12u) != M4V_AV_AUDIO_FRAMES) return 0;
    for (i = 0; i < M4V_AV_AUDIO_FRAMES; ++i) nj_test_amr_movie_put32(out + at + 16u + 4u * i, 13u);
    at = nj_test_amr_movie_tag(out, NJ_TEST_AMR_MOVIE_BYTES, "stco");
    if (!at || nj_test_amr_movie_be32(out + at + 8u) != 2u) return 0;
    nj_test_amr_movie_put32(out + at + 12u, 40u);
    nj_test_amr_movie_put32(out + at + 16u, 40u + 3u * 13u);
    if (audio_only) {
        for (i = 4u; i + 4u <= NJ_TEST_AMR_MOVIE_BYTES; ++i) {
            if (!memcmp(out + i, "trak", 4u)) { memcpy(out + i, "free", 4u); break; }
        }
        if (i + 4u > NJ_TEST_AMR_MOVIE_BYTES) return 0;
    }
    return 1;
}
#endif
