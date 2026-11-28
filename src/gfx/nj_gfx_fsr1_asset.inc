/* Magnified artwork drawn straight into the physical target can use the same
 * reconstruction the FSR1 presentation pass runs: a Catmull-Rom cubic that
 * keeps the source's shape and a clamped contrast sharpening step comparable
 * to RCAS. Two rules keep a 2D blitter safe:
 *
 *  - only blocks whose whole 4x4 source neighbourhood is opaque may be
 *    reshaped. A translucent edge has to composite with whatever lies behind
 *    it, so those pixels keep the exact premultiplied smooth path.
 *  - the reconstructed value is clamped to that neighbourhood's per-channel
 *    minimum and maximum, which removes cubic overshoot and ringing on text
 *    and pixel art while still steepening the ramp.
 *
 * Catmull-Rom puts four taps on i-1, i, i+1, i+2 for a sample at t in [0,1):
 *     -0.5t^3 + t^2 - 0.5t, 1.5t^3 - 2.5t^2 + 1,
 *     -1.5t^3 + 2t^2 + 0.5t, 0.5t^3 - 0.5t^2
 * and they sum to one. They are stored as Q8 so two axes multiply to Q16, the
 * same scale hd_asset_weight() uses for the smooth path; the last weight is
 * derived from the first three so a flat source reproduces exactly.
 *
 * Both the reconstruction and the sharpening are linear in the source pixels,
 * so they are folded into one set of taps: out = (1+s)*cubic - s*bilinear.
 * The same tap table serves the horizontal and vertical axis (both use
 * hd_asset_weight() for their sample position), which leaves four multiplies
 * per channel and per output pixel; a separate bilinear pass and the per-pixel
 * sharpening step are gone.
 *
 * The filter is separable, so each output row first builds the vertical value
 * and the per-column tap bounds for every source column and then walks the
 * output pixels doing only the horizontal pass. Keeping the 4x4 taps in these
 * row arrays removes the per-pixel divisions and the repeated tap extraction
 * that made the naive form far too slow for a sprite blitter. */

#define HD_FSR1_MAX_SCALE 16 /* render scale 8 with the optional 2x SSAA */
#ifndef HD_FSR1_SHARP
#define HD_FSR1_SHARP 96     /* 0.375 in Q8: the RCAS-style contrast amount */
#endif
#define HD_FSR1_SLACK 2      /* columns the four-tap window reaches past a row */


typedef struct hd_fsr1_kernel {
    nj_s4 tap[4][HD_FSR1_MAX_SCALE]; /* Q8: (1+s)*cubic - s*bilinear per axis */
} hd_fsr1_kernel;

/* One row of vertical work, one entry per source column plus slack columns so
 * the horizontal window never needs a bounds check. */
typedef struct hd_fsr1_row {
    int *vert[3]; /* Q8: sharpened vertical reconstruction */
    int *lo[3];   /* per-channel minimum of the column's four taps */
    int *hi[3];   /* per-channel maximum of the column's four taps */
    unsigned char *opaque; /* every one of the column's four taps is opaque */
} hd_fsr1_row;

static nj_s4 hd_fsr1_round(double v)
{
    return (nj_s4)(v * 256.0 + (v < 0.0 ? -0.5 : 0.5));
}

