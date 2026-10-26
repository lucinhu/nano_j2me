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
    /* Symbols are table indices; the intra path shifts the index left two
     * bits, the inter path inverts it first (cbpy ^ 0x0F). */
    { 3, 4, 0 }, { 5, 5, 1 }, { 4, 5, 2 }, { 9, 4, 3 }, { 3, 5, 4 },
    { 7, 4, 5 }, { 2, 6, 6 }, { 11, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 },
    { 5, 4, 10 }, { 10, 4, 11 }, { 4, 4, 12 }, { 8, 4, 13 }, { 6, 4, 14 },
    { 3, 2, 15 }
};

/* Generic prefix lookup: the intra tables fit in nine bits, the inter MCBPC
 * one needs thirteen. */
static int nj_vlc_lookup_n(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                           const nj_vlc_entry *tab, int count, int maxlen, int *sym)
{
    nj_u4 code = 0;
    int len;

    for (len = 1; len <= maxlen; len++) {
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

static int nj_vlc_lookup(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                         const nj_vlc_entry *tab, int count, int *sym)
{
    return nj_vlc_lookup_n(b, n, pos, tab, count, 9, sym);
}


/* Motion vector VLC, from ff_mvtab: 33 codes; the decoded symbol is the index,
 * and the index (not the codeword) is what the H.263 arithmetic uses. */
static const nj_vlc_entry nj_vlc_mvtab[33] = {
    {  1,  1,  0 }, {  1,  2,  1 }, {  1,  3,  2 }, {  1,  4,  3 },
    {  3,  6,  4 }, {  5,  7,  5 }, {  4,  7,  6 }, {  3,  7,  7 },
    { 11,  9,  8 }, { 10,  9,  9 }, {  9,  9, 10 }, { 17, 10, 11 },
    { 16, 10, 12 }, { 15, 10, 13 }, { 14, 10, 14 }, { 13, 10, 15 },
    { 12, 10, 16 }, { 11, 10, 17 }, { 10, 10, 18 }, {  9, 10, 19 },
    {  8, 10, 20 }, {  7, 10, 21 }, {  6, 10, 22 }, {  5, 10, 23 },
    {  4, 10, 24 }, {  7, 11, 25 }, {  6, 11, 26 }, {  5, 11, 27 },
    {  4, 11, 28 }, {  3, 11, 29 }, {  2, 11, 30 }, {  3, 12, 31 },
    {  2, 12, 32 }
};

/* inter MCBPC: symbols are table indices whose low bits are the flags
 * (bits 0-1 cbpc, bit 2 intra, bit 3 dquant, bit 4 four motion vectors),
 * matching ff_h263_inter_MCBPC_code[28]. */
static const nj_vlc_entry nj_vlc_inter_mcbpc[28] = {
    {  1,  1,  0 }, {  3,  4,  1 }, {  2,  4,  2 }, {  5,  6,  3 },
    {  3,  5,  4 }, {  4,  8,  5 }, {  3,  8,  6 }, {  3,  7,  7 },
    {  3,  3,  8 }, {  7,  7,  9 }, {  6,  7, 10 }, {  5,  9, 11 },
    {  4,  6, 12 }, {  4,  9, 13 }, {  3,  9, 14 }, {  2,  9, 15 },
    {  2,  3, 16 }, {  5,  7, 17 }, {  4,  7, 18 }, {  5,  8, 19 },
    {  1,  9, 20 }, {  0,  0, 21 }, {  0,  0, 22 }, {  0,  0, 23 },
    {  2, 11, 24 }, { 12, 13, 25 }, { 14, 13, 26 }, { 15, 13, 27 }
};

#endif
