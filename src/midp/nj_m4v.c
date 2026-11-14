/*
 * nano_j2me - src/midp/nj_m4v.c
 *
 * MPEG-4 Visual framing.  See include/nj2me/nj_m4v.h.
 *
 * Original framing/provider glue, H.263 equation-based reconstruction, and
 * MIT-licensed MPEG-4 syntax/prediction adaptations.  Detailed provenance and
 * validation: docs/notes/2026-11-12-mpeg4-independent-implementation.md.
 */
/* Coefficient event, packet and predictor adaptations:
 * oxideav-mpeg4video, revision ebfef14e48acf63547f73a9a0f4c36a153db92b2
 * https://github.com/OxideAV/oxideav-mpeg4video
 *
 * MIT License
 *
 * Copyright (c) 2026 Karpelès Lab Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "nj2me/nj_m4v.h"
#include "nj2me/nj_avcodec.h"
#include "nj_mp4.h"

#define NJ_M4V_DC_ERR (-0x7fffffff)
#include <stdio.h>
#include <stdlib.h>
#include "nj2me/nj_m4v_tables.h"
#include "nj2me/nj_m4v_escape.h"

#if NJ_ENABLE_M4V

/* Inside the guard: with the decoder compiled out these tables would be
 * unused static data, which a -Werror build reports as dead code. */
#include "nj_vlc_intra.h"
#include "nj_vlc_inter.h"

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

/* Optional complexity flags are format facts retained from the project's
 * original header research (de7aa2c), not an adaptation of a decoder loop.
 * A field contributes a fixed number of metadata bits to one picture class.
 * Describe the syntax as data so its boundaries and totals can be checked. */
typedef struct m4v_complexity_field { nj_u1 picture, width; } m4v_complexity_field;

static int m4v_complexity_fields(m4v_bits *bits,
                                 const m4v_complexity_field *fields, int count,
                                 int totals[3])
{
    int field, present;
    for (field = 0; field < count; field++) {
        if (!m4v_bit(bits, &present)) return 0;
        if (present) totals[fields[field].picture] += fields[field].width;
    }
    return 1;
}

static int m4v_complexity_header(m4v_bits *bits, nj_m4v_vol *vol)
{
    static const m4v_complexity_field fields[] = {
        {0,8},{0,8},{0,8},{0,8},{0,8},{0,8}, /* shape */
        {0,8},{1,8},{1,8},{0,8},             /* texture set 1 */
        {0,8},{0,8},{0,8},{0,4},             /* texture set 2 */
        {1,8},{1,8},{2,8},{1,8},{1,8},{1,8}, /* motion compensation */
        {0,8},{1,8}                          /* estimation method 1 */
    };
    static const nj_u1 counts[4] = {6,4,4,6};
    int totals[3] = {0,0,0}, group, offset = 0, disabled;
    nj_u4 method;
    if (!m4v_bits_get(bits, 2, &method)) return 0;
    if (method < 2) {
        for (group = 0; group < 4; group++) {
            if (!m4v_bit(bits, &disabled)) return 0;
            if (!disabled && !m4v_complexity_fields(bits, fields+offset,
                                                     counts[group], totals))
                return 0;
            offset += counts[group];
            if ((group == 1 || group == 3) && !m4v_marker(bits)) return 0;
        }
        if (method == 1 && !m4v_complexity_fields(bits, fields+offset, 2, totals))
            return 0;
    }
    vol->cplx_i = totals[0];
    vol->cplx_p = totals[1];
    vol->cplx_b = totals[2];
    return 1;
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
        if (b8 == 0 && !m4v_complexity_header(&s, out)) return 0;
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

static int m4v_skip(m4v_bits *bits, int count)
{
    if (count < 0 || bits->pos > bits->n*8u ||
        (nj_u4)count > bits->n*8u-bits->pos) return 0;
    bits->pos += (nj_u4)count;
    return 1;
}

/* MIT vop.rs common I/P grammar, with this project's optional shape/complexity
 * metadata retained as declarative field walks.  No metadata value is needed
 * by Simple Profile reconstruction; its complete encoded width still matters.
 * Output and caller cursor are published together only after a valid header. */
static int m4v_parse_picture_header(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos,
                                    nj_u4 time_res, int cplx_i, int cplx_p,
                                    int interlaced, int quant_precision,
                                    int shape, nj_m4v_vop *out)
{
    m4v_bits bits;
    nj_m4v_vop picture;
    nj_u4 value;
    int flag, field, width;
    if (b == NULL || bitpos == NULL || out == NULL || time_res < 2u) return 0;
    bits.b = b; bits.n = n; bits.pos = *bitpos;
    memset(&picture, 0, sizeof(picture));
    if (!m4v_bits_get(&bits, 2, &value) || value == 2) return 0;
    picture.type = (int)value;
    do { if (!m4v_bit(&bits, &flag)) return 0; } while (flag);
    width = (int)m4v_log2(time_res-1u) + 1;
    if (!m4v_marker(&bits) || !m4v_bits_get(&bits, width, &value) ||
        !m4v_marker(&bits) || !m4v_bit(&bits, &picture.coded)) return 0;
    picture.time_increment = value;
    if (picture.coded) {
        if (shape != 1 && picture.type == 1 &&
            !m4v_bit(&bits, &picture.no_rounding)) return 0;
        if (shape != 0) {
            if (picture.type != 0) {
                /* width, height, horizontal reference, vertical reference */
                for (field = 0; field < 4; field++) {
                    if (!m4v_skip(&bits, 13)) return 0;
                    if (field != 3 && !m4v_marker(&bits)) return 0;
                }
            }
            if (!m4v_skip(&bits, 1) || !m4v_bit(&bits, &flag) ||
                (flag && !m4v_skip(&bits, 8))) return 0;
        }
        if (shape != 1) {
            if (!m4v_skip(&bits, cplx_i) ||
                (picture.type != 0 && !m4v_skip(&bits, cplx_p)) ||
                !m4v_bits_get(&bits, 3, &value)) return 0;
            picture.dc_vlc_thr = (int)value;
            if (interlaced && !m4v_skip(&bits, 2)) return 0;
        }
        if (quant_precision < 1 || quant_precision > 12) quant_precision = 5;
        if (!m4v_bits_get(&bits, quant_precision, &value) || value == 0) return 0;
        picture.qscale = (int)value;
        if (picture.type == 1) {
            if (!m4v_bits_get(&bits, 3, &value) || value == 0) return 0;
            picture.f_code = (int)value;
        }
    }
    *bitpos = bits.pos;
    *out = picture;
    return 1;
}

int nj_m4v_parse_vop_bits(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos, nj_u4 time_res,
                          nj_m4v_vop *out)
{
    return m4v_parse_picture_header(b, n, bitpos, time_res, 0, 0, 0, 5, 0, out);
}

int nj_m4v_parse_vop_bits_vol(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos,
                              const nj_m4v_vol *vol, nj_m4v_vop *out)
{
    if (vol == NULL) {
        return 0;
    }
    return m4v_parse_picture_header(b, n, bitpos, vol->time_res,
                                vol->cplx_i, vol->cplx_p, vol->interlaced,
                                vol->quant_precision, vol->shape, out);
}


/* ------------------------------------------------------------------ */
/* Video packet (resync) headers, ISO/IEC 14496-2 6.2.5.2              */
/* ------------------------------------------------------------------ */
/* Packet number width is ceil(log2(macroblock_count)), at least one. */
static int m4v_mb_num_bits(int mb_total)
{
    int bits = 0;
    int v = mb_total - 1;

    while (v > 0) {
        bits++;
        v >>= 1;
    }
    return bits > 0 ? bits : 1;
}

/* Rectangular video-packet syntax, adapted from MIT video_packet.rs.
 * Work on a bounded local cursor so a refused header cannot move the caller. */
static int m4v_packet_start(m4v_bits *bits, int pict_type, int f_code)
{
    int value, zeroes, expected;
    if (pict_type != 0 && pict_type != 1) return 0;
    if (pict_type == 1 && (f_code < 1 || f_code > 7)) return 0;
    if (!m4v_bit(bits, &value) || value != 0) return 0;
    while (bits->pos & 7u) {
        if (!m4v_bit(bits, &value) || value != 1) return 0;
    }
    expected = pict_type == 0 ? 16 : 15 + f_code;
    for (zeroes = 0; zeroes < expected; zeroes++) {
        if (!m4v_bit(bits, &value) || value != 0) return 0;
    }
    return m4v_bit(bits, &value) && value == 1;
}

static int m4v_is_resync(const nj_u1 *b, nj_u4 n, nj_u4 pos, int mb_total,
                         int pict_type, int f_code)
{
    m4v_bits bits = { b, n, pos };
    nj_u4 number;
    if (b == NULL || mb_total < 1 ||
        !m4v_packet_start(&bits, pict_type, f_code) ||
        !m4v_bits_get(&bits, m4v_mb_num_bits(mb_total), &number)) return 0;
    return number > 0 && number < (nj_u4)mb_total ? (int)number : 0;
}

int nj_m4v_resync(const nj_u1 *b, nj_u4 n, nj_u4 *pos, const nj_m4v_vol *vol,
                   int pict_type, int f_code, int *qscale, int *mbnum)
{
    m4v_bits bits;
    nj_u4 columns, rows, number, quant;
    int extension, total;
    if (b == NULL || pos == NULL || vol == NULL || qscale == NULL ||
        mbnum == NULL || vol->shape != 0 || vol->quant_precision < 3 ||
        vol->quant_precision > 9 ||
        nj_m4v_frame_macroblocks(vol->width, vol->height, &columns, &rows)) return -1;
    total = (int)(columns * rows);
    bits.b = b; bits.n = n; bits.pos = *pos;
    if (!m4v_packet_start(&bits, pict_type, f_code) ||
        !m4v_bits_get(&bits, m4v_mb_num_bits(total), &number) ||
        number == 0 || number >= (nj_u4)total ||
        !m4v_bits_get(&bits, vol->quant_precision, &quant) || quant == 0 ||
        !m4v_bit(&bits, &extension)) return -1;
    if (extension) return -2;
    *qscale = (int)quant;
    *mbnum = (int)number;
    *pos = bits.pos;
    return 1;
}

int nj_m4v_parse_vop(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_u4 time_res,
                     nj_m4v_vop *out)
{
    nj_u4 bitpos = pos * 8u;
    int r = nj_m4v_parse_vop_bits(b, n, &bitpos, time_res, out);

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
        *last = ((int)i >= (int)NJ_M4V_AC_LAST);
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
        *last = (sym >= (int)NJ_M4V_AC_LAST);
    }
    return 1;
}


