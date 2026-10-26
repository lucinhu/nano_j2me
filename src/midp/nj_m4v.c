/*
 * nano_j2me - src/midp/nj_m4v.c
 *
 * MPEG-4 Visual framing.  See include/nj2me/nj_m4v.h.
 *
 * The bit order below is the one the reference decoder uses: a random-access
 * flag, the video object type, an optional object layer identifier, the aspect
 * ratio, optional VOL control parameters, then the layer shape.  Only after
 * that comes the marker/time-increment block and, for a rectangular layer, the
 * 13-bit width and height.  Getting this order wrong is subtle: every field is
 * self-consistent, so only a real decoder configuration can tell.
 */
#include "nj2me/nj_m4v.h"
#include "nj_vlc_intra.h"
#include "nj_vlc_inter.h"

#define NJ_M4V_DC_ERR (-0x7fffffff)
#include <stdio.h>
#include <stdlib.h>
#include "nj2me/nj_m4v_tables.h"
#include "nj2me/nj_m4v_escape.h"
#include "nj_idct.h"

/* Defined with the block decoders further down; the escape reader needs them. */
int nj_m4v_qmul(int qscale);
int nj_m4v_qadd(int qscale);

typedef struct m4v_bits {
    const nj_u1 *b;
    nj_u4        n;      /* bytes  */
    nj_u4        pos;    /* bits   */
} m4v_bits;

static int m4v_bit(m4v_bits *s, int *v)
{
    nj_u4 byte;

    if (s->pos >= s->n * 8u) {
        return 0;
    }
    byte = s->b[s->pos >> 3];
    *v = (int)((byte >> (7u - (s->pos & 7u))) & 1u);
    s->pos++;
    return 1;
}

static int m4v_bits_get(m4v_bits *s, int count, nj_u4 *out)
{
    nj_u4 v = 0;
    int   i, bit;

    if (count < 0 || count > 32) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (!m4v_bit(s, &bit)) {
            return 0;
        }
        v = (v << 1) | (nj_u4)bit;
    }
    *out = v;
    return 1;
}

static int m4v_marker(m4v_bits *s)
{
    nj_u4 v;

    return m4v_bits_get(s, 1, &v) && v == 1u;
}

static nj_u4 m4v_log2(nj_u4 v)
{
    nj_u4 r = 0;

    while (v > 1u) {
        v >>= 1;
        r++;
    }
    return r;
}

int nj_m4v_start_code(const nj_u1 *b, nj_u4 n, nj_u4 from, nj_u4 *code, nj_u4 *pos)
{
    nj_u4 i;

    for (i = from; i + 3u < n; i++) {
        if (b[i] == 0x00u && b[i + 1u] == 0x00u && b[i + 2u] == 0x01u) {
            *code = b[i + 3u];
            *pos = i + 4u;
            return 1;
        }
    }
    return 0;
}

int nj_m4v_parse_vol(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_m4v_vol *out)
{
    m4v_bits s;
    nj_u4 vo_type = 0, v = 0;
    int   ra, is_ol, verid = 1, aspect, vol_control = 0, shape = 0, fixed = 0;

    if (b == NULL || out == NULL) {
        return 0;
    }
    s.b = b;
    s.n = n;
    s.pos = pos * 8u;

    if (!m4v_bit(&s, &ra) || !m4v_bits_get(&s, 8, &vo_type)) {
        return 0;
    }
    if (!m4v_bit(&s, &is_ol)) {
        return 0;
    }
    if (is_ol != 0) {
        if (!m4v_bits_get(&s, 4, &v)) {
            return 0;
        }
        verid = (int)v;
        if (!m4v_bits_get(&s, 3, &v)) {
            return 0;
        }
    }
    if (!m4v_bits_get(&s, 4, &v)) {
        return 0;
    }
    aspect = (int)v;
    if (aspect == 15 && (!m4v_bits_get(&s, 16, &v))) {
        return 0;
    }
    if (!m4v_bit(&s, &vol_control)) {
        return 0;
    }
    if (vol_control != 0) {
        nj_u4 chroma, vbv;
        int   low_delay, vbv_flag;
        if (!m4v_bits_get(&s, 2, &chroma) || !m4v_bit(&s, &low_delay)) {
            return 0;
        }
        if (!m4v_bit(&s, &vbv_flag)) {
            return 0;
        }
        vbv = (nj_u4)vbv_flag;
        if (vbv != 0u) {
            int i;
            for (i = 0; i < 3; i++) {
                if (!m4v_bits_get(&s, 15, &v) || !m4v_marker(&s)) {
                    return 0;
                }
            }
            if (!m4v_bits_get(&s, 3, &v) || !m4v_bits_get(&s, 11, &v) ||
                !m4v_marker(&s) || !m4v_bits_get(&s, 15, &v) || !m4v_marker(&s)) {
                return 0;
            }
        }
    }
    if (!m4v_bits_get(&s, 2, &v)) {
        return 0;
    }
    shape = (int)v;
    if (shape == 2 && verid != 1) {
        if (!m4v_bits_get(&s, 4, &v)) {
            return 0;
        }
    }
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bits_get(&s, 16, &v) || v == 0u) {
        return 0;
    }
    out->time_res = v;
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bit(&s, &fixed)) {
        return 0;
    }
    out->has_fixed_rate = fixed;
    out->fixed_inc = 0;
    if (fixed != 0) {
        nj_u4 inc_bits = m4v_log2(out->time_res - 1u) + 1u;
        if (!m4v_bits_get(&s, (int)inc_bits, &v)) {
            return 0;
        }
        out->fixed_inc = v;
    }
    out->width = 0;
    out->height = 0;
    out->shape = shape;
    out->vo_type = vo_type;
    if (shape != 1) {
        if (shape == 0) {
            if (!m4v_marker(&s) || !m4v_bits_get(&s, 13, &v)) {
                return 0;
            }
            out->width = v;
            if (!m4v_marker(&s) || !m4v_bits_get(&s, 13, &v)) {
                return 0;
            }
            out->height = v;
            if (!m4v_marker(&s)) {
                return 0;
            }
        }
    }
    out->interlaced = 0;
    out->obmc_disabled = 1;
    out->sprite_usage = 0;
    out->not_8_bit = 0;
    out->quant_type = 0;
    out->quarter_sample = 0;
    out->quant_precision = 5;
    out->complexity_disabled = 1;
    out->resync_disabled = 1;
    out->data_partitioned = 0;
    out->reversible_vlc = 0;
    out->intra_dc_thr = 0;
    out->cplx_i = 0;
    out->cplx_p = 0;
    out->cplx_b = 0;
    out->flags_known = 0;
    if (shape != 1) {
        int b8 = 0, spr = 0;

        if (!m4v_bit(&s, &b8)) return 0;
        out->interlaced = b8;
        if (!m4v_bit(&s, &b8)) return 0;
        out->obmc_disabled = b8;
        if (verid == 1) {
            if (!m4v_bit(&s, &spr)) return 0;
            out->sprite_usage = spr;
        } else {
            if (!m4v_bits_get(&s, 2, &v)) return 0;
            out->sprite_usage = (int)v;
        }
        if (!m4v_bit(&s, &b8)) return 0;
        out->not_8_bit = b8;
        if (b8 != 0) {
            if (!m4v_bits_get(&s, 4, &v)) return 0;
            out->quant_precision = (int)v;
            if (out->quant_precision < 3 || out->quant_precision > 9) {
                out->quant_precision = 5;
            }
            if (!m4v_bits_get(&s, 4, &v)) return 0;
        }
        if (!m4v_bit(&s, &b8)) return 0;
        out->quant_type = b8;
        if (b8 != 0 && verid != 1) {
            return out->width > 0u && out->height > 0u;
        }
        if (verid >= 2) {
            if (!m4v_bit(&s, &b8)) return 0;
            out->quarter_sample = b8;
        }
        if (!m4v_bit(&s, &b8)) return 0;
        out->complexity_disabled = b8;
        if (b8 == 0) {
            /* Complexity-estimation syntax: the VOP header skips the bits these
             * flags promise - see decode_vop_header()'s "..._trash_i" skip. */
            int method = 0, f = 0, z;

            if (!m4v_bits_get(&s, 2, &v)) return 0;
            method = (int)v;
            if (method < 2) {
                if (!m4v_bit(&s, &f)) return 0;
                if (!f) {
                    for (z = 0; z < 6; z++) {
                        if (!m4v_bit(&s, &b8)) return 0;
                        out->cplx_i += 8 * b8;
                    }
                }
                if (!m4v_bit(&s, &f)) return 0;
                if (!f) {
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_i += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_p += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_p += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_i += 8 * b8;
                }
                if (!m4v_marker(&s)) return 0;
                if (!m4v_bit(&s, &f)) return 0;
                if (!f) {
                    for (z = 0; z < 3; z++) {
                        if (!m4v_bit(&s, &b8)) return 0;
                        out->cplx_i += 8 * b8;
                    }
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_i += 4 * b8;
                }
                if (!m4v_bit(&s, &f)) return 0;
                if (!f) {
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_p += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_p += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_b += 8 * b8;
                    for (z = 0; z < 3; z++) {
                        if (!m4v_bit(&s, &b8)) return 0;
                        out->cplx_p += 8 * b8;
                    }
                }
                if (!m4v_marker(&s)) return 0;
                if (method == 1) {
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_i += 8 * b8;
                    if (!m4v_bit(&s, &b8)) return 0;
                    out->cplx_p += 8 * b8;
                }
            }
        }
        if (!m4v_bit(&s, &b8)) return 0;
        out->resync_disabled = b8;
        if (!m4v_bit(&s, &b8)) return 0;
        out->data_partitioned = b8;
        if (verid >= 2) {
            if (!m4v_bit(&s, &b8)) return 0;
            out->reversible_vlc = b8;
        }
        if (!m4v_bits_get(&s, 3, &v)) return 0;
        out->intra_dc_thr = (int)v;
        out->flags_known = 1;
    }
    return out->width > 0u && out->height > 0u;
}

