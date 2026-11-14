/* SPDX-License-Identifier: MIT
 * Full-pixel comparison against independently decoded test fixtures.  These
 * bounds are a regression contract for these short clips, not a general bound
 * on accumulated MPEG-4 prediction drift.  Normative IDCT accuracy is tested
 * separately in test_m4v_equations.c.
 */
#ifndef NJ_TEST_M4V_REFERENCE_H
#define NJ_TEST_M4V_REFERENCE_H
#include <stdio.h>
#include "nj2me/nj_inflate.h"
#include "nj2me/nj_m4v.h"
#include "m4v_reference_data.h"

static inline int m4v_reference_unpack(const nj_u1 *packed, nj_u4 packed_size,
                                nj_u1 *out, nj_u4 expected_size)
{
    nj_u4 size = expected_size;
    return nj_inflate_zlib(packed, packed_size, out, &size) == NJ_OK && size == expected_size;
}

/* At most one level per YUV sample and plane MSE <= .40.  Unlike Annex A,
 * this compares two valid integer reconstruction schemes on coded frames.
 * The maximum observed plane MSE is .376953 on this AC-prediction clip. */
static inline int m4v_reference_plane(const nj_u1 *actual, const nj_u1 *expected, nj_u4 n)
{
    nj_u4 i;
    nj_u8 square = 0;
    int peak = 0;
    for (i = 0; i < n; i++) {
        int delta = (int)actual[i] - (int)expected[i];
        int magnitude = delta < 0 ? -delta : delta;
        if (magnitude > peak) peak = magnitude;
        square += (nj_u8)(delta * delta);
    }
    if (peak > 1 || square * 5u > (nj_u8)n * 2u) {
        fprintf(stderr, "MPEG4 plane drift: peak=%d MSE=%.6f (%u samples)\n",
                peak, n ? (double)square / n : 0, (unsigned)n);
        return 0;
    }
    return 1;
}

static inline int m4v_reference_clip_rgb(const nj_u1 *actual, nj_u4 n, int frame)
{
    static nj_u1 yuv[M4V_REFERENCE_CLIP_SIZE];
    static int loaded;
    nj_u1 expected[64 * 48 * 2];
    const nj_u1 *plane;
    nj_u4 i, changed = 0;
    int peak = 0;
    if (n != sizeof expected || frame < 0 || frame >= 4) return 0;
    if (!loaded) {
        loaded = m4v_reference_unpack(m4v_reference_clip, sizeof m4v_reference_clip,
                                      yuv, sizeof yuv);
        if (!loaded) return 0;
    }
    plane = yuv + frame * (64 * 48 * 3 / 2);
    nj_m4v_yuv420_to_rgb565(plane, 64, plane + 64 * 48, 32,
                            plane + 64 * 48 * 5 / 4, 32, 64, 48, expected);
    for (i = 0; i < n; i += 2) {
        unsigned a = actual[i] | ((unsigned)actual[i+1] << 8);
        unsigned b = expected[i] | ((unsigned)expected[i+1] << 8);
        int dr = (int)(a >> 11) - (int)(b >> 11);
        int dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63);
        int db = (int)(a & 31) - (int)(b & 31);
        if (dr < 0) dr = -dr;
        if (dg < 0) dg = -dg;
        if (db < 0) db = -db;
        if (dr > peak) peak = dr;
        if (dg > peak) peak = dg;
        if (db > peak) peak = db;
        changed += a != b;
    }
    /* RGB565 conversion quantises an eight-bit level to 5/6 bits.  Allow one
     * code value per channel, and at most 2% of this fixture's pixels changed. */
    if (peak > 1 || changed * 50u > n / 2) {
        fprintf(stderr, "MPEG4 RGB drift frame %d: peak=%d changed=%u/%u\n",
                frame, peak, (unsigned)changed, (unsigned)(n/2));
        return 0;
    }
    return 1;
}
#endif