/*
 * ISO/IEC 14496-2 7.4.3: the coefficient scan for a non-interlaced picture is
 * the usual zigzag.  nj_m4v_scan_index(k) is the natural index of the k-th
 * coefficient in that order, which is what the AC loop needs to place run/level
 * pairs into an 8x8 block.
 */
/* Inverse of the scan-index grids in MIT scan.rs, Figure 7-4.
 * The upstream grid maps raster position to scan position; this API returns
 * the raster destination of a scan position.  All 192 entries were compared. */
static const nj_u1 nj_m4v_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

static const nj_u1 nj_m4v_scan_h[64] = {
     0,  1,  2,  3,  8,  9, 16, 17,
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33,
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49,
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59,
    52, 53, 54, 55, 60, 61, 62, 63,
};

static const nj_u1 nj_m4v_scan_v[64] = {
     0,  8, 16, 24,  1,  9,  2, 10,
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12,
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14,
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31,
    38, 46, 54, 62, 39, 47, 55, 63,
};

int nj_m4v_scan_index_mode(int k, int mode)
{
    if (k < 0 || k > 63) {
        return -1;
    }
    if (mode == 1) {            /* alternate horizontal */
        return (int)nj_m4v_scan_h[k];
    }
    if (mode == 2) {            /* alternate vertical */
        return (int)nj_m4v_scan_v[k];
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



int nj_m4v_qmul(int qscale)
{
    return (qscale > 0 && qscale < 32) ? qscale * 2 : 0;
}

int nj_m4v_qadd(int qscale)
{
    return (qscale > 0 && qscale < 32) ? ((qscale - 1) | 1) : 0;
}


/* Adapted from oxideav-mpeg4video texture.rs, MIT; see the notice above.
 * Each event owns its LAST flag.  Scan position and inverse quantization are
 * separate operations; no end-of-block flag is packed into a run value. */
typedef struct m4v_event {
    int last, run, level;
} m4v_event;

static int m4v_event_vlc(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int intra,
                          m4v_event *event)
{
    nj_u4 look = 0, ignored;
    nj_u2 entry;
    int bit, symbol, length, sign;
    if (b == NULL || pos == NULL || event == NULL || *pos >= n * 8u) return 0;
    for (bit = 0; bit < 12; bit++) {
        nj_u4 at = *pos + (nj_u4)bit;
        look = (look << 1) | (at < n * 8u ?
            ((b[at >> 3] >> (7u - (at & 7u))) & 1u) : 0u);
    }
    entry = intra ? nj_m4v_ac_lut[look] : nj_vlc_inter_lut[look];
    if (entry == 0xffffu) return 0;
    length = entry >> 8;
    symbol = entry & 255;
    if (!m4v_rbits(b, n, pos, length, &ignored) ||
        !m4v_rbit(b, n, pos, &sign)) return 0;
    event->last = symbol >= (intra ? (int)NJ_M4V_AC_LAST : 58);
    event->run = intra ? nj_m4v_ac_run[symbol] : nj_vlc_inter_run[symbol];
    event->level = intra ? nj_m4v_ac_level[symbol] : nj_vlc_inter_level[symbol];
    if (sign) event->level = -event->level;
    return 1;
}

static int m4v_escape_event(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int intra,
                             int compatibility_q, m4v_event *event)
{
    int first, second = 0, marker, magnitude, extension;
    if (!m4v_rbit(b, n, pos, &first)) return 0;
    if (first && !m4v_rbit(b, n, pos, &second)) return 0;
    if (first && second) {
        nj_u4 run;
        if (!m4v_rbit(b, n, pos, &event->last) ||
            !m4v_rbits(b, n, pos, 6, &run) ||
            !m4v_rbit(b, n, pos, &marker) || marker != 1 ||
            !m4v_rsbits(b, n, pos, 12, &event->level) ||
            !m4v_rbit(b, n, pos, &marker) || marker != 1) return 0;
        event->run = (int)run;
        if (event->level == 0 || event->level == -2048) return 0;
        if (compatibility_q) {
            magnitude = event->level < 0 ? -event->level : event->level;
            magnitude = magnitude*(2*compatibility_q) + ((compatibility_q-1)|1);
            event->level = event->level < 0 ? -magnitude : magnitude;
            if (event->level < -2048) event->level = -2048;
            if (event->level > 2047) event->level = 2047;
        }
        return 1;
    }
    if (!m4v_event_vlc(b, n, pos, intra, event)) return 0;
    magnitude = event->level < 0 ? -event->level : event->level;
    if (!first) {
        if (intra) {
            if (event->run > NJ_M4V_MAX_RUN) return 0;
            extension = event->last ? nj_m4v_max_level_1[event->run]
                                    : nj_m4v_max_level_0[event->run];
        } else {
            if (event->run > 63) return 0;
            extension = nj_vlc_inter_max_level[event->last][event->run];
        }
        if (extension == 0) return 0;
        magnitude += extension * (compatibility_q ? 2*compatibility_q : 1);
        event->level = event->level < 0 ? -magnitude : magnitude;
    } else {
        if (compatibility_q) magnitude /= 2*compatibility_q;
        if (intra) {
            if (magnitude > NJ_M4V_MAX_LEVEL) return 0;
            extension = event->last ? nj_m4v_max_run_1[magnitude]
                                    : nj_m4v_max_run_0[magnitude];
        } else {
            if (magnitude > 63) return 0;
            extension = nj_vlc_inter_max_run[event->last][magnitude];
        }
        event->run += extension + 1;
    }
    return 1;
}

static int m4v_read_event(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int intra,
                           m4v_event *event)
{
    nj_u4 at, prefix;
    if (b == NULL || pos == NULL || event == NULL) return 0;
    at = *pos;
    if (!m4v_rbits(b, n, &at, 7, &prefix) || prefix != 3u)
        return m4v_event_vlc(b, n, pos, intra, event);
    *pos = at;
    return m4v_escape_event(b, n, pos, intra, 0, event);
}

/* Historical low-level helpers expose q-scaled escape arithmetic.  The
 * picture decoder uses m4v_read_event() with unscaled quantized events. */
static int m4v_escape_compat(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                            int wanted, int *last, int *run, int *level)
{
    m4v_event event;
    nj_u4 at;
    int one, two = 0, form;
    if (b == NULL || pos == NULL || qscale < 1 || qscale > 31) return -1;
    at = *pos;
    if (!m4v_rbit(b, n, &at, &one) ||
        (one && !m4v_rbit(b, n, &at, &two))) return -1;
    form = !one ? 1 : (!two ? 2 : 3);
    if (form != wanted) return -3;
    at = *pos;
    if (!m4v_escape_event(b, n, &at, 1, qscale, &event)) return -1;
    *pos = at;
    if (last != NULL) *last = event.last;
    if (run != NULL) *run = event.run;
    if (level != NULL) *level = event.level;
    return 0;
}

int nj_m4v_escape1(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                    int *last, int *run, int *level)
{
    return m4v_escape_compat(b, n, pos, qscale, 1, last, run, level);
}
int nj_m4v_escape2(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                    int *last, int *run, int *level)
{
    return m4v_escape_compat(b, n, pos, qscale, 2, last, run, level);
}
int nj_m4v_escape3(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                    int *last, int *run, int *level)
{
    return m4v_escape_compat(b, n, pos, qscale, 3, last, run, level);
}

/* H.263 6.2.1 / MPEG-4 H.263-style scalar inverse quantization. */
static int m4v_inverse_level(int quantized, int quantizer)
{
    int magnitude, reconstructed;
    if (quantized == 0) return 0;
    magnitude = quantized < 0 ? -quantized : quantized;
    reconstructed = quantizer*(2*magnitude+1) - (quantizer%2 == 0);
    if (quantized < 0) reconstructed = -reconstructed;
    if (reconstructed < -2048) reconstructed = -2048;
    if (reconstructed > 2047) reconstructed = 2047;
    return reconstructed;
}

static int m4v_event_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                           nj_s4 *coef, int start, int intra,
                           int qscale, int scan_mode)
{
    int cursor = start;
    if (b == NULL || pos == NULL || coef == NULL || start < 0 || start > 63 ||
        qscale < 1 || qscale > 31 || scan_mode < 0 || scan_mode > 2) return -1;
    while (cursor < 64) {
        m4v_event event;
        int level, index;
        if (!m4v_read_event(b, n, pos, intra, &event)) return -1;
        cursor += event.run;
        if (cursor > 63) return -1;
        index = nj_m4v_scan_index_mode(cursor, scan_mode);
        level = event.level;
        if (!intra) level = m4v_inverse_level(level, qscale);
        coef[index] = (nj_s4)level;
        if (event.last) return 0;
        cursor++;
    }
    return -1;
}

int nj_m4v_ac_block_scan(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef,
                          int k, int qscale, int scan_mode)
{
    return m4v_event_block(b, n, pos, coef, k, 1, qscale, scan_mode);
}

/* The public entry point keeps the plain zigzag: AC prediction is the only
 * user of the alternate scans. */
int nj_m4v_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef, int k,
                    int qscale)
{
    return nj_m4v_ac_block_scan(b, n, pos, coef, k, qscale, 0);
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

/* Inverse transform from the separable cosine equation (H.263 6.2.4).
 * B[u,x] = floor(2^20 * C(u)/2 * cos(pi*(2*x+1)*u/16) + 0.5), C(0)=1/sqrt(2).
 * Keep both matrix products in 64 bits and round only the final 40-bit scale.
 * For 12-bit coefficients the absolute accumulator bound is below 2^56.
 * This is an original matrix implementation, not a decoder's butterfly.
 * See docs/notes/2026-11-12-mpeg4-independent-implementation.md for provenance
 * and the independent H.263 Annex A accuracy test.
 */
static const nj_s4 m4v_cosine_basis[8][8] = {
    {  370728,  370728,  370728,  370728,  370728,  370728,  370728,  370728 },
    {  514214,  435930,  291279,  102284, -102284, -291279, -435930, -514214 },
    {  484379,  200636, -200636, -484379, -484379, -200636,  200636,  484379 },
    {  435930, -102284, -514214, -291279,  291279,  514214,  102284, -435930 },
    {  370728, -370728, -370728,  370728,  370728, -370728, -370728,  370728 },
    {  291279, -514214,  102284,  435930, -435930, -102284,  514214, -291279 },
    {  200636, -484379,  484379, -200636, -200636,  484379, -484379,  200636 },
    {  102284, -291279,  435930, -514214,  514214, -435930,  291279, -102284 }
};

void nj_m4v_idct(const nj_s4 *coef, int *out)
{
    nj_s8 horizontal[64];
    const nj_s8 scale = (nj_s8)1 << 40;
    int x, y, u, v;

    if (coef == NULL || out == NULL) {
        return;
    }
    /* The constant block has the exact rational value DC/8.  Besides saving
     * work on blank blocks, this avoids a basis approximation deciding a
     * half-integer tie in the wrong direction. */
    for (u = 1; u < 64 && coef[u] == 0; u++) { }
    if (u == 64) {
        nj_s8 dc = (nj_s8)coef[0] + 4;
        int value = (int)(dc / 8 - (dc % 8 < 0));
        for (x = 0; x < 64; x++) out[x] = value;
        return;
    }
    for (v = 0; v < 8; v++) {
        for (x = 0; x < 8; x++) {
            nj_s8 sum = 0;
            for (u = 0; u < 8; u++)
                sum += (nj_s8)coef[v * 8 + u] * m4v_cosine_basis[u][x];
            horizontal[v * 8 + x] = sum;
        }
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            nj_s8 sum = scale / 2;
            for (v = 0; v < 8; v++)
                sum += horizontal[v * 8 + x] * m4v_cosine_basis[v][y];
            /* C division truncates toward zero; nearest-integer rounding
             * requires floor after adding one half, including below zero. */
            out[y * 8 + x] = (int)(sum / scale - (sum % scale < 0));
        }
    }
}