static int parse_vop_bits_trash(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos, nj_u4 time_res,
                                int cplx_i, int cplx_p, int interlaced,
                                int quant_precision, int shape, nj_m4v_vop *out)
{
    m4v_bits s;
    nj_u4 v, tv = 0;
    int   type, bit, coded = 1, qscale = 0, thr = 0;

    if (b == NULL || bitpos == NULL || out == NULL || time_res < 2u) {
        return 0;
    }
    if (quant_precision < 1 || quant_precision > 12) {
        quant_precision = 5;
    }
    s.b = b;
    s.n = n;
    s.pos = *bitpos;
    if (!m4v_bits_get(&s, 2, &v)) {
        return 0;
    }
    type = (int)v;
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "type", (unsigned)s.pos);
    if (type == 2) {
        return 0;               /* B-VOPs are not part of Simple Profile */
    }
    /* modulo_time_base: unary ones terminated by a zero. */
    if (!m4v_bit(&s, &bit)) {
        return 0;
    }
    while (bit != 0) {
        if (!m4v_bit(&s, &bit)) {
            return 0;
        }
    }
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bits_get(&s, (int)(m4v_log2(time_res - 1u) + 1u), &tv)) {
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "marker1", (unsigned)s.pos);
        return 0;
    }
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bit(&s, &bit)) {
        return 0;
    }
    coded = bit;
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "coded", (unsigned)s.pos);
    /* The order below is the one decode_vop_header() uses, and every field
     * has to be read even when its value is unused: they all sit between
     * vop_coded and the macroblock layer. */
    if (coded && shape != 1 && type == 1) {
        if (!m4v_bit(&s, &bit)) {
            return 0;           /* rounding_type: P-VOPs only */
        }
    }
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "round", (unsigned)s.pos);
    if (coded && shape != 0) {
        if (type != 0) {
            if (!m4v_bits_get(&s, 13, &v) || !m4v_marker(&s) ||
                !m4v_bits_get(&s, 13, &v) || !m4v_marker(&s) ||
                !m4v_bits_get(&s, 13, &v) || !m4v_marker(&s) ||
                !m4v_bits_get(&s, 13, &v)) {
                return 0;       /* width, height, hor_ref, ver_ref */
            }
        }
        if (!m4v_bit(&s, &bit)) {
            return 0;           /* change_CR_disable */
        }
        if (!m4v_bit(&s, &bit)) {
            return 0;
        }
        if (bit != 0 && !m4v_bits_get(&s, 8, &v)) {
            return 0;           /* constant_alpha_value */
        }
    }
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "shape", (unsigned)s.pos);
    if (coded && shape != 1) {
        if (cplx_i > 0) {
            if (s.pos + (nj_u4)cplx_i > n * 8u) {
                return 0;
            }
            s.pos += (nj_u4)cplx_i;     /* complexity-estimation body */
        }
        if (type != 0) {
            if (cplx_p > 0) {
                if (s.pos + (nj_u4)cplx_p > n * 8u) {
                    return 0;
                }
                s.pos += (nj_u4)cplx_p;
            }
        }
        if (!m4v_bits_get(&s, 3, &v)) {
            return 0;           /* intra_dc_vlc_thr */
        }
        thr = (int)v;
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "thr", (unsigned)s.pos);
        if (interlaced != 0) {
            if (!m4v_bit(&s, &bit) || !m4v_bit(&s, &bit)) {
                return 0;       /* top_field_first, alternate_scan */
            }
        }
    }
    if (coded) {
        if (!m4v_bits_get(&s, quant_precision, &v)) {
            return 0;           /* vop_quant */
        }
        qscale = (int)v;
    if (getenv("NJ_M4VTR")) fprintf(stderr, "TR %s pos=%u\n", "quant", (unsigned)s.pos);
        if (type == 1) {
            if (!m4v_bits_get(&s, 3, &v)) {
                return 0;       /* vop_fcode_forward */
            }
            out->f_code = (int)v;
            if (out->f_code < 1) {
                return 0;
            }
        }
        if (qscale < 1) {
            return 0;
        }
    }
    out->type = type;
    out->time_increment = tv;
    out->coded = coded;
    out->qscale = qscale;
    out->dc_vlc_thr = thr;
    if (type != 1) {
        out->f_code = 0;
    }
    *bitpos = s.pos;
    return 1;
}

int nj_m4v_parse_vop_bits(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos, nj_u4 time_res,
                          nj_m4v_vop *out)
{
    return parse_vop_bits_trash(b, n, bitpos, time_res, 0, 0, 0, 5, 0, out);
}

int nj_m4v_parse_vop_bits_vol(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos,
                              const nj_m4v_vol *vol, nj_m4v_vop *out)
{
    if (vol == NULL) {
        return 0;
    }
    return parse_vop_bits_trash(b, n, bitpos, vol->time_res,
                                vol->cplx_i, vol->cplx_p, vol->interlaced,
                                vol->quant_precision, vol->shape, out);
}


int nj_m4v_resync(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mb_total, int mb_width,
                  int *qscale, int *mbnum)
{
    nj_u4 at = *pos;
    int zeros = 0, mb_bits = 1, v, q;

    while (zeros < 32 && at < n * 8u &&
           ((b[at >> 3] >> (7u - (at & 7u))) & 1u) == 0u) {
        at++;
        zeros++;
    }
    if (zeros < 16 || at >= n * 8u) {
        return 0;                       /* not a resync marker */
    }
    at++;                               /* the terminating one bit */

    while ((1 << mb_bits) < mb_total) {
        mb_bits++;
    }
    if (at + (nj_u4)mb_bits + 5u + 1u > n * 8u) {
        return -1;
    }
    v = 0;
    {
        int i;
        for (i = 0; i < mb_bits; i++, at++) {
            v = (v << 1) | (int)((b[at >> 3] >> (7u - (at & 7u))) & 1u);
        }
    }
    if (v <= 0 || v >= mb_total) {
        return -1;
    }
    q = 0;
    {
        int i;
        for (i = 0; i < 5; i++, at++) {         /* quant_precision */
            q = (q << 1) | (int)((b[at >> 3] >> (7u - (at & 7u))) & 1u);
        }
    }
    if (((b[at >> 3] >> (7u - (at & 7u))) & 1u) != 0u) {
        return -2;                      /* header extension: not implemented */
    }
    at++;
    if (q > 0) {
        *qscale = q;
    }
    *pos = at;
    *mbnum = v;
    return 1;
}

int nj_m4v_parse_vop(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_u4 time_res,
                     nj_m4v_vop *out)
{
    nj_u4 bitpos = pos * 8u;
    int r = nj_m4v_parse_vop_bits(b, n, &bitpos, time_res, out);

    if (getenv("NJ_M4VHDR") != NULL) {
        nj_u4 at = bitpos, z;
        unsigned w = 0;
        for (z = 0; z < 32u; z++) {
            nj_u4 q = at + z;
            unsigned bit = (q < n * 8u) ? (unsigned)((b[q >> 3] >> (7u - (q & 7u))) & 1u) : 0u;
            w = (w << 1) | bit;
        }
        fprintf(stderr, "VOPHDR end=%u win=%08x\n", (unsigned)at, w);
    }
    return r;
}


int nj_m4v_find_vop(const nj_u1 *b, nj_u4 n, nj_u4 *pos)
{
    nj_u4 code = 0, at = 0, from = 0;

    while (nj_m4v_start_code(b, n, from, &code, &at)) {
        if (code == NJ_M4V_VOP) {
            *pos = at;
            return 1;
        }
        from = at;
    }
    return 0;
}



static int m4v_rbit(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *v)
{
    if (*pos >= n * 8u) {
        return 0;
    }
    *v = (int)((b[*pos >> 3] >> (7u - (*pos & 7u))) & 1u);
    (*pos)++;
    return 1;
}

static int m4v_rbits(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int count, nj_u4 *out)
{
    nj_u4 v = 0;
    int   i, bit;

    for (i = 0; i < count; i++) {
        if (!m4v_rbit(b, n, pos, &bit)) {
            return 0;
        }
        v = (v << 1) | (nj_u4)bit;
    }
    *out = v;
    return 1;
}

static int m4v_rsbits(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int count, int *out)
{
    nj_u4 v;
    nj_u4 half;

    if (!m4v_rbits(b, n, pos, count, &v)) {
        return 0;
    }
    half = 1u << (count - 1);
    *out = (int)((v & half) ? (nj_s4)(v - half) - (nj_s4)half : (nj_s4)v);
    return 1;
}

int nj_m4v_decode_ac(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *last)
{
    nj_u4 i, v = 0;
    int   k;

    if (b == NULL || pos == NULL || *pos >= n * 8u) {
        return -1;
    }
    for (k = 0; k < NJ_M4V_AC_BITS; k++) {
        v = (v << 1) | (((*pos + (nj_u4)k) < n * 8u)
                        ? (nj_u4)((b[(*pos + (nj_u4)k) >> 3] >>
                                   (7u - ((*pos + (nj_u4)k) & 7u))) & 1u)
                        : 0u);
    }
    i = nj_m4v_ac_lut[v];
    if (i == 0xffffu) {
        /* The escape is the 103rd code word of the intra table, not one of the
         * 102 symbols: bits 0000011.  The LUT covers symbols only, so it is
         * invalid there; report the escape to the caller instead of damage. */
        if ((v >> (NJ_M4V_AC_BITS - 7)) == 3u) {
            *pos += 7u;
            return -2;
        }
        return -1;
    }
    *pos += i >> 8;
    i &= 0xffu;
    if (last != NULL) {
        *last = ((int)i >= NJ_M4V_AC_LAST);
    }
    return (int)i;
}


int nj_m4v_decode_dc(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int luma)
{
    const nj_m4v_dc_entry *tab = luma ? nj_m4v_dc_lum : nj_m4v_dc_chrom;
    int len, i;

    for (len = 1; len <= 13; len++) {
        nj_u4 v = 0;
        int   k;

        if (*pos + (nj_u4)len > n * 8u) {
            return -1;
        }
        for (k = 0; k < len; k++) {
            nj_u4 at = *pos + (nj_u4)k;

            v = (v << 1) | (nj_u4)((b[at >> 3] >> (7u - (at & 7u))) & 1u);
        }
        for (i = 0; i < 13; i++) {
            if (tab[i].len == len && tab[i].code == v) {
                *pos += (nj_u4)len;
                return tab[i].size;
            }
        }
    }
    return -1;
}

