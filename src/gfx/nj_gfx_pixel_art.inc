/* Reconstruct a staircase from its 3x3 neighbourhood. The classifier runs
 * once per logical texel; each selected corner covers a triangular run of
 * physical samples. Source colours (including alpha) are never interpolated. */
typedef struct hd_art_col {
    nj_u4 top, mid, bot;
} hd_art_col;

static nj_u4 hd_art_sample(const nj_u1 *src, nj_s8 offset, int format)
{
    nj_u4 c = nj_src_pixel(src, offset, format);
    return c >> 24 ? c : 0; /* hidden RGB is not an edge */
}

static hd_art_col hd_art_column(const nj_u1 *src, const nj_s8 rows[3], nj_s8 offset, int format)
{
    hd_art_col col = {hd_art_sample(src, rows[0] + offset, format),
                      hd_art_sample(src, rows[1] + offset, format),
                      hd_art_sample(src, rows[2] + offset, format)};
    return col;
}

/* Consider the upper-left corner; rotate the arguments for the other three.
 * The north/west pair describes the candidate diagonal. A continuation on
 * its tangent distinguishes a staircase from the corner of a rectangle.
 * A one-texel diagonal stroke can also pass directly through the centre. */
static nj_u4 hd_art_corner(nj_u4 c, nj_u4 n, nj_u4 w, nj_u4 s, nj_u4 e,
                           nj_u4 nw, nj_u4 ne, nj_u4 sw, nj_u4 se)
{
    if (n == c || n != w) return c;
    /* Require a continuation of the centre colour as well. A highlight
     * surrounded by several different palette entries is still isolated. */
    int tangent = ne == c || sw == c;
    int connected = s == c || e == c || se == c || nw == c;
    if (n != s && w != e && (tangent || (nw != n && connected))) return n;
    if (ne == c && sw == c && nw == n && se == n) return n;
    return c;
}

static void hd_art_span(nj_u1 *dst, int count, nj_u4 color)
{
    if (!count || !color) return;
    if ((color >> 24) == 255) {
        for (int x = 0; x < count; ++x, dst += 4) memcpy(dst, &color, 4);
    } else {
        hd_asset_color c = hd_asset_unpack(color);
        c.a <<= 16; c.r <<= 16; c.g <<= 16; c.b <<= 16;
        for (int x = 0; x < count; ++x, dst += 4) hd_asset_over(dst, c);
    }
}

/* A subpixel exactly on the dividing line belongs to one colour on both
 * sides of the edge. A fixed palette ordering makes the tie rotation-safe. */
static int hd_art_radius(int k, nj_u4 corner, nj_u4 center)
{
    if (corner == center) return 0;
    return (k - 1) / 2 + (!(k & 1) && corner > center);
}

static void hd_art_cell(nj_u1 *dst, int pitch, int k, hd_art_col l, hd_art_col m, hd_art_col r)
{
    nj_u4 c = m.mid;
    nj_u4 corners[4] = {
        hd_art_corner(c,m.top,l.mid,m.bot,r.mid,l.top,r.top,l.bot,r.bot),
        hd_art_corner(c,m.top,r.mid,m.bot,l.mid,r.top,l.top,r.bot,l.bot),
        hd_art_corner(c,m.bot,l.mid,m.top,r.mid,l.bot,r.bot,l.top,r.top),
        hd_art_corner(c,m.bot,r.mid,m.top,l.mid,r.bot,l.bot,r.top,l.top)
    };
    if (corners[0] == c && corners[1] == c && corners[2] == c && corners[3] == c) {
        if (!c) return;
        if ((c >> 24) == 255) {
            hd_art_span(dst, k, c);
            hd_repeat_row(dst, pitch, (size_t)k * 4, k);
        } else {
            for (int y = 0; y < k; ++y) hd_art_span(dst + (nj_s8)y * pitch, k, c);
        }
        return;
    }
    int radius[4];
    for (int i = 0; i < 4; ++i) radius[i] = hd_art_radius(k, corners[i], c);
    for (int y = 0; y < k; ++y) {
        int side = y < k / 2 ? 0 : 2;
        int depth = side ? k - 1 - y : y;
        int left = radius[side] > depth ? radius[side] - depth : 0;
        int right = radius[side+1] > depth ? radius[side+1] - depth : 0;
        nj_u1 *row = dst + (nj_s8)y * pitch;
        hd_art_span(row, left, corners[side]);
        hd_art_span(row + (nj_s8)left * 4, k - left - right, c);
        hd_art_span(row + (nj_s8)(k - right) * 4, right, corners[side+1]);
    }
}

static void hd_pixel_art_bitmap(nj_surface *dst, int k, const nj_u1 *src, nj_s8 stride,
                                int format, int bpp, int sx, int sy, int sw, int sh, int transform,
                                nj_s8 dx, nj_s8 dy, int x0, int y0, int x1, int y1)
{
    int tw, th, first = (int)(x0 - dx);
    nj_s8 step = nj_trans_row_step(transform, bpp, stride);
    nj_trans_size(transform, sw, sh, &tw, &th);
    for (int y = y0; y <= y1; ++y) {
        int j = (int)(y - dy);
        nj_s8 rows[3];
        for (int n = -1; n <= 1; ++n) {
            int scan = j + n, u, v;
            nj_trans_inverse(transform, 0, scan < 0 ? 0 : scan >= th ? th - 1 : scan,
                             sw, sh, &u, &v);
            rows[n+1] = ((nj_s8)sy + v) * stride + ((nj_s8)sx + u) * bpp;
        }
        /* Clamp to the transformed source frame, even if the destination
         * clip only exposes part of it. Each column is decoded once. */
        hd_art_col m = hd_art_column(src, rows, first * step, format);
        hd_art_col l = first ? hd_art_column(src, rows, (first - 1) * step, format) : m;
        nj_u1 *row = (nj_u1 *)dst->pixels + (nj_s8)y * k * dst->stride + (nj_s8)x0 * k * 4;
        for (int x = x0; x <= x1; ++x) {
            int next = (int)(x - dx) + 1;
            hd_art_col r = next < tw ? hd_art_column(src, rows, next * step, format) : m;
            hd_art_cell(row + (nj_s8)(x - x0) * k * 4, dst->stride, k, l, m, r);
            l = m;
            m = r;
        }
    }
}