void nj_m4v_idct_put(const nj_s4 *coef, int *out)
{
    int sample;

    if (coef == NULL || out == NULL) {
        return;
    }
    nj_m4v_idct(coef, out);
    /* Intra DC already contains the image's DC level.  No +128 shift. */
    for (sample = 0; sample < 64; sample++) {
        if (out[sample] < 0) out[sample] = 0;
        if (out[sample] > 255) out[sample] = 255;
    }
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


/* Adapted from MIT predictor.rs: Figure 7-5 uses left/corner/above.
 * A tie selects left; unavailable 8-bit intra DC is 1024. */
int nj_m4v_dc_predict_q(int a, int b, int c, int scale, int *dir)
{
    int left = a < 0 ? 1024 : a;
    int corner = b < 0 ? 1024 : b;
    int above = c < 0 ? 1024 : c;
    int dl = left-corner, dt = corner-above;
    int vertical;
    if (scale <= 0) return -1;
    if (dl < 0) dl = -dl;
    if (dt < 0) dt = -dt;
    vertical = dl < dt;
    if (dir != NULL) *dir = vertical;
    return nj_m4v_rounded_div(vertical ? above : left, scale);
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
    int quantized, reconstructed;
    if (scale <= 0) return -1;
    quantized = delta + nj_m4v_rounded_div(pred_dequant, scale);
    reconstructed = quantized*scale;
    if (reconstructed < 0) reconstructed = 0;
    if (reconstructed > 2047) reconstructed = 2047;
    if (stored != NULL) *stored = reconstructed;
    return quantized;
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
    int left = 1024, corner = 1024, above = 1024, prediction, level, stored;
    if (grid == NULL || wrap <= 0 || bx < 0 || bx >= wrap || by < 0 ||
        scale <= 0) return -1;
    if (bx) left = grid[by*wrap + bx-1];
    if (by) above = grid[(by-1)*wrap + bx];
    if (bx && by) corner = grid[(by-1)*wrap + bx-1];
    prediction = nj_m4v_dc_predict_q(left, corner, above, scale, NULL);
    level = nj_m4v_dc_level(delta, prediction*scale, scale, &stored);
    grid[by*wrap + bx] = (nj_s2)stored;
    return level;
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


/* AC-prediction state is defined with its helpers further down; the frame
 * drivers and the intra macroblock body only need the type here. */
typedef struct m4v_ac_state {
    int    mbw, mbh, packet_start;
    nj_s2 *luma;        /* (2*mbw) * (2*mbh) blocks, 16 entries each  */
    nj_s2 *cb;          /* mbw * mbh blocks, 16 entries each          */
    nj_s2 *cr;
} m4v_ac_state;

static m4v_ac_state *m4v_ac_new(int mbw, int mbh);
static void m4v_ac_free(m4v_ac_state *ac);
static void m4v_ac_clean_slice(m4v_ac_state *ac, int rx, int ry);

static int m4v_intra_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                             nj_s2 *dc_c2, int stride_c2,
                             int scale, int *qscale, int *ac_bits,
                             nj_u1 *yp, int ys, nj_u1 *up, int us, nj_u1 *vp, int vs,
                             int cbp, int cbpc, int dq, int first_slice_line,
                             int resync_x, int resync_y, m4v_ac_state *ac,
                             int ac_pred);

static int m4v_intra_mb_pix_slice(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx,
                                  int mby, nj_s2 *dc_y, int stride_y, nj_s2 *dc_c,
                                  int stride_c, nj_s2 *dc_c2, int stride_c2,
                                  int scale, int *qscale, int *ac_bits,
                                  nj_u1 *yp, int ys, nj_u1 *up, int us,
                                  nj_u1 *vp, int vs, int first_slice_line,
                                  int resync_x, int resync_y, m4v_ac_state *ac);

int nj_m4v_decode_ivop(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                       const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                       nj_s4 *grids, nj_u1 *yp, int ys,
                       nj_u1 *up, int us, nj_u1 *vp, int vs)
{
    nj_u4 mbw = 0, mbh = 0, cw = 0, ch = 0;
    nj_s2 *gl, *gc, *gcr;
    int    glw, gcw, lscale, cscale, i, rc = 0;
    m4v_ac_state *ac;

    if (b == NULL || pos == NULL || vol == NULL || vop == NULL || grids == NULL ||
        yp == NULL || up == NULL || vp == NULL || ys <= 0 || us <= 0 || vs <= 0) {
        return -1;
    }
    if (!vop->coded) {
        return 0;
    }
    if (vop->type != 0) {
        return -1;                  /* not an I-VOP */
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
    ac = m4v_ac_new((int)mbw, (int)mbh);
    if (ac == NULL) {
        return -1;
    }
    {
        int qsc = (int)vop->qscale;   /* dquant adjusts this for the rest of the frame */
        int mb_total = (int)(mbw * mbh);
        int cur = 0;
        int resync_x = 0, resync_y = 0;

        while (cur < mb_total) {
            int mbx = cur % (int)mbw;
            int mby = cur / (int)mbw;
            int first_slice_line = (mby == resync_y);
            int acb[6];

            rc = m4v_intra_mb_pix_slice(b, n, pos, mbx, mby,
                                        gl, glw, gc, gcw, gcr, gcw,
                                        lscale, &qsc, acb,
                                        yp, ys, up, us, vp, vs,
                                        first_slice_line, resync_x, resync_y,
                                        ac);
            if (rc != 0) {
                break;
            }
            cur++;
            if (cur < mb_total && vol->resync_disabled == 0) {
                int next = m4v_is_resync(b, n, *pos, mb_total, 0, vop->f_code);

                if (next != 0) {
                    int mbnum = 0;
                    int hrc;

                    if (next == mb_total) {
                        rc = -1;    /* VOP would end before its last MB */
                        break;
                    }
                    hrc = nj_m4v_resync(b, n, pos, vol, 0, vop->f_code, &qsc,
                                        &mbnum);
                    if (hrc != 1 || mbnum != next || mbnum < cur) {
                        rc = -1;    /* marker, header and position disagree */
                        break;
                    }
                    cur = mbnum;
                    resync_x = cur % (int)mbw;
                    resync_y = cur / (int)mbw;
                    m4v_ac_clean_slice(ac, resync_x, resync_y);
                }
            }
        }
    }
    m4v_ac_free(ac);
    return rc;
}

/* ------------------------------------------------------------------ */
/* P-VOPs: motion vectors, motion compensation, inter reconstruction   */
/* ------------------------------------------------------------------ */

/* Median of three values, the H.263 predictor's inner operation. */
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

/* MIT motion.rs: decode one signed delta, then wrap the predicted result
 * into Table 7-9's [-32*f, 32*f-1] interval. */
int nj_m4v_decode_mv(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int pred, int f_code)
{
    int code, negative = 0, factor, result, delta = 0;
    nj_u4 residual = 0;
    if (b == NULL || pos == NULL || f_code < 1 || f_code > 7 ||
        nj_vlc_lookup_n(b, n, pos, nj_vlc_mvtab, 33, 12, &code)) return 0xffff;
    /* Preserve the public helper's identity operation for the zero delta,
     * including callers supplying a predictor outside a coded MV interval. */
    if (code == 0) return pred;
    factor = 1 << (f_code - 1);
    if (code != 0) {
        if (!m4v_rbit(b, n, pos, &negative) ||
            !m4v_rbits(b, n, pos, f_code - 1, &residual)) return 0xffff;
        delta = (code - 1) * factor + (int)residual + 1;
        if (negative) delta = -delta;
    }
    result = pred + delta;
    if (result < -32 * factor) result += 64 * factor;
    if (result >= 32 * factor) result -= 64 * factor;
    return result;
}

/* Figure 7-34 / MIT mv_predictor_grid.rs.  Candidate validity is decided
 * geometrically before gathering values.  packet_start is a macroblock index
 * in raster order; an intra/skipped neighbour within it has a valid zero MV. */
static void m4v_motion_predict(const nj_s2 *mvx, const nj_s2 *mvy, int wrap,
                               int pad, int mbx, int mby, int block,
                               int packet_start, int *px, int *py)
{
    int row[3], col[3], x[3] = {0,0,0}, y[3] = {0,0,0};
    int width = (wrap - 1) / 2, valid = 0, only = 0, k;
    if (px == NULL || py == NULL) return;
    *px = *py = 0;
    if (mvx == NULL || mvy == NULL || width < 1 || mbx < 0 || mby < 0 ||
        block < 0 || block > 3) return;
    row[0] = 2*mby + block/2; col[0] = 2*mbx + block%2 - 1;
    row[1] = block >= 2 ? 2*mby : 2*mby-1;
    col[1] = 2*mbx + (block == 1);
    row[2] = row[1]; col[2] = 2*mbx + (block < 2 ? 2 : 1);
    for (k = 0; k < 3; k++) {
        int owner;
        nj_size index;
        if (row[k] < 0 || col[k] < 0 || col[k] >= width*2) continue;
        owner = (row[k]/2)*width + col[k]/2;
        if (owner < packet_start) continue;
        index = (nj_size)pad + (nj_size)row[k]*(nj_size)wrap + (nj_size)col[k];
        x[k] = mvx[index]; y[k] = mvy[index]; valid++; only = k;
    }
    if (valid == 1) { *px = x[only]; *py = y[only]; }
    else if (valid > 1) {
        *px = m4v_mid_pred(x[0], x[1], x[2]);
        *py = m4v_mid_pred(y[0], y[1], y[2]);
    }
}

void nj_m4v_pred_motion(const nj_s2 *mvx, const nj_s2 *mvy, int wrap, int pad,
                         int mbx, int mby, int block, int first_slice_line,
                         int resync_x, int *px, int *py)
{
    int packet_start = first_slice_line ? mby*((wrap-1)/2) + resync_x : 0;
    m4v_motion_predict(mvx, mvy, wrap, pad, mbx, mby, block, packet_start, px, py);
}

/* H.263 6.1.2 Figure 13: bilinear half-pixel interpolation.  Source
 * coordinates outside the image repeat the boundary sample (Annex D.1).
 * Integer division here is explicitly floor division, including negative MVs.
 */
static int m4v_floor_div(int value, int divisor)
{
    int quotient = value / divisor;
    return quotient - (value % divisor < 0);
}

static int m4v_boundary(int coordinate, int extent)
{
    if (coordinate < 0) return 0;
    if (coordinate >= extent) return extent - 1;
    return coordinate;
}

void nj_m4v_mc_block8(int sx, int sy, int mx, int my, const nj_u1 *pl,
                      int stride, int fw, int fh, int shift, int no_rounding,
                      nj_u1 *out)
{
    int unit, origin_x, origin_y, half_x, half_y, taps, row, column;

    if (pl == NULL || out == NULL || stride <= 0 || fw <= 0 || fh <= 0 ||
        shift < 0 || shift > 1) {
        return;
    }
    /* Table 18 maps each non-integral chroma quarter-pixel to a half-pixel.
     * For luma, the only fractional position already is a half-pixel. */
    unit = shift ? 4 : 2;
    origin_x = sx + m4v_floor_div(mx, unit);
    origin_y = sy + m4v_floor_div(my, unit);
    half_x = (mx % unit != 0);
    half_y = (my % unit != 0);
    taps = (half_x + 1) * (half_y + 1);
    for (row = 0; row < 8; row++) {
        for (column = 0; column < 8; column++) {
            int sum = 0, dx, dy;
            for (dy = 0; dy <= half_y; dy++) {
                int y = m4v_boundary(origin_y + row + dy, fh);
                for (dx = 0; dx <= half_x; dx++) {
                    int x = m4v_boundary(origin_x + column + dx, fw);
                    sum += pl[(nj_size)y * (nj_size)stride + (nj_size)x];
                }
            }
            /* Figure 13 biases: 1-RCONTROL for two taps, 2-RCONTROL
             * for four; an integral pixel has no interpolation bias. */
            if (taps > 1) sum += taps / 2 - (no_rounding != 0);
            out[row * 8 + column] = (nj_u1)(sum / taps);
        }
    }
}

/* H.263 Annex F.2 Table F.1, expressed as intervals of a sixteenth-pixel:
 * residues 0..2 -> 0, 3..13 -> 1/2, 14..15 -> 1.  Output units are half-pels.
 */
int nj_m4v_chroma_round(int x)
{
    int whole = m4v_floor_div(x, 16);
    int fraction = x - whole * 16;
    return whole * 2 + (fraction >= 3) + (fraction >= 14);
}

/* The same event grammar supplies inter residuals; inverse quantization
 * is applied only after the signed quantized event has been recovered. */
static int m4v_inter_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                              nj_s4 *coef, nj_u4 mbx, nj_u4 mby, int blk)
{
    (void)mbx; (void)mby; (void)blk;
    return m4v_event_block(b, n, pos, coef, 0, 0, qscale, 0);
}

/* One inter macroblock: the six residual blocks come first in the bit stream,
 * then motion compensation fills the prediction and reconstruction adds the
 * inverse transform of each residual block.  `mv' holds the 8x8 luma vectors
 * (for a 16x16 block all four are the same); `four_mv' selects the four-vector
 * chroma derivation for the 8x8 case (one vector from the sum). */
static int m4v_inter_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             const nj_u1 *py, int pys, const nj_u1 *pu, int pus,
                             const nj_u1 *pv, int pvs,
                             nj_u1 *yp, int ys, nj_u1 *up, int us,
                             nj_u1 *vp, int vs,
                             int fw, int fh, int cw, int ch,
                             int cbp, int qscale, int mv[4][2], int four_mv,
                             int no_rounding)
{
    nj_s4 coef[6][64];
    int blk, k;

    for (blk = 0; blk < 6; blk++) {
        for (k = 0; k < 64; k++) {
            coef[blk][k] = 0;
        }
        if ((cbp >> (5 - blk)) & 1) {
            int rc = m4v_inter_ac_block(b, n, pos, qscale, coef[blk],
                                        (nj_u4)mbx, (nj_u4)mby, blk);
                        
            if (rc != 0) {
                return rc;
            }
        }
    }
    for (blk = 0; blk < 4; blk++) {
        nj_u1 pred[64];
        int   res[64];
        int   bx = (blk & 1) * 8;
        int   by = (blk >> 1) * 8;

        nj_m4v_mc_block8(mbx * 16 + bx, mby * 16 + by, mv[blk][0], mv[blk][1],
                         py, pys, fw, fh, 0, no_rounding, pred);
        nj_m4v_idct(coef[blk], res);
                for (k = 0; k < 64; k++) {
            int v = (int)pred[k] + res[k];

            if (v < 0) v = 0; else if (v > 255) v = 255;
                        yp[(nj_size)(mby * 16 + by + k / 8) * (nj_size)ys +
               (nj_size)(mbx * 16 + bx + k % 8)] = (nj_u1)v;
        }
    }
    {
        nj_u1 pred[64];
        int   res[64];
        int   cmx, cmy, cshift;

        if (four_mv != 0) {
            int sx = 0, sy = 0;

            for (blk = 0; blk < 4; blk++) {
                sx += mv[blk][0];
                sy += mv[blk][1];
            }
            cmx = nj_m4v_chroma_round(sx);
            cmy = nj_m4v_chroma_round(sy);
            cshift = 0;
        } else {
            cmx = mv[0][0];
            cmy = mv[0][1];
            cshift = 1;
        }
        nj_m4v_mc_block8(mbx * 8, mby * 8, cmx, cmy, pu, pus, cw, ch, cshift,
                         no_rounding, pred);
        nj_m4v_idct(coef[4], res);
        for (k = 0; k < 64; k++) {
            int v = (int)pred[k] + res[k];

            if (v < 0) v = 0; else if (v > 255) v = 255;
            up[(nj_size)(mby * 8 + k / 8) * (nj_size)us +
               (nj_size)(mbx * 8 + k % 8)] = (nj_u1)v;
        }
        nj_m4v_mc_block8(mbx * 8, mby * 8, cmx, cmy, pv, pvs, cw, ch, cshift,
                         no_rounding, pred);
        nj_m4v_idct(coef[5], res);
        for (k = 0; k < 64; k++) {
            int v = (int)pred[k] + res[k];

            if (v < 0) v = 0; else if (v > 255) v = 255;
            vp[(nj_size)(mby * 8 + k / 8) * (nj_size)vs +
               (nj_size)(mbx * 8 + k % 8)] = (nj_u1)v;
        }
    }
    return 0;
}