int nj_m4v_dc_delta(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int size)
{
    nj_u4 v = 0;
    int   k, half;

    if (size <= 0) {
        return 0;
    }
    if (size > 12 || *pos + (nj_u4)size > n * 8u) {
        return NJ_M4V_DC_ERR;
    }
    for (k = 0; k < size; k++) {
        v = (v << 1) | (nj_u4)((b[(*pos + (nj_u4)k) >> 3] >>
                                (7u - ((*pos + (nj_u4)k) & 7u))) & 1u);
    }
    *pos += (nj_u4)size;
    if (size > 8) {
        if (*pos >= n * 8u) {
            return NJ_M4V_DC_ERR;
        }
        *pos += 1u;             /* marker bit that follows a long DC code */
    }
    half = 1 << (size - 1);
    /* Symbol-first: a set leading bit means positive, a clear one means the
     * value is the remaining bits minus (2^size - 1) - MPEG-4's get_xbits(). */
    if (!((int)v & half)) {
        v -= (nj_u4)((1 << size) - 1);
    }
    return (int)v;
}


int nj_m4v_ac_sym(int sym, int *run, int *level, int *last)
{
    if (sym < 0 || sym >= NJ_M4V_AC_SYMS) {
        return 0;       /* out of range is not a symbol; the escape is its own
                         * code word and never reaches this function */
    }
    if (run != NULL) {
        *run = (int)nj_m4v_ac_run[sym];
    }
    if (level != NULL) {
        *level = (int)nj_m4v_ac_level[sym];
    }
    if (last != NULL) {
        *last = (sym >= NJ_M4V_AC_LAST);
    }
    return 1;
}


/*
 * ISO/IEC 14496-2 7.4.3: the coefficient scan for a non-interlaced picture is
 * the usual zigzag.  nj_m4v_scan_index(k) is the natural index of the k-th
 * coefficient in that order, which is what the AC loop needs to place run/level
 * pairs into an 8x8 block.
 */
static const nj_u1 nj_m4v_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};


int nj_m4v_scan_index_mode(int k, int mode)
{
    if (k < 0 || k > 63) return -1;
    if (mode == 1) {            /* horizontal: row major */
        return k;
    }
    if (mode == 2) {            /* vertical: column major */
        return (k & 7) * 8 + (k >> 3);
    }
    return nj_m4v_scan_index(k);
}
int nj_m4v_scan_index(int k)
{
    if (k < 0 || k >= 64) {
        return -1;
    }
    return (int)nj_m4v_zigzag[k];
}


void nj_m4v_coef_store(nj_s4 *coef, int k, int level)
{
    int at = nj_m4v_scan_index(k);

    if (coef != NULL && at >= 0) {
        coef[at] = (nj_s4)level;
    }
}


int nj_m4v_coef_apply(nj_s4 *coef, int *k, int sym, int *last)
{
    int run, level, is_last;

    if (k == NULL || *k >= 64) {
        return -1;
    }
    if (!nj_m4v_ac_sym(sym, &run, &level, &is_last)) {
        return -2;      /* escape symbol: the caller reads the extended form */
    }
    *k += run;
    if (*k >= 64) {
        return -1;      /* the run walks off the block */
    }
    nj_m4v_coef_store(coef, *k, level);
    (*k)++;
    if (last != NULL) {
        *last = is_last;
    }
    return 0;
}



/* Third escape form (14496-2): after the escape word, bits "11" then
 * last(1) run(6) marker level(12 signed) marker.  The level is scaled with
 * qmul/qadd and clamped to [-2048, 2047], as the reference decoder does.
 * Returns 0, or -3 when the form bits are not "11", or -1 on damage. */
int nj_m4v_escape1(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level);
int nj_m4v_escape2(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level)
{
    nj_u4 save = *pos;
    int   f0 = 0, f1 = 0, sym, r, lv, sign = 0, lt = 0, qm = nj_m4v_qmul(qscale);
    int   idx;

    if (!m4v_rbit(b, n, pos, &f0) || !m4v_rbit(b, n, pos, &f1)) {
        *pos = save;
        return -1;
    }
    if (f0 != 1 || f1 != 0) {
        *pos = save;
        return -3;
    }
    sym = nj_m4v_decode_ac(b, n, pos, &lt);
    if (sym < 0 || qm <= 0) {
        *pos = save;
        return -1;
    }
    r  = (int)nj_m4v_ac_run[sym];
    lv = (int)nj_m4v_ac_level[sym];
    idx = lv / qm;
    if (idx >= 0 && idx < NJ_M4V_MAX_LEVEL + 1) {
        r += (int)(lt ? nj_m4v_max_run_1[idx] : nj_m4v_max_run_0[idx]) + 1;
    }
    if (!m4v_rbit(b, n, pos, &sign)) {
        *pos = save;
        return -1;
    }
    if (sign) {
        lv = -lv;
    }
    if (last != NULL) *last = lt;
    if (run != NULL) *run = r;
    if (level != NULL) *level = lv;
    return 0;
}


/* First escape form: after the escape word the next bit is 0, then the run
 * and level come from the escape half of the same VLC table; the level is
 * corrected by max_level[last][run] * qmul and finished with a sign bit. */
int nj_m4v_escape1(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level)
{
    nj_u4 save = *pos;
    int   form = 0, sym, r, lv, sign = 0, lt = 0, qm = nj_m4v_qmul(qscale);

    if (!m4v_rbit(b, n, pos, &form)) {
        *pos = save;
        return -1;
    }
    if (form != 0) {
        *pos = save;
        return -3;
    }
    sym = nj_m4v_decode_ac(b, n, pos, &lt);
    if (sym < 0) {
        *pos = save;
        return -1;
    }
    if (qm <= 0) {
        return -1;
    }
    r = (int)nj_m4v_ac_run[sym];
    lv = (int)nj_m4v_ac_level[sym];
    if (r >= 0 && r < NJ_M4V_MAX_RUN + 1) {
        lv += (int)(lt ? nj_m4v_max_level_1[r] : nj_m4v_max_level_0[r]) * qm;
    }
    if (!m4v_rbit(b, n, pos, &sign)) {
        *pos = save;
        return -1;
    }
    if (sign) {
        lv = -lv;
    }
    if (last != NULL) *last = lt;
    if (run != NULL) *run = r;
    if (level != NULL) *level = lv;
    return 0;
}

int nj_m4v_escape3(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                       int *last, int *run, int *level)
{
    nj_u4 save = *pos;
    int   l, r6, marker, lv;
    nj_u4 tmp;
    int   qm = nj_m4v_qmul(qscale), qa = nj_m4v_qadd(qscale);

    if (!m4v_rbit(b, n, pos, &l) || !m4v_rbit(b, n, pos, &marker)) {
        *pos = save;
        return -1;
    }
    if (l != 1 || marker != 1) {
        *pos = save;
        return -3;
    }
    if (!m4v_rbit(b, n, pos, &l) ||
        !m4v_rbits(b, n, pos, 6, &tmp)) {
        *pos = save;
        return -1;
    }
    r6 = (int)tmp;
    if (!m4v_rbit(b, n, pos, &marker) || marker != 1 ||
        !m4v_rsbits(b, n, pos, 12, &lv) ||
        !m4v_rbit(b, n, pos, &marker) || marker != 1) {
        *pos = save;
        return -1;
    }
    if (qm <= 0) {
        return -1;
    }
    lv = (lv > 0) ? (lv * qm + qa) : (lv * qm - qa);
    if (lv < -2048) {
        lv = -2048;
    } else if (lv > 2047) {
        lv = 2047;
    }
    if (last != NULL) *last = l;
    if (run != NULL) *run = r6;
    if (level != NULL) *level = lv;
    return 0;
}

int nj_m4v_qmul(int qscale)
{
    return (qscale > 0 && qscale < 32) ? qscale * 2 : 0;
}

int nj_m4v_qadd(int qscale)
{
    return (qscale > 0 && qscale < 32) ? ((qscale - 1) | 1) : 0;
}


static nj_u4 m4v_peek12(const nj_u1 *b, nj_u4 n, nj_u4 pos);

/*
 * Decode one intra AC VLC symbol at *pos: run (as stored in the table, before
 * the "+1 skip the DC slot" rule), level magnitude and the last flag.
 */
static int m4v_vlc_peek(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *level, int *run,
                        int *last)
{
    nj_u4 w = m4v_peek12(b, n, *pos);
    nj_u2 ent = nj_m4v_ac_lut[w];
    int len, sym;
    nj_u4 u = 0;

    if (ent == 0xffffu) {
        return 0;
    }
    len = (int)(ent >> 8);
    sym = (int)(ent & 0xffu);
    if (!m4v_rbits(b, n, pos, (nj_u4)len, &u)) {
        return 0;
    }
    if (level != NULL) *level = (int)nj_m4v_ac_level[sym];
    if (run != NULL) *run = (int)nj_m4v_ac_run[sym];
    if (last != NULL) *last = (sym >= (int)NJ_M4V_AC_LAST);
    return 1;
}

/* Peek up to twelve bits without advancing; bits past the end read as zero. */
static nj_u4 m4v_peek12(const nj_u1 *b, nj_u4 n, nj_u4 pos)
{
    nj_u4 v = 0;
    int i;

    for (i = 0; i < 12; i++) {
        nj_u4 bit = pos + (nj_u4)i;
        v <<= 1;
        if (bit < n * 8u) {
            v |= (nj_u4)((b[bit >> 3] >> (7u - (bit & 7u))) & 1u);
        }
    }
    return v;
}

/*
 * ISO/IEC 14496-2 7.4.3 AC coefficient loop, transcribed from the reference
 * decoder's mpeg4_decode_block() (non-RVLC, non partitioned, mpeg_quant off).
 * Symbols come from the intra RL table; the escape code word 0000011 opens a
 * three form extension (first/second/third escape).  Level sign is a separate
 * bit, and a "last" symbol adds 192 to the coefficient index so that the wrap
 * at i > 62 terminates the block.
 */