static void hd_fsr1_kernel_build(hd_fsr1_kernel *w, int k)
{
    for (int phase = 0; phase < k; ++phase) {
        /* hd_asset_weight() holds the sample position the smooth path uses, so
         * both modes agree where a texel centre lands. */
        unsigned bw = hd_asset_weight(phase, k);
        double t = (double)bw * (1.0 / 256.0);
        double t2 = t * t, t3 = t2 * t;
        nj_s4 cubic[4], sharpened[4];
        cubic[0] = hd_fsr1_round(-0.5 * t3 + t2 - 0.5 * t);
        cubic[1] = hd_fsr1_round(1.5 * t3 - 2.5 * t2 + 1.0);
        cubic[2] = hd_fsr1_round(-1.5 * t3 + 2.0 * t2 + 0.5 * t);
        cubic[3] = 256 - cubic[0] - cubic[1] - cubic[2];
        /* Fold the RCAS-style contrast into the taps: the bilinear base layer
         * only uses taps 1 and 2, so a single axis pass produces the sharpened
         * reconstruction directly. */
        for (int i = 0; i < 4; ++i) {
            int base = i == 1 ? (int)(256u - bw) : i == 2 ? (int)bw : 0;
            int value = (int)cubic[i] * (256 + HD_FSR1_SHARP) - base * HD_FSR1_SHARP;
            sharpened[i] = (value + 128) >> 8;
        }
        /* Keep the taps exact on a flat source. */
        w->tap[0][phase] = sharpened[0];
        w->tap[1][phase] = sharpened[1];
        w->tap[2][phase] = sharpened[2];
        w->tap[3][phase] = 256 - sharpened[0] - sharpened[1] - sharpened[2];
    }
}

static int hd_fsr1_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Arithmetic shift that rounds towards the nearest integer for both signs. */
static int hd_fsr1_shift(int value, int bits)
{
    int half = 1 << (bits - 1);
    return (value + (value < 0 ? -half : half)) >> bits;
}

static int hd_fsr1_min(int a, int b) { return a < b ? a : b; }
static int hd_fsr1_max(int a, int b) { return a > b ? a : b; }

static void hd_fsr1_row_free(hd_fsr1_row *row)
{
    for (int c = 0; c < 3; ++c) {
        free(row->vert[c]);
        free(row->lo[c]);
        free(row->hi[c]);
    }
    free(row->opaque);
    memset(row, 0, sizeof *row);
}

static int hd_fsr1_row_alloc(hd_fsr1_row *row, int columns)
{
    memset(row, 0, sizeof *row);
    for (int c = 0; c < 3; ++c) {
        row->vert[c] = (int *)malloc((size_t)columns * sizeof(int));
        row->lo[c] = (int *)malloc((size_t)columns * sizeof(int));
        row->hi[c] = (int *)malloc((size_t)columns * sizeof(int));
        if (!row->vert[c] || !row->lo[c] || !row->hi[c]) {
            hd_fsr1_row_free(row);
            return 0;
        }
    }
    row->opaque = (unsigned char *)malloc((size_t)columns);
    if (!row->opaque) {
        hd_fsr1_row_free(row);
        return 0;
    }
    return 1;
}

/* Build the vertical pass for one output row. `rows` holds the byte offset of
 * the four source scan rows the Catmull-Rom window needs (clamped to the
 * image); `kern` holds the four sharpened Q8 taps of this row's phase. Columns
 * outside the source row repeat the edge texel. */
static void hd_fsr1_column(hd_fsr1_row *row, int c, nj_u4 p0, nj_u4 p1, nj_u4 p2, nj_u4 p3,
                           const nj_s4 *kern)
{
    int r[4], g[4], b[4];
    r[0] = (int)((p0 >> 16) & 255u); r[1] = (int)((p1 >> 16) & 255u);
    r[2] = (int)((p2 >> 16) & 255u); r[3] = (int)((p3 >> 16) & 255u);
    g[0] = (int)((p0 >> 8) & 255u); g[1] = (int)((p1 >> 8) & 255u);
    g[2] = (int)((p2 >> 8) & 255u); g[3] = (int)((p3 >> 8) & 255u);
    b[0] = (int)(p0 & 255u); b[1] = (int)(p1 & 255u);
    b[2] = (int)(p2 & 255u); b[3] = (int)(p3 & 255u);
    row->vert[0][c] = kern[0] * r[0] + kern[1] * r[1] + kern[2] * r[2] + kern[3] * r[3];
    row->vert[1][c] = kern[0] * g[0] + kern[1] * g[1] + kern[2] * g[2] + kern[3] * g[3];
    row->vert[2][c] = kern[0] * b[0] + kern[1] * b[1] + kern[2] * b[2] + kern[3] * b[3];
    row->lo[0][c] = hd_fsr1_min(hd_fsr1_min(r[0], r[1]), hd_fsr1_min(r[2], r[3]));
    row->lo[1][c] = hd_fsr1_min(hd_fsr1_min(g[0], g[1]), hd_fsr1_min(g[2], g[3]));
    row->lo[2][c] = hd_fsr1_min(hd_fsr1_min(b[0], b[1]), hd_fsr1_min(b[2], b[3]));
    row->hi[0][c] = hd_fsr1_max(hd_fsr1_max(r[0], r[1]), hd_fsr1_max(r[2], r[3]));
    row->hi[1][c] = hd_fsr1_max(hd_fsr1_max(g[0], g[1]), hd_fsr1_max(g[2], g[3]));
    row->hi[2][c] = hd_fsr1_max(hd_fsr1_max(b[0], b[1]), hd_fsr1_max(b[2], b[3]));
    row->opaque[c] = ((p0 & p1 & p2 & p3) >> 24) == 255u;
}