/* The predicted motion vectors of one P macroblock, filled by the header
 * parser: four_mv tells the reconstructor which chroma derivation to use. */
typedef struct m4v_pmv {
    int mv[4][2];
    int four_mv;
    int ac_pred;        /* intra macroblock: AC prediction flag from the header */
} m4v_pmv;

/* MIT macroblock.rs + motion.rs rectangular P-MB header.  Stuffing restarts
 * the macroblock grammar, including not_coded.  Intra-body dquant remains in
 * the existing body API; inter dquant is consumed before its motion data. */
static int m4v_pvop_mb_header(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                               const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                               nj_u4 mbx, nj_u4 mby, nj_s2 *mvx, nj_s2 *mvy,
                               int wrap, int pad, int packet_start,
                               int *qscale, int *cbp, int *dq,
                               int *cbpc_out, m4v_pmv *pmv)
{
    int symbol, skip, intra, coded_luma, block, count;
    if (b == NULL || pos == NULL || vol == NULL || vop == NULL ||
        qscale == NULL || cbp == NULL || dq == NULL || pmv == NULL) return -1;
    for (;;) {
        if (!m4v_rbit(b, n, pos, &skip)) return -1;
        if (skip) return 1;
        if (nj_vlc_lookup_n(b, n, pos, nj_vlc_inter_mcbpc, 28, 13, &symbol))
            return -1;
        if (symbol != 20) break;
    }
    intra = (symbol & 4) != 0;
    *dq = (symbol & 8) != 0;
    if (cbpc_out != NULL) *cbpc_out = symbol;
    if (intra && !m4v_rbit(b, n, pos, &pmv->ac_pred)) return -1;
    if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &coded_luma)) return -1;
    if (!intra) coded_luma = 15 - coded_luma;
    *cbp = (coded_luma << 2) | (symbol & 3);
    if (intra) return 0;
    if (*dq) {
        nj_u4 code;
        int change;
        if (!m4v_rbits(b, n, pos, 2, &code)) return -1;
        change = code < 2 ? -1-(int)code : (int)code-1;
        *qscale += change;
        if (*qscale < 1) *qscale = 1;
        if (*qscale > 31) *qscale = 31;
    }
    if (vol->interlaced && *cbp) {
        int ignored;
        if (!m4v_rbit(b, n, pos, &ignored)) return -1;
    }
    pmv->four_mv = (symbol & 16) != 0;
    count = pmv->four_mv ? 4 : 1;
    for (block = 0; block < count; block++) {
        int px, py, mx, my, slot;
        m4v_motion_predict(mvx, mvy, wrap, pad, (int)mbx, (int)mby, block,
                           packet_start, &px, &py);
        mx = nj_m4v_decode_mv(b, n, pos, px, vop->f_code);
        if (mx == 0xffff) return -1;
        my = nj_m4v_decode_mv(b, n, pos, py, vop->f_code);
        if (my == 0xffff) return -1;
        for (slot = block; slot < (count == 1 ? 4 : block+1); slot++) {
            nj_size index = (nj_size)pad +
                ((nj_size)mby*2u + (nj_size)(slot/2))*(nj_size)wrap +
                (nj_size)mbx*2u + (nj_size)(slot%2);
            mvx[index] = (nj_s2)mx;
            mvy[index] = (nj_s2)my;
            pmv->mv[slot][0] = mx;
            pmv->mv[slot][1] = my;
        }
    }
    return 2;
}

