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
 * The filter is separable, so each output row first builds the vertical cubic,
 * vertical bilinear and per-column tap bounds for every source column and then
 * walks the output pixels doing only the horizontal cubic. Keeping the 4x4
 * taps in these row arrays removes the per-pixel divisions and the repeated
 * tap extraction that made the naive form far too slow for a sprite blitter. */

#define HD_FSR1_MAX_SCALE 16 /* render scale 8 with the optional 2x SSAA */
#ifndef HD_FSR1_SHARP
#define HD_FSR1_SHARP 96     /* 0.375 in Q8: the RCAS-style contrast amount */
#endif
#define HD_FSR1_SLACK 2      /* columns the four-tap window reaches past a row */


typedef struct hd_fsr1_kernel {
    nj_s4 tap[4][HD_FSR1_MAX_SCALE];
} hd_fsr1_kernel;

/* One row of vertical work, one entry per source column plus slack columns so
 * the horizontal window never needs a bounds check. */
typedef struct hd_fsr1_row {
    int *vert[3]; /* Q8: Catmull-Rom vertical reconstruction */
    int *bil[3];  /* Q8: vertical bilinear, the FSR1 base layer */
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
        double t = (double)hd_asset_weight(phase, k) * (1.0 / 256.0);
        double t2 = t * t, t3 = t2 * t;
        w->tap[0][phase] = hd_fsr1_round(-0.5 * t3 + t2 - 0.5 * t);
        w->tap[1][phase] = hd_fsr1_round(1.5 * t3 - 2.5 * t2 + 1.0);
        w->tap[2][phase] = hd_fsr1_round(-1.5 * t3 + 2.0 * t2 + 0.5 * t);
        w->tap[3][phase] = 256 - w->tap[0][phase] - w->tap[1][phase] - w->tap[2][phase];
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
        free(row->bil[c]);
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
        row->bil[c] = (int *)malloc((size_t)columns * sizeof(int));
        row->lo[c] = (int *)malloc((size_t)columns * sizeof(int));
        row->hi[c] = (int *)malloc((size_t)columns * sizeof(int));
        if (!row->vert[c] || !row->bil[c] || !row->lo[c] || !row->hi[c]) {
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
 * image); `wy` is the vertical bilinear weight in Q8. Columns outside the
 * source row repeat the edge texel, matching the clamp the horizontal window
 * used to apply per tap. */
static void hd_fsr1_column(hd_fsr1_row *row, int c, nj_u4 p0, nj_u4 p1, nj_u4 p2, nj_u4 p3,
                           const nj_s4 *kern, unsigned wy)
{
    unsigned rem = 256u - wy;
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
    row->bil[0][c] = (int)rem * r[1] + (int)wy * r[2];
    row->bil[1][c] = (int)rem * g[1] + (int)wy * g[2];
    row->bil[2][c] = (int)rem * b[1] + (int)wy * b[2];
    row->lo[0][c] = hd_fsr1_min(hd_fsr1_min(r[0], r[1]), hd_fsr1_min(r[2], r[3]));
    row->lo[1][c] = hd_fsr1_min(hd_fsr1_min(g[0], g[1]), hd_fsr1_min(g[2], g[3]));
    row->lo[2][c] = hd_fsr1_min(hd_fsr1_min(b[0], b[1]), hd_fsr1_min(b[2], b[3]));
    row->hi[0][c] = hd_fsr1_max(hd_fsr1_max(r[0], r[1]), hd_fsr1_max(r[2], r[3]));
    row->hi[1][c] = hd_fsr1_max(hd_fsr1_max(g[0], g[1]), hd_fsr1_max(g[2], g[3]));
    row->hi[2][c] = hd_fsr1_max(hd_fsr1_max(b[0], b[1]), hd_fsr1_max(b[2], b[3]));
    row->opaque[c] = ((p0 & p1 & p2 & p3) >> 24) == 255u;
}

/* Build the vertical pass for one output row. `rows` holds the byte offset of
 * the four source scan rows the Catmull-Rom window needs (clamped to the
 * image); `wy` is the vertical bilinear weight in Q8. Columns outside the
 * source row repeat the edge texel, matching the clamp the horizontal window
 * used to apply per tap. */
static void hd_fsr1_row_build(hd_fsr1_row *row, int columns, int tw, nj_s8 step,
                              const nj_u1 *src, const nj_s8 *rows, const nj_s4 *kern,
                              unsigned wy, int format)
{
    int c;
    if (format == NJ_PIXFMT_ARGB8888) {
        /* The HD mirror is always ARGB8888, so the common path reads the words
         * directly instead of calling the format-dispatching accessor for every
         * tap. That branch was a measurable part of the row cost. */
        const nj_u1 *row0 = src + rows[0], *row1 = src + rows[1];
        const nj_u1 *row2 = src + rows[2], *row3 = src + rows[3];
        for (c = 0; c < columns; ++c) {
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
            hd_fsr1_column(row, c, p0, p1, p2, p3, kern, wy);
        }
        return;
    }
    for (c = 0; c < columns; ++c) {
        int index = c - HD_FSR1_SLACK;
        nj_s8 off;
        if (index < 0) index = 0;
        if (index >= tw) index = tw - 1;
        off = (nj_s8)index * step;
        hd_fsr1_column(row, c,
                       nj_src_pixel(src + rows[0], off, format),
                       nj_src_pixel(src + rows[1], off, format),
                       nj_src_pixel(src + rows[2], off, format),
                       nj_src_pixel(src + rows[3], off, format), kern, wy);
    }
}

static void hd_fsr1_store(nj_u1 *dst, int r, int g, int b)
{
    nj_u4 color = 0xff000000u | ((nj_u4)r << 16) | ((nj_u4)g << 8) | (nj_u4)b;
    memcpy(dst, &color, sizeof color);
}

static void hd_fsr1_span(nj_u1 *out, int n, int phase, const hd_fsr1_kernel *kernel,
                         const hd_fsr1_row *row, const int *wx, int c0, int c1, int c2,
                         int c3)
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
    for (int i = 0; i < n; ++i) {
        int ph = phase + i;
        int k0 = kernel->tap[0][ph], k1 = kernel->tap[1][ph];
        int k2 = kernel->tap[2][ph], k3 = kernel->tap[3][ph];
        int w = wx[ph], wr = 256 - w;
        int r, g, b;
        int curve, base, delta, value;
        /* Red */
        curve = hd_fsr1_shift(k0 * row->vert[0][c0] + k1 * row->vert[0][c1] +
                              k2 * row->vert[0][c2] + k3 * row->vert[0][c3], 16);
        base = hd_fsr1_shift(wr * row->bil[0][c1] + w * row->bil[0][c2], 16);
        delta = (curve - base) * HD_FSR1_SHARP;
        value = curve + hd_fsr1_shift(delta, 8);
        r = hd_fsr1_clamp(value, lo[0], hi[0]);
        /* Green */
        curve = hd_fsr1_shift(k0 * row->vert[1][c0] + k1 * row->vert[1][c1] +
                              k2 * row->vert[1][c2] + k3 * row->vert[1][c3], 16);
        base = hd_fsr1_shift(wr * row->bil[1][c1] + w * row->bil[1][c2], 16);
        delta = (curve - base) * HD_FSR1_SHARP;
        value = curve + hd_fsr1_shift(delta, 8);
        g = hd_fsr1_clamp(value, lo[1], hi[1]);
        /* Blue */
        curve = hd_fsr1_shift(k0 * row->vert[2][c0] + k1 * row->vert[2][c1] +
                              k2 * row->vert[2][c2] + k3 * row->vert[2][c3], 16);
        base = hd_fsr1_shift(wr * row->bil[2][c1] + w * row->bil[2][c2], 16);
        delta = (curve - base) * HD_FSR1_SHARP;
        value = curve + hd_fsr1_shift(delta, 8);
        b = hd_fsr1_clamp(value, lo[2], hi[2]);
        hd_fsr1_store(out + (size_t)i * 4, r, g, b);
    }
}