int nj_m4v_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef, int k,
                    int qscale)
{
    int i, attempt;

    (void)k;
    if (b == NULL || pos == NULL || coef == NULL || qscale <= 0) {
        return -1;
    }
    i = 0;
    for (attempt = 0; attempt < 64; attempt++) {
        nj_u4 w = m4v_peek12(b, n, *pos);
        if (getenv("NJ_M4VACDBG") != NULL) {
            fprintf(stderr, "ACDBG pos=%u i=%d w=%03x\n", (unsigned)*pos, i, (unsigned)w);
        }
        int run, level = 0, last = 0, lt;
        nj_u4 u = 0;

        if ((w >> 5) == 3u) {
            /* escape word: after the seven bit code, 0 selects the first form,
             * 10 the second and 11 the third (7.4.3.4). */
            nj_u4 f1 = 0, f2 = 0;
            int form;

            if (!m4v_rbits(b, n, pos, 7, &u) ||
                !m4v_rbits(b, n, pos, 1, &f1)) {
                return -1;
            }
            form = (int)f1;
            if (form != 0) {
                if (!m4v_rbits(b, n, pos, 1, &f2)) {
                    return -1;
                }
                form = f2 ? 3 : 2;
            }
            if (form == 1) {            /* first escape */
                int run2 = 0, level2 = 0, last2 = 0, ri;

                if (!m4v_vlc_peek(b, n, pos, &level2, &run2, &last2)) {
                    return -1;
                }
                i += run2 + 1 + (last2 ? 192 : 0);
                ri = (run2)& 63;
                if (ri > 20) {
                    ri = 20;
                }
                level = level2 + (int)(last2 ? nj_m4v_max_level_1[ri]
                                            : nj_m4v_max_level_0[ri]);
            } else if (form == 2) {     /* second escape */
                int run2 = 0, level2 = 0, last2 = 0, li;

                if (!m4v_vlc_peek(b, n, pos, &level2, &run2, &last2)) {
                    return -1;
                }
                level = level2;
                li = level2 < 0 ? 0 : level2;
                if (li > 27) {
                    li = 27;
                }
                i += run2 + 1 + (last2 ? 192 : 0) +
                     (int)(last2 ? nj_m4v_max_run_1[li] : nj_m4v_max_run_0[li]) + 1;
            } else {                    /* third escape */
                nj_u4 lastbit = 0, lb = 0, marker = 0;
                int lv = 0;

                if (!m4v_rbits(b, n, pos, 1, &lastbit) ||
                    !m4v_rbits(b, n, pos, 6, &lb) ||
                    !m4v_rbits(b, n, pos, 1, &marker) || marker != 1u ||
                    !m4v_rsbits(b, n, pos, 12, &lv) ||
                    !m4v_rbits(b, n, pos, 1, &marker) || marker != 1u) {
                    return -1;
                }
                last = (int)lastbit;
                level = lv;
                if (level < -2048) {
                    level = -2048;
                } else if (level > 2047) {
                    level = 2047;
                }
                i += (int)lb + 1;
                if (last) {
                    i += 192;
                }
            }
            {
                nj_u4 sign = 0;
                if (!m4v_rbits(b, n, pos, 1, &sign)) {
                    return -1;
                }
                level = ((nj_s4)level ^ -(nj_s4)sign) + (nj_s4)sign;
            }
        } else {
            nj_u2 ent = nj_m4v_ac_lut[w];
            int len;

            if (ent == 0xffffu) {
                return -1;
            }
            len = (int)(ent >> 8);
            {
                int sym = (int)(ent & 0xffu);

                run = (int)nj_m4v_ac_run[sym];
                level = (int)nj_m4v_ac_level[sym];
                last = (sym >= (int)NJ_M4V_AC_LAST);
            }
            if (!m4v_rbits(b, n, pos, (nj_u4)len, &u)) {
                return -1;
            }
            i += run + 1 + (last ? 192 : 0);
            {
                nj_u4 sign = 0;
                if (!m4v_rbits(b, n, pos, 1, &sign)) {
                    return -1;
                }
                level = ((nj_s4)level ^ -(nj_s4)sign) + (nj_s4)sign;
            }
        }
        if (i > 62) {
            i -= 192;
            if (i < 0 || i > 63) {
                return -1;
            }
            coef[nj_m4v_scan_index(i)] = (nj_s4)level;
            return 0;
        }
        if (i < 0 || i > 63) {
            return -1;
        }
        coef[nj_m4v_scan_index(i)] = (nj_s4)level;
    }
    return -1;
}

int nj_m4v_decode_intra_block_ex(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                                 nj_s4 *coef, int luma, int dc_pred, int qscale,
                                 int coded)
{
    int size, delta, dc, k, i;

    if (coef == NULL || nj_m4v_qmul(qscale) == 0) {
        return -1;
    }
    for (i = 0; i < 64; i++) {
        coef[i] = 0;
    }
    size = nj_m4v_decode_dc(b, n, pos, luma);
    if (size < 0) {
        return -1;
    }
    delta = nj_m4v_dc_delta(b, n, pos, size);
    if (delta == NJ_M4V_DC_ERR) {
        return -1;
    }
    dc = dc_pred + delta;
    coef[0] = (nj_s4)dc;
    if (!coded) {
        return 0;      /* intra blocks always carry DC; AC only when cbp says so */
    }
    k = nj_m4v_ac_block(b, n, pos, coef, 1, qscale);
    if (k == -3) {
        return -3;
    }
    return k < 0 ? -1 : 0;
}

int nj_m4v_decode_intra_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef,
                              int luma, int dc_pred, int qscale)
{
    return nj_m4v_decode_intra_block_ex(b, n, pos, coef, luma, dc_pred, qscale, 1);
}

void nj_m4v_idct(const nj_s4 *coef, int *out)
{
    nj_idct8x8(coef, out);
}


int nj_m4v_decode_intra_pixels(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_u1 *pix,
                               int luma, int dc_pred, int qscale)
{
    nj_s4 coef[64];
    int   out[64];
    int   rc, i;

    if (pix == NULL) {
        return -1;
    }
    rc = nj_m4v_decode_intra_block(b, n, pos, coef, luma, dc_pred, qscale);
    if (rc != 0) {
        return rc;
    }
    nj_m4v_idct(coef, out);
    for (i = 0; i < 64; i++) {
        int v = out[i] + 128;

        pix[i] = (nj_u1)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    return 0;
}


int nj_m4v_frame_macroblocks(nj_u4 width, nj_u4 height, nj_u4 *mbw, nj_u4 *mbh)
{
    if (width == 0u || height == 0u) {
        return -1;
    }
    if (mbw != NULL) {
        *mbw = (width + 15u) / 16u;
    }
    if (mbh != NULL) {
        *mbh = (height + 15u) / 16u;
    }
    return 0;
}


int nj_m4v_block_origin(nj_u4 mbx, nj_u4 mby, int blk, nj_u4 *x, nj_u4 *y)
{
    if (blk < 0 || blk > 5) {
        return -1;
    }
    if (blk < 4) {
        /* Lua: four 8x8 blocks in raster order inside the 16x16 macroblock. */
        if (x != NULL) {
            *x = mbx * 16u + (nj_u4)(blk & 1) * 8u;
        }
        if (y != NULL) {
            *y = mby * 16u + (nj_u4)(blk >> 1) * 8u;
        }
    } else {
        /* 4:2:0: one 8x8 block per chroma plane per macroblock. */
        if (x != NULL) {
            *x = mbx * 8u;
        }
        if (y != NULL) {
            *y = mby * 8u;
        }
    }
    return 0;
}


int nj_m4v_dc_scale(int q, int luma)
{
    if (q < 1 || q > 31) {
        return -1;
    }
    return (int)(luma ? nj_m4v_dc_scale_lum[q] : nj_m4v_dc_scale_chrom[q]);
}


int nj_m4v_plane_size(nj_u4 width, nj_u4 height, nj_u4 *cw, nj_u4 *ch)
{
    if (width == 0u || height == 0u) {
        return -1;
    }
    if (cw != NULL) {
        *cw = (width + 1u) / 2u;
    }
    if (ch != NULL) {
        *ch = (height + 1u) / 2u;
    }
    return 0;
}


int nj_m4v_dc_predict(int prev_dc, int prev_scale, int cur_scale)
{
    nj_s8 v;

    if (prev_scale <= 0 || cur_scale <= 0) {
        return -1;
    }
    v = (nj_s8)prev_dc * (nj_s8)cur_scale;
    v = (v >= 0) ? (v + prev_scale / 2) / prev_scale
                 : -((-v + prev_scale / 2) / prev_scale);
    return (int)v;
}


int nj_m4v_dc_predict_q(int a, int b, int c, int scale, int *dir)
{
    int pred;

    if (scale <= 0) {
        return -1;
    }
    /* B C
     * A X   -- a: left, b: top, c: top-left, in the dequantised domain.
     * Out-of-picture neighbours are 1024, as the reference decoder does. */
    if (a < 0 || b < 0 || c < 0) {
        if (a < 0) a = 1024;
        if (b < 0) b = 1024;
        if (c < 0) c = 1024;
    }
    do {
        int ab = a - b, bc = b - c;

        if (ab < 0) ab = -ab;
        if (bc < 0) bc = -bc;
        if (ab < bc) {
            pred = c;
            if (dir != NULL) *dir = 1;      /* from the top */
            break;
        }
        pred = a;
        if (dir != NULL) *dir = 0;          /* from the left */
    } while (0);
    return (pred + scale / 2) / scale;
}

int nj_m4v_dc_coef(int level, int scale)
{
    if (scale <= 0) {
        return level;
    }
    return level * scale;
}

int nj_m4v_dc_level(int delta, int pred_dequant, int scale, int *stored)
{
    int pred_q, ret, val;

    if (scale <= 0) {
        return -1;
    }
    pred_q = (pred_dequant + scale / 2) / scale;
    ret = delta + pred_q;
    val = ret * scale;
    if (val < 0) {
        val = 0;
    } else if (val > 2047) {
        val = 2047;
    }
    if (stored != NULL) {
        *stored = val;
    }
    return ret;
}


void nj_m4v_dc_avail(int bx, int by, int *has_a, int *has_b, int *has_c)
{
    if (has_a != NULL) *has_a = (bx > 0);
    if (has_b != NULL) *has_b = (by > 0);
    if (has_c != NULL) *has_c = (bx > 0 && by > 0);
}


int nj_m4v_block_grid(int blk, int mbx, int mby, int *gx, int *gy)
{
    int x, y;

    if (blk < 0 || blk > 5 || mbx < 0 || mby < 0) {
        return -1;
    }
    if (blk < 4) {
        /* Luma blocks sit on the 8x8 grid inside the 16x16 macroblock. */
        x = mbx * 2 + (blk & 1);
        y = mby * 2 + (blk >> 1);
    } else {
        /* Cb/Cr have their own half-resolution grid: one block per macroblock. */
        x = mbx;
        y = mby;
    }
    if (gx != NULL) *gx = x;
    if (gy != NULL) *gy = y;
    return 0;
}


int nj_m4v_dc_block(nj_s2 *grid, int wrap, int bx, int by, int delta, int scale)
{
    int a = 1024, b = 1024, c = 1024, pred_q, ret, val, nb = 0;

    if (grid == NULL || wrap <= 0 || bx < 0 || by < 0 || scale <= 0) {
        return -1;
    }
    if (bx > 0) {
        a = grid[by * wrap + bx - 1];
    }
    if (by > 0) {
        b = grid[(by - 1) * wrap + bx];
    }
    if (bx > 0 && by > 0) {
        c = grid[(by - 1) * wrap + bx - 1];
    }
    pred_q = nj_m4v_dc_predict_q(a, b, c, scale, &nb);
    if (pred_q < 0) {
        return -1;
    }
    ret = nj_m4v_dc_level(delta, pred_q * scale, scale, &val);
    if (ret < 0) {
        return -1;
    }
    grid[by * wrap + bx] = (nj_s2)val;
    return ret;
}

static int m4v_clip8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void nj_m4v_yuv420_to_rgb565(const nj_u1 *yp, int ys, const nj_u1 *up, int us,
                             const nj_u1 *vp, int vs, int w, int h, nj_u1 *out)
{
    int x, y;

    for (y = 0; y < h; y++) {
        const nj_u1 *yr = yp + (nj_size)y * (nj_size)ys;
        const nj_u1 *ur = up + (nj_size)(y >> 1) * (nj_size)us;
        const nj_u1 *vr = vp + (nj_size)(y >> 1) * (nj_size)vs;
        nj_u1 *orow = out + (nj_size)y * (nj_size)w * 2u;

        for (x = 0; x < w; x++) {
            int c = (int)yr[x] - 16;
            int d = (int)ur[x >> 1] - 128;
            int e = (int)vr[x >> 1] - 128;
            int r = m4v_clip8((298 * c + 409 * e + 128) >> 8);
            int g = m4v_clip8((298 * c - 100 * d - 208 * e + 128) >> 8);
            int b = m4v_clip8((298 * c + 516 * d + 128) >> 8);
            nj_u2 px = (nj_u2)(((nj_u2)(r >> 3) << 11) |
                               ((nj_u2)(g >> 2) << 5) | (nj_u2)(b >> 3));

            orow[2 * x] = (nj_u1)(px & 0xFFu);
            orow[2 * x + 1] = (nj_u1)(px >> 8);
        }
    }
}


static int m4v_intra_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                             nj_s2 *dc_c2, int stride_c2,
                             int scale, int *qscale, int *ac_bits,
                             nj_u1 *yp, int ys, nj_u1 *up, int us,
                             nj_u1 *vp, int vs, int cbp, int cbpc, int dq);