/* ------------------------------------------------------------------ */
/* AC prediction state: MIT predictor.rs / neighbour.rs, section 7.4.3.3.
 * Each block slot contains its quantizer/validity plus reconstructed quantized
 * first row and column.  Neighbours preceding packet_start are unavailable;
 * inter/skipped blocks invalidate their own six slots.  Prediction rescales
 * the selected neighbour's quantizer before scalar inverse quantization. */
static void m4v_ac_free(m4v_ac_state *ac)
{
    if (ac == NULL) {
        return;
    }
    free(ac->luma);
    free(ac->cb);
    free(ac->cr);
    free(ac);
}

static m4v_ac_state *m4v_ac_new(int mbw, int mbh)
{
    m4v_ac_state *ac;
    size_t        nl, nc;

    if (mbw <= 0 || mbh <= 0) {
        return NULL;
    }
    ac = (m4v_ac_state *)calloc(1, sizeof(*ac));
    if (ac == NULL) {
        return NULL;
    }
    ac->mbw = mbw;
    ac->mbh = mbh;
    nl = (size_t)(mbw * 2) * (size_t)(mbh * 2);
    nc = (size_t)mbw * (size_t)mbh;
    ac->luma = (nj_s2 *)calloc(nl * 16u, sizeof(nj_s2));
    ac->cb = (nj_s2 *)calloc(nc * 16u, sizeof(nj_s2));
    ac->cr = (nj_s2 *)calloc(nc * 16u, sizeof(nj_s2));
    if (ac->luma == NULL || ac->cb == NULL || ac->cr == NULL) {
        m4v_ac_free(ac);
        return NULL;
    }
    return ac;
}

/* The 16-entry slot of one 8x8 block: [1..7] is the block's first column
 * (rows 1..7 of column 0), [8..15] is its first row (columns 1..7 of row 0). */
static nj_s2 *m4v_ac_slot(m4v_ac_state *ac, int mbx, int mby, int blk)
{
    if (blk < 4) {
        size_t row = (size_t)(mby * 2 + (blk >> 1));
        size_t col = (size_t)(mbx * 2 + (blk & 1));

        return ac->luma + (row * (size_t)(ac->mbw * 2) + col) * 16u;
    }
    {
        size_t at = (size_t)mby * (size_t)ac->mbw + (size_t)mbx;

        return (blk == 4 ? ac->cb : ac->cr) + at * 16u;
    }
}

/* MIT predictor.rs rounding: nearest integer, ties away from zero. */
int nj_m4v_rounded_div(int a, int b)
{
    int magnitude;
    if (b <= 0) return 0;
    magnitude = a < 0 ? -a : a;
    magnitude = (magnitude + b/2) / b;
    return a < 0 ? -magnitude : magnitude;
}

/* MIT neighbour.rs availability: a block in an earlier packet, a non-intra
 * macroblock or outside the picture contributes no AC.  slot[0] stores QP;
 * zero means unavailable.  slot[1..7]/[9..15] hold its column/row. */