static void hd_fsr1_bitmap(nj_surface *dst, int k, const nj_u1 *src, nj_s8 stride,
                           int format, int bpp, int sx, int sy, int sw, int sh, int transform,
                           nj_s8 dx, nj_s8 dy, int x0, int y0, int x1, int y1)
{
    hd_fsr1_kernel kernel;
    hd_fsr1_row row;
    int tw, th, first, width, phase_first, columns, c;
    int wx[HD_FSR1_MAX_SCALE];
    nj_s8 step;
    if (k > HD_FSR1_MAX_SCALE) {
        hd_filter_bitmap(dst, k, src, stride, format, bpp, sx, sy, sw, sh,
                         transform, dx, dy, x0, y0, x1, y1);
        return;
    }
    hd_fsr1_kernel_build(&kernel, k);
    for (c = 0; c < k; ++c) wx[c] = (int)hd_asset_weight(c, k);
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
        hd_fsr1_row_build(&row, columns, tw, step, src, rows, kern, wy, format);
        out = (nj_u1 *)dst->pixels + (nj_s8)y * dst->stride + (nj_s8)x0 * k * 4;
        count = width * k;
        p = 0;
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
                hd_fsr1_span(out, n, phase, &kernel, &row, wx, c0, c1, c2, c3);
            } else {
                int index = base < 0 ? 0 : base >= tw ? tw - 1 : base;
                int next = base + 1 < 0 ? 0 : base + 1 >= tw ? tw - 1 : base + 1;
                hd_asset_color l = hd_asset_column(src + rows[1], src + rows[2],
                                                   (nj_s8)index * step, format, wy);
                hd_asset_color r = hd_asset_column(src + rows[1], src + rows[2],
                                                   (nj_s8)next * step, format, wy);
                if (l.a == (255u << 8) && r.a == (255u << 8)) {
                    hd_asset_opaque_span(out, n, l, r, NULL, phase, k);
                } else if (l.a || r.a) {
                    for (int i = 0; i < n; ++i) {
                        unsigned fx = hd_asset_weight(phase + i, k);
                        hd_asset_over(out + (size_t)i * 4, hd_asset_mix(l, r, fx));
                    }
                }
            }
            out += (size_t)n * 4;
            count -= n;
            p += n;
        }
    }
    hd_fsr1_row_free(&row);
}