int nj_m4v_decode_ivop(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                       const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                       nj_s4 *grids, nj_u1 *yp, int ys,
                       nj_u1 *up, int us, nj_u1 *vp, int vs)
{
    nj_u4 mbw = 0, mbh = 0, cw = 0, ch = 0;
    nj_s2 *gl, *gc, *gcr;
    int    glw, gcw, lscale, cscale, i;
    nj_u4  mbx, mby;

    if (b == NULL || pos == NULL || vol == NULL || vop == NULL || grids == NULL ||
        yp == NULL || up == NULL || vp == NULL || ys <= 0 || us <= 0 || vs <= 0) {
        return -1;
    }
    if (!vop->coded) {
        return 0;
    }
    if (vop->type != 0) {
        return -1;                  /* P-VOPs are not implemented yet */
    }
    if (nj_m4v_frame_macroblocks(vol->width, vol->height, &mbw, &mbh) != 0 ||
        nj_m4v_plane_size(vol->width, vol->height, &cw, &ch) != 0) {
        return -1;
    }
    lscale = nj_m4v_dc_scale(vop->qscale, 1);
    cscale = nj_m4v_dc_scale(vop->qscale, 0);
    if (lscale <= 0 || cscale <= 0) {
        return -1;
    }
    glw = (int)mbw * 2;
    gcw = (int)mbw;
    gl  = (nj_s2 *)(void *)grids;
    gc  = gl + (nj_size)glw * (nj_size)(mbh * 2u);
    gcr = gc + (nj_size)gcw * (nj_size)mbh;
    for (i = 0; i < glw * (int)(mbh * 2u); i++) {
        gl[i] = 1024;
    }
    for (i = 0; i < gcw * (int)mbh; i++) {
        gc[i] = 1024;
        gcr[i] = 1024;
    }

    {
    int qsc = (int)vop->qscale;   /* dquant adjusts this for the whole rest of the frame */
    for (mby = 0; mby < mbh; mby++) {
        for (mbx = 0; mbx < mbw; mbx++) {
            int ac[6];
            int   rc;
            rc = nj_m4v_decode_intra_mb_pix(b, n, pos, (int)mbx, (int)mby,
                                                  gl, glw, gc, gcw, gcr, gcw,
                                                  lscale, &qsc, ac,
                                                  yp, ys, up, us, vp, vs);
            if (rc != 0) {
                return rc;
            }
        }
    }
    }
    return 0;
}

/* Reads the parts of a P-VOP macroblock header that precede the block data.
 * Returns 0 for an intra macroblock, 1 for a skipped one and -2 when the
 * macroblock is inter coded (motion compensation is not implemented yet). */
static int m4v_mid_pred(int a, int b, int c)
{
    int lo = a < b ? a : b;
    int hi = a > b ? a : b;
    if (c < lo) {
        lo = c;
    } else if (c > hi) {
        hi = c;
    }
    return a + b + c - lo - hi;
}

/* Decodes one motion vector component: H.263/MPEG-4 share the table and the
 * arithmetic (ituh263dec.c ff_h263_decode_motion).  Returns 0xffff on a
 * broken stream, which callers treat as an error. */
static int m4v_mv_component(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int pred, int f_code)
{
    int sym = 0, sign, shift, val;

    if (nj_vlc_lookup_n(b, n, pos, nj_vlc_mvtab, 33, 12, &sym) != 0) {
        return 0xffff;
    }
    if (sym == 0) {
        return pred;
    }
    {
        nj_u4 bp = *pos;

        if (bp >= n * 8u) {
            return 0xffff;
        }
        sign = (int)((b[bp >> 3] >> (7u - (bp & 7u))) & 1u);
        *pos = bp + 1u;
    }
    shift = f_code - 1;
    val = sym;
    if (shift > 0) {
        nj_u4 extra = 0;
        int   k;

        if (*pos + (nj_u4)shift > n * 8u) {
            return 0xffff;
        }
        for (k = 0; k < shift; k++) {
            nj_u4 bp = *pos;

            extra = (extra << 1) | ((b[bp >> 3] >> (7u - (bp & 7u))) & 1u);
            *pos = bp + 1u;
        }
        val = (val - 1) << shift;
        val |= (int)extra;
        val++;
    }
    if (sign != 0) {
        val = -val;
    }
    val += pred;
    {
        int bits = 5 + f_code;
        int mask = (1 << bits) - 1;

        val &= mask;
        if ((val & (1 << (bits - 1))) != 0) {
            val -= (1 << bits);
        }
    }
    return val;
}

/* Median predictor of the 16x16 motion vector (h263.c ff_h263_pred_motion,
 * block 0).  mvx/mvy hold one entry per 8x8 block with a one-entry border. */
static void m4v_mv_pred(const nj_s2 *mvx, const nj_s2 *mvy, nj_u4 mbx, nj_u4 mby,
                        int wrap, int pad, int *px, int *py)
{
    nj_size at = (nj_size)pad + (nj_size)mby * 2 * (nj_size)wrap + (nj_size)mbx * 2;

    if (mby == 0u) {
        if (mbx == 0u) {
            *px = 0;
            *py = 0;
        } else {
            *px = mvx[at - 1];
            *py = mvy[at - 1];
        }
        return;
    }
    *px = m4v_mid_pred(mvx[at - 1], mvx[at - wrap], mvx[at + 2 - wrap]);
    *py = m4v_mid_pred(mvy[at - 1], mvy[at - wrap], mvy[at + 2 - wrap]);
}

/* ---------- inter AC: ff_h263_rl_inter (n=102, last from index 58) ---------- */

#define NJ_M4V_IAC_BITS 12

/* Returns the symbol index, -2 for the escape word, -1 on damage. */
static int m4v_inter_decode_ac(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *last)
{
    nj_u4 v = 0;
    int   i, k;

    for (k = 0; k < NJ_M4V_IAC_BITS; k++) {
        v = (v << 1) | ((*pos + (nj_u4)k < n * 8u)
                        ? (nj_u4)((b[(*pos + (nj_u4)k) >> 3] >>
                                   (7u - ((*pos + (nj_u4)k) & 7u))) & 1u)
                        : 0u);
    }
    i = (int)nj_vlc_inter_lut[v];
    if (i == 0xffffu) {
        if ((v >> (NJ_M4V_IAC_BITS - 7)) == 3u) {
            *pos += 7u;
            return -2;
        }
        return -1;
    }
    *pos += (nj_u4)(i >> 8);
    i &= 0xffu;
    if (last != NULL) {
        *last = (i >= 58);
    }
    return i;
}

/* Inter residual, ported from mpeg4_decode_block()'s non-RVLC path: i starts
 * at -1, the run value carries "last" as +192, and the three escape forms sit
 * behind the same escape VLC. */