static nj_s2 *m4v_ac_neighbour(m4v_ac_state *ac, int mbx, int mby, int blk,
                               int above)
{
    int unit = blk < 4 ? 2 : 1;
    int x = mbx*unit + (blk < 4 ? blk%2 : 0) - !above;
    int y = mby*unit + (blk < 4 ? blk/2 : 0) - above;
    int owner;
    nj_s2 *plane, *slot;
    if (x < 0 || y < 0) return NULL;
    owner = (y/unit)*ac->mbw + x/unit;
    if (owner < ac->packet_start) return NULL;
    plane = blk < 4 ? ac->luma : (blk == 4 ? ac->cb : ac->cr);
    slot = plane + ((nj_size)y*(nj_size)(unit*ac->mbw) + (nj_size)x)*16u;
    return slot[0] > 0 ? slot : NULL;
}

static void m4v_ac_predict(m4v_ac_state *ac, nj_s4 *coef, int mbx, int mby,
                            int blk, int qscale, int dir)
{
    nj_s2 *source = m4v_ac_neighbour(ac, mbx, mby, blk, dir != 0);
    int k;
    if (source == NULL) return;
    for (k = 1; k < 8; k++) {
        int index = dir ? k : k*8;
        int prediction = source[(dir ? 8 : 0) + k];
        coef[index] += nj_m4v_rounded_div(prediction*source[0], qscale);
    }
}

static void m4v_ac_store(m4v_ac_state *ac, const nj_s4 *coef, int mbx, int mby,
                          int blk, int qscale)
{
    nj_s2 *destination = m4v_ac_slot(ac, mbx, mby, blk);
    int k;
    destination[0] = (nj_s2)qscale;
    for (k = 1; k < 8; k++) {
        destination[k] = (nj_s2)coef[k*8];
        destination[8+k] = (nj_s2)coef[k];
    }
}

/* Explicit validity replaces reference-implementation clearing bands. */
static void m4v_ac_clean_mb(m4v_ac_state *ac, int mbx, int mby)
{
    int block;
    for (block = 0; block < 6; block++)
        m4v_ac_slot(ac, mbx, mby, block)[0] = 0;
}

static void m4v_ac_clean_slice(m4v_ac_state *ac, int rx, int ry)
{
    ac->packet_start = ry*ac->mbw + rx;
}

/* MIT motion/neighbor semantics: skipped and intra MBs contribute zero MVs;
 * inter MBs supply the unavailable-intra DC default (1024 for 8-bit video). */
static void m4v_zero_mv(nj_s2 *mvx, nj_s2 *mvy, int wrap, int pad,
                         int mbx, int mby)
{
    int block;
    for (block = 0; block < 4; block++) {
        nj_size index = (nj_size)pad + (nj_size)(mby*2 + block/2)*(nj_size)wrap +
                       (nj_size)(mbx*2 + block%2);
        mvx[index] = mvy[index] = 0;
    }
}

static void m4v_clean_dc(nj_s2 *gl, int glw, nj_s2 *gc, nj_s2 *gcr, int gcw,
                          int mbx, int mby)
{
    int block;
    for (block = 0; block < 6; block++) {
        int unit = block < 4 ? 2 : 1;
        nj_s2 *plane = block < 4 ? gl : (block == 4 ? gc : gcr);
        int stride = block < 4 ? glw : gcw;
        int x = mbx*unit + (block < 4 ? block%2 : 0);
        int y = mby*unit + (block < 4 ? block/2 : 0);
        plane[(nj_size)y*(nj_size)stride + (nj_size)x] = 1024;
    }
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
    int    glw, gcw, lscale, cscale, qsc;
    int    rc = 0;
    m4v_ac_state *ac;

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
    qsc = (int)vop->qscale;
    ac = m4v_ac_new((int)mbw, (int)mbh);
    if (ac == NULL) {
        return -1;
    }
    {
        int wrap = (int)mbw * 2 + 1;
        int pad  = wrap + 2;
        nj_size mvn = (nj_size)pad + (nj_size)(mbh * 2u + 2u) * (nj_size)wrap;
        nj_s2 *mvx = (nj_s2 *)calloc((size_t)(mvn * 2), sizeof(nj_s2));
        nj_s2 *mvy = mvx != NULL ? mvx + mvn : NULL;

        if (mvx == NULL || mvy == NULL) {
            free(mvx);
            m4v_ac_free(ac);
            return -1;
        }
        {
            int mb_total = (int)(mbw * mbh);
            int cur = 0;
            int resync_x = 0, resync_y = 0;

            while (cur < mb_total) {
                int mbx = cur % (int)mbw;
                int mby = cur / (int)mbw;
                int first_slice_line = (mby == resync_y);
                int acb[6];
                int cbp = 0, dq = 0, cbpc = 0;
                int is_intra = 0;
                m4v_pmv pmv;

                memset(&pmv, 0, sizeof(pmv));
                rc = m4v_pvop_mb_header(b, n, pos, vol, vop, (nj_u4)mbx,
                                        (nj_u4)mby, mvx, mvy, wrap, pad,
                                        resync_y*(int)mbw + resync_x, &qsc, &cbp,
                                        &dq, &cbpc, &pmv);
                if (rc == 1) {
                    rc = m4v_copy_skipped_mb(mbx, mby, yp, ys, up, us,
                                             vp, vs, py, pys, pu, pus, pv, pvs);
                    if (rc != 0) {
                        break;
                    }
                    m4v_zero_mv(mvx, mvy, wrap, pad, mbx, mby);
                    m4v_clean_dc(gl, glw, gc, gcr, gcw, mbx, mby);
                } else if (rc == 0) {
                    is_intra = 1;
                    rc = m4v_intra_mb_body(b, n, pos, mbx, mby,
                                           gl, glw, gc, gcw, gcr, gcw,
                                           lscale, &qsc, acb, yp, ys, up, us,
                                           vp, vs, cbp, cbpc, dq,
                                           first_slice_line, resync_x,
                                           resync_y, ac, pmv.ac_pred);
                    if (rc != 0) {
                        break;
                    }
                    m4v_zero_mv(mvx, mvy, wrap, pad, mbx, mby);
                } else if (rc == 2) {
                    int fw = (int)mbw * 16;
                    int fh = (int)mbh * 16;
                    int cw2 = (int)mbw * 8;
                    int ch2 = (int)mbh * 8;

                    rc = m4v_inter_mb_body(b, n, pos, mbx, mby,
                                           py, pys, pu, pus, pv, pvs,
                                           yp, ys, up, us, vp, vs,
                                           fw, fh, cw2, ch2, cbp, qsc,
                                           pmv.mv, pmv.four_mv, vop->no_rounding);
                    if (rc != 0) {
                        break;
                    }
                    m4v_clean_dc(gl, glw, gc, gcr, gcw, mbx, mby);
                } else {
                    break;
                }
                if (!is_intra) {
                    m4v_ac_clean_mb(ac, mbx, mby);
                }
                    cur++;
                if (cur < mb_total && vol->resync_disabled == 0) {
                    int next = m4v_is_resync(b, n, *pos, mb_total, 1,
                                             vop->f_code);

                    if (next != 0) {
                        int mbnum = 0;
                        int hrc;

                        if (next == mb_total) {
                            rc = -1;    /* VOP would end before its last MB */
                            break;
                        }
                        hrc = nj_m4v_resync(b, n, pos, vol, 1, vop->f_code,
                                            &qsc, &mbnum);
                        if (hrc != 1 || mbnum != next || mbnum < cur) {
                            rc = -1;    /* marker, header and position disagree */
                            break;
                        }
                        cur = mbnum;
                        resync_x = cur % (int)mbw;
                        resync_y = cur / (int)mbw;
                        m4v_ac_clean_slice(ac, resync_x, resync_y);
                    }
                }
            }
        }
        free(mvx);
        m4v_ac_free(ac);
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
    int cbpy = 0;

    (void)dq;

    if (bp < n * 8u) {
        *acp = (int)((b[bp >> 3] >> (7u - (bp & 7u))) & 1u);
        *pos = bp + 1u;
    }
        if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
        return -1;
    }
        *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
    return 0;
}

