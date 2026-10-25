/*
 * nano_j2me - src/midp/nj_idct.h
 *
 * The integer inverse DCT shared by the H.263 and MPEG-4 Part 2 decoders.
 * 12-bit basis with 1/sqrt(2) folded into the first row/column; two rounded
 * passes; the output is in the pixel domain (no +128 offset).  The error
 * against an exact IDCT is at most about one level.
 */
#ifndef NJ_IDCT_H
#define NJ_IDCT_H

#include "nj2me/nj.h"

static const nj_s2 nj_idct_basis[64] = {
1448,1448,1448,1448,1448,1448,1448,1448,2009,1703,1138,400,-400,-1138,-1703,-2009,
1892,784,-784,-1892,-1892,-784,784,1892,1703,-400,-2009,-1138,1138,2009,400,-1703,
1448,-1448,-1448,1448,1448,-1448,-1448,1448,1138,-2009,400,1703,-1703,-400,2009,-1138,
784,-1892,1892,-784,-784,1892,-1892,784,400,-1138,1703,-2009,2009,-1703,1138,-400 };

static void nj_idct8x8(const nj_s4 *coef, int *out)
{
    nj_s4 tmp[64];
    int u, v, x, y;

    for (v = 0; v < 8; v++) {
        for (x = 0; x < 8; x++) {
            nj_s8 sum = 0;
            for (u = 0; u < 8; u++) {
                sum += (nj_s8)coef[v * 8 + u] * nj_idct_basis[u * 8 + x];
            }
            tmp[v * 8 + x] = (nj_s4)((sum + 2048) >> 12);
        }
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            nj_s8 sum = 0;
            for (v = 0; v < 8; v++) {
                sum += (nj_s8)tmp[v * 8 + x] * nj_idct_basis[v * 8 + y];
            }
            out[y * 8 + x] = (int)((sum + 2048) >> 12);
        }
    }
}

#endif
