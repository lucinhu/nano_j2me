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
    return out->width > 0u && out->height > 0u;
}

int nj_m4v_parse_vop(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_u4 time_res,
                     nj_m4v_vop *out)
{
    m4v_bits s;
    nj_u4 v, tv = 0;
    int   type, bit, coded = 1, qscale = 0, thr = 0;

    if (b == NULL || out == NULL || time_res < 2u) {
        return 0;
    }
    s.b = b;
    s.n = n;
    s.pos = pos * 8u;
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
    if (type == 1) {
        /* P-VOP: time_incr unary ones terminated by a zero. */
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
    if (coded) {
        /* complexity_estimation_disable lives in the VOL, not here: the VOP
         * goes straight to intra_dc_vlc_thr and then vop_quant. */
        if (!m4v_bits_get(&s, 3, &v)) {
            return 0;
        }
        thr = (int)v;
        if (type == 1 && !m4v_bits_get(&s, 3, &v)) {
            return 0;           /* vop_fcode_forward */
        }
        if (!m4v_bits_get(&s, 5, &v)) {
            return 0;
        }
        qscale = (int)v;
        if (qscale < 1) {
            return 0;
        }
    }
    out->type = type;
    out->time_increment = tv;
    out->coded = coded;
    out->qscale = qscale;
    out->dc_vlc_thr = thr;
    return 1;
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
    const nj_u2 *tab = luma ? nj_m4v_dc_lum : nj_m4v_dc_chrom;
    int len, i;

    for (len = 1; len <= 13; len++) {
        nj_u4 v = 0;
        int   k;

        if (*pos + (nj_u4)len > n * 8u) {
            return -1;
        }
        for (k = 0; k < len; k++) {
            v = (v << 1) | (nj_u4)((b[(*pos + (nj_u4)k) >> 3] >>
                                    (7u - ((*pos + (nj_u4)k) & 7u))) & 1u);
        }
        for (i = 0; i < 13; i++) {
            if ((int)(tab[i] & 0x0fu) == len && (int)(tab[i] >> 4) == (int)v) {
                *pos += (nj_u4)len;
                return i;
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
        return -1;
    }
    for (k = 0; k < size; k++) {
        v = (v << 1) | (nj_u4)((b[(*pos + (nj_u4)k) >> 3] >>
                                (7u - ((*pos + (nj_u4)k) & 7u))) & 1u);
    }
    *pos += (nj_u4)size;
    half = 1 << (size - 1);
    if ((int)v & half) {
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

int nj_m4v_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef, int k,
                    int qscale)
{
    int last = 0;

    while (!last) {
        int sym = nj_m4v_decode_ac(b, n, pos, &last);
        int rc;

        if (sym == -2) {
            int el = 0, er = 0, el3 = 0, rc3;

            rc3 = nj_m4v_escape3(b, n, pos, qscale, &el, &er, &el3);
            if (rc3 == -3) {
                rc3 = nj_m4v_escape1(b, n, pos, qscale, &el, &er, &el3);
            }
            if (rc3 == -3) {
                rc3 = nj_m4v_escape2(b, n, pos, qscale, &el, &er, &el3);
            }
            if (rc3 < 0) {
                return -1;
            }
            if (rc3 < 0) {
                return -1;
            }
            k += er;
            if (k >= 64) {
                return -1;
            }
            nj_m4v_coef_store(coef, k, el3);
            k++;
            last = el;
            continue;
        }
        if (sym < 0) {
            return -1;
        }
        rc = nj_m4v_coef_apply(coef, &k, sym, &last);
        if (rc == -2) {
            return -3;      /* escape form is not decoded yet */
        }
        if (rc < 0) {
            return -1;
        }
    }
    return k;
}


int nj_m4v_qmul(int qscale)
{
    return (qscale > 0 && qscale < 32) ? qscale * 2 : 0;
}

int nj_m4v_qadd(int qscale)
{
    return (qscale > 0 && qscale < 32) ? ((qscale - 1) | 1) : 0;
}

int nj_m4v_decode_intra_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef,
                              int luma, int dc_pred, int qscale)
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
    if (delta < 0) {
        return -1;
    }
    dc = dc_pred + delta;
    coef[0] = (nj_s4)dc;
    k = nj_m4v_ac_block(b, n, pos, coef, 1, qscale);
    if (k == -3) {
        return -3;
    }
    return k < 0 ? -1 : 0;
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