static int m4v_intra_mb_body(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx, int mby,
                             nj_s2 *dc_y, int stride_y, nj_s2 *dc_c, int stride_c,
                             nj_s2 *dc_c2, int stride_c2,
                             int scale, int *qscale, int *ac_bits,
                             nj_u1 *yp, int ys, nj_u1 *up, int us,
                             nj_u1 *vp, int vs, int cbp, int cbpc, int dq,
                             int first_slice_line, int resync_x, int resync_y,
                             m4v_ac_state *ac, int ac_pred)
{
    int blk;
    nj_s4 all_coef[6][64];

    (void)cbpc;
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
        delta = (int)(((b[at >> 3] >> (7u - (at & 7u))) & 1u) << 1);
        delta |= (int)(b[(at + 1u) >> 3] >> (7u - ((at + 1u) & 7u))) & 1;
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
        for (blk = 0; blk < 6; blk++) {
        nj_s2 *grid = (blk < 4) ? dc_y : (blk == 4 ? dc_c : (dc_c2 != NULL ? dc_c2 : dc_c));
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
        /* MIT neighbour.rs: component coordinates map back to MB raster
         * indices.  No predictor may come from a preceding packet. */
        {
            int unit = blk < 4 ? 2 : 1;
            int width = ac != NULL ? ac->mbw : stride/unit;
            int packet = resync_y*width + resync_x;
            if (gx <= 0 || (gy/unit)*width + (gx-1)/unit < packet) a = 1024;
            if (gy <= 0 || ((gy-1)/unit)*width + gx/unit < packet) c = 1024;
            if (gx <= 0 || gy <= 0 ||
                ((gy-1)/unit)*width + (gx-1)/unit < packet) bq = 1024;
        }
        (void)first_slice_line;
        pred = nj_m4v_dc_predict_q(a, bq, c, dscale, &dir);
        rc = nj_m4v_decode_intra_block_ex(b, n, pos, coef, blk < 4, pred,
                                          *qscale, 0);
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
        /* Intra DC is dequantized before the inverse transform.  Its image
         * brightness is already present; idct_put adds no output bias. */
        all_coef[blk][0] = (nj_s4)nj_m4v_dc_coef((int)coef[0], (int)dscale);
        (void)scale;
        {
            int coded = nj_m4v_cbp_has_ac(cbp, blk), arc = 0;
            int scan = 0;

            if (ac_bits != NULL) {
                ac_bits[blk] = coded;
            }
            /* Figure 7-4 alternate scan follows the prediction direction. */
            if (ac != NULL && ac_pred != 0) {
                scan = (dir == 0) ? 2 : 1;
            }
            if (coded) {
                arc = nj_m4v_ac_block_scan(b, n, pos, all_coef[blk], 1,
                                           *qscale, scan);
                if (arc == -3) {
                    return -3;
                }
                if (arc < 0) {
                    return -1;
                }
            }
            if (ac != NULL) {
                if (ac_pred != 0) {
                    m4v_ac_predict(ac, all_coef[blk], mbx, mby, blk, *qscale,
                                   dir);
                }
                m4v_ac_store(ac, all_coef[blk], mbx, mby, blk, *qscale);
            }
        }
    }
    /* Prediction stays in the quantized domain; dequantize after storing
     * the neighbour coefficients for each of the six blocks. */
    {
        int coefficient;
        for (blk = 0; blk < 6; blk++)
            for (coefficient = 1; coefficient < 64; coefficient++)
                all_coef[blk][coefficient] = m4v_inverse_level(
                    all_coef[blk][coefficient], *qscale);
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

            nj_m4v_idct_put(all_coef[blk], out);
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
                    int v2 = out[r * 8 + cc2];
                    {
                        nj_size wi = (nj_size)(oy + r) * (nj_size)stride + (nj_size)(ox + cc2);
                                                dst[wi] = (nj_u1)v2;
                    }
                }
            }
        }
    }
    return 0;
}

/* One intra macroblock with geometric availability relative to packet start. */
static int m4v_intra_mb_pix_slice(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int mbx,
                                  int mby, nj_s2 *dc_y, int stride_y, nj_s2 *dc_c,
                                  int stride_c, nj_s2 *dc_c2, int stride_c2,
                                  int scale, int *qscale, int *ac_bits,
                                  nj_u1 *yp, int ys, nj_u1 *up, int us,
                                  nj_u1 *vp, int vs, int first_slice_line,
                                  int resync_x, int resync_y, m4v_ac_state *ac)
{
    int cbp = 0, cbpc = 0, dq = 0, acp = 0;

    if (nj_m4v_mb_core(b, n, pos, &cbpc, &dq) != 0) {
        return -1;
    }
    if (m4v_read_cbp(b, n, pos, cbpc, dq, &cbp, &acp) != 0) {
        return -1;
    }
    return m4v_intra_mb_body(b, n, pos, mbx, mby, dc_y, stride_y, dc_c, stride_c,
                             dc_c2, stride_c2, scale, qscale, ac_bits,
                             yp, ys, up, us, vp, vs, cbp, cbpc, dq,
                             first_slice_line, resync_x, resync_y, ac, acp);
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
                             yp, ys, up, us, vp, vs, cbp, cbpc, dq,
                             (mby == 0), 0, 0, NULL, 0);
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


/* ------------------------------------------------------------------ */
/* The nj_avcodec video provider                                       */
/* ------------------------------------------------------------------ */
/*
 * The runtime's video seam is provider-shaped (include/nj2me/nj_avcodec.h):
 * accepts() answers "this is mine", open() parses the container and the
 * decoder configuration, decode() consumes one container sample and writes one
 * RGB565 frame.  The MPEG-4 Part 2 provider is registered next to H.263 by
 * nj_avcodec_reset(), and because later installs are tried first it is offered
 * every ISO base media file before the H.263 provider: its accepts() therefore
 * has to identify mp4v precisely instead of claiming every ftyp container.
 *
 * The coded picture is always a whole number of 16x16 macroblocks, while the
 * VOL's width and height may not be (Tom Clancy's H.A.W.X. is 240x180).  The
 * decoder reconstructs the padding rows and crops them away when it
 * outputs the frame, so the provider keeps the planes at their coded size and
 * converts only the visible rectangle to RGB565.
 */

#define M4V_ERR_OUTPUT (-100)   /* provider-specific: frame buffer too small */

typedef struct m4v_provider {
    const nj_av_mem *mem;
    const nj_u1     *data;      /* the caller's whole container, kept alive */
    nj_u4            len;
    nj_mp4           mp4;
    nj_m4v_vol       vol;

    nj_u4 width;                /* visible size, from the VOL                */
    nj_u4 height;
    nj_u4 y_stride;             /* coded width: mbw * 16                     */
    nj_u4 c_stride;             /* coded chroma width: mbw * 8               */
    nj_u4 c_rows;               /* coded chroma height: mbh * 8              */
    nj_u4 y_bytes;              /* coded luma plane bytes                    */
    nj_u4 c_bytes;              /* coded chroma plane bytes                  */

    nj_s4 *grids;               /* DC predictor state, carried across frames */
    nj_u1 *front_y, *front_u, *front_v;   /* last decoded picture           */
    nj_u1 *work_y, *work_u, *work_v;      /* the one being decoded          */
    int    have_front;
} m4v_provider;

/* Finds the VOL inside the decoder configuration bytes and parses it. */
static int m4v_find_vol(const nj_u1 *b, nj_u4 n, nj_m4v_vol *vol)
{
    nj_u4 code = 0, pos = 0, from = 0;

    if (b == NULL || n == 0u || vol == NULL) {
        return 0;
    }
    while (nj_m4v_start_code(b, n, from, &code, &pos)) {
        if (code == NJ_M4V_VOL) {
            return nj_m4v_parse_vol(b, n, pos, vol);
        }
        from = pos;
    }
    return 0;
}

/* The feature set this decoder implements: rectangular, progressive, H.263
 * quantisation, no data partitioning, no reversible VLC, no quarter sample,
 * no sprites.  Everything the corpus and the Simple Profile need. */
static int m4v_vol_supported(const nj_m4v_vol *vol)
{
    return vol->shape == 0 && vol->interlaced == 0 &&
           vol->data_partitioned == 0 && vol->reversible_vlc == 0 &&
           vol->quarter_sample == 0 && vol->sprite_usage == 0 &&
           vol->width >= 16u && vol->height >= 16u &&
           vol->width <= 4096u && vol->height <= 4096u;
}

static int m4v_p_accepts(const nj_u1 *data, nj_u4 len)
{
    nj_mp4     m;
    nj_m4v_vol vol;

    if (data == NULL || len < 8u) {
        return 0;
    }
    /* Cheap reject first: the ISO base media signature is at offset 4. */
    if (data[4] != 'f' || data[5] != 't' || data[6] != 'y' || data[7] != 'p') {
        return 0;
    }
    if (nj_mp4_open(&m, data, len) != NJ_MP4_OK) {
        return 0;
    }
    if (m.codec != NJ_MP4_CODEC_MP4V || m.cfg_size == 0u) {
        return 0;
    }
    if (!m4v_find_vol(data + m.cfg_at, m.cfg_size, &vol)) {
        return 0;
    }
    return m4v_vol_supported(&vol);
}

static void m4v_p_free(m4v_provider *d)
{
    const nj_av_mem *mem;
    nj_av_free_fn    fr;

    if (d == NULL) {
        return;
    }
    mem = d->mem;
    if (mem == NULL || mem->free == NULL) {
        return;
    }
    fr = mem->free;
    fr(mem->ud, d->grids);
    fr(mem->ud, d->front_y);
    fr(mem->ud, d->front_u);
    fr(mem->ud, d->front_v);
    fr(mem->ud, d->work_y);
    fr(mem->ud, d->work_u);
    fr(mem->ud, d->work_v);
    fr(mem->ud, d);
}