static int m4v_inter_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                              nj_s4 *coef, nj_u4 mbx, nj_u4 mby, int blk)
{
    int i = -1;
    int qm = qscale << 1;
    int qa = (qscale - 1) | 1;

    (void)mbx; (void)mby; (void)blk;
    for (;;) {
        int last = 0, level = 0, run = 0, sign = 0;
        int sym = m4v_inter_decode_ac(b, n, pos, &last);

        if (sym == -2) {
            int c1 = 0, c2 = 0;

            if (!m4v_rbit(b, n, pos, &c1)) {
                return -1;
            }
            if (c1 == 0) {
                /* first escape: one symbol, run/level fixed up from max_level */
                int lt2 = 0, s2 = m4v_inter_decode_ac(b, n, pos, &lt2);

                if (s2 < 0) {
                    return -1;
                }
                run = nj_vlc_inter_run[s2] + 1;
                if (lt2) {
                    run += 192;
                }
                i += run;
                level = nj_vlc_inter_level[s2] * qm + qa;
                level += (int)nj_vlc_inter_max_level[lt2][(run - 1) & 63] * qm;
                if (!m4v_rbit(b, n, pos, &sign)) {
                    return -1;
                }
                if (sign) {
                    level = -level;
                }
                last = lt2;
            } else if (!m4v_rbit(b, n, pos, &c2)) {
                return -1;
            } else if (c2 == 0) {
                /* second escape: run extended by the max_run of the level */
                int lt2 = 0, s2 = m4v_inter_decode_ac(b, n, pos, &lt2);

                if (s2 < 0) {
                    return -1;
                }
                run = nj_vlc_inter_run[s2] + 1;
                if (lt2) {
                    run += 192;
                }
                level = nj_vlc_inter_level[s2] * qm + qa;
                i += run + (int)nj_vlc_inter_max_run[lt2][(level / qm) < 0 ? 0 : ((level / qm) > 63 ? 63 : (level / qm))] + 1;
                if (!m4v_rbit(b, n, pos, &sign)) {
                    return -1;
                }
                if (sign) {
                    level = -level;
                }
                last = lt2;
            } else {
                /* third escape: explicit last/run/level, already dequantised */
                int r6 = 0, lvl = 0, marker = 0;

                if (!m4v_rbit(b, n, pos, &last) ||
                    !m4v_rbits(b, n, pos, 6, &r6) ||
                    !m4v_rbit(b, n, pos, &marker) ||
                    !m4v_rsbits(b, n, pos, 12, &lvl) ||
                    !m4v_rbit(b, n, pos, &marker)) {
                    return -1;
                }
                level = (lvl > 0) ? lvl * qm + qa : lvl * qm - qa;
                if (level > 2047) {
                    level = 2047;
                } else if (level < -2048) {
                    level = -2048;
                }
                i += (int)r6 + 1;
                if (last) {
                    i += 192;
                }
            }
        } else if (sym < 0) {
            if (getenv("NJ_M4VIAC")) {
                int k;
                unsigned w = 0;

                for (k = 0; k < 16; k++) {
                    w = (w << 1) | ((*pos + (nj_u4)k < n * 8u)
                                    ? (unsigned)((b[(*pos + (nj_u4)k) >> 3] >>
                                                  (7u - ((*pos + (nj_u4)k) & 7u))) & 1u)
                                    : 0u);
                }
                fprintf(stderr, "IACFAIL %ux%u b%d at=%u win=%04x\n",
                        (unsigned)mbx, (unsigned)mby, blk, (unsigned)*pos, w);
            }
            return -1;
        } else {
            run = nj_vlc_inter_run[sym] + 1;
            if (last) {
                run += 192;
            }
            i += run;
            level = nj_vlc_inter_level[sym] * qm + qa;
            if (!m4v_rbit(b, n, pos, &sign)) {
                return -1;
            }
            if (sign) {
                level = -level;
            }
        }
        if (i > 62) {
            i -= 192;
            if ((i & ~63) != 0) {
                return -1;
            }
            coef[nj_m4v_scan_index(i)] = (nj_s4)level;
            break;
        }
        if (i < 0) {
            i = 0;
        }
        coef[nj_m4v_scan_index(i)] = (nj_s4)level;
    }
    return 0;
}

/* Half-pel motion compensation of one 8x8 block with edge emulation: samples
 * outside the frame repeat the edge pixel. */
static void m4v_mc_block(int sx, int sy, int mx, int my, const nj_u1 *pl,
                         int stride, int fw, int fh, nj_u1 *out)
{
    int x0 = sx + (mx >> 1);
    int y0 = sy + (my >> 1);
    int fx = mx & 1;
    int fy = my & 1;
    int y;

    for (y = 0; y < 8; y++) {
        int yy = y0 + y;
        int yy2 = y0 + y + fy;
        int x;

        if (yy < 0) yy = 0; else if (yy >= fh) yy = fh - 1;
        if (yy2 < 0) yy2 = 0; else if (yy2 >= fh) yy2 = fh - 1;
        for (x = 0; x < 8; x++) {
            int xx = x0 + x;
            int xx2 = x0 + x + fx;
            const nj_u1 *r0 = pl + (nj_size)yy * (nj_size)stride;
            const nj_u1 *r1 = pl + (nj_size)yy2 * (nj_size)stride;
            int a, v;

            if (xx < 0) xx = 0; else if (xx >= fw) xx = fw - 1;
            if (xx2 < 0) xx2 = 0; else if (xx2 >= fw) xx2 = fw - 1;
            a = r0[xx];
            if (fx != 0 && fy != 0) {
                v = (a + r0[xx2] + r1[xx] + r1[xx2] + 2) >> 2;
            } else if (fx != 0) {
                v = (a + r0[xx2] + 1) >> 1;
            } else if (fy != 0) {
                v = (a + r1[xx] + 1) >> 1;
            } else {
                v = a;
            }
            out[y * 8 + x] = (nj_u1)v;
        }
    }
}

/* One inter macroblock: motion compensated prediction plus the coded
 * residual.  Luma blocks use the 16x16 vector, chroma uses half of it. */
static int m4v_inter_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             const nj_u1 *py, int pys, const nj_u1 *pu, int pus,
                             const nj_u1 *pv, int pvs,
                             nj_u1 *yp, int ys, nj_u1 *up, int us,
                             nj_u1 *vp, int vs,
                             int fw, int fh, int cw, int ch,
                             int cbp, int qscale, int mx, int my)
{
    int blk;

    for (blk = 0; blk < 6; blk++) {
        nj_u1 pred[64];
        nj_s4 coef[64];
        int   coded = (cbp >> (5 - blk)) & 1;
        int   k;

        for (k = 0; k < 64; k++) {
            coef[k] = 0;
        }
        if (blk < 4) {
            int bx = (blk & 1) * 8;
            int by = (blk >> 1) * 8;

            m4v_mc_block(mbx * 16 + bx, mby * 16 + by, mx, my, py, pys, fw, fh, pred);
            if (coded != 0) {
                int rc = m4v_inter_ac_block(b, n, pos, qscale, coef,
                                            (nj_u4)mbx, (nj_u4)mby, blk);

                if (rc != 0) {
                    return rc;
                }
            }
            for (k = 0; k < 64; k++) {
                int v = (int)pred[k] + (int)coef[k];

                if (v < 0) v = 0; else if (v > 255) v = 255;
                yp[(nj_size)(mby * 16 + by + k / 8) * (nj_size)ys +
                   (nj_size)(mbx * 16 + bx + k % 8)] = (nj_u1)v;
            }
        } else {
            int is_v = (blk == 5);
            const nj_u1 *pp = is_v ? pv : pu;
            int         ps = is_v ? pvs : pus;
            nj_u1      *op = is_v ? vp : up;
            int         os = is_v ? vs : us;
            int         cmx = mx >> 1;
            int         cmy = my >> 1;

            m4v_mc_block(mbx * 8, mby * 8, cmx, cmy, pp, ps, cw, ch, pred);
            if (coded != 0) {
                int rc = m4v_inter_ac_block(b, n, pos, qscale, coef,
                                            (nj_u4)mbx, (nj_u4)mby, blk);

                if (rc != 0) {
                    return rc;
                }
            }
            for (k = 0; k < 64; k++) {
                int v = (int)pred[k] + (int)coef[k];

                if (v < 0) v = 0; else if (v > 255) v = 255;
                op[(nj_size)(mby * 8 + k / 8) * (nj_size)os +
                   (nj_size)(mbx * 8 + k % 8)] = (nj_u1)v;
            }
        }
    }
    return 0;
}

/* Macroblock header of a P picture (mpeg4_decode_mb P branch): not_coded,
 * MCBPC, cbpy, dquant, interlaced_dct and the motion vector. */
static int m4v_pvop_mb_header(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                              const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                              nj_u4 mbx, nj_u4 mby, nj_s2 *mvx, nj_s2 *mvy,
                              int wrap, int pad, int *qscale,
                              int *cbp, int *dq, int *cbpc_out,
                              int *mx_out, int *my_out)
{
    static const int quant_tab[4] = { -1, -2, 1, 2 };
    int cbpc = 0, cbpy = 0, skip = 0;

    if (*pos + 1u > n * 8u) {
        return -1;
    }
    skip = (int)((b[*pos >> 3] >> (7u - (*pos & 7u))) & 1u);
    *pos += 1u;
    if (skip != 0) {
        return 1;
    }
    do {
        if (nj_vlc_lookup_n(b, n, pos, nj_vlc_inter_mcbpc, 28, 13, &cbpc) != 0) {
            return -1;
        }
    } while (cbpc == 20);
    *dq = (cbpc & 8) != 0;
    if (cbpc_out != NULL) {
        *cbpc_out = cbpc;
    }
    if ((cbpc & 4) != 0) {          /* intra macroblock inside a P picture */
        if (*pos + 1u > n * 8u) {
            return -1;
        }
        *pos += 1u;                 /* ac_pred */
        if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
            return -1;
        }
        *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
        return 0;                   /* the body reads dquant, then the blocks */
    }
    if ((cbpc & 16) != 0) {
        return -2;                  /* four motion vectors: not implemented */
    }
    if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
        return -1;
    }
    cbpy ^= 0x0f;
    *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
    if (*dq) {
        int d = 0;

        if (*pos + 2u > n * 8u) {
            return -1;
        }
        d = (int)((b[*pos >> 3] >> (6u - (*pos & 7u))) & 3u);
        *pos += 2u;
        *qscale += quant_tab[d];
        if (*qscale < 1) {
            *qscale = 1;
        } else if (*qscale > 31) {
            *qscale = 31;
        }
    }
    if (vol->interlaced != 0 && *cbp != 0) {
        if (*pos + 1u > n * 8u) {
            return -1;
        }
        *pos += 1u;                 /* interlaced_dct */
    }
    {
        int px = 0, py = 0, mx, my;

        m4v_mv_pred(mvx, mvy, mbx, mby, wrap, pad, &px, &py);
        mx = m4v_mv_component(b, n, pos, px, vop->f_code);
        if (mx == 0xffff) {
            return -1;
        }
        my = m4v_mv_component(b, n, pos, py, vop->f_code);
        if (my == 0xffff) {
            return -1;
        }
        {
            nj_size at = (nj_size)pad + (nj_size)mby * 2 * (nj_size)wrap +
                         (nj_size)mbx * 2;

            mvx[at] = (nj_s2)mx;
            mvy[at] = (nj_s2)my;
        }
        if (mx_out != NULL) *mx_out = mx;
        if (my_out != NULL) *my_out = my;
    }
    return 2;                       /* inter macroblock */
}

