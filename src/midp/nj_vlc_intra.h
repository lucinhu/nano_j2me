/*
 * The two H.263/MPEG-4 intra macroblock-header VLCs, shared by the H.263 and
 * MPEG-4 decoders so the tables cannot drift apart.  Entry layout matches the
 * reference implementation: {code, length, symbol}.
 */
#ifndef NJ_VLC_INTRA_H
#define NJ_VLC_INTRA_H

#include "nj2me/nj_types.h"

typedef struct nj_vlc_entry {
    nj_u4 code;
    int   len;
    int   sym;
} nj_vlc_entry;

static const nj_vlc_entry nj_vlc_intra_mcbpc[9] = {
    { 1, 1, 0 }, { 1, 3, 1 }, { 2, 3, 2 }, { 3, 3, 3 }, { 1, 4, 4 },
    { 1, 6, 5 }, { 2, 6, 6 }, { 3, 6, 7 }, { 1, 9, 8 }
};

static const nj_vlc_entry nj_vlc_cbpy[16] = {
    { 3, 2, 0 }, { 3, 4, 15 }, { 4, 4, 3 }, { 5, 4, 5 }, { 6, 4, 1 },
    { 7, 4, 10 }, { 8, 4, 2 }, { 9, 4, 3 }, { 10, 4, 11 }, { 11, 4, 7 },
    { 2, 5, 8 }, { 3, 5, 4 }, { 4, 5, 2 }, { 5, 5, 1 }, { 2, 6, 6 },
    { 3, 6, 9 }
};

/* Longest code is nine bits, so look ahead at most that far. */
static int nj_vlc_lookup(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                         const nj_vlc_entry *tab, int count, int *sym)
{
    nj_u4 code = 0;
    int len;

    for (len = 1; len <= 9; len++) {
        nj_u4 bitpos = *pos + (nj_u4)len - 1u;
        int i;

        if (bitpos >= (nj_u4)n * 8u) {   /* n is a byte count */
            return -1;
        }
        code = (code << 1) | (nj_u4)((b[bitpos >> 3] >> (7u - (bitpos & 7u))) & 1u);
        for (i = 0; i < count; i++) {
            if (tab[i].len == len && tab[i].code == code) {
                *sym = tab[i].sym;
                *pos += (nj_u4)len;
                return 0;
            }
        }
    }
    return -1;
}

#endif