static nj_av_dec *m4v_p_open(const nj_u1 *data, nj_u4 len,
                             const nj_av_mem *mem, nj_av_info *info,
                             const char **err)
{
    m4v_provider *d;
    nj_mp4_sample s;
    nj_u4 glw, gcw, grid_count;
    int rc;

    if (err != NULL) {
        *err = NULL;
    }
    if (data == NULL || len < 8u || mem == NULL ||
        mem->alloc == NULL || mem->free == NULL) {
        return NULL;
    }
    d = (m4v_provider *)mem->alloc(mem->ud, (nj_u4)sizeof(*d));
    if (d == NULL) {
        if (err != NULL) {
            *err = "MPEG-4: out of memory";
        }
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->mem = mem;
    d->data = data;
    d->len = len;

    rc = nj_mp4_open(&d->mp4, data, len);
    if (rc != NJ_MP4_OK) {
        if (err != NULL) {
            if (rc == NJ_MP4_ERR_NO_VIDEO) {
                *err = "MPEG-4: the file has no video track";
            } else if (rc == NJ_MP4_ERR_FEATURE) {
                *err = "MPEG-4: unsupported movie layout";
            } else {
                *err = "MPEG-4: corrupt ISO base media file";
            }
        }
        m4v_p_free(d);
        return NULL;
    }
    if (d->mp4.codec != NJ_MP4_CODEC_MP4V) {
        if (err != NULL) {
            *err = "MPEG-4: the video track is not MPEG-4 Part 2";
        }
        m4v_p_free(d);
        return NULL;
    }
    if (d->mp4.cfg_size == 0u) {
        if (err != NULL) {
            *err = "MPEG-4: the video track carries no decoder configuration";
        }
        m4v_p_free(d);
        return NULL;
    }
    if (!m4v_find_vol(data + d->mp4.cfg_at, d->mp4.cfg_size, &d->vol)) {
        if (err != NULL) {
            *err = "MPEG-4: the decoder configuration has no usable VOL";
        }
        m4v_p_free(d);
        return NULL;
    }
    if (!m4v_vol_supported(&d->vol)) {
        if (err != NULL) {
            *err = "MPEG-4: only Simple Profile rectangular progressive video "
                   "is decoded";
        }
        m4v_p_free(d);
        return NULL;
    }
    {
        nj_u4 mbw = 0, mbh = 0;

        if (nj_m4v_frame_macroblocks(d->vol.width, d->vol.height, &mbw, &mbh) != 0) {
            if (err != NULL) {
                *err = "MPEG-4: unsupported picture size";
            }
            m4v_p_free(d);
            return NULL;
        }
        d->width = d->vol.width;
        d->height = d->vol.height;
        d->y_stride = mbw * 16u;
        d->c_stride = mbw * 8u;
        d->c_rows = mbh * 8u;
        d->y_bytes = d->y_stride * mbh * 16u;
        d->c_bytes = d->c_stride * d->c_rows;
        glw = mbw * 2u;
        gcw = mbw;
        grid_count = glw * (mbh * 2u) + gcw * (mbh * 2u);
        d->grids = (nj_s4 *)mem->alloc(mem->ud,
                                      grid_count * (nj_u4)sizeof(nj_s2));
    }
    d->front_y = (nj_u1 *)mem->alloc(mem->ud, d->y_bytes);
    d->front_u = (nj_u1 *)mem->alloc(mem->ud, d->c_bytes);
    d->front_v = (nj_u1 *)mem->alloc(mem->ud, d->c_bytes);
    d->work_y = (nj_u1 *)mem->alloc(mem->ud, d->y_bytes);
    d->work_u = (nj_u1 *)mem->alloc(mem->ud, d->c_bytes);
    d->work_v = (nj_u1 *)mem->alloc(mem->ud, d->c_bytes);
    if (d->grids == NULL || d->front_y == NULL || d->front_u == NULL ||
        d->front_v == NULL || d->work_y == NULL || d->work_u == NULL ||
        d->work_v == NULL) {
        if (err != NULL) {
            *err = "MPEG-4: out of memory";
        }
        m4v_p_free(d);
        return NULL;
    }
    memset(d->grids, 0, (size_t)grid_count * sizeof(nj_s2));
    /* The iteration starts at the first sample both here and in decode(). */
    rc = nj_mp4_next(&d->mp4, &s);
    if (rc != 1 || s.size < 4u) {
        if (err != NULL) {
            *err = "MPEG-4: the video track has no usable samples";
        }
        m4v_p_free(d);
        return NULL;
    }
    nj_mp4_rewind(&d->mp4);
    d->have_front = 0;
    if (info != NULL) {
        nj_u8 ms;

        memset(info, 0, sizeof(*info));
        info->format = NJ_AV_VIDEO_MPEG4;
        info->width = d->width;
        info->height = d->height;
        info->fps = d->mp4.fps != 0u ? d->mp4.fps : 25u;
        info->frames = d->mp4.sample_count;
        info->pixfmt = NJ_AV_PIX_RGB565;
        if (d->mp4.timescale != 0u && d->mp4.duration != 0u) {
            ms = ((nj_u8)d->mp4.duration * 1000u + d->mp4.timescale / 2u) /
                 d->mp4.timescale;
            info->duration_ms = ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : (nj_u4)ms;
        }
    }
    return (nj_av_dec *)d;
}

static int m4v_p_decode(nj_av_dec *dec, nj_av_step *step)
{
    m4v_provider *d = (m4v_provider *)dec;
    nj_mp4_sample s;
    nj_m4v_vop    vop;
    nj_u4         vpos, bitpos;
    int           rc;

    if (d == NULL || step == NULL) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (step->out == NULL ||
        (nj_u8)step->out_cap < (nj_u8)d->width * d->height * 2u) {
        return M4V_ERR_OUTPUT;
    }
    step->out_used = 0u;
    step->frames = 0u;
    step->in_used = 0u;
    step->time_us = -1;

    rc = nj_mp4_next(&d->mp4, &s);
    if (rc == 0) {
        return NJ_AV_ERR_END;
    }
    if (rc < 0) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (s.offset >= d->len || s.size > d->len - s.offset) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (nj_m4v_find_vop(d->data + s.offset, s.size, &vpos) <= 0) {
        return NJ_AV_ERR_CORRUPT;
    }
    bitpos = vpos * 8u;
    if (!nj_m4v_parse_vop_bits_vol(d->data + s.offset, s.size, &bitpos,
                                   &d->vol, &vop)) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (vop.type != 0 && vop.type != 1) {
        return NJ_AV_ERR_CORRUPT;   /* B-VOPs are not Simple Profile */
    }
    if (!vop.coded) {
        /* An uncoded VOP repeats the preceding picture. */
        if (!d->have_front) {
            return NJ_AV_ERR_CORRUPT;
        }
        memcpy(d->work_y, d->front_y, (size_t)d->y_bytes);
        memcpy(d->work_u, d->front_u, (size_t)d->c_bytes);
        memcpy(d->work_v, d->front_v, (size_t)d->c_bytes);
    } else if (vop.type == 0) {
        rc = nj_m4v_decode_ivop(d->data + s.offset, s.size, &bitpos, &d->vol,
                                &vop, d->grids,
                                d->work_y, (int)d->y_stride,
                                d->work_u, (int)d->c_stride,
                                d->work_v, (int)d->c_stride);
        if (rc != 0) {
            return NJ_AV_ERR_CORRUPT;
        }
    } else {
        rc = nj_m4v_decode_pvop(d->data + s.offset, s.size, &bitpos, &d->vol,
                                &vop, d->grids,
                                d->work_y, (int)d->y_stride,
                                d->work_u, (int)d->c_stride,
                                d->work_v, (int)d->c_stride,
                                d->front_y, (int)d->y_stride,
                                d->front_u, (int)d->c_stride,
                                d->front_v, (int)d->c_stride);
        if (rc != 0) {
            return rc == -3 ? NJ_AV_ERR_UNSUPPORTED : NJ_AV_ERR_CORRUPT;
        }
    }
    nj_m4v_yuv420_to_rgb565(d->work_y, (int)d->y_stride,
                            d->work_u, (int)d->c_stride,
                            d->work_v, (int)d->c_stride,
                            (int)d->width, (int)d->height, step->out);
    step->out_used = d->width * d->height * 2u;
    step->frames = 1u;
    if (d->mp4.timescale != 0u) {
        step->time_us = (nj_s8)(((nj_u8)s.dts * 1000000u +
                                 d->mp4.timescale / 2u) / d->mp4.timescale);
    }
    {
        nj_u1 *t;

        t = d->front_y; d->front_y = d->work_y; d->work_y = t;
        t = d->front_u; d->front_u = d->work_u; d->work_u = t;
        t = d->front_v; d->front_v = d->work_v; d->work_v = t;
    }
    d->have_front = 1;
    return NJ_AV_OK;
}

static void m4v_p_close(nj_av_dec *dec)
{
    m4v_p_free((m4v_provider *)dec);
}

const nj_av_video_ops nj_av_video_mpeg4 = {
    "mpeg4-part2",
    NJ_AV_VIDEO_MPEG4,
    m4v_p_accepts,
    m4v_p_open,
    m4v_p_decode,
    m4v_p_close
};

#endif /* NJ_ENABLE_M4V */