static int m4v_copy_skipped_mb(int mbx, int mby,
                               nj_u1 *yp, int ys, nj_u1 *up, int us,
                               nj_u1 *vp, int vs,
                               const nj_u1 *py, int pys, const nj_u1 *pu, int pus,
                               const nj_u1 *pv, int pvs)
{
    int r, c;

    if (yp == NULL || py == NULL || pys <= 0 || ys <= 0) {
        return -1;
    }
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            int x = mbx * 16 + c, y = mby * 16 + r;
            yp[(nj_size)y * (nj_size)ys + (nj_size)x] =
                py[(nj_size)y * (nj_size)pys + (nj_size)x];
        }
    }
    if (up != NULL && pu != NULL && us > 0 && pus > 0) {
        for (r = 0; r < 8; r++) {
            for (c = 0; c < 8; c++) {
                int x = mbx * 8 + c, y = mby * 8 + r;
                up[(nj_size)y * (nj_size)us + (nj_size)x] =
                    pu[(nj_size)y * (nj_size)pus + (nj_size)x];
            }
        }
    }
    if (vp != NULL && pv != NULL && vs > 0 && pvs > 0) {
        for (r = 0; r < 8; r++) {
            for (c = 0; c < 8; c++) {
                int x = mbx * 8 + c, y = mby * 8 + r;
                vp[(nj_size)y * (nj_size)vs + (nj_size)x] =
                    pv[(nj_size)y * (nj_size)pvs + (nj_size)x];
            }
        }
    }
    return 0;
}

