/* Physical LCDUI primitives. Work in logical units and sample the centres of
 * physical pixels: clipping bounds the work even for extreme Java coordinates.
 * Every stroke is one logical pixel wide, and every covered sample is painted
 * once (important for both SRC_OVER and XOR). */
static int hd_surface_ok(const nj_surface *s)
{
    return nj_surface_ok(s) && s->format == NJ_PIXFMT_ARGB8888 && nj_surface_bpp(s) == 4;
}

int nj_gfx_mirror_context(const nj_gfx *g, nj_gfx *physical)
{
    nj_render_mirror *m;
    nj_s8 k;
    int x1, y1;
    if (!physical || !g || !(m = g->mirror) || (k = m->scale) < 1 || !nj_surface_ok(g->surf) ||
        !hd_surface_ok(m->target) || g->surf->pixels == m->target->pixels ||
        (nj_s8)g->surf->width * k != m->target->width ||
        (nj_s8)g->surf->height * k != m->target->height)
        return 0;
    /* A resident backend owns the physical target between CPU accesses: bring
       it up to date (or decline the physical draw) before touching pixels. */
    if (m->sync && !m->sync(m->sync_ud, m->target))
        return 0;
    *physical = *g;
    physical->surf = m->target;
    physical->mirror = NULL;
    physical->fill_rect = NULL;
    physical->fill_ud = NULL;
    nj_gfx_set_accel(physical, NULL, NULL);
    physical->translate_x = (int)nj_clamp64(k * g->translate_x, INT_MIN, INT_MAX);
    physical->translate_y = (int)nj_clamp64(k * g->translate_y, INT_MIN, INT_MAX);
    physical->clip_x = (int)nj_clamp64(k * g->clip_x, 0, physical->surf->width);
    physical->clip_y = (int)nj_clamp64(k * g->clip_y, 0, physical->surf->height);
    x1 = (int)nj_clamp64(k * ((nj_s8)g->clip_x + g->clip_w), physical->clip_x,
                         physical->surf->width);
    y1 = (int)nj_clamp64(k * ((nj_s8)g->clip_y + g->clip_h), physical->clip_y,
                         physical->surf->height);
    physical->clip_w = x1 - physical->clip_x;
    physical->clip_h = y1 - physical->clip_y;
    return (int)k;
}

/* x/y/w/h are already clipped logical device coordinates. */
static void hd_block(nj_gfx *g, int x, int y, int w, int h, int coverage)
{
    nj_gfx p;
    int k = nj_gfx_mirror_context(g, &p), xx, yy;
    if (!k)
        return;
    for (yy = y * k; yy < (y + h) * k; yy++) {
        if (coverage >= 255)
            nj_span(&p, x * k, (x + w) * k - 1, yy, g->color);
        else
            for (xx = x * k; xx < (x + w) * k; xx++)
                nj_gfx_blend_pixel(p.surf, xx, yy, g->color, coverage);
    }
}

/* First physical pixel with sample centre >= v, clamped before narrowing. */
static int hd_bound(double v, int k, int lo, int hi)
{
    double u = v * k - 0.5;
    if (u <= lo)
        return lo;
    if (u >= hi)
        return hi;
    return (int)ceil(u);
}

typedef struct hd_point {
    double x, y;
} hd_point;
typedef struct hd_interval {
    double left, right;
} hd_interval;

/* Intersect the stroke with one sample row. The major axis advances in
 * logical pixel units, including the half-cells before/after the endpoints. */
static int hd_line_row(hd_point a, hd_point b, double y, hd_interval *r)
{
    double dx = b.x - a.x, dy = b.y - a.y, t;
    if (y < fmin(a.y, b.y) - 0.5 || y >= fmax(a.y, b.y) + 0.5)
        return 0;
    if (fabs(dy) > fabs(dx)) {
        double slope = dx / dy;
        double radius = 0.5 * sqrt(1 + slope * slope);
        t = fmax(0, fmin(1, (y - a.y) / dy));
        r->left = fmax(fmin(a.x, b.x) - 0.5, a.x + t * dx - radius);
        r->right = fmin(fmax(a.x, b.x) + 0.5, a.x + t * dx + radius);
    } else if (dy == 0) {
        r->left = fmin(a.x, b.x) - 0.5;
        r->right = fmax(a.x, b.x) + 0.5;
    } else {
        double slope = dy / dx;
        double radius = 0.5 * sqrt(1 + slope * slope);
        double t0 = (y - radius - a.y) / dy, t1 = (y + radius - a.y) / dy;
        double lo = fmax(0, fmin(t0, t1)), hi = fmin(1, fmax(t0, t1));
        double x0 = a.x + dx * lo, x1 = a.x + dx * hi;
        if (lo == 0)
            x0 += dx < 0 ? 0.5 : -0.5;
        if (hi == 1)
            x1 += dx < 0 ? -0.5 : 0.5;
        r->left = fmin(x0, x1);
        r->right = fmax(x0, x1);
    }
    return 1;
}