#if defined(__x86_64__) || defined(__i386__)
#include <smmintrin.h>
/* The four-tap reconstruction is the hot loop, and it is pure integer math on
 * four adjacent output samples whose phases are consecutive, so one SSE4.1
 * multiply-add lane group handles four pixels at once. Runtime-detected: the
 * project builds without -msse4.1 and 32-bit x86 hosts may lack it. */
#if defined(__ANDROID__)
/* The Android x86_64 ABI baseline is SSE4.2, so this kernel is available
 * unconditionally there. Probing with __builtin_cpu_supports on this target
 * would also add 28 bytes of writable BSS for the compiler's CPU model, which
 * the project's no-static-state gate rejects. */
static int hd_fsr1_sse41(void) { return 1; }
#else
static int hd_fsr1_sse41(void)
{
    /* No cached flag: the project forbids writable global state. */
    return __builtin_cpu_supports("sse4.1");
}
#endif

__attribute__((target("sse4.1")))
static int hd_fsr1_span_sse41(nj_u1 *out, int n, int phase,
                              const hd_fsr1_kernel *kernel,
                              const hd_fsr1_row *row, int c0, int c1, int c2,
                              int c3, const int *lo, const int *hi)
{
    const __m128i unit = _mm_set1_epi32(0xff);
    int done = 0;
    while (done + 4 <= n) {
        __m128i k0 = _mm_loadu_si128((const __m128i *)(kernel->tap[0] + phase + done));
        __m128i k1 = _mm_loadu_si128((const __m128i *)(kernel->tap[1] + phase + done));
        __m128i k2 = _mm_loadu_si128((const __m128i *)(kernel->tap[2] + phase + done));
        __m128i k3 = _mm_loadu_si128((const __m128i *)(kernel->tap[3] + phase + done));
        __m128i res[3];
        __m128i bytes[4];
        for (int ch = 0; ch < 3; ++ch) {
            __m128i a = _mm_mullo_epi32(k0, _mm_set1_epi32(row->vert[ch][c0]));
            __m128i b = _mm_mullo_epi32(k1, _mm_set1_epi32(row->vert[ch][c1]));
            __m128i c = _mm_mullo_epi32(k2, _mm_set1_epi32(row->vert[ch][c2]));
            __m128i d = _mm_mullo_epi32(k3, _mm_set1_epi32(row->vert[ch][c3]));
            __m128i sign;
            a = _mm_add_epi32(_mm_add_epi32(a, b), _mm_add_epi32(c, d));
            /* Round half away from zero before the Q16 shift, like
             * hd_fsr1_shift(): +32768 for positive, -32768 for negative. */
            sign = _mm_srai_epi32(a, 31);
            a = _mm_add_epi32(a, _mm_or_si128(_mm_and_si128(sign, _mm_set1_epi32(-65536)),
                                              _mm_set1_epi32(0x8000)));
            a = _mm_srai_epi32(a, 16);
            a = _mm_max_epi32(a, _mm_set1_epi32(lo[ch]));
            a = _mm_min_epi32(a, _mm_set1_epi32(hi[ch]));
            res[ch] = a;
        }
        for (int ch = 0; ch < 4; ++ch) {
            __m128i values = ch == 3 ? unit : res[ch == 0 ? 2 : ch == 1 ? 1 : 0];
            if (ch == 3) values = _mm_set1_epi32(0xff);
            values = _mm_packus_epi32(values, values);
            bytes[ch] = _mm_packus_epi16(values, values);
        }
        /* b,g then r,a: two 16-bit words per pixel, low then high half. */
        {
            __m128i bg = _mm_unpacklo_epi8(bytes[0], bytes[1]);
            __m128i ra = _mm_unpacklo_epi8(bytes[2], bytes[3]);
            _mm_storeu_si128((__m128i *)(out + (size_t)done * 4),
                             _mm_unpacklo_epi16(bg, ra));
        }
        done += 4;
    }
    return done;
}