int nj_m4v_decode_pvop(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                       const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                       nj_s4 *grids, nj_u1 *yp, int ys, nj_u1 *up, int us,
                       nj_u1 *vp, int vs,
                       const nj_u1 *py, int pys, const nj_u1 *pu, int pus,
                       const nj_u1 *pv, int pvs)
{
    nj_u4 mbw = 0, mbh = 0, cw = 0, ch = 0;
    nj_s2 *gl, *gc, *gcr;
    int    glw, gcw, lscale, cscale, i, qsc;
    int    rc = 0;
    nj_u4  mbx, mby;

    if (b == NULL || pos == NULL || vol == NULL || vop == NULL || grids == NULL ||
        yp == NULL || up == NULL || vp == NULL || ys <= 0 || us <= 0 || vs <= 0) {
        return -1;
    }
    if (!vop->coded) {
        return 0;
    }
    if (vop->type != 1) {
        return -1;
    }
    if (vol->interlaced) {
        return -3;              /* interlaced P-VOPs are not supported yet */
    }
    if (nj_m4v_frame_macroblocks(vol->width, vol->height, &mbw, &mbh) != 0 ||
        nj_m4v_plane_size(vol->width, vol->height, &cw, &ch) != 0) {
        return -1;
    }
    lscale = nj_m4v_dc_scale(vop->qscale, 1);
    cscale = nj_m4v_dc_scale(vop->qscale, 0);
    if (lscale <= 0 || cscale <= 0) {
        return -1;
    }
    glw = (int)mbw * 2;
    gcw = (int)mbw;
    gl  = (nj_s2 *)(void *)grids;
    gc  = gl + (nj_size)glw * (nj_size)(mbh * 2u);
    gcr = gc + (nj_size)gcw * (nj_size)mbh;
    /* The DC predictors are NOT reset here: an intra macroblock in a P-VOP
     * predicts from the frame that is already reconstructed, so the caller
     * hands us the grids that nj_m4v_decode_ivop left behind. */
    (void)i;
    qsc = (int)vop->qscale;
    {
        int wrap = (int)mbw * 2 + 1;
        int pad  = wrap + 2;
        nj_size mvn = (nj_size)pad + (nj_size)(mbh * 2u + 2u) * (nj_size)wrap;
        nj_s2 *mvx = (nj_s2 *)calloc((size_t)(mvn * 2), sizeof(nj_s2));
        nj_s2 *mvy = mvx != NULL ? mvx + mvn : NULL;

        if (mvx == NULL || mvy == NULL) {
            free(mvx);
            return -1;
        }
        for (mby = 0; mby < mbh; mby++) {
            for (mbx = 0; mbx < mbw; mbx++) {
                nj_s4 ac[6];
                int cbp = 0, dq = 0, cbpc = 0, mx = 0, my = 0;

                {
                    nj_u4 mbin = *pos;

                rc = m4v_pvop_mb_header(b, n, pos, vol, vop, mbx, mby, mvx, mvy,
                                        wrap, pad, &qsc, &cbp, &dq, &cbpc, &mx, &my);
                if (getenv("NJ_M4VPOS")) {
                    fprintf(stderr, "MB %ux%u in=%u rc=%d hdr_end=%u cbp=0x%02x mv=%d,%d\n",
                            (unsigned)mbx, (unsigned)mby, (unsigned)mbin, rc,
                            (unsigned)*pos, (unsigned)cbp, mx, my);
                }
                }
                if (rc == 1) {
                    rc = m4v_copy_skipped_mb((int)mbx, (int)mby, yp, ys, up, us,
                                             vp, vs, py, pys, pu, pus, pv, pvs);
                    if (rc != 0) {
                        break;
                    }
                    continue;
                }
                if (rc == 0) {
                    rc = m4v_intra_mb_body(b, n, pos, (int)mbx, (int)mby,
                                           gl, glw, gc, gcw, gcr, gcw,
                                           lscale, &qsc, ac, yp, ys, up, us, vp, vs,
                                           cbp, cbpc, dq);
                    if (rc != 0) {
                        break;
                    }
                    continue;
                }
                if (rc != 2) {
                    break;
                }
                /* Inter macroblock: motion compensated prediction plus the
                 * coded residual. */
                {
                    int fw = (int)mbw * 16;
                    int fh = (int)mbh * 16;
                    int cw2 = (int)mbw * 8;
                    int ch2 = (int)mbh * 8;

                    rc = m4v_inter_mb_body(b, n, pos, (int)mbx, (int)mby,
                                           py, pys, pu, pus, pv, pvs,
                                           yp, ys, up, us, vp, vs,
                                           fw, fh, cw2, ch2, cbp, qsc, mx, my);
                    if (rc != 0) {
                        break;
                    }
                    continue;
                }
            }
            if (mby < mbh && rc != 0) {
                break;
            }
        }
        free(mvx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

int nj_m4v_mb_intra_header(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *cbp, int *dq)
{
    int mcbpc = 0, cbpy = 0;

    for (;;) {
        if (nj_vlc_lookup(b, n, pos, nj_vlc_intra_mcbpc, 9, &mcbpc) != 0) {
            return -1;
        }
        if (mcbpc != 8) {
            break;
        }
    }
    if (dq != NULL) *dq = (mcbpc & 4) != 0;
    /* ac_pred comes between the MCBPC and the CBPY (7.4.3.5). */
    if (*pos + 1u > n * 8u) {
        return -1;
    }
    *pos += 1u;
    if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
        return -1;
    }
    if ((mcbpc & 4) != 0) {
        if (*pos + 2u > n * 8u) {
            return -1;
        }
        *pos += 2u;             /* dquant follows CBPY */
    }
    if (cbp != NULL) *cbp = ((mcbpc & 3) | (cbpy << 2)) & 0x3f;
    return 0;
}


int nj_m4v_cbp_has_ac(int cbp, int block)
{
    if (block < 0 || block > 5) {
        return 0;
    }
    return (cbp >> (5 - block)) & 1;
}


int nj_m4v_dc_neighbour(const nj_s2 *grid, int stride, int gx, int gy, int which)
{
    int x = gx, y = gy;

    if (grid == NULL || stride <= 0) {
        return 1024;
    }
    if (which == 0) {
        x = gx - 1;                 /* left  */
    } else if (which == 1) {
        y = gy - 1;                 /* top   */
    } else {
        x = gx - 1;                 /* top-left */
        y = gy - 1;
    }
    if (x < 0 || y < 0) {
        return 1024;                /* outside the picture predicts mid-grey */
    }
    return (int)grid[(nj_size)y * (nj_size)stride + (nj_size)x];
}


static int nj_m4v_mb_core(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *cbpc, int *dq)
{
    int mcbpc = 0;

    for (;;) {
        if (nj_vlc_lookup(b, n, pos, nj_vlc_intra_mcbpc, 9, &mcbpc) != 0) {
            return -1;
        }
        if (mcbpc != 8) {
            break;
        }
    }
    if (cbpc != NULL) *cbpc = mcbpc & 3;
    if (dq != NULL) *dq = (mcbpc & 4) != 0;
    return 0;
}

static int m4v_read_cbp(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int cbpc, int dq, int *cbp,
                        int *acp)
{
    nj_u4 bp = *pos;
    int v = 0, cbpy = 0;

    if (bp < n * 8u) {
        *acp = (int)((b[bp >> 3] >> (7u - (bp & 7u))) & 1u);
        *pos = bp + 1u;
    }
    if (getenv("NJ_M4VCBPY")) {
        nj_u4 at = *pos; int z; unsigned win = 0;
        for (z = 0; z < 12; z++) {
            nj_u4 q = at + (nj_u4)z;
            unsigned bit = 0;
            if (q < n * 8u) bit = (b[q >> 3] >> (7u - (q & 7u))) & 1u;
            win = (win << 1) | bit;
        }
        fprintf(stderr, "CBPY win=%03x pos=%u\n", win, (unsigned)at);
    }
    if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
        return -1;
    }
    if (getenv("NJ_M4VRAW") != NULL) fprintf(stderr, "CBPY sym=%d pos=%u\n", cbpy, (unsigned)*pos);
    *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
    (void)v;
    return 0;
}

static int m4v_intra_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                             nj_s2 *dc_c2, int stride_c2,
                             int scale, int *qscale, int *ac_bits,
                             nj_u1 *yp, int ys, nj_u1 *up, int us,
                             nj_u1 *vp, int vs, int cbp, int cbpc, int dq)
{
    int blk;
    nj_s4 all_coef[6][64];
    {
        int zi, zj;
        for (zi = 0; zi < 6; zi++) {
            for (zj = 0; zj < 64; zj++) {
                all_coef[zi][zj] = 0;
            }
        }
    }
    /* dquant follows cbpy, still inside the macroblock header, before the
     * DC coefficients of the six blocks (7.4.3.5). */
    if (dq != 0) {
        nj_u4 at = *pos;
        int   delta;

        if (at + 2u > n * 8u) {
            return -1;
        }
        delta = (int)((b[at >> 3] >> (6u - (at & 7u))) & 3u);
        *qscale += (delta == 0) ? -1 : (delta == 1) ? -2 : (delta == 2) ? 1 : 2;
        if (*qscale < 1) {
            *qscale = 1;
        } else if (*qscale > 31) {
            *qscale = 31;
        }
        *pos += 2u;
    }
    /* Per block: its DC, then its AC.  That is the order the bit stream
     * uses (7.4.3.5), and a block's DC predictor may depend on blocks of
     * the same macroblock, so the two cannot be split into two passes. */
    if (getenv("NJ_M4VMB")) fprintf(stderr, "MB pos=%u cbpc=%d cbp=0x%02x\n", (unsigned)*pos, cbpc, (unsigned)cbp);
    for (blk = 0; blk < 6; blk++) {
        nj_s2 *grid = (blk < 4) ? dc_y : (blk == 4 ? dc_c : (dc_c2 != NULL ? dc_c2 : dc_c));
        if (getenv("NJ_M4VBLK")) fprintf(stderr, "BLK %d at=%u\n", blk, (unsigned)*pos);
        int stride = (blk < 4) ? stride_y : (blk == 4 ? stride_c
                      : (dc_c2 != NULL ? stride_c2 : stride_c));
        nj_s4 coef[64];
        int gx = 0, gy = 0, a, bq, c, dir = 0, pred, rc, level;
        int dscale = nj_m4v_dc_scale(*qscale, blk < 4);

        if (nj_m4v_block_grid(blk, mbx, mby, &gx, &gy) != 0) {
            return -1;
        }
        a = nj_m4v_dc_neighbour(grid, stride, gx, gy, 0);
        bq = nj_m4v_dc_neighbour(grid, stride, gx, gy, 2);
        c = nj_m4v_dc_neighbour(grid, stride, gx, gy, 1);
        /* Outside-slice handling, as the reference decoder does it: on the
         * first line of a slice the top neighbours are not predictors, and
         * at the left edge of the slice so is the left one.  Chroma blocks
         * (n=4 and n=5) follow the same rules. */
        if (mby == 0) {
            if (blk != 3) {
                if (blk != 2) {
                    bq = 1024;
                    c = 1024;
                }
                if (blk != 1 && mbx == 0) {
                    bq = 1024;
                    a = 1024;
                }
            }
        }
        pred = nj_m4v_dc_predict_q(a, bq, c, dscale, &dir);
        rc = nj_m4v_decode_intra_block_ex(b, n, pos, coef, blk < 4, pred,
                                          *qscale, 0);
        if (getenv("NJ_M4VDC")) fprintf(stderr, "DC mb(%d,%d) blk=%d rc=%d pos=%u pred=%d a=%d b=%d c=%d s=%d\n", mbx, mby, blk, rc, (unsigned)*pos, pred, a, bq, c, dscale);
        if (getenv("NJ_M4VDBG")) fprintf(stderr, "DC blk=%d rc=%d pos=%u\n", blk, rc, (unsigned)*pos);
        if (rc != 0) {
            return rc;
        }
        level = (nj_s4)coef[0] * (nj_s4)dscale;
        if (level < 0) {
            level = 0;
        } else if (level > 2047) {
            level = 2047;
        }
        if (grid != NULL && stride > 0) {
            grid[(nj_size)gy * (nj_size)stride + (nj_size)gx] = (nj_s2)level;
        }
        /* Reconstruction uses ffmpeg's convention: the IDCT input is the
         * coefficient scaled by the DC quantiser, and the IDCT already puts
         * mid-grey (1024) at 128.  Our nj_m4v_idct() returns a residual that
         * still needs +128, so subtract the 1024 bias here. */
        all_coef[blk][0] = (nj_s4)(nj_m4v_dc_coef((int)coef[0], (int)dscale) -
                                   NJ_M4V_DC_GREY);
        (void)dir;
        (void)scale;
        int coded = nj_m4v_cbp_has_ac(cbp, blk), arc;

        if (getenv("NJ_M4V_ACINV") != NULL) {
            coded = !coded;
        }

        if (ac_bits != NULL) {
            ac_bits[blk] = coded;
        }
        if (!coded) {
            continue;
        }
        {
            nj_u4 ac_start = *pos;
            arc = nj_m4v_ac_block(b, n, pos, all_coef[blk], 1, *qscale);
            if (getenv("NJ_M4VAC")) fprintf(stderr, "BLK mb(%d,%d) blk=%d rc=%d %u..%u\n", mbx, mby, blk, arc, (unsigned)ac_start, (unsigned)*pos);
        }
        if (getenv("NJ_M4VDBG")) fprintf(stderr, "AC blk=%d rc=%d pos=%u\n", blk, arc, (unsigned)*pos);
        if (arc == -3) {
            return -3;
        }
        if (arc < 0) {
            return -1;
        }
    }
    if (getenv("NJ_M4VCOEF") != NULL) {
        int zz, qq;
        for (zz = 0; zz < 6; zz++) {
            fprintf(stderr, "COEF mb(%d,%d) b%d:", mbx, mby, zz);
            for (qq = 0; qq < 8; qq++) fprintf(stderr, " %d", (int)all_coef[zz][qq]);
            fprintf(stderr, "\n");
        }
    }
    if (yp != NULL) {
        int cw = 0, ch = 0;
        int  scale2 = 1;

        (void)scale2;
        if (nj_m4v_plane_size((nj_u4)mbx, (nj_u4)mby, (nj_u4 *)&cw, (nj_u4 *)&ch) != 0) {
            cw = 0;
        }
        for (blk = 0; blk < 6; blk++) {
            int out[64];
            int r, cc2;
            nj_u1 *dst;
            int stride;
            int ox = 0, oy = 0;

            nj_m4v_idct(all_coef[blk], out);
            if (nj_m4v_block_origin((nj_u4)mbx, (nj_u4)mby, blk,
                                    (nj_u4 *)&ox, (nj_u4 *)&oy) != 0) {
                return -1;
            }
            if (blk < 4) {
                dst = yp; stride = ys;
            } else if (blk == 4) {
                dst = up; stride = us;
                /* One 8x8 chroma block covers the macroblock's 16x16 luma
                 * area, so its origin is (mbx, mby) in chroma samples - not
                 * half of the luma block origin. */
                ox = mbx * 8; oy = mby * 8;
            } else {
                dst = vp; stride = vs;
                ox = mbx * 8; oy = mby * 8;
            }
            for (r = 0; r < 8; r++) {
                for (cc2 = 0; cc2 < 8; cc2++) {
                    int v2 = out[r * 8 + cc2] + 128;
                    if (v2 < 0) { v2 = 0; } else if (v2 > 255) { v2 = 255; }
                    {
                        nj_size wi = (nj_size)(oy + r) * (nj_size)stride + (nj_size)(ox + cc2);
                        if (getenv("NJ_M4VW") && wi >= 6300u) {
                            fprintf(stderr, "WRITE blk=%d mb(%d,%d) ox=%d oy=%d r=%d c=%d stride=%d idx=%u dst=%s\n",
                                    blk, mbx, mby, ox, oy, r, cc2, stride, (unsigned)wi,
                                    dst == yp ? "yp" : (dst == up ? "up" : "vp"));
                        }
                        dst[wi] = (nj_u1)v2;
                    }
                }
            }
        }
    }
    return 0;
}

int nj_m4v_decode_intra_mb_pix(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                               nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                               nj_s2 *dc_c2, int stride_c2,
                               int scale, int *qscale, int *ac_bits,
                               nj_u1 *yp, int ys, nj_u1 *up, int us,
                               nj_u1 *vp, int vs)
{
    int cbp = 0, cbpc = 0, dq = 0;

    if (nj_m4v_mb_core(b, n, pos, &cbpc, &dq) != 0) {
        return -1;
    }
    {
        int acp = 0;

        if (m4v_read_cbp(b, n, pos, cbpc, dq, &cbp, &acp) != 0) {
            return -1;
        }
        (void)acp;
    }
    return m4v_intra_mb_body(b, n, pos, mbx, mby, dc_y, stride_y, dc_c, stride_c,
                             dc_c2, stride_c2, scale, qscale, ac_bits,
                             yp, ys, up, us, vp, vs, cbp, cbpc, dq);
}
int nj_m4v_decode_intra_mb(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                           nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                           int scale, int *qscale, int *ac_bits)
{
    return nj_m4v_decode_intra_mb_pix(b, n, pos, mbx, mby, dc_y, stride_y, dc_c,
                                      stride_c, NULL, 0, scale, qscale, ac_bits,
                                      NULL, 0, NULL, 0, NULL, 0);
}

int nj_m4v_accepts(const nj_u1 *b, nj_u4 n)
{
    nj_u4 code = 0, pos = 0, from = 0;
    int   seen_vos = 0;
    nj_m4v_vol vol;

    while (nj_m4v_start_code(b, n, from, &code, &pos)) {
        if (code == NJ_M4V_VOS) {
            seen_vos = 1;
        } else if (code == NJ_M4V_VOL && nj_m4v_parse_vol(b, n, pos, &vol)) {
            return 1;
        }
        from = pos;
    }
    (void)seen_vos;
    return 0;
}