static void hd_polygon(nj_gfx *g, int x1, int y1, int x2, int y2, int x3, int y3, int vertices,
                       int filled)
{
    nj_gfx p;
    int k = nj_gfx_mirror_context(g, &p), yy, end, i;
    hd_point v[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
    double miny, maxy;
    if (!k)
        return;
    for (i = 0; i < 3; i++) {
        v[i].x += (double)g->translate_x + 0.5;
        v[i].y += (double)g->translate_y + 0.5;
    }
    miny = fmin(v[0].y, fmin(v[1].y, v[2].y));
    maxy = fmax(v[0].y, fmax(v[1].y, v[2].y));
    yy = hd_bound(miny - 0.5, k, p.clip_y, p.clip_y + p.clip_h);
    end = hd_bound(maxy + 0.5, k, yy, p.clip_y + p.clip_h);
    for (; yy < end; yy++) {
        double y = (yy + 0.5) / k, left = 0, right = 0;
        hd_interval rows[3];
        int valid[3] = {0}, n = 0, xx, xmax, edges = vertices == 2 ? 1 : 3;
        for (i = 0; i < edges; i++) {
            hd_point a = v[i], b = v[(i + 1) % vertices];
            if (filled) {
                double x;
                if (a.y == b.y || y < fmin(a.y, b.y) || y >= fmax(a.y, b.y))
                    continue;
                x = a.x + (b.x - a.x) * ((y - a.y) / (b.y - a.y));
                left = n ? fmin(left, x) : x;
                right = n ? fmax(right, x) : x;
            } else {
                if (!hd_line_row(a, b, y, &rows[i]))
                    continue;
                valid[i] = 1;
                left = n ? fmin(left, rows[i].left) : rows[i].left;
                right = n ? fmax(right, rows[i].right) : rows[i].right;
            }
            n++;
        }
        if (!n)
            continue;
        xx = hd_bound(left, k, p.clip_x, p.clip_x + p.clip_w);
        xmax = hd_bound(right, k, xx, p.clip_x + p.clip_w);
        if (filled || (n == 1 && p.stroke == NJ_STROKE_SOLID)) {
            nj_span(&p, xx, xmax - 1, yy, p.color);
            continue;
        }
        for (; xx < xmax; xx++) {
            double x = (xx + 0.5) / k;
            for (i = 0; i < edges; i++) {
                hd_point a = v[i], b = v[(i + 1) % vertices];
                if (!valid[i] || x < rows[i].left || x >= rows[i].right)
                    continue;
                if (p.stroke == NJ_STROKE_DOTTED) {
                    double d = fabs(b.x - a.x) >= fabs(b.y - a.y) ? x - a.x : y - a.y;
                    if ((nj_s8)floor(fabs(d) + 0.5) & 1)
                        continue;
                }
                nj_plot(&p, xx, yy, p.color);
                break;
            }
        }
    }
}

/* A rounded box covers [left+inset,right-inset) at this sample height.
 * An ellipse is a rounded box whose corner diameters equal both dimensions. */
static double hd_inset(double y, double h, double rx, double ry)
{
    double d;
    if (rx <= 0 || ry <= 0)
        return 0;
    d = (ry - fmin(y, h - y)) / ry;
    return d > 0 ? rx * (1 - sqrt(fmax(0, 1 - d * d))) : 0;
}

/* Perimeter distance for a dotted rounded rectangle, measured clockwise
 * from the top-left tangent. Quarter-ellipse length uses Ramanujan's first
 * approximation. Straight sections keep exactly one logical pixel per dot. */
static double hd_perimeter_pos(double x, double y, double w, double h, double rx, double ry)
{
    const double pi = 3.14159265358979323846;
    double cx = fmin(fmax(x, rx), w - rx), cy = fmin(fmax(y, ry), h - ry);
    double top = w - 2 * rx, side = h - 2 * ry;
    if (rx == 0 || ry == 0) {
        /* Match the logical corner-once walk, including degenerate sides. */
        double i = floor(x), j = floor(y);
        if (j == 0)
            return i;
        if (i == w - 1)
            return w - 1 + j;
        if (j == h - 1)
            return w - 1 + h - 1 + (w - 1 - i);
        return 2 * (w - 1) + h - 1 + (h - 1 - j);
    }
    double quarter = pi * (3 * (rx + ry) - sqrt((3 * rx + ry) * (rx + 3 * ry))) / 4;
    if (x >= rx && x < w - rx && y < 1)
        return x - rx;
    if (x >= w - 1 && y >= ry && y < h - ry)
        return top + quarter + y - ry;
    if (x >= rx && x < w - rx && y >= h - 1)
        return top + 2 * quarter + side + w - rx - x;
    if (x < 1 && y >= ry && y < h - ry)
        return 2 * top + 3 * quarter + side + h - ry - y;
    if (rx > 0 && ry > 0) {
        double t = atan2(fabs(y - cy) / ry, fabs(x - cx) / rx) * (2 / pi);
        if (x >= w / 2 && y < h / 2)
            return top + (1 - t) * quarter;
        if (x >= w / 2)
            return top + quarter + side + t * quarter;
        if (y >= h / 2)
            return 2 * top + 2 * quarter + side + (1 - t) * quarter;
        return 2 * top + 3 * quarter + 2 * side + t * quarter;
    }
    return 0;
}

/* Subtract the inset inner shape to obtain a one-unit outline. The angular
 * test uses parametric ellipse coordinates for non-square MIDP arc bounds. */
static void hd_curve(nj_gfx *g, int x, int y, int w, int h, int rw, int rh, int start,
                     int extent, int arc, int filled)
{
    nj_gfx p;
    int k = nj_gfx_mirror_context(g, &p), yy, end;
    double X = (double)x + g->translate_x, Y = (double)y + g->translate_y;
    double W = (double)w + !filled, H = (double)h + !filled;
    double rx = 0, ry = 0, cs = 0, ss = 0, ce = 0, se = 0;
    int wedge = arc && extent > -360 && extent < 360;
    int dotted = !filled && g->stroke == NJ_STROKE_DOTTED;
    if (!k || W <= 0 || H <= 0)
        return;
    if (arc || (rw > 0 && rh > 0)) {
        rx = ((double)rw + !filled) * 0.5;
        ry = ((double)rh + !filled) * 0.5;
    }
    if (wedge) {
        int a = extent > 0 ? start : start + extent;
        int b = extent > 0 ? start + extent : start;
        cs = nj_cos_q14(a);
        ss = nj_sin_q14(a);
        ce = nj_cos_q14(b);
        se = nj_sin_q14(b);
    }
    yy = hd_bound(Y, k, p.clip_y, p.clip_y + p.clip_h);
    end = hd_bound(Y + H, k, yy, p.clip_y + p.clip_h);
    for (; yy < end; yy++) {
        double ly = (yy + 0.5) / k - Y, inset = hd_inset(ly, H, rx, ry);
        int runs[4], n = 2, r;
        runs[0] = hd_bound(X + inset, k, p.clip_x, p.clip_x + p.clip_w);
        runs[1] = hd_bound(X + W - inset, k, runs[0], p.clip_x + p.clip_w);
        if (!filled && ly >= 1 && ly < H - 1 && W > 2) {
            inset = 1 + hd_inset(ly - 1, H - 2, rx - 1, ry - 1);
            runs[3] = runs[1];
            runs[1] = hd_bound(X + inset, k, runs[0], runs[3]);
            runs[2] = hd_bound(X + W - inset, k, runs[1], runs[3]);
            n = 4;
        }
        for (r = 0; r < n; r += 2) {
            int xx;
            if (!wedge && !dotted) {
                nj_span(&p, runs[r], runs[r + 1] - 1, yy, p.color);
                continue;
            }
            for (xx = runs[r]; xx < runs[r + 1]; xx++) {
                double lx = (xx + 0.5) / k - X;
                if (wedge) {
                    double u = lx / W - 0.5, v = 0.5 - ly / H;
                    int after = cs * v - ss * u >= 0, before = u * se - v * ce >= 0;
                    if (extent > 180 || extent < -180 ? !(after || before) : !(after && before))
                        continue;
                }
                if (dotted && ((nj_s8)hd_perimeter_pos(lx, ly, W, H, rx, ry) & 1))
                    continue;
                nj_plot(&p, xx, yy, p.color);
            }
        }
    }
}
