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
#include "nj2me/nj_avcodec.h"
#include "nj_mp4.h"
#include "nj_vlc_intra.h"
#include "nj_vlc_inter.h"

#define NJ_M4V_DC_ERR (-0x7fffffff)
#include <stdio.h>
#include <stdlib.h>
#include "nj2me/nj_m4v_tables.h"
#include "nj2me/nj_m4v_escape.h"

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
    out->no_rounding = 0;
    if (!m4v_bits_get(&s, 2, &v)) {
        return 0;
    }
    type = (int)v;
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
            return 0;
    }
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bit(&s, &bit)) {
        return 0;
    }
    coded = bit;
        /* The order below is the one decode_vop_header() uses, and every field
     * has to be read even when its value is unused: they all sit between
     * vop_coded and the macroblock layer. */
    if (coded && shape != 1 && type == 1) {
        if (!m4v_bit(&s, &bit)) {
            return 0;           /* rounding_type: P-VOPs only */
        }
        out->no_rounding = bit;
    }
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


/* ------------------------------------------------------------------ */
/* Video packet (resync) headers, ISO/IEC 14496-2 6.2.5.2              */
/* ------------------------------------------------------------------ */
/*
 * A VOP encoded with resynchronisation is split into video packets.  Every
 * packet after the first starts with: a stuffing byte (a zero bit then ones to
 * the next byte boundary), a zero prefix 16 bits long for an I-VOP or
 * f_code+15 bits long for a P-VOP, a one bit, the absolute macroblock number,
 * the quantiser (quant_precision bits) and a header-extension flag.  The
 * reference decoder detects the marker while still decoding the macroblock
 * (mpeg4_is_resync), returns SLICE_END for the current macroblock and then
 * parses the header from the byte-aligned position (ff_h263_resync ->
 * ff_mpeg4_decode_video_packet_header).  This is that logic; the quantiser and
 * the predictor rules at the packet boundary are the caller's job.
 */
static int m4v_packet_prefix_len(int pict_type, int f_code)
{
    if (pict_type == 0) {
        return 16;                              /* I-VOP */
    }
    return 15 + (f_code < 1 ? 1 : f_code);      /* P-VOP */
}

/* Bit width of an absolute macroblock number (av_log2(mb_total - 1) + 1). */
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

static int m4v_peek_bit(const nj_u1 *b, nj_u4 n, nj_u4 pos)
{
    if (pos >= n * 8u) {
        return 0;
    }
    return (int)((b[pos >> 3] >> (7u - (pos & 7u))) & 1u);
}

static nj_u4 m4v_peek16(const nj_u1 *b, nj_u4 n, nj_u4 pos)
{
    nj_u4 v = 0;
    int   i;

    for (i = 0; i < 16; i++) {
        v = (v << 1) | (nj_u4)m4v_peek_bit(b, n, pos + (nj_u4)i);
    }
    return v;
}

/* mpeg4_is_resync(): detects the packet marker at exactly `pos' (the bit after
 * the last macroblock's data) and validates the announced macroblock number.
 * Returns that number (1..mb_total, mb_total meaning "end of the VOP") or 0
 * when the bits there are not a well-formed marker.  Does not advance `pos'. */
static int m4v_is_resync(const nj_u1 *b, nj_u4 n, nj_u4 pos, int mb_total,
                         int pict_type, int f_code)
{
    static const nj_u2 prefix_tab[8] = {
        0x7F00, 0x7E00, 0x7C00, 0x7800, 0x7000, 0x6000, 0x4000, 0x0000
    };
    nj_u4 at = pos;
    int   bits_count = (int)pos;
    int   ptype = pict_type + 1;    /* reference picture-type enum: I=1, P=2 */
    nj_u4 v = m4v_peek16(b, n, at);
    int   len, mb_num, mb_bits;

    while (v <= 0xFFu) {
        if (ptype >= 8 || (v >> (8 - ptype)) != 1u) {
            break;
        }
        at += 8u + (nj_u4)ptype;
        bits_count += 8 + ptype;
        v = m4v_peek16(b, n, at);
    }
    if ((nj_u4)bits_count + 8u >= n * 8u) {
        /* End of the VOP: reference sees the padding pattern 0x7F. */
        v >>= 8;
        v |= (nj_u4)(0x7F >> (7 - (bits_count & 7)));
        return v == 0x7Fu ? mb_total : 0;
    }
    if (v != (nj_u4)prefix_tab[bits_count & 7]) {
        return 0;
    }
    /* Peek the header: skip the terminator bit, align to a byte, read the
     * unary prefix length and the macroblock number. */
    at += 1u;
    at = (at + 7u) & ~7u;
    for (len = 0; len < 32; len++) {
        if (m4v_peek_bit(b, n, at + (nj_u4)len) != 0) {
            break;
        }
    }
    mb_bits = m4v_mb_num_bits(mb_total);
    mb_num = 0;
    {
        int i;
        /* get_bits1() consumed the terminating one bit, so the number starts
         * one bit after the last counted zero. */
        nj_u4 p = at + (nj_u4)len + 1u;

        for (i = 0; i < mb_bits; i++) {
            mb_num = (mb_num << 1) | m4v_peek_bit(b, n, p + (nj_u4)i);
        }
        if (len < 32 && (mb_num <= 0 || mb_num > mb_total ||
                         p + (nj_u4)(mb_bits + 6) > n * 8u)) {
            mb_num = -1;
        }
    }
    if (len >= m4v_packet_prefix_len(pict_type, f_code)) {
        return mb_num;
    }
    return 0;
}

