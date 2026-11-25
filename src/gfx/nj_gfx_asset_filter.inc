/* Sample in transformed frame coordinates, clamping to this atlas region.
 * Keep RGB multiplied by alpha at full precision through both filter axes.
 * The largest accumulator is 255 * 255 * 256 * 256 (< UINT32_MAX). */
typedef struct hd_asset_color {
    nj_u4 a, r, g, b;
} hd_asset_color;

static hd_asset_color hd_asset_unpack(nj_u4 pixel)
{
    nj_u4 a = pixel >> 24;
    hd_asset_color c = {a, ((pixel >> 16) & 255u) * a,
                        ((pixel >> 8) & 255u) * a, (pixel & 255u) * a};
    return c;
}

static hd_asset_color hd_asset_mix(hd_asset_color a, hd_asset_color b, unsigned w)
{
    unsigned v = 256 - w;
    hd_asset_color c = {v * a.a + w * b.a, v * a.r + w * b.r,
                        v * a.g + w * b.g, v * a.b + w * b.b};
    return c;
}

static hd_asset_color hd_asset_column(const nj_u1 *top, const nj_u1 *bottom,
                                      nj_s8 offset, int format, unsigned wy)
{
    return hd_asset_mix(hd_asset_unpack(nj_src_pixel(top, offset, format)),
                        hd_asset_unpack(nj_src_pixel(bottom, offset, format)), wy);
}

static void hd_asset_over(nj_u1 *dst, hd_asset_color c)
{
    nj_u4 a = (c.a + 32768u) >> 16;
    if (a == 0) return;
    nj_u4 d = c.a == (255u << 16) ? 0xff000000u : nj_word_get(dst, 0);
    nj_u4 da = d >> 24;
    nj_u4 r, g, b;
    if (da == 255u) {
        nj_u4 rem = 255u - a;
        r = (((c.r + 32768u) >> 16) + rem * ((d >> 16) & 255u) + 127u) / 255u;
        g = (((c.g + 32768u) >> 16) + rem * ((d >> 8) & 255u) + 127u) / 255u;
        b = (((c.b + 32768u) >> 16) + rem * (d & 255u) + 127u) / 255u;
        a = 255u;
    } else {
        /* Mutable images store straight ARGB. Weight the destination by its
         * own alpha and normalize once, before this image is drawn again. */
        nj_u4 dst_weight = (((255u << 16) - c.a) * da + 127u) / 255u;
        nj_u4 total = c.a + dst_weight;
        r = (c.r + dst_weight * ((d >> 16) & 255u) + total / 2u) / total;
        g = (c.g + dst_weight * ((d >> 8) & 255u) + total / 2u) / total;
        b = (c.b + dst_weight * (d & 255u) + total / 2u) / total;
        a = (total + 32768u) >> 16;
    }
    nj_u4 out = (a << 24) | (r << 16) | (g << 8) | b;
    memcpy(dst, &out, sizeof out);
}

/* At odd scales the first sample lies on the source centre. At even scales
 * it is offset by half a physical pixel. */
static unsigned hd_asset_weight(int phase, int scale)
{
    return (unsigned)((((nj_s8)phase * 2 + !(scale & 1)) * 128 + scale / 2) / scale);
}

/* When both columns are opaque the alpha factor of 255 cancels. Round the
 * resulting Q16 RGB value once. This has the same half-up threshold as the
 * premultiplied source-over calculation, including at channel midpoints. */
static void hd_asset_opaque_span(nj_u1 *dst, int n, hd_asset_color l, hd_asset_color r,
                                 const unsigned *weights, int phase, int k)
{
    nj_u4 red = l.r / 255u, green = l.g / 255u, blue = l.b / 255u;
    if (l.r == r.r && l.g == r.g && l.b == r.b) {
        nj_u4 color = 0xff000000u | (((red + 128u) >> 8) << 16) |
                      (((green + 128u) >> 8) << 8) | ((blue + 128u) >> 8);
        for (int i = 0; i < n; ++i) memcpy(dst + (size_t)i * 4, &color, 4);
        return;
    }
    nj_u4 dr = r.r / 255u - red, dg = r.g / 255u - green, db = r.b / 255u - blue;
    red = (red << 8) + 32768u;
    green = (green << 8) + 32768u;
    blue = (blue << 8) + 32768u;
    for (int i = 0; i < n; ++i) {
        unsigned w = weights ? weights[phase+i] : hd_asset_weight(phase+i, k);
        nj_u4 color = 0xff000000u | (((red + w * dr) >> 16) << 16) |
                      (((green + w * dg) >> 16) << 8) | ((blue + w * db) >> 16);
        memcpy(dst + (size_t)i * 4, &color, 4);
    }
}

static void hd_filter_bitmap(nj_surface *dst, int k, const nj_u1 *src, nj_s8 stride,
                             int format, int bpp, int sx, int sy, int sw, int sh, int transform,
                             nj_s8 dx, nj_s8 dy, int x0, int y0, int x1, int y1)
{
    unsigned weights[32]; /* Covers VM scales, with a fallback for other callers. */
    int cached = k <= (int)(sizeof weights / sizeof *weights);
    int tw, th;
    nj_trans_size(transform, sw, sh, &tw, &th);
    if (cached) for (int n = 0; n < k; ++n) weights[n] = hd_asset_weight(n, k);
    nj_s8 step = nj_trans_row_step(transform, bpp, stride);
    int first = (int)(x0 - dx);
    for (int y = y0 * k; y < (y1 + 1) * k; ++y) {
        int j = (int)(y / k - dy), phase_y = y % k - k / 2;
        if (phase_y < 0) { --j; phase_y += k; }
        unsigned wy = cached ? weights[phase_y] : hd_asset_weight(phase_y, k);
        int u, v;
        nj_trans_inverse(transform, 0, j < 0 ? 0 : j, sw, sh, &u, &v);
        nj_s8 top = (sy + (nj_s8)v) * stride + (sx + (nj_s8)u) * bpp;
        nj_trans_inverse(transform, 0, j + 1 < th ? j + 1 : th - 1, sw, sh, &u, &v);
        nj_s8 bot = (sy + (nj_s8)v) * stride + (sx + (nj_s8)u) * bpp;
        nj_u1 *row = (nj_u1 *)dst->pixels + (nj_s8)y * dst->stride + (nj_s8)x0 * k * 4;
        int count = (x1 - x0 + 1) * k, i = first - 1, phase = k - k / 2;
        /* Adjacent output intervals share a source column. Reuse its vertical
         * interpolation across the boundary, including for rotated frames. */
        nj_s8 offset = (i < 0 ? 0 : i) * step;
        hd_asset_color l = hd_asset_column(src + top, src + bot, offset, format, wy);
        while (count) {
            nj_s8 right = (i + 1 < tw ? i + 1 : tw - 1) * step;
            hd_asset_color r = hd_asset_column(src + top, src + bot, right, format, wy);
            int n = k - phase < count ? k - phase : count;
            if (l.a == (255u << 8) && r.a == (255u << 8)) {
                hd_asset_opaque_span(row, n, l, r, cached ? weights : NULL, phase, k);
            } else if (l.a || r.a) {
                for (int p = 0; p < n; ++p) {
                    unsigned wx = cached ? weights[phase+p] : hd_asset_weight(phase+p, k);
                    hd_asset_over(row + (size_t)p * 4, hd_asset_mix(l, r, wx));
                }
            }
            row += (size_t)n * 4;
            count -= n;
            phase = 0;
            l = r;
            ++i;
        }
    }
}
