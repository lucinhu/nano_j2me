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
#include "nj_idct.h"

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
    nj_u4 v;
    int   type, bit, coded = 1;

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
    if (!m4v_bits_get(&s, (int)(m4v_log2(time_res - 1u) + 1u), &v)) {
        return 0;
    }
    if (!m4v_marker(&s)) {
        return 0;
    }
    if (!m4v_bit(&s, &bit)) {
        return 0;
    }
    coded = bit;
    out->type = type;
    out->time_increment = v;
    out->coded = coded;
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
    if (sym <= 0 || sym >= NJ_M4V_AC_SYMS) {
        return 0;       /* symbol 0 is the escape; out of range is not a symbol */
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


int nj_m4v_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef, int k)
{
    int last = 0;

    while (!last) {
        int sym = nj_m4v_decode_ac(b, n, pos, &last);
        int rc;

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


int nj_m4v_decode_intra_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef,
                              int luma, int dc_pred)
{
    int size, delta, dc, k, i;

    if (coef == NULL) {
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
    k = nj_m4v_ac_block(b, n, pos, coef, 1);
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
                               int luma, int dc_pred)
{
    nj_s4 coef[64];
    int   out[64];
    int   rc, i;

    if (pix == NULL) {
        return -1;
    }
    rc = nj_m4v_decode_intra_block(b, n, pos, coef, luma, dc_pred);
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