/* The translucent half of the FSR1 asset filter composites four pixels at a
 * time when the destination is opaque: every division in hd_asset_over() is
 * exact for 16-bit numerators through the (x * 0x8081) >> 23 reciprocal, so
 * the scalar result can be reproduced bit for bit. Groups with a transparent
 * destination pixel, a zero alpha or the full-alpha shortcut fall back to the
 * scalar helper. */
__attribute__((target("sse4.1")))
static int hd_fsr1_over_sse41(nj_u1 *out, int n, int phase,
                              const unsigned *weights,
                              const hd_asset_color *lp, const hd_asset_color *rp)
{
    const hd_asset_color l = *lp, r = *rp;
    const __m128i unit = _mm_set1_epi32(255);
    const __m128i bias = _mm_set1_epi32(32768);
    const __m128i magic = _mm_set1_epi32(0x8081);
    const __m128i full = _mm_set1_epi32(255 << 16);
    int done = 0;
    for (; done + 4 <= n; done += 4) {
        __m128i fx = _mm_loadu_si128((const __m128i *)(weights + phase + done));
        __m128i dst = _mm_loadu_si128((const __m128i *)(out + (size_t)done * 4));
        __m128i dcmp = _mm_cmpeq_epi32(_mm_srli_epi32(dst, 24), unit);
        __m128i ca, c[3];
        __m128i a8, rem, word;
        if (_mm_movemask_ps(_mm_castsi128_ps(dcmp)) != 0xF) break;
        {
            __m128i v = _mm_sub_epi32(_mm_set1_epi32(256), fx);
            ca = _mm_add_epi32(_mm_mullo_epi32(v, _mm_set1_epi32((int)l.a)),
                               _mm_mullo_epi32(fx, _mm_set1_epi32((int)r.a)));
            for (int ch = 0; ch < 3; ++ch) {
                int left = ch == 0 ? (int)l.r : ch == 1 ? (int)l.g : (int)l.b;
                int right = ch == 0 ? (int)r.r : ch == 1 ? (int)r.g : (int)r.b;
                c[ch] = _mm_add_epi32(_mm_mullo_epi32(v, _mm_set1_epi32(left)),
                                      _mm_mullo_epi32(fx, _mm_set1_epi32(right)));
            }
        }
        a8 = _mm_srli_epi32(_mm_add_epi32(ca, bias), 16);
        /* hd_asset_over() returns for alpha 0 and treats a full Q16 alpha as
         * the "source covers everything" shortcut; leave both to the scalar
         * path rather than duplicating those branches. */
        if (_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(a8, _mm_setzero_si128()))) ||
            _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(ca, full))))
            break;
        rem = _mm_sub_epi32(unit, a8);
        {
            __m128i channels[3];
            for (int ch = 0; ch < 3; ++ch) {
                __m128i src = _mm_srli_epi32(_mm_add_epi32(c[ch], bias), 16);
                __m128i d = ch == 0 ? _mm_and_si128(_mm_srli_epi32(dst, 16), unit)
                           : ch == 1 ? _mm_and_si128(_mm_srli_epi32(dst, 8), unit)
                                     : _mm_and_si128(dst, unit);
                __m128i sum = _mm_add_epi32(
                    _mm_add_epi32(src, _mm_mullo_epi32(rem, d)),
                    _mm_set1_epi32(127));
                channels[ch] = _mm_srli_epi32(_mm_mullo_epi32(sum, magic), 23);
            }
            word = _mm_or_si128(_mm_set1_epi32((int)(255u << 24)),
                                _mm_or_si128(_mm_or_si128(
                                                 _mm_slli_epi32(channels[0], 16),
                                                 _mm_slli_epi32(channels[1], 8)),
                                             channels[2]));
        }
        _mm_storeu_si128((__m128i *)(out + (size_t)done * 4), word);
    }
    return done;
}
#endif