/* ff_h263_resync() + ff_mpeg4_decode_video_packet_header() for a rectangular
 * stream: skip the stuffing, parse the packet header and advance *pos to the
 * first macroblock of the packet.  Returns 1 with *mbnum set, -2 when the
 * optional header extension is present (not supported), -1 on damage. */
int nj_m4v_resync(const nj_u1 *b, nj_u4 n, nj_u4 *pos, const nj_m4v_vol *vol,
                  int pict_type, int f_code, int *qscale, int *mbnum)
{
    nj_u4 at;
    nj_u4 mbw = 0, mbh = 0;
    int   mb_total, len, mb_bits, v, q, ext = 0;

    if (b == NULL || pos == NULL || vol == NULL || qscale == NULL ||
        mbnum == NULL) {
        return -1;
    }
    if (nj_m4v_frame_macroblocks(vol->width, vol->height, &mbw, &mbh) != 0) {
        return -1;
    }
    mb_total = (int)(mbw * mbh);
    /* ff_h263_resync: skip one bit and byte-align. */
    at = (*pos + 1u + 7u) & ~7u;
    /* Primary path: the encoder flushed the slice, so the prefix starts at the
     * aligned position.  The reference falls back to scanning from its last
     * resync point; a stream that needs that is refused rather than guessed. */
    if (m4v_peek16(b, n, at) != 0u) {
        return -1;
    }
    for (len = 0; len < 32; len++) {
        if (at + (nj_u4)len >= n * 8u) {
            return -1;
        }
        if (m4v_peek_bit(b, n, at + (nj_u4)len) != 0) {
            break;
        }
    }
    if (len != m4v_packet_prefix_len(pict_type, f_code)) {
        return -1;
    }
    at += (nj_u4)len + 1u;              /* prefix zeros and the one bit */
    mb_bits = m4v_mb_num_bits(mb_total);
    if (at + (nj_u4)(mb_bits + vol->quant_precision + 1) > n * 8u) {
        return -1;
    }
    v = 0;
    {
        int i;

        for (i = 0; i < mb_bits; i++) {
            v = (v << 1) | m4v_peek_bit(b, n, at + (nj_u4)i);
        }
        at += (nj_u4)mb_bits;
    }
    if (v <= 0 || v >= mb_total) {
        return -1;
    }
    q = 0;
    {
        int i;

        for (i = 0; i < vol->quant_precision; i++) {
            q = (q << 1) | m4v_peek_bit(b, n, at + (nj_u4)i);
        }
        at += (nj_u4)vol->quant_precision;
    }
    if (q > 0) {
        *qscale = q;
    }
    ext = m4v_peek_bit(b, n, at);
    at += 1u;
    if (ext != 0) {
        *pos = at;
        return -2;                      /* header extension: not supported */
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


/* The two alternate scans an intra block uses when AC prediction is on
 * (ISO/IEC 14496-2 7.4.3.4, ff_mpeg4's permutated_intra_h/v_scantable built
 * from ff_alternate_horizontal_scan / ff_alternate_vertical_scan).  They are
 * not plain row- and column-major orders. */
static const nj_u1 nj_m4v_scan_h[64] = {
     0,  1,  2,  3,  8,  9, 16, 17,
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33,
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49,
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59,
    52, 53, 54, 55, 60, 61, 62, 63
};

static const nj_u1 nj_m4v_scan_v[64] = {
     0,  8, 16, 24,  1,  9,  2, 10,
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12,
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14,
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31,
    38, 46, 54, 62, 39, 47, 55, 63
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
int nj_m4v_ac_block_scan(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef,
                         int k, int qscale, int scan_mode)
{
    int i, attempt;

    (void)k;
    if (b == NULL || pos == NULL || coef == NULL || qscale <= 0) {
        return -1;
    }
    i = 0;
    for (attempt = 0; attempt < 64; attempt++) {
        nj_u4 w = m4v_peek12(b, n, *pos);
        int run, level = 0, last = 0;
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
            if (f1 == 0u) {
                form = 1;               /* first escape: a single 0 bit */
            } else {
                if (!m4v_rbits(b, n, pos, 1, &f2)) {
                    return -1;
                }
                form = f2 ? 3 : 2;      /* 10 second, 11 third */
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
                {
                    nj_u4 sign = 0;

                    if (!m4v_rbits(b, n, pos, 1, &sign)) {
                        return -1;
                    }
                    level = ((nj_s4)level ^ -(nj_s4)sign) + (nj_s4)sign;
                }
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
                {
                    nj_u4 sign = 0;

                    if (!m4v_rbits(b, n, pos, 1, &sign)) {
                        return -1;
                    }
                    level = ((nj_s4)level ^ -(nj_s4)sign) + (nj_s4)sign;
                }
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
                /* The third escape carries its own sign inside the 12-bit
                 * level field; the reference does not read another sign bit
                 * for it (mpeg4_decode_block). */
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
            coef[nj_m4v_scan_index_mode(i, scan_mode)] = (nj_s4)level;
            return 0;
        }
        if (i < 0 || i > 63) {
            return -1;
        }
        coef[nj_m4v_scan_index_mode(i, scan_mode)] = (nj_s4)level;
    }
    return -1;
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

/* ------------------------------------------------------------------ */
/* The inverse transform: FFmpeg's Simple IDCT, transcribed            */
/* ------------------------------------------------------------------ */
/*
 * The shared integer IDCT in src/midp/nj_idct.h is a basis-matrix transform
 * accurate to about one level; the reference decoder for MPEG-4 Part 2 uses
 * the Simple IDCT (simple_idct_template.c), and a different rounding leaves a
 * one-level difference on a few percent of samples.  For pixel-exact output
 * the arithmetic is reproduced here: 16-bit row pass with a DC shortcut, then
 * an unsigned column butterfly with the reference's own rounding constants
 * (W1..W7, ROW_SHIFT 11, COL_SHIFT 20, DC_SHIFT 3, and the
 * (1 << 19) / W4 = 32 offset in the column pass).  `put' writes pixels (the
 * intra path, whose DC carries the mid-grey bias in the coefficient domain),
 * `add' produces the residual the inter path adds to its prediction.
 */
#define NJ_M4V_IDCT_W1 22725
#define NJ_M4V_IDCT_W2 21407
#define NJ_M4V_IDCT_W3 19266
#define NJ_M4V_IDCT_W4 16383
#define NJ_M4V_IDCT_W5 12873
#define NJ_M4V_IDCT_W6  8867
#define NJ_M4V_IDCT_W7  4520
#define NJ_M4V_IDCT_ROW_SHIFT 11
#define NJ_M4V_IDCT_COL_SHIFT 20
#define NJ_M4V_IDCT_DC_SHIFT  3
#define NJ_M4V_IDCT_COL_BIAS  ((1 << (NJ_M4V_IDCT_COL_SHIFT - 1)) / NJ_M4V_IDCT_W4)

/* One row: the reference's idctRowCondDC with extra_shift 0. */
static void m4v_idct_row(nj_s2 *row)
{
    unsigned a0, a1, a2, a3, b0, b1, b2, b3;
    int      i;

    if (row[1] == 0 && row[2] == 0 && row[3] == 0 && row[4] == 0 &&
        row[5] == 0 && row[6] == 0 && row[7] == 0) {
        nj_s2 temp = (nj_s2)((int)row[0] * (1 << NJ_M4V_IDCT_DC_SHIFT));

        for (i = 0; i < 8; i++) {
            row[i] = temp;
        }
        return;
    }
    a0 = (unsigned)NJ_M4V_IDCT_W4 * (unsigned)row[0] +
         (1u << (NJ_M4V_IDCT_ROW_SHIFT - 1));
    a1 = a0;
    a2 = a0;
    a3 = a0;

    a0 += (unsigned)NJ_M4V_IDCT_W2 * (unsigned)row[2];
    a1 += (unsigned)NJ_M4V_IDCT_W6 * (unsigned)row[2];
    a2 -= (unsigned)NJ_M4V_IDCT_W6 * (unsigned)row[2];
    a3 -= (unsigned)NJ_M4V_IDCT_W2 * (unsigned)row[2];

    b0 = (unsigned)NJ_M4V_IDCT_W1 * (unsigned)row[1];
    b0 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)row[3];
    b1 = (unsigned)NJ_M4V_IDCT_W3 * (unsigned)row[1];
    b1 -= (unsigned)NJ_M4V_IDCT_W7 * (unsigned)row[3];
    b2 = (unsigned)NJ_M4V_IDCT_W5 * (unsigned)row[1];
    b2 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)row[3];
    b3 = (unsigned)NJ_M4V_IDCT_W7 * (unsigned)row[1];
    b3 -= (unsigned)NJ_M4V_IDCT_W5 * (unsigned)row[3];

    if (row[4] != 0 || row[5] != 0 || row[6] != 0 || row[7] != 0) {
        a0 += (unsigned)NJ_M4V_IDCT_W4 * (unsigned)row[4];
        a0 += (unsigned)NJ_M4V_IDCT_W6 * (unsigned)row[6];
        a1 -= (unsigned)NJ_M4V_IDCT_W4 * (unsigned)row[4];
        a1 -= (unsigned)NJ_M4V_IDCT_W2 * (unsigned)row[6];
        a2 -= (unsigned)NJ_M4V_IDCT_W4 * (unsigned)row[4];
        a2 += (unsigned)NJ_M4V_IDCT_W2 * (unsigned)row[6];
        a3 += (unsigned)NJ_M4V_IDCT_W4 * (unsigned)row[4];
        a3 -= (unsigned)NJ_M4V_IDCT_W6 * (unsigned)row[6];

        b0 += (unsigned)NJ_M4V_IDCT_W5 * (unsigned)row[5];
        b0 += (unsigned)NJ_M4V_IDCT_W7 * (unsigned)row[7];
        b1 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)row[5];
        b1 -= (unsigned)NJ_M4V_IDCT_W5 * (unsigned)row[7];
        b2 += (unsigned)NJ_M4V_IDCT_W7 * (unsigned)row[5];
        b2 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)row[7];
        b3 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)row[5];
        b3 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)row[7];
    }
    row[0] = (nj_s2)((int)(a0 + b0) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[7] = (nj_s2)((int)(a0 - b0) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[1] = (nj_s2)((int)(a1 + b1) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[6] = (nj_s2)((int)(a1 - b1) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[2] = (nj_s2)((int)(a2 + b2) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[5] = (nj_s2)((int)(a2 - b2) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[3] = (nj_s2)((int)(a3 + b3) >> NJ_M4V_IDCT_ROW_SHIFT);
    row[4] = (nj_s2)((int)(a3 - b3) >> NJ_M4V_IDCT_ROW_SHIFT);
}

/*
 * The column butterfly (IDCT_COLS followed by the output store).  `mode' is 0
 * for the residual (idctSparseCol / idctSparseColAdd) and 1 for put, which is
 * the same arithmetic; the caller decides whether to clip and offset.
 */
#define NJ_M4V_IDCT_COLS \
    do { \
        a0 = (unsigned)NJ_M4V_IDCT_W4 * \
             (unsigned)(col[0] + NJ_M4V_IDCT_COL_BIAS); \
        a1 = a0; \
        a2 = a0; \
        a3 = a0; \
        a0 += (unsigned)NJ_M4V_IDCT_W2 * (unsigned)col[16]; \
        a1 += (unsigned)NJ_M4V_IDCT_W6 * (unsigned)col[16]; \
        a2 -= (unsigned)NJ_M4V_IDCT_W6 * (unsigned)col[16]; \
        a3 -= (unsigned)NJ_M4V_IDCT_W2 * (unsigned)col[16]; \
        b0 = (unsigned)NJ_M4V_IDCT_W1 * (unsigned)col[8]; \
        b1 = (unsigned)NJ_M4V_IDCT_W3 * (unsigned)col[8]; \
        b2 = (unsigned)NJ_M4V_IDCT_W5 * (unsigned)col[8]; \
        b3 = (unsigned)NJ_M4V_IDCT_W7 * (unsigned)col[8]; \
        b0 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)col[24]; \
        b1 -= (unsigned)NJ_M4V_IDCT_W7 * (unsigned)col[24]; \
        b2 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)col[24]; \
        b3 -= (unsigned)NJ_M4V_IDCT_W5 * (unsigned)col[24]; \
        if (col[32] != 0) { \
            a0 += (unsigned)NJ_M4V_IDCT_W4 * (unsigned)col[32]; \
            a1 -= (unsigned)NJ_M4V_IDCT_W4 * (unsigned)col[32]; \
            a2 -= (unsigned)NJ_M4V_IDCT_W4 * (unsigned)col[32]; \
            a3 += (unsigned)NJ_M4V_IDCT_W4 * (unsigned)col[32]; \
        } \
        if (col[40] != 0) { \
            b0 += (unsigned)NJ_M4V_IDCT_W5 * (unsigned)col[40]; \
            b1 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)col[40]; \
            b2 += (unsigned)NJ_M4V_IDCT_W7 * (unsigned)col[40]; \
            b3 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)col[40]; \
        } \
        if (col[48] != 0) { \
            a0 += (unsigned)NJ_M4V_IDCT_W6 * (unsigned)col[48]; \
            a1 -= (unsigned)NJ_M4V_IDCT_W2 * (unsigned)col[48]; \
            a2 += (unsigned)NJ_M4V_IDCT_W2 * (unsigned)col[48]; \
            a3 -= (unsigned)NJ_M4V_IDCT_W6 * (unsigned)col[48]; \
        } \
        if (col[56] != 0) { \
            b0 += (unsigned)NJ_M4V_IDCT_W7 * (unsigned)col[56]; \
            b1 -= (unsigned)NJ_M4V_IDCT_W5 * (unsigned)col[56]; \
            b2 += (unsigned)NJ_M4V_IDCT_W3 * (unsigned)col[56]; \
            b3 -= (unsigned)NJ_M4V_IDCT_W1 * (unsigned)col[56]; \
        } \
    } while (0)

static void m4v_idct_rows(nj_s2 *bb)
{
    int i;

    for (i = 0; i < 8; i++) {
        m4v_idct_row(bb + i * 8);
    }
}

/*
 * Residual in the pixel domain (FFmpeg ff_simple_idct / the add path's
 * residual): the column outputs of the Simple IDCT, without the clipping and
 * without the 128 offset.
 */
void nj_m4v_idct(const nj_s4 *coef, int *out)
{
    nj_s2 bb[64];
    int   i;

    if (coef == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < 64; i++) {
        bb[i] = (nj_s2)coef[i];
    }
    m4v_idct_rows(bb);
    for (i = 0; i < 8; i++) {
        unsigned a0, a1, a2, a3, b0, b1, b2, b3;
        nj_s2 *col = bb + i;

        NJ_M4V_IDCT_COLS;
        out[i]      = (int)(a0 + b0) >> NJ_M4V_IDCT_COL_SHIFT;
        out[8 + i]  = (int)(a1 + b1) >> NJ_M4V_IDCT_COL_SHIFT;
        out[16 + i] = (int)(a2 + b2) >> NJ_M4V_IDCT_COL_SHIFT;
        out[24 + i] = (int)(a3 + b3) >> NJ_M4V_IDCT_COL_SHIFT;
        out[32 + i] = (int)(a3 - b3) >> NJ_M4V_IDCT_COL_SHIFT;
        out[40 + i] = (int)(a2 - b2) >> NJ_M4V_IDCT_COL_SHIFT;
        out[48 + i] = (int)(a1 - b1) >> NJ_M4V_IDCT_COL_SHIFT;
        out[56 + i] = (int)(a0 - b0) >> NJ_M4V_IDCT_COL_SHIFT;
    }
}

/*
 * The put variant (FFmpeg ff_simple_idct_put_int16_8bit): the coefficient
 * block of an intra macroblock already carries the 1024 mid-grey bias in its
 * DC term (that is what the reference's dct_unquantize_h263_intra produces),
 * so the column output *is* the pixel, clipped to 0..255.
 */
void nj_m4v_idct_put(const nj_s4 *coef, int *out)
{
    nj_s2 bb[64];
    int   i;

    if (coef == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < 64; i++) {
        bb[i] = (nj_s2)coef[i];
    }
    m4v_idct_rows(bb);
    for (i = 0; i < 8; i++) {
        unsigned a0, a1, a2, a3, b0, b1, b2, b3;
        const nj_s2 *col = bb + i;
        int v;

        NJ_M4V_IDCT_COLS;
        v = (int)(a0 + b0) >> NJ_M4V_IDCT_COL_SHIFT;
        out[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a1 + b1) >> NJ_M4V_IDCT_COL_SHIFT;
        out[8 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a2 + b2) >> NJ_M4V_IDCT_COL_SHIFT;
        out[16 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a3 + b3) >> NJ_M4V_IDCT_COL_SHIFT;
        out[24 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a3 - b3) >> NJ_M4V_IDCT_COL_SHIFT;
        out[32 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a2 - b2) >> NJ_M4V_IDCT_COL_SHIFT;
        out[40 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a1 - b1) >> NJ_M4V_IDCT_COL_SHIFT;
        out[48 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
        v = (int)(a0 - b0) >> NJ_M4V_IDCT_COL_SHIFT;
        out[56 + i] = v < 0 ? 0 : (v > 255 ? 255 : v);
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


/* AC-prediction state is defined with its helpers further down; the frame
 * drivers and the intra macroblock body only need the type here. */
typedef struct m4v_ac_state {
    int    mbw, mbh;
    nj_s2 *luma;        /* (2*mbw) * (2*mbh) blocks, 16 entries each  */
    nj_s2 *cb;          /* mbw * mbh blocks, 16 entries each          */
    nj_s2 *cr;
    nj_s1 *q;           /* per-macroblock quantiser                   */
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
            ac->q[cur] = (nj_s1)qsc;
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

/* Decodes one motion vector component: the H.263/MPEG-4 VLC (ff_mvtab)
 * followed by a sign bit and f_code-1 range bits, added to the predictor and
 * wrapped into the 5+f_code-bit signed range (ituh263dec.c
 * ff_h263_decode_motion).  Returns 0xffff on a broken stream, which callers
 * treat as an error. */
int nj_m4v_decode_mv(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int pred, int f_code)
{
    int sym = 0, sign, shift, val;

    if (f_code < 1 || f_code > 7 || pos == NULL) {
        return 0xffff;
    }
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

/* The median predictor of one 8x8 block's motion vector (h263.c
 * ff_h263_pred_motion, rectangular picture, one slice).  mvx/mvy hold one pair
 * per 8x8 luma block with `pad' pairs of border in front of block (0,0), at
 * `wrap' = 2 * macroblocks per row + 1 pairs per block row.  The reference
 * always predicts from the left (A), top (B) and top-right (C) blocks with the
 * offsets below; on the first macroblock row of a slice the top neighbours are
 * outside the picture and only the left one is used.  `resync_x' is the
 * column the current slice starts at (0 when the VOP has one slice). */
void nj_m4v_pred_motion(const nj_s2 *mvx, const nj_s2 *mvy, int wrap, int pad,
                        int mbx, int mby, int block, int first_slice_line,
                        int resync_x, int *px, int *py)
{
    static const int off[4] = { 2, 1, 1, -1 };
    nj_size at = (nj_size)pad + ((nj_size)mby * 2 + (nj_size)(block >> 1)) *
                 (nj_size)wrap + (nj_size)(mbx * 2 + (block & 1));
    nj_size cidx = at + (nj_size)off[block] - (nj_size)wrap;

    if (px == NULL || py == NULL || mvx == NULL || mvy == NULL || wrap <= 0 ||
        block < 0 || block > 3) {
        return;
    }
    if (first_slice_line != 0 && block < 3) {
        if (block == 0) {
            if (mbx == resync_x) {
                *px = 0;
                *py = 0;
            } else if (mbx + 1 == resync_x) {
                /* The slice starts in the middle of this row: the top-right
                 * neighbour is the only one the reference still reads. */
                *px = (mbx == 0) ? mvx[cidx] : m4v_mid_pred(mvx[at - 1], 0, mvx[cidx]);
                *py = (mbx == 0) ? mvy[cidx] : m4v_mid_pred(mvy[at - 1], 0, mvy[cidx]);
            } else {
                *px = mvx[at - 1];
                *py = mvy[at - 1];
            }
            return;
        }
        if (block == 1) {
            if (mbx + 1 == resync_x) {
                *px = m4v_mid_pred(mvx[at - 1], 0, mvx[cidx]);
                *py = m4v_mid_pred(mvy[at - 1], 0, mvy[cidx]);
            } else {
                *px = mvx[at - 1];
                *py = mvy[at - 1];
            }
            return;
        }
        /* block 2 keeps the normal formula; only a slice's first column
         * forces the left neighbour to zero (the border entry already is). */
        *px = m4v_mid_pred(mbx == resync_x ? 0 : mvx[at - 1],
                           mvx[at - wrap], mvx[cidx]);
        *py = m4v_mid_pred(mbx == resync_x ? 0 : mvy[at - 1],
                           mvy[at - wrap], mvy[cidx]);
        return;
    }
    *px = m4v_mid_pred(mvx[at - 1], mvx[at - wrap], mvx[cidx]);
    *py = m4v_mid_pred(mvy[at - 1], mvy[at - wrap], mvy[cidx]);
}

/* Half-pel motion compensation of one 8x8 block with edge emulation: samples
 * outside the frame repeat the nearest edge sample.
 *
 * `shift' maps a luma motion vector to the plane, exactly as the reference
 * does for 4:2:0 (mpeg_motion_internal, FMT_H263 branch): luma uses mv>>1 with
 * mv&1 as the half-pel flag, chroma uses mv>>2 with (mv&3)!=0.  The chroma
 * mapping is not a second shift of a rounded vector; it is the reference's
 * H.263 workaround for the 4:2:0 chroma vector. */
void nj_m4v_mc_block8(int sx, int sy, int mx, int my, const nj_u1 *pl,
                      int stride, int fw, int fh, int shift, int no_rounding,
                      nj_u1 *out)
{
    int x0 = sx + (mx >> (1 + shift));
    int y0 = sy + (my >> (1 + shift));
    int fx = shift ? ((mx & 3) != 0) : (mx & 1);
    int fy = shift ? ((my & 3) != 0) : (my & 1);
    int y;

    if (pl == NULL || out == NULL || stride <= 0 || fw <= 0 || fh <= 0) {
        return;
    }
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
                v = (a + r0[xx2] + r1[xx] + r1[xx2] + (no_rounding ? 1 : 2)) >> 2;
            } else if (fx != 0) {
                v = (a + r0[xx2] + (no_rounding ? 0 : 1)) >> 1;
            } else if (fy != 0) {
                v = (a + r1[xx] + (no_rounding ? 0 : 1)) >> 1;
            } else {
                v = a;
            }
            out[y * 8 + x] = (nj_u1)v;
        }
    }
}

/* ff_h263_round_chroma: one chroma vector from the sum of four luma 8x8
 * vectors.  The table is the reference's h263_chroma_roundtab; transcribing it
 * matters, x/8 with a plain rounding rule does not give the same pixels. */
int nj_m4v_chroma_round(int x)
{
    static const nj_u1 roundtab[16] = {
        0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1
    };

    return (int)roundtab[x & 0xf] + (x >> 3);
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
                int lvl = 0, marker = 0;
                nj_u4 r6 = 0;

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

/* One inter macroblock: the six residual blocks come first in the bit stream,
 * then motion compensation fills the prediction and reconstruction adds the
 * inverse transform of each residual block.  `mv' holds the 8x8 luma vectors
 * (for a 16x16 block all four are the same); `four_mv' selects the reference's
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

/* Macroblock header of a P picture (mpeg4_decode_mb P branch): not_coded,
 * MCBPC, cbpy, dquant, interlaced_dct and the motion vectors.  Returns 0 for
 * an intra macroblock (whose body reads dquant and the blocks), 1 for a
 * skipped one, 2 for an inter one and -1 on damage. */
static int m4v_pvop_mb_header(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                              const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                              nj_u4 mbx, nj_u4 mby, nj_s2 *mvx, nj_s2 *mvy,
                              int wrap, int pad, int first_slice_line,
                              int resync_x, int *qscale, int *cbp, int *dq,
                              int *cbpc_out, m4v_pmv *pmv)
{
    static const int quant_tab[4] = { -1, -2, 1, 2 };
    int cbpc = 0, cbpy = 0, skip = 0;

    if (pos == NULL || qscale == NULL || cbp == NULL || dq == NULL || pmv == NULL) {
        return -1;
    }
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
        pmv->ac_pred = m4v_peek_bit(b, n, *pos);
        *pos += 1u;                 /* ac_pred */
        if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
            return -1;
        }
        *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
        return 0;                   /* the body reads dquant, then the blocks */
    }
    if (nj_vlc_lookup(b, n, pos, nj_vlc_cbpy, 16, &cbpy) != 0) {
        return -1;
    }
    cbpy ^= 0x0f;
    *cbp = ((cbpc & 3) | (cbpy << 2)) & 0x3f;
    if (*dq) {
        nj_u4 d = 0;

        if (!m4v_rbits(b, n, pos, 2, &d)) {
            return -1;
        }
        *qscale += quant_tab[d & 3u];
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
    if ((cbpc & 16) != 0) {
        /* Four 8x8 motion vectors (mpeg4_decode_mb, MV_TYPE_8X8).  Each one is
         * predicted from the already decoded block grid and stored back before
         * the next block is predicted. */
        int i;

        pmv->four_mv = 1;
        for (i = 0; i < 4; i++) {
            int px = 0, py = 0, mx, my;
            nj_size at = (nj_size)pad +
                         ((nj_size)mby * 2 + (nj_size)(i >> 1)) * (nj_size)wrap +
                         (nj_size)(mbx * 2 + (i & 1));

            nj_m4v_pred_motion(mvx, mvy, wrap, pad, (int)mbx, (int)mby, i,
                               first_slice_line, resync_x, &px, &py);
            mx = nj_m4v_decode_mv(b, n, pos, px, vop->f_code);
            if (mx == 0xffff) {
                return -1;
            }
            my = nj_m4v_decode_mv(b, n, pos, py, vop->f_code);
            if (my == 0xffff) {
                return -1;
            }
            mvx[at] = (nj_s2)mx;
            mvy[at] = (nj_s2)my;
            pmv->mv[i][0] = mx;
            pmv->mv[i][1] = my;
        }
    } else {
        int px = 0, py = 0, mx, my;
        nj_size at = (nj_size)pad + (nj_size)mby * 2 * (nj_size)wrap +
                     (nj_size)mbx * 2;

        pmv->four_mv = 0;
        nj_m4v_pred_motion(mvx, mvy, wrap, pad, (int)mbx, (int)mby, 0,
                           first_slice_line, resync_x, &px, &py);
        mx = nj_m4v_decode_mv(b, n, pos, px, vop->f_code);
        if (mx == 0xffff) {
            return -1;
        }
        my = nj_m4v_decode_mv(b, n, pos, py, vop->f_code);
        if (my == 0xffff) {
            return -1;
        }
        /* The reference stores a 16x16 vector in all four 8x8 slots
         * (ff_h263_update_motion_val), and later predictors read them. */
        mvx[at] = (nj_s2)mx;
        mvy[at] = (nj_s2)my;
        mvx[at + 1] = (nj_s2)mx;
        mvy[at + 1] = (nj_s2)my;
        mvx[at + wrap] = (nj_s2)mx;
        mvy[at + wrap] = (nj_s2)my;
        mvx[at + 1 + wrap] = (nj_s2)mx;
        mvy[at + 1 + wrap] = (nj_s2)my;
        pmv->mv[0][0] = mx;
        pmv->mv[0][1] = my;
        pmv->mv[1][0] = mx;
        pmv->mv[1][1] = my;
        pmv->mv[2][0] = mx;
        pmv->mv[2][1] = my;
        pmv->mv[3][0] = mx;
        pmv->mv[3][1] = my;
    }
    return 2;                       /* inter macroblock */
}

/* ------------------------------------------------------------------ */
/* AC prediction state (ISO/IEC 14496-2 7.4.3.4)                       */
/* ------------------------------------------------------------------ */
/*
 * An intra block decodes its AC coefficients as a difference from the
 * reconstructed ALREADY-DECODED neighbouring block: the left column when the
 * DC predictor chose the block to the left, the top row when it chose the one
 * above.  The reference keeps, per 8x8 block slot, the block's first column
 * and first row in the *quantised* domain (`ff_mpeg4_pred_ac` stores them in
 * ac_val before dct_unquantize_h263_intra scales them), rescales the
 * prediction when the neighbour macroblock used a different quantiser, and
 * zeroes the slots that a non-intra macroblock would otherwise leave behind
 * (`ff_clean_intra_table_entries` / `ff_mpeg4_clean_buffers`).
 *
 * The state is per frame: every predictor is a neighbouring block decoded
 * earlier in the same picture, and the frame drivers allocate it, so nothing
 * is global.  `q` keeps the quantiser each decoded macroblock used, for the
 * rescale rule.
 */
static void m4v_ac_free(m4v_ac_state *ac)
{
    if (ac == NULL) {
        return;
    }
    free(ac->luma);
    free(ac->cb);
    free(ac->cr);
    free(ac->q);
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
    ac->q = (nj_s1 *)calloc(nc, sizeof(nj_s1));
    if (ac->luma == NULL || ac->cb == NULL || ac->cr == NULL || ac->q == NULL) {
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

/* ROUNDED_DIV() of the reference: divide rounding half away from zero. */
int nj_m4v_rounded_div(int a, int b)
{
    return (a >= 0) ? (a + (b >> 1)) / b : -((-a + (b >> 1)) / b);
}

/* Adds the neighbour's left column (dir 0) or top row (dir 1) to the raw
 * coefficients, exactly like ff_mpeg4_pred_ac(). */
static void m4v_ac_predict(m4v_ac_state *ac, nj_s4 *coef, int mbx, int mby,
                           int blk, int qscale, int dir)
{
    nj_s2 *slot = m4v_ac_slot(ac, mbx, mby, blk);
    int    i;

    if (dir == 0) {
        int same_mb = (blk == 1 || blk == 3);

        if (!same_mb && mbx == 0) {
            return;                 /* the left neighbours are outside the picture */
        }
        if (same_mb || qscale == (int)ac->q[mby * ac->mbw + (mbx - 1)]) {
            for (i = 1; i < 8; i++) {
                coef[i << 3] += (nj_s4)slot[-16 + i];
            }
        } else {
            int prev = (int)ac->q[mby * ac->mbw + (mbx - 1)];

            for (i = 1; i < 8; i++) {
                coef[i << 3] += (nj_s4)nj_m4v_rounded_div(slot[-16 + i] * prev,
                                                          qscale);
            }
        }
    } else {
        int same_mb = (blk == 2 || blk == 3);
        nj_s2 *above;

        if (!same_mb && mby == 0) {
            return;
        }
        above = slot - 16 * (blk < 4 ? ac->mbw * 2 : ac->mbw);
        if (same_mb || qscale == (int)ac->q[(mby - 1) * ac->mbw + mbx]) {
            for (i = 1; i < 8; i++) {
                coef[i] += (nj_s4)above[8 + i];
            }
        } else {
            int prev = (int)ac->q[(mby - 1) * ac->mbw + mbx];

            for (i = 1; i < 8; i++) {
                coef[i] += (nj_s4)nj_m4v_rounded_div(above[8 + i] * prev, qscale);
            }
        }
    }
}

/* Copies the reconstructed raw coefficients into the slot for the next block's
 * prediction (the store half of ff_mpeg4_pred_ac). */
static void m4v_ac_store(m4v_ac_state *ac, const nj_s4 *coef, int mbx, int mby,
                         int blk)
{
    nj_s2 *slot = m4v_ac_slot(ac, mbx, mby, blk);
    int    i;

    for (i = 1; i < 8; i++) {
        slot[i] = (nj_s2)coef[i << 3];
        slot[8 + i] = (nj_s2)coef[i];
    }
}

/* A non-intra macroblock leaves no usable AC neighbour behind: the reference
 * clears every slot of the six blocks except the upper-left luma one, which is
 * only ever read from inside the same macroblock. */
static void m4v_ac_clean_mb(m4v_ac_state *ac, int mbx, int mby)
{
    int blk, i;

    for (blk = 1; blk < 6; blk++) {
        nj_s2 *slot = m4v_ac_slot(ac, mbx, mby, blk);

        for (i = 0; i < 16; i++) {
            slot[i] = 0;
        }
    }
}

/* At a slice boundary the blocks outside the slice must not be predicted
 * from: the reference's ff_mpeg4_clean_buffers() zeroes a band at the slice
 * start.  Clearing whole rows here covers the same slots (the cells that the
 * current slice has not decoded yet are zero either way). */
static void m4v_ac_clean_slice(m4v_ac_state *ac, int rx, int ry)
{
    int r, c, i;

    (void)rx;
    for (r = ry * 2 - 1; r <= ry * 2 + 1; r++) {
        if (r < 0 || r >= ac->mbh * 2) {
            continue;
        }
        for (c = 0; c < ac->mbw * 2; c++) {
            nj_s2 *slot = ac->luma + ((size_t)r * (size_t)(ac->mbw * 2) +
                                      (size_t)c) * 16u;

            for (i = 0; i < 16; i++) {
                slot[i] = 0;
            }
        }
    }
    for (r = ry - 1; r <= ry; r++) {
        if (r < 0 || r >= ac->mbh) {
            continue;
        }
        for (c = 0; c < ac->mbw; c++) {
            nj_s2 *cb = ac->cb + ((size_t)r * (size_t)ac->mbw + (size_t)c) * 16u;
            nj_s2 *cr = ac->cr + ((size_t)r * (size_t)ac->mbw + (size_t)c) * 16u;

            for (i = 0; i < 16; i++) {
                cb[i] = 0;
                cr[i] = 0;
            }
        }
    }
}

/* Clears the four 8x8 motion-vector slots of one macroblock: an intra or
 * skipped macroblock predicts with a zero vector (ff_h263_update_motion_val)
 * and later predictors must see that. */
static void m4v_zero_mv(nj_s2 *mvx, nj_s2 *mvy, int wrap, int pad,
                        int mbx, int mby)
{
    nj_size at = (nj_size)pad + (nj_size)mby * 2 * (nj_size)wrap +
                 (nj_size)mbx * 2;

    mvx[at] = 0;
    mvy[at] = 0;
    mvx[at + 1] = 0;
    mvy[at + 1] = 0;
    mvx[at + wrap] = 0;
    mvy[at + wrap] = 0;
    mvx[at + 1 + wrap] = 0;
    mvy[at + 1 + wrap] = 0;
}

/* Resets the six DC predictors of one macroblock to mid-grey, which is what
 * the reference does after decoding a non-intra macroblock
 * (ff_clean_intra_table_entries): an intra macroblock later in the picture
 * predicts from a neutral neighbour, never from an inter block's stale DC. */
static void m4v_clean_dc(nj_s2 *gl, int glw, nj_s2 *gc, nj_s2 *gcr, int gcw,
                         int mbx, int mby)
{
    int i, j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            gl[(nj_size)(mby * 2 + i) * (nj_size)glw +
               (nj_size)(mbx * 2 + j)] = 1024;
        }
    }
    gc[(nj_size)mby * (nj_size)gcw + (nj_size)mbx] = 1024;
    if (gcr != NULL) {
        gcr[(nj_size)mby * (nj_size)gcw + (nj_size)mbx] = 1024;
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
                nj_s4 acb[6];
                int cbp = 0, dq = 0, cbpc = 0;
                int is_intra = 0;
                m4v_pmv pmv;

                memset(&pmv, 0, sizeof(pmv));
                rc = m4v_pvop_mb_header(b, n, pos, vol, vop, (nj_u4)mbx,
                                        (nj_u4)mby, mvx, mvy, wrap, pad,
                                        first_slice_line, resync_x, &qsc, &cbp,
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
                ac->q[cur] = (nj_s1)qsc;
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
        /* Outside-slice handling, as the reference decoder does it
         * (mpeg4_pred_dc): on the first line of a slice the top neighbours
         * are not predictors, and at the left edge of the slice so is the
         * left one.  Chroma blocks (n=4 and n=5) follow the same rules.  The
         * second rule neutralises the top-left predictor on the second
         * macroblock row of a slice. */
        if (first_slice_line != 0 && blk != 3) {
            if (blk != 2) {
                bq = 1024;
                c = 1024;
            }
            if (blk != 1 && mbx == resync_x) {
                bq = 1024;
                a = 1024;
            }
        }
        if (mbx == resync_x && mby == resync_y + 1 &&
            (blk == 0 || blk == 4 || blk == 5)) {
            bq = 1024;
        }
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
        /* The intra IDCT input is the reference's own block: the DC term is
         * the dequantised level (the 1024 mid-grey bias lives inside it) and
         * nj_m4v_idct_put() then writes pixels directly. */
        all_coef[blk][0] = (nj_s4)nj_m4v_dc_coef((int)coef[0], (int)dscale);
        (void)scale;
        {
            int coded = nj_m4v_cbp_has_ac(cbp, blk), arc = 0;
            int scan = 0;

            if (ac_bits != NULL) {
                ac_bits[blk] = coded;
            }
            /* With AC prediction the coefficient scan is the reference's
             * alternate table for the prediction direction (7.4.3.4). */
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
                m4v_ac_store(ac, all_coef[blk], mbx, mby, blk);
            }
        }
    }
    /* Intra AC levels reach the reconstruction raw, exactly as the reference
     * stores them in mpeg4_decode_block (qmul = 1, qadd = 0 there); its
     * dct_unquantize_h263_intra() scales them when the block is put, with
     * qmul = qscale << 1 and qadd = (qscale - 1) | 1.  Only the inter
     * residual arrives already dequantised, because that path reads the
     * per-quantiser RL table. */
    {
        int qm = (*qscale) << 1;
        int qa = ((*qscale) - 1) | 1;

        int kk;

        for (blk = 0; blk < 6; blk++) {
            for (kk = 1; kk < 64; kk++) {
                nj_s4 lv = all_coef[blk][kk];

                if (lv != 0) {
                    all_coef[blk][kk] = (lv > 0) ? (nj_s4)(lv * qm + qa)
                                                 : (nj_s4)(lv * qm - qa);
                }
            }
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

/* One intra macroblock of a slice: same syntax as nj_m4v_decode_intra_mb_pix,
 * plus the slice's first-line/left edges so the DC predictor applies the
 * reference's outside-slice rules. */
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
 * reference decoder reconstructs the padding rows and crops them away when it
 * outputs the frame, so the provider keeps the planes at their coded size and
 * converts only the visible rectangle to RGB565.
 */
#if NJ_ENABLE_VIDEO

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
        /* vop_coded 0 repeats the previous picture, as the reference does. */
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

#endif /* NJ_ENABLE_VIDEO */