__attribute__((target("sse4.1")))
static void hd_fsr1_row_sse41(hd_fsr1_row *row, int c, const nj_u1 *src0,
                              const nj_u1 *src1, const nj_u1 *src2,
                              const nj_u1 *src3, nj_s8 base, const nj_s4 *kern)
{
    /* Little-endian ARGB8888 keeps blue in the low byte, so channel 0 (the
     * row's red slot, see hd_fsr1_column) picks byte 2, green byte 1, blue 0. */
    const __m128i pick[3] = {
        _mm_setr_epi8(2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
        _mm_setr_epi8(1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
        _mm_setr_epi8(0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
    };
    const __m128i one = _mm_set1_epi8(1);
    __m128i p0 = _mm_loadu_si128((const __m128i *)(src0 + base));
    __m128i p1 = _mm_loadu_si128((const __m128i *)(src1 + base));
    __m128i p2 = _mm_loadu_si128((const __m128i *)(src2 + base));
    __m128i p3 = _mm_loadu_si128((const __m128i *)(src3 + base));
    int ch;

    /* Opaque means every one of the sixteen taps has alpha 255: AND the four
     * colour words (the alpha byte is included) and compare the bytes. */
    {
        __m128i alphas = _mm_and_si128(_mm_and_si128(p0, p1), _mm_and_si128(p2, p3));
        __m128i opaque = _mm_cmpeq_epi8(alphas, _mm_set1_epi8((char)0xff));
        __m128i low = _mm_shuffle_epi8(opaque,
            _mm_setr_epi8(3, 7, 11, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
        nj_u4 bits = (nj_u4)_mm_cvtsi128_si32(_mm_and_si128(low, one));
        memcpy(&row->opaque[c], &bits, 4);
    }
    for (ch = 0; ch < 3; ++ch) {
        __m128i v0 = _mm_cvtepu8_epi32(_mm_shuffle_epi8(p0, pick[ch]));
        __m128i v1 = _mm_cvtepu8_epi32(_mm_shuffle_epi8(p1, pick[ch]));
        __m128i v2 = _mm_cvtepu8_epi32(_mm_shuffle_epi8(p2, pick[ch]));
        __m128i v3 = _mm_cvtepu8_epi32(_mm_shuffle_epi8(p3, pick[ch]));
        __m128i lo = _mm_min_epi32(_mm_min_epi32(v0, v1), _mm_min_epi32(v2, v3));
        __m128i hi = _mm_max_epi32(_mm_max_epi32(v0, v1), _mm_max_epi32(v2, v3));
        __m128i acc = _mm_add_epi32(
            _mm_add_epi32(_mm_mullo_epi32(v0, _mm_set1_epi32(kern[0])),
                          _mm_mullo_epi32(v1, _mm_set1_epi32(kern[1]))),
            _mm_add_epi32(_mm_mullo_epi32(v2, _mm_set1_epi32(kern[2])),
                          _mm_mullo_epi32(v3, _mm_set1_epi32(kern[3]))));
        _mm_storeu_si128((__m128i *)&row->vert[ch][c], acc);
        _mm_storeu_si128((__m128i *)&row->lo[ch][c], lo);
        _mm_storeu_si128((__m128i *)&row->hi[ch][c], hi);
    }
}


/* One column of the vertical pass: read the same texel from the four scan
 * rows and fold them into the Q8 cubic, the per-channel 4-tap envelope and the
 * opacity flag. */
static void hd_fsr1_row_one(hd_fsr1_row *row, int c, int tw, nj_s8 step,
                            const nj_u1 *row0, const nj_u1 *row1,
                            const nj_u1 *row2, const nj_u1 *row3,
                            const nj_s4 *kern)
{
    int index = c - HD_FSR1_SLACK;
    nj_s8 off;
    nj_u4 p0, p1, p2, p3;
    if (index < 0) index = 0;
    if (index >= tw) index = tw - 1;
    off = (nj_s8)index * step;
    memcpy(&p0, row0 + off, sizeof p0);
    memcpy(&p1, row1 + off, sizeof p1);
    memcpy(&p2, row2 + off, sizeof p2);
    memcpy(&p3, row3 + off, sizeof p3);
    hd_fsr1_column(row, c, p0, p1, p2, p3, kern);
}

static void hd_fsr1_row_build(hd_fsr1_row *row, int columns, int tw, nj_s8 step,
                              const nj_u1 *src, const nj_s8 *rows, const nj_s4 *kern,
                              int format)
{
    int c = 0;
    if (format == NJ_PIXFMT_ARGB8888) {
        /* The HD mirror is always ARGB8888, so the common path reads the words
         * directly instead of calling the format-dispatching accessor for every
         * tap. That branch was a measurable part of the row cost. */
        const nj_u1 *row0 = src + rows[0], *row1 = src + rows[1];
        const nj_u1 *row2 = src + rows[2], *row3 = src + rows[3];
#if defined(__x86_64__) || defined(__i386__)
        if (step == 4 && hd_fsr1_sse41()) {
            /* The interior columns read four consecutive texels per scan row,
             * so four columns share one load per row. Only the clamped edges
             * need the scalar column helper. */
            int tail = tw + HD_FSR1_SLACK - 3;
            if (tail > columns) tail = columns;
            for (; c < HD_FSR1_SLACK && c < columns; ++c)
                hd_fsr1_row_one(row, c, tw, step, row0, row1, row2, row3, kern);
            for (; c + 4 <= tail; c += 4)
                hd_fsr1_row_sse41(row, c, row0, row1, row2, row3,
                                  (nj_s8)(c - HD_FSR1_SLACK) * 4, kern);
        }
#endif
        for (; c < columns; ++c)
            hd_fsr1_row_one(row, c, tw, step, row0, row1, row2, row3, kern);
        return;
    }
    for (; c < columns; ++c) {
        int index = c - HD_FSR1_SLACK;
        nj_s8 off;
        if (index < 0) index = 0;
        if (index >= tw) index = tw - 1;
        off = (nj_s8)index * step;
        hd_fsr1_column(row, c,
                       nj_src_pixel(src + rows[0], off, format),
                       nj_src_pixel(src + rows[1], off, format),
                       nj_src_pixel(src + rows[2], off, format),
                       nj_src_pixel(src + rows[3], off, format), kern);
    }
}

static void hd_fsr1_store(nj_u1 *dst, int r, int g, int b)
{
    nj_u4 color = 0xff000000u | ((nj_u4)r << 16) | ((nj_u4)g << 8) | (nj_u4)b;
    memcpy(dst, &color, sizeof color);
}

static void hd_fsr1_span(nj_u1 *out, int n, int phase, const hd_fsr1_kernel *kernel,
                         const hd_fsr1_row *row, int c0, int c1, int c2, int c3)
{
    int lo[3], hi[3], c;
    for (c = 0; c < 3; ++c) {
        lo[c] = hd_fsr1_min(hd_fsr1_min(row->lo[c][c0], row->lo[c][c1]),
                            hd_fsr1_min(row->lo[c][c2], row->lo[c][c3]));
        hi[c] = hd_fsr1_max(hd_fsr1_max(row->hi[c][c0], row->hi[c][c1]),
                            hd_fsr1_max(row->hi[c][c2], row->hi[c][c3]));
    }
    if (lo[0] == hi[0] && lo[1] == hi[1] && lo[2] == hi[2]) {
        /* A flat 4x4 window reconstructs to the source colour, so a run of
         * them (the body of a tile, a sky, a solid sprite) is a fill. Artwork
         * drawn this way spends its time on edges, not on colour fields. */
        hd_fsr1_store(out, lo[0], lo[1], lo[2]);
        for (int i = 1; i < n; ++i) memcpy(out + (size_t)i * 4, out, 4);
        return;
    }
    {
        int done = 0;
#if defined(__x86_64__) || defined(__i386__)
        if (n >= 4 && hd_fsr1_sse41())
            done = hd_fsr1_span_sse41(out, n, phase, kernel, row, c0, c1, c2, c3,
                                      lo, hi);
#endif
        if (done) {
            out += (size_t)done * 4;
            phase += done;
            n -= done;
        }
    }
    for (int i = 0; i < n; ++i) {
        int ph = phase + i;
        int k0 = kernel->tap[0][ph], k1 = kernel->tap[1][ph];
        int k2 = kernel->tap[2][ph], k3 = kernel->tap[3][ph];
        int r, g, b;
        r = hd_fsr1_clamp(hd_fsr1_shift(k0 * row->vert[0][c0] + k1 * row->vert[0][c1] +
                                        k2 * row->vert[0][c2] + k3 * row->vert[0][c3], 16),
                          lo[0], hi[0]);
        g = hd_fsr1_clamp(hd_fsr1_shift(k0 * row->vert[1][c0] + k1 * row->vert[1][c1] +
                                        k2 * row->vert[1][c2] + k3 * row->vert[1][c3], 16),
                          lo[1], hi[1]);
        b = hd_fsr1_clamp(hd_fsr1_shift(k0 * row->vert[2][c0] + k1 * row->vert[2][c1] +
                                        k2 * row->vert[2][c2] + k3 * row->vert[2][c3], 16),
                          lo[2], hi[2]);
        hd_fsr1_store(out + (size_t)i * 4, r, g, b);
    }
}

static void hd_fsr1_over_span(nj_u1 *out, int n, int phase, const unsigned *weights,
                              const hd_asset_color *l, const hd_asset_color *r)
{
    int done = 0;
#if defined(__x86_64__) || defined(__i386__)
    if (hd_fsr1_sse41()) done = hd_fsr1_over_sse41(out, n, phase, weights, l, r);
#endif
    for (; done < n; ++done) {
        unsigned fx = weights[phase + done];
        hd_asset_over(out + (size_t)done * 4, hd_asset_mix(*l, *r, fx));
    }
}

/* ARGB8888 is the HD mirror format and the common case for the asset filter,
 * so the per-segment boundary colours read the source words directly instead
 * of going through the format dispatch. */
static hd_asset_color hd_asset_column_argb(const nj_u1 *top, const nj_u1 *bottom,
                                           nj_s8 offset, unsigned wy)
{
    nj_u4 pt, pb;
    memcpy(&pt, top + offset, sizeof pt);
    memcpy(&pb, bottom + offset, sizeof pb);
    return hd_asset_mix(hd_asset_unpack(pt), hd_asset_unpack(pb), wy);
}



static void hd_fsr1_bitmap(nj_surface *dst, int k, const nj_u1 *src, nj_s8 stride,
                           int format, int bpp, int sx, int sy, int sw, int sh, int transform,
                           nj_s8 dx, nj_s8 dy, int x0, int y0, int x1, int y1)
{
    hd_fsr1_kernel kernel;
    hd_fsr1_row row;
    unsigned weights[HD_FSR1_MAX_SCALE];
    int tw, th, first, width, phase_first, columns, c;
    nj_s8 step;
    if (k > HD_FSR1_MAX_SCALE) {
        hd_filter_bitmap(dst, k, src, stride, format, bpp, sx, sy, sw, sh,
                         transform, dx, dy, x0, y0, x1, y1);
        return;
    }
    hd_fsr1_kernel_build(&kernel, k);
    for (int n = 0; n < k; ++n) weights[n] = hd_asset_weight(n, k);
    nj_trans_size(transform, sw, sh, &tw, &th);
    step = nj_trans_row_step(transform, bpp, stride);
    first = (int)(x0 - dx);
    width = x1 - x0 + 1;
    phase_first = k - k / 2;
    columns = tw + 2 * HD_FSR1_SLACK;
    if (!hd_fsr1_row_alloc(&row, columns)) {
        hd_filter_bitmap(dst, k, src, stride, format, bpp, sx, sy, sw, sh,
                         transform, dx, dy, x0, y0, x1, y1);
        return;
    }
    for (int y = y0 * k; y < (y1 + 1) * k; ++y) {
        int j = (int)(y / k - dy), phase_y = y % k - k / 2, u, v, count, p;
        nj_s8 rows[4];
        unsigned wy;
        nj_u1 *out;
        nj_s4 kern[4];
        if (phase_y < 0) { --j; phase_y += k; }
        for (int r = 0; r < 4; ++r) {
            int scan = j - 1 + r;
            nj_trans_inverse(transform, 0, scan < 0 ? 0 : scan >= th ? th - 1 : scan,
                             sw, sh, &u, &v);
            rows[r] = ((nj_s8)sy + (nj_s8)v) * stride + ((nj_s8)sx + (nj_s8)u) * bpp;
        }
        wy = hd_asset_weight(phase_y, k);
        for (c = 0; c < 4; ++c) kern[c] = kernel.tap[c][phase_y];
        hd_fsr1_row_build(&row, columns, tw, step, src, rows, kern, format);
        out = (nj_u1 *)dst->pixels + (nj_s8)y * dst->stride + (nj_s8)x0 * k * 4;
        count = width * k;
        p = 0;
        {
        hd_asset_color left;
        int left_index = -1, have_left = 0;
        while (count > 0) {
            int g = p + phase_first;
            int phase = g % k, base = first - 1 + g / k;
            int n = k - phase < count ? k - phase : count;
            int c0 = base - 1 + HD_FSR1_SLACK;
            int c1 = base + HD_FSR1_SLACK, c2 = base + 1 + HD_FSR1_SLACK;
            int c3 = base + 2 + HD_FSR1_SLACK;
            c0 = c0 < 0 ? 0 : c0 >= columns ? columns - 1 : c0;
            c1 = c1 < 0 ? 0 : c1 >= columns ? columns - 1 : c1;
            c2 = c2 < 0 ? 0 : c2 >= columns ? columns - 1 : c2;
            c3 = c3 < 0 ? 0 : c3 >= columns ? columns - 1 : c3;
            if (row.opaque[c0] && row.opaque[c1] && row.opaque[c2] && row.opaque[c3]) {
                hd_fsr1_span(out, n, phase, &kernel, &row, c0, c1, c2, c3);
            } else {
                int index = base < 0 ? 0 : base >= tw ? tw - 1 : base;
                int next = base + 1 < 0 ? 0 : base + 1 >= tw ? tw - 1 : base + 1;
                hd_asset_color l, r;
                if (format == NJ_PIXFMT_ARGB8888) {
                    l = have_left && left_index == index ? left
                        : hd_asset_column_argb(src + rows[1], src + rows[2],
                                               (nj_s8)index * step, wy);
                    r = hd_asset_column_argb(src + rows[1], src + rows[2],
                                             (nj_s8)next * step, wy);
                } else {
                    l = hd_asset_column(src + rows[1], src + rows[2],
                                        (nj_s8)index * step, format, wy);
                    r = hd_asset_column(src + rows[1], src + rows[2],
                                        (nj_s8)next * step, format, wy);
                }
                left = r;
                left_index = next;
                have_left = 1;
                if (l.a == (255u << 8) && r.a == (255u << 8)) {
                    hd_asset_opaque_span(out, n, l, r, NULL, phase, k);
                } else if (l.a || r.a) {
                    hd_fsr1_over_span(out, n, phase, weights, &l, &r);
                }
            }
                out += (size_t)n * 4;
                count -= n;
                p += n;
            }
        }
    }
    hd_fsr1_row_free(&row);
}
