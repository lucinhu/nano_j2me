/*
 * nano_j2me - nj_ttf.c
 *
 * A TrueType (glyf) rasteriser.  See nj_ttf.h for the API.
 *
 * libm functions used: floor, fabs, ceil, round.  Nothing else: no sqrt, no
 * trig, no libm state.  The quadratic flattening picks its subdivision count
 * by doubling, which is why sqrt is not needed.
 *
 * What is implemented
 * -------------------
 *  - Table directory: both sfnt versions, every offset and length validated
 *    against the file before use, all arithmetic overflow-safe on 32 bits.
 *  - head (unitsPerEm - 1024 and 1000 fonts and everything else), hhea,
 *    hmtx (including the trailing-LSB encoding), maxp, loca (short and long),
 *    glyf, cmap, OS/2 (metrics fallback), name (for nj_font.name).
 *  - cmap format 4 (with idRangeOffset arrays) and format 12; a Unicode
 *    subtable is preferred over symbol and Mac subtables, and both a Unicode
 *    and a symbol table can be kept so a character the first cannot map is
 *    looked up in the second.  Symbol faces are also probed at 0xF000|ch,
 *    which is what most of them actually use.
 *  - Simple glyphs, including repeat-flag run-length points, and composite
 *    glyphs with ARGS_ARE_XY_VALUES, ARG_1_AND_2_ARE_WORDS, the uniform,
 *    x/y and 2x2 component transforms, SCALED_COMPONENT_OFFSET and nested
 *    components (to a depth of five).
 *  - Quadratic Beziers with implied on-curve midpoints between consecutive
 *    off-curve points and the wrap-around rule for a contour that starts
 *    off-curve.
 *  - Non-zero winding fill by exact-area scanline coverage: 16 sub-scanlines
 *    per pixel row, exact horizontal overlap per sub-scanline.  Horizontal
 *    edges, edges that land exactly on a scanline, self-intersecting and
 *    retraced contours are all handled by the half-open winding rule.
 *  - Synthetic bold (a one-pixel horizontal smear) and italic (a 12-degree
 *    shear about the baseline), applied by the rasteriser.
 *
 * What is not implemented, and why
 * --------------------------------
 *  - Hinting.  TrueType instructions are skipped and the font is rendered
 *    unhinted, like FreeType's FT_LOAD_NO_HINTING.  Hinting is a bytecode
 *    interpreter, it is a much bigger job than the rasteriser, and unhinted
 *    antialiased text at J2ME sizes reads well.  Loading flags are still read
 *    so that glyph data can be walked correctly.
 *  - CFF/OTTO outlines: rejected at open (this is a glyf rasteriser).
 *  - Composite ARGS_ARE_XY_VALUES unset (point-matching attachment): the
 *    glyph is rejected (returns NULL) rather than drawn wrong.  It is rare,
 *    and a misattached component is worse than a missing glyph, which the
 *    renderer's fallback chain already handles.
 *  - Cmap formats other than 0, 4, 6 and 12.  6 is read because it is two
 *    lines and turns up in old faces; the rest are ignored.
 *  - USE_MY_METRICS on a composite component: the composite's own hmtx
 *    advance always wins.  No font this was tested against needs otherwise.
 *  - Vertical metrics (vhea/vmtx), kerning, GPOS/GSUB, colour, bitmaps.
 *
 * Limits (a glyph that exceeds one is rejected, not clamped: the renderer
 * then falls back, and half a glyph is worse than none)
 * ------------------------------------------------------
 *  - 512 points per simple glyph, 256 contours, 16 components per composite,
 *    5 levels of component nesting, 2048 flattened vertices per glyph.
 *  - Bitmap boxes up to 512 wide and 1024 tall; the rasterisation cache holds
 *    whatever the caller's block gives it, and a glyph that cannot fit in the
 *    pool at all is rejected.
 *  - Coordinates are held in 1/32-pixel fixed point, so a face larger than
 *    about 1000 pixels per em is rejected.
 *
 * Memory
 * ------
 *  - No mutable statics, no globals, no malloc, no file I/O, no printf.
 *  - The caller's block holds the face, its scratch buffers and its glyph
 *    cache; the scratch is transient and the cache is a bump arena that is
 *    dropped whole when it fills.
 */
#include "nj_ttf.h"

#include <math.h>
#include <string.h>

#if NJ_ENABLE_TTF

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */
#define NJ_TTF_MAX_POINTS     512
#define NJ_TTF_MAX_CONTOURS   256
#define NJ_TTF_MAX_VERTS      2048
#define NJ_TTF_MAX_CROSS      512
#define NJ_TTF_MAX_W          512
#define NJ_TTF_MAX_H          1024
#define NJ_TTF_MAX_COMPONENTS 16
#define NJ_TTF_MAX_DEPTH      5
#define NJ_TTF_MAX_CACHE      4096
#define NJ_TTF_POOL_PER_GLYPH 256
#define NJ_TTF_SUBSCAN        16
#define NJ_TTF_NAME_MAX       64
#define NJ_TTF_ONE            32          /* 1/32 pixel fixed point */
#define NJ_TTF_TOL            2           /* flatness: 1/16 pixel */
#define NJ_TTF_SHEAR          0.21255656167002213 /* tan(12 degrees) */
#define NJ_TTF_MAX_VAL        32767.0     /* nj_s2 range, in 1/32 px */

#define NJ_TTF_TAG(a, b, c, d) \
    (((nj_u4)(a) << 24) | ((nj_u4)(b) << 16) | ((nj_u4)(c) << 8) | (nj_u4)(d))

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */
typedef struct ttf_slice {
    nj_u4 off;
    nj_u4 len;
} ttf_slice;

typedef struct ttf_slot {
    nj_u4 gid;
    nj_u4 off;      /* byte offset into the pool */
    nj_u4 w;
    nj_u4 h;
    nj_u4 adv;
} ttf_slot;

typedef struct ttf_cross {
    nj_s2 x;        /* 1/32 px */
    nj_s1 d;        /* winding delta */
    nj_u1 pad;
} ttf_cross;

typedef struct ttf_sub {
    ttf_slice s;
    nj_u4 seg_count;    /* format 4 */
    nj_u4 n_groups;     /* format 12 */
    int format;
    int symbol;         /* (3,0): also probe 0xF000|ch */
} ttf_sub;

struct nj_ttf {
    const nj_u1 *data;
    nj_u4 len;
    double scale;       /* pixels per font unit */
    int px_size;
    int upem;

    int ascent;
    int descent;
    int leading;
    int height;

    int num_glyphs;
    int num_hmetrics;
    int default_adv;
    int loca_long;
    int style;          /* the NJ_FONT_STYLE_* the cache was built for */

    ttf_slice cmap;
    ttf_slice loca;
    ttf_slice glyf;
    ttf_slice hmtx;
    ttf_slice name;
    ttf_slice os2;

    ttf_sub sub[2];
    int nsub;

    char namebuf[NJ_TTF_NAME_MAX];

    /* Glyph cache: slots[] index entries stored in pool[]. */
    ttf_slot *slots;
    int cache_cap;
    int cache_used;
    nj_u1 *pool;
    nj_u4 pool_used;
    nj_u4 pool_bytes;

    /* Rasterisation scratch. */
    nj_s2 *pts;         /* NJ_TTF_MAX_POINTS font-unit pairs, then 1/32 px */
    nj_u1 *pflags;      /* NJ_TTF_MAX_POINTS raw point flags */
    nj_s2 *verts;       /* NJ_TTF_MAX_VERTS closed-outline vertices */
    ttf_cross *cross;   /* NJ_TTF_MAX_CROSS scanline crossings */
    nj_u4 *rowacc;      /* NJ_TTF_MAX_W sub-scanline coverage sums */
    nj_u2 ends[NJ_TTF_MAX_CONTOURS];

    /*
     * Where each contour ends in the emitted vertex list (one past its last
     * vertex).  The rasteriser walks contour by contour: the edge from one
     * contour's last vertex to the next contour's first is not part of the
     * outline, and filling it paints a wedge between the two.  Two contours
     * happen to hide that - the two connectors are the same segment walked in
     * opposite directions, so their windings cancel - which is why this only
     * shows up from the third contour on (a composite, or any ordinary letter
     * with three counters and more).
     */
    nj_u2 cend[NJ_TTF_MAX_CONTOURS];
    int ncont_out;

    /* The glyph currently being built. */
    int gn;             /* vertices emitted */
    int gempty;         /* no outline at all */
    int failed;         /* limit hit: abandon this glyph */
    nj_s4 gxmin, gymin, gxmax, gymax;   /* 1/32 px, y up from the baseline */
};

/* ------------------------------------------------------------------ */
/* Bounds-checked readers                                              */
/* ------------------------------------------------------------------ */
static int ttf_in(const nj_ttf *t, nj_u4 off, nj_u4 n)
{
    return off <= t->len && n <= t->len - off;
}

static int ttf_u1(const nj_ttf *t, nj_u4 off, nj_u4 *out)
{
    if (!ttf_in(t, off, 1u)) {
        return 0;
    }
    *out = (nj_u4)t->data[off];
    return 1;
}

static int ttf_u2(const nj_ttf *t, nj_u4 off, nj_u4 *out)
{
    if (!ttf_in(t, off, 2u)) {
        return 0;
    }
    *out = ((nj_u4)t->data[off] << 8) | (nj_u4)t->data[off + 1u];
    return 1;
}

static int ttf_s2(const nj_ttf *t, nj_u4 off, int *out)
{
    nj_u4 v;
    if (!ttf_u2(t, off, &v)) {
        return 0;
    }
    *out = (v & 0x8000u) ? (int)v - 0x10000 : (int)v;
    return 1;
}

static int ttf_u4v(const nj_ttf *t, nj_u4 off, nj_u4 *out)
{
    if (!ttf_in(t, off, 4u)) {
        return 0;
    }
    *out = ((nj_u4)t->data[off] << 24) | ((nj_u4)t->data[off + 1u] << 16) |
           ((nj_u4)t->data[off + 2u] << 8) | (nj_u4)t->data[off + 3u];
    return 1;
}

/* Table-slice-relative readers: `rel` is validated against the slice first,
 * so off + rel cannot leave the region that was itself validated at parse. */
static int sl_u1(const nj_ttf *t, ttf_slice s, nj_u4 rel, nj_u4 *out)
{
    if (rel >= s.len) {
        return 0;
    }
    return ttf_u1(t, s.off + rel, out);
}

static int sl_u2(const nj_ttf *t, ttf_slice s, nj_u4 rel, nj_u4 *out)
{
    if (rel > s.len || 2u > s.len - rel) {
        return 0;
    }
    return ttf_u2(t, s.off + rel, out);
}

static int sl_s2(const nj_ttf *t, ttf_slice s, nj_u4 rel, int *out)
{
    nj_u4 v;
    if (!sl_u2(t, s, rel, &v)) {
        return 0;
    }
    *out = (v & 0x8000u) ? (int)v - 0x10000 : (int)v;
    return 1;
}

static int sl_u4(const nj_ttf *t, ttf_slice s, nj_u4 rel, nj_u4 *out)
{
    if (rel > s.len || 4u > s.len - rel) {
        return 0;
    }
    return ttf_u4v(t, s.off + rel, out);
}

/* ------------------------------------------------------------------ */
/* Layout arithmetic                                                   */
/* ------------------------------------------------------------------ */
static nj_size ttf_align8(nj_size n)
{
    return (nj_size)((n + 7u) & ~(nj_size)7u);
}

static nj_size ttf_scratch_bytes(void)
{
    return (nj_size)(NJ_TTF_MAX_POINTS * 4) + (nj_size)NJ_TTF_MAX_POINTS +
           (nj_size)(NJ_TTF_MAX_VERTS * 4) +
           (nj_size)(NJ_TTF_MAX_CROSS * (int)sizeof(ttf_cross)) +
           (nj_size)(NJ_TTF_MAX_W * 4);
}

static nj_size ttf_base_bytes(void)
{
    return ttf_align8((nj_size)sizeof(nj_ttf)) + ttf_scratch_bytes();
}

nj_size nj_ttf_sizeof(int cache_glyphs)
{
    nj_size cap;
    if (cache_glyphs < 1) {
        cache_glyphs = 1;
    }
    if (cache_glyphs > NJ_TTF_MAX_CACHE) {
        cache_glyphs = NJ_TTF_MAX_CACHE;
    }
    cap = (nj_size)cache_glyphs;
    return ttf_base_bytes() + ttf_align8(cap * (nj_size)sizeof(ttf_slot)) +
           cap * (nj_size)NJ_TTF_POOL_PER_GLYPH + 8u;
}

/* ------------------------------------------------------------------ */
/* Table directory                                                     */
/* ------------------------------------------------------------------ */
static int ttf_find_table(const nj_ttf *t, nj_u4 tag, ttf_slice *out)
{
    nj_u4 ntab, i;
    if (!ttf_u2(t, 4u, &ntab)) {
        return 0;
    }
    for (i = 0; i < ntab; i++) {
        nj_u4 rec = 12u + i * 16u;
        nj_u4 tg, off, len;
        if (!ttf_u4v(t, rec, &tg)) {
            break;              /* directory ran off the end: stop looking */
        }
        if (tg != tag) {
            continue;
        }
        if (!ttf_u4v(t, rec + 8u, &off) || !ttf_u4v(t, rec + 12u, &len)) {
            return 0;
        }
        if (!ttf_in(t, off, len)) {
            return 0;           /* the table we need is not really there */
        }
        out->off = off;
        out->len = len;
        return 1;
    }
    return 0;
}

/* Whole-directory sanity: every record must point inside the file.  A record
 * that does not is a malformed font even if it is a table we never read. */
static int ttf_check_directory(const nj_ttf *t)
{
    nj_u4 ntab, i;
    if (!ttf_u2(t, 4u, &ntab)) {
        return 0;
    }
    if (ntab > 4096u) {
        return 0;
    }
    if (!ttf_in(t, 12u, ntab * 16u)) {
        return 0;
    }
    for (i = 0; i < ntab; i++) {
        nj_u4 rec = 12u + i * 16u;
        nj_u4 off, len;
        if (!ttf_u4v(t, rec + 8u, &off) || !ttf_u4v(t, rec + 12u, &len)) {
            return 0;
        }
        if (!ttf_in(t, off, len)) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* cmap                                                                */
/* ------------------------------------------------------------------ */
static int ttf_cmap4_lookup(const nj_ttf *t, const ttf_sub *su, nj_u4 ch)
{
    nj_u4 lo = 0, hi = su->seg_count;
    nj_u4 seg_count = su->seg_count;
    nj_u4 end_code = 14u;
    nj_u4 start_code = 16u + 2u * seg_count;
    nj_u4 id_delta = 16u + 4u * seg_count;
    nj_u4 id_range = 16u + 6u * seg_count;
    nj_u4 gid = 0;
    nj_u4 seg_start = 0;
    nj_u4 v;

    /* First segment whose endCode is >= ch. */
    while (lo < hi) {
        nj_u4 mid = lo + (hi - lo) / 2u;
        if (!sl_u2(t, su->s, end_code + 2u * mid, &v)) {
            return 0;
        }
        if (v < ch) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= seg_count) {
        return 0;
    }
    if (!sl_u2(t, su->s, end_code + 2u * lo, &v) || v < ch) {
        return 0;
    }
    if (!sl_u2(t, su->s, start_code + 2u * lo, &seg_start) || seg_start > ch) {
        return 0;
    }
    if (!sl_u2(t, su->s, id_range + 2u * lo, &v)) {
        return 0;
    }
    if (v == 0u) {
        nj_u4 delta;
        if (!sl_u2(t, su->s, id_delta + 2u * lo, &delta)) {
            return 0;
        }
        gid = (ch + delta) & 0xFFFFu;
    } else {
        /* The infamous address arithmetic: the glyph index is read at
         * &idRangeOffset[lo] + idRangeOffset[lo] + 2*(ch - startCode). */
        nj_u4 rel = id_range + 2u * lo + v + 2u * (ch - seg_start);
        nj_u4 delta;
        if (!sl_u2(t, su->s, rel, &gid)) {
            return 0;
        }
        if (gid != 0u) {
            if (!sl_u2(t, su->s, id_delta + 2u * lo, &delta)) {
                return 0;
            }
            gid = (gid + delta) & 0xFFFFu;
        }
    }
    if (gid == 0u || gid >= (nj_u4)t->num_glyphs) {
        return 0;
    }
    return (int)gid;
}

static int ttf_cmap12_lookup(const nj_ttf *t, const ttf_sub *su, nj_u4 ch)
{
    nj_u4 lo = 0, hi = su->n_groups;
    while (lo < hi) {
        nj_u4 mid = lo + (hi - lo) / 2u;
        nj_u4 start, end;
        if (!sl_u4(t, su->s, 16u + 12u * mid, &start) ||
            !sl_u4(t, su->s, 16u + 12u * mid + 4u, &end)) {
            return 0;
        }
        if (ch < start) {
            hi = mid;
        } else if (ch > end) {
            lo = mid + 1u;
        } else {
            nj_u4 gid;
            if (!sl_u4(t, su->s, 16u + 12u * mid + 8u, &gid)) {
                return 0;
            }
            gid += ch - start;
            if (gid == 0u || gid >= (nj_u4)t->num_glyphs) {
                return 0;
            }
            return (int)gid;
        }
    }
    return 0;
}

static int ttf_cmap6_lookup(const nj_ttf *t, const ttf_sub *su, nj_u4 ch)
{
    nj_u4 first, count, gid;
    if (!sl_u2(t, su->s, 6u, &first) || !sl_u2(t, su->s, 8u, &count)) {
        return 0;
    }
    if (ch < first || ch - first >= count) {
        return 0;
    }
    if (!sl_u2(t, su->s, 10u + 2u * (ch - first), &gid)) {
        return 0;
    }
    if (gid == 0u || gid >= (nj_u4)t->num_glyphs) {
        return 0;
    }
    return (int)gid;
}

static int ttf_sub_lookup(const nj_ttf *t, const ttf_sub *su, nj_u4 ch)
{
    int gid = 0;
    switch (su->format) {
    case 0:
        if (ch < 256u) {
            nj_u4 v;
            if (sl_u1(t, su->s, 6u + ch, &v) && v != 0u &&
                v < (nj_u4)t->num_glyphs) {
                gid = (int)v;
            }
        }
        break;
    case 4:
        gid = ttf_cmap4_lookup(t, su, ch);
        break;
    case 6:
        gid = ttf_cmap6_lookup(t, su, ch);
        break;
    case 12:
        gid = ttf_cmap12_lookup(t, su, ch);
        break;
    default:
        break;
    }
    if (gid == 0 && su->symbol && ch < 256u) {
        /* Microsoft symbol faces index the 0xF000 page, and some index the
         * low page as well; try both rather than guess. */
        switch (su->format) {
        case 0:
            break;
        case 4:
            gid = ttf_cmap4_lookup(t, su, 0xF000u | ch);
            break;
        case 6:
            gid = ttf_cmap6_lookup(t, su, 0xF000u | ch);
            break;
        case 12:
            gid = ttf_cmap12_lookup(t, su, 0xF000u | ch);
            break;
        default:
            break;
        }
    }
    return gid;
}

/* Character -> glyph index; 0 means "no glyph", which is what the renderer
 * wants for a blank of the right width. */
static int ttf_map_char(const nj_ttf *t, nj_u2 ch)
{
    int i;
    for (i = 0; i < t->nsub; i++) {
        int gid = ttf_sub_lookup(t, &t->sub[i], (nj_u4)ch);
        if (gid > 0) {
            return gid;
        }
    }
    return 0;
}

/* Rank a cmap subtable: lower is better.  Unicode beats symbol beats Mac,
 * format 12 beats format 4 beats format 0/6. */
static int ttf_cmap_rank(nj_u4 plat, nj_u4 enc, int format)
{
    int style;
    int base;
    if (format != 12 && format != 4 && format != 6 && format != 0) {
        return -1;
    }
    if (format == 12) {
        base = 0;
    } else if (format == 4) {
        base = 20;
    } else {
        base = 60;          /* format 0 and 6: legacy byte encodings */
    }
    if (plat == 3u && enc == 10u) {
        style = 0;
    } else if (plat == 0u) {
        style = 2;
    } else if (plat == 3u && enc == 1u) {
        style = 4;
    } else if (plat == 3u && enc == 0u) {
        style = 6;
    } else if (plat == 1u) {
        style = 8;
    } else {
        return -1;
    }
    return base + style;
}

static void ttf_sub_init(nj_ttf *t, ttf_sub *su, nj_u4 sub_off, nj_u4 avail,
                         int symbol)
{
    nj_u4 format;
    su->s.off = t->cmap.off + sub_off;
    su->s.len = avail;
    su->seg_count = 0;
    su->n_groups = 0;
    su->format = -1;
    su->symbol = symbol;

    if (!sl_u2(t, t->cmap, sub_off, &format)) {
        return;
    }
    if (format == 0u) {
        if (avail < 262u) {
            return;
        }
        su->s.len = 262u;
        su->format = 0;
    } else if (format == 4u) {
        nj_u4 seg_x2, declared, need;
        if (!sl_u2(t, t->cmap, sub_off + 2u, &declared)) {
            return;
        }
        if (!sl_u2(t, t->cmap, sub_off + 6u, &seg_x2)) {
            return;
        }
        if (declared > avail || seg_x2 == 0u || (seg_x2 & 1u)) {
            return;
        }
        need = 16u + 4u * seg_x2;
        if (need > declared) {
            return;         /* segCountX2 claims more than the subtable holds */
        }
        su->s.len = declared;
        su->seg_count = seg_x2 / 2u;
        su->format = 4;
    } else if (format == 6u) {
        nj_u4 declared, count, need;
        if (!sl_u2(t, t->cmap, sub_off + 2u, &declared)) {
            return;
        }
        if (!sl_u2(t, t->cmap, sub_off + 8u, &count)) {
            return;
        }
        need = 10u + 2u * count;
        if (declared > avail || need > declared) {
            return;
        }
        su->s.len = declared;
        su->format = 6;
    } else if (format == 12u) {
        nj_u4 declared, groups, need;
        if (!sl_u4(t, t->cmap, sub_off + 4u, &declared)) {
            return;
        }
        if (!sl_u4(t, t->cmap, sub_off + 12u, &groups)) {
            return;
        }
        if (declared > avail || avail < 16u ||
            groups > (avail - 16u) / 12u) {
            return;
        }
        need = 16u + 12u * groups;
        if (need > declared) {
            return;
        }
        su->s.len = declared;
        su->n_groups = groups;
        su->format = 12;
    }
}

static int ttf_parse_cmap(nj_ttf *t)
{
    nj_u4 ntab, i;
    int best_rank[2];
    nj_u4 best_off[2];
    int symbol[2];
    int n = 0;

    if (t->cmap.len < 4u) {
        return 0;
    }
    if (!ttf_u2(t, t->cmap.off + 2u, &ntab)) {
        return 0;
    }
    if (ntab > 1024u || 4u + 8u * ntab > t->cmap.len) {
        return 0;
    }
    best_rank[0] = best_rank[1] = 1 << 20;
    best_off[0] = best_off[1] = 0u;
    symbol[0] = symbol[1] = 0;

    for (i = 0; i < ntab; i++) {
        nj_u4 rec = 4u + 8u * i;
        nj_u4 plat, enc, off, format;
        int rank;
        if (!sl_u2(t, t->cmap, rec, &plat) ||
            !sl_u2(t, t->cmap, rec + 2u, &enc) ||
            !sl_u4(t, t->cmap, rec + 4u, &off)) {
            return 0;
        }
        if (off < 4u + 8u * ntab || off >= t->cmap.len) {
            continue;           /* record points outside the table: ignore */
        }
        if (!sl_u2(t, t->cmap, off, &format)) {
            continue;
        }
        rank = ttf_cmap_rank(plat, enc, (int)format);
        if (rank < 0) {
            continue;
        }
        if (n < 2 && rank < best_rank[0]) {
            /* Keep the old best as the runner-up. */
            best_rank[1] = best_rank[0];
            best_off[1] = best_off[0];
            symbol[1] = symbol[0];
            n = (n == 0) ? 1 : 2;
            best_rank[0] = rank;
            best_off[0] = off;
            symbol[0] = (plat == 3u && enc == 0u);
        } else if (n < 2 && rank < best_rank[1]) {
            best_rank[1] = rank;
            best_off[1] = off;
            symbol[1] = (plat == 3u && enc == 0u);
            n = 2;
        } else if (n == 2 && rank < best_rank[1]) {
            best_rank[1] = rank;
            best_off[1] = off;
            symbol[1] = (plat == 3u && enc == 0u);
        }
    }
    if (n == 0) {
        return 0;
    }
    t->nsub = 0;
    for (i = 0; i < (nj_u4)n; i++) {
        ttf_sub su;
        ttf_sub_init(t, &su, best_off[i], t->cmap.len - best_off[i],
                     symbol[i]);
        if (su.format < 0) {
            continue;
        }
        if (su.format == 12u && su.n_groups == 0u) {
            continue;
        }
        /* Groups must be sorted and disjoint, or the binary search lies. */
        if (su.format == 12u) {
            nj_u4 g;
            nj_u4 prev_end = 0;
            int ordered = 1;
            for (g = 0; g < su.n_groups; g++) {
                nj_u4 sc, ec;
                if (!sl_u4(t, su.s, 16u + 12u * g, &sc) ||
                    !sl_u4(t, su.s, 16u + 12u * g + 4u, &ec) || sc > ec ||
                    (g > 0u && sc <= prev_end)) {
                    ordered = 0;
                    break;
                }
                prev_end = ec;
            }
            if (!ordered) {
                continue;
            }
        }
        if (su.format == 4u) {
            /* endCodes must be non-decreasing for the search to work. */
            nj_u4 g;
            int ordered = 1;
            nj_u4 prev = 0;
            for (g = 0; g < su.seg_count; g++) {
                nj_u4 e;
                if (!sl_u2(t, su.s, 14u + 2u * g, &e) ||
                    (g > 0u && e < prev)) {
                    ordered = 0;
                    break;
                }
                prev = e;
            }
            if (!ordered) {
                continue;
            }
        }
        t->sub[t->nsub++] = su;
        if (t->nsub == 2) {
            break;
        }
    }
    return t->nsub > 0;
}

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */
static int ttf_adv_units(const nj_ttf *t, int gid)
{
    nj_u4 idx;
    nj_u4 v = 0;
    if (t->num_hmetrics <= 0 || t->hmtx.len == 0u) {
        return 0;
    }
    if (gid < 0) {
        gid = 0;
    }
    idx = (gid < t->num_hmetrics) ? (nj_u4)gid : (nj_u4)(t->num_hmetrics - 1);
    if (!sl_u2(t, t->hmtx, 4u * idx, &v)) {
        return 0;
    }
    return (int)v;
}

static int ttf_adv_px(const nj_ttf *t, int gid)
{
    double px = floor((double)ttf_adv_units(t, gid) * t->scale + 0.5);
    if (px < 0.0) {
        px = 0.0;
    }
    if (px > 32767.0) {
        px = 32767.0;
    }
    return (int)px;
}

/* A sensible advance for a character the face cannot map.  The renderer walks
 * its fallback chain for such a character and only uses this to blank out the
 * right number of pixels, so an average is the honest answer. */
static int ttf_default_advance(const nj_ttf *t)
{
    static const nj_u2 probe[6] = { 'n', 'o', 'x', 'M', '0', ' ' };
    int i;
    long sum = 0;
    int n = 0;
    for (i = 0; i < 6; i++) {
        int gid = ttf_map_char(t, probe[i]);
        if (gid > 0) {
            return ttf_adv_px(t, gid);
        }
    }
    if (t->num_hmetrics > 0) {
        for (i = 0; i < t->num_hmetrics && i < 32; i++) {
            sum += ttf_adv_units(t, i);
            n++;
        }
    }
    if (n == 0) {
        return 1;
    }
    {
        int px = (int)floor((double)sum / (double)n * t->scale + 0.5);
        return (px > 0) ? px : 1;
    }
}

/* ------------------------------------------------------------------ */
/* name                                                                */
/* ------------------------------------------------------------------ */
static void ttf_read_name(nj_ttf *t)
{
    nj_u4 count, string_off, i;
    int best_score = 1 << 20;
    nj_u4 best_len = 0, best_off = 0;
    int best_plat = 3;

    memcpy(t->namebuf, "TrueType", 9);
    if (t->name.len < 6u) {
        return;
    }
    if (!sl_u2(t, t->name, 2u, &count) || !sl_u2(t, t->name, 4u, &string_off)) {
        return;
    }
    if (count > 256u || 6u + 12u * count > t->name.len) {
        return;
    }
    for (i = 0; i < count; i++) {
        nj_u4 rec = 6u + 12u * i;
        nj_u4 plat, enc, lang, nid, len, off;
        int score;
        if (!sl_u2(t, t->name, rec, &plat) ||
            !sl_u2(t, t->name, rec + 2u, &enc) ||
            !sl_u2(t, t->name, rec + 4u, &lang) ||
            !sl_u2(t, t->name, rec + 6u, &nid) ||
            !sl_u2(t, t->name, rec + 8u, &len) ||
            !sl_u2(t, t->name, rec + 10u, &off)) {
            return;
        }
        if (nid == 4u) {
            score = 0;
        } else if (nid == 1u) {
            score = 4;
        } else if (nid == 6u) {
            score = 8;
        } else {
            continue;
        }
        if (plat == 3u && enc == 1u) {
            score += 0;
        } else if (plat == 1u && enc == 0u) {
            score += 1;
        } else {
            continue;
        }
        if (lang != 0x409u) {
            score += 2;
        }
        if (score >= best_score) {
            continue;
        }
        /* The string lives at name + stringOffset + offset. */
        if (string_off > t->name.len || off > t->name.len - string_off ||
            len > t->name.len - string_off - off) {
            continue;
        }
        best_score = score;
        best_plat = (plat == 3u) ? 3 : 1;
        best_len = len;
        best_off = string_off + off;
    }
    if (best_score >= (1 << 20)) {
        return;
    }
    {
        nj_u4 i2;
        nj_u4 n = 0;
        for (i2 = 0; i2 < best_len && n + 1u < (nj_u4)NJ_TTF_NAME_MAX; i2++) {
            nj_u4 c;
            if (!ttf_u1(t, t->name.off + best_off + i2, &c)) {
                break;
            }
            if (best_plat == 3) {
                nj_u4 hi;
                if ((i2 & 1u) != 0u) {
                    continue;               /* low byte of a UTF-16BE pair */
                }
                if (!ttf_u1(t, t->name.off + best_off + i2, &hi) ||
                    !ttf_u1(t, t->name.off + best_off + i2 + 1u, &c)) {
                    break;
                }
                if (hi != 0u) {
                    c = (nj_u4)'?';
                }
            }
            if (c < 0x20u || c > 0x7Eu) {
                c = (nj_u4)'?';
            }
            t->namebuf[n++] = (char)c;
        }
        t->namebuf[n] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Outline: vertices                                                   */
/* ------------------------------------------------------------------ */
static int ttf_add_vert(nj_ttf *t, double x, double y)
{
    nj_s2 qx = (nj_s2)x;
    nj_s2 qy = (nj_s2)y;
    if (t->gn >= NJ_TTF_MAX_VERTS) {
        t->failed = 1;
        return 0;
    }
    t->verts[2 * t->gn] = qx;
    t->verts[2 * t->gn + 1] = qy;
    if (t->gn == 0) {
        t->gxmin = t->gxmax = (nj_s4)qx;
        t->gymin = t->gymax = (nj_s4)qy;
    } else {
        if ((nj_s4)qx < t->gxmin) { t->gxmin = (nj_s4)qx; }
        if ((nj_s4)qx > t->gxmax) { t->gxmax = (nj_s4)qx; }
        if ((nj_s4)qy < t->gymin) { t->gymin = (nj_s4)qy; }
        if ((nj_s4)qy > t->gymax) { t->gymax = (nj_s4)qy; }
    }
    t->gn++;
    return 1;
}

/* One quadratic, flattened to within NJ_TTF_TOL (1/16 px) of the curve.  All
 * coordinates are 1/32-pixel integers expressed as doubles. */
static int ttf_quad(nj_ttf *t, double x0, double y0, double cx, double cy,
                    double x1, double y1)
{
    double dx = x0 - 2.0 * cx + x1;
    double dy = y0 - 2.0 * cy + y1;
    double dev = fabs(dx) + fabs(dy);
    int n = 1;
    int k;

    while (n < 32 && dev / (4.0 * (double)n * (double)n) > (double)NJ_TTF_TOL) {
        n *= 2;
    }
    for (k = 1; k <= n; k++) {
        double u = (double)k / (double)n;
        double mu = 1.0 - u;
        double x = mu * mu * x0 + 2.0 * mu * u * cx + u * u * x1;
        double y = mu * mu * y0 + 2.0 * mu * u * cy + u * u * y1;
        if (!ttf_add_vert(t, x, y)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Walk one closed contour and append its flattened outline.  `first`/`last`
 * index into the point arrays.  The TrueType rules: an off-curve point with
 * an off-curve successor implies an on-curve midpoint between them, and a
 * contour that begins off-curve begins at the midpoint of the last and first
 * points (or, if the last point is on-curve, at that point).
 */
static int ttf_walk_contour(nj_ttf *t, int first, int last)
{
    int n = last - first + 1;
    int start = -1;
    int i;
    double curx, cury;

    if (n < 2) {
        return 1;               /* a single point has no area */
    }
    for (i = 0; i < n; i++) {
        if (t->pflags[first + i] & 0x01u) {
            start = i;
            break;
        }
    }
    if (start >= 0) {
        curx = (double)t->pts[2 * (first + start)];
        cury = (double)t->pts[2 * (first + start) + 1];
    } else if (t->pflags[first + n - 1] & 0x01u) {
        start = n - 1;
        curx = (double)t->pts[2 * (first + start)];
        cury = (double)t->pts[2 * (first + start) + 1];
    } else {
        /* Every point is off-curve: start at the implied midpoint. */
        start = n - 1;
        curx = 0.5 * ((double)t->pts[2 * first] +
                      (double)t->pts[2 * (first + n - 1)]);
        cury = 0.5 * ((double)t->pts[2 * first + 1] +
                      (double)t->pts[2 * (first + n - 1) + 1]);
    }
    if (!ttf_add_vert(t, curx, cury)) {
        return 0;
    }

    i = 1;
    while (i <= n) {
        int idx = start + i;
        int on;
        double px, py;
        if (idx >= n) {
            idx -= n;
        }
        on = (t->pflags[first + idx] & 0x01u) ? 1 : 0;
        px = (double)t->pts[2 * (first + idx)];
        py = (double)t->pts[2 * (first + idx) + 1];
        if (on) {
            if (!ttf_add_vert(t, px, py)) {
                return 0;
            }
            curx = px;
            cury = py;
            i++;
        } else {
            int nxt = idx + 1;
            if (nxt >= n) {
                nxt -= n;
            }
            if (t->pflags[first + nxt] & 0x01u) {
                double ex = (double)t->pts[2 * (first + nxt)];
                double ey = (double)t->pts[2 * (first + nxt) + 1];
                if (!ttf_quad(t, curx, cury, px, py, ex, ey)) {
                    return 0;
                }
                curx = ex;
                cury = ey;
                i += 2;
            } else {
                double mx = 0.5 * (px + (double)t->pts[2 * (first + nxt)]);
                double my = 0.5 * (py + (double)t->pts[2 * (first + nxt) + 1]);
                if (!ttf_quad(t, curx, cury, px, py, mx, my)) {
                    return 0;
                }
                curx = mx;
                cury = my;
                i++;
            }
        }
    }
    return 1;
}

/*
 * Transform the points of the glyph being decoded into 1/32-pixel space, in
 * place, and append every contour.  (a,b,c,d,dx,dy) maps font units to font
 * units (the composite transform); the em scale and the synthetic italic
 * shear are applied on top.
 */
static int ttf_emit_points(nj_ttf *t, int ncont, int npts,
                           double a, double b, double c, double d,
                           double dx, double dy)
{
    int i;
    int first = 0;
    for (i = 0; i < npts; i++) {
        double x = (double)t->pts[2 * i];
        double y = (double)t->pts[2 * i + 1];
        double px = (a * x + c * y + dx) * t->scale;
        double py = (b * x + d * y + dy) * t->scale;
        double qx, qy;
        if (t->style & NJ_FONT_STYLE_ITALIC) {
            px += NJ_TTF_SHEAR * py;
        }
        qx = floor(px * (double)NJ_TTF_ONE + 0.5);
        qy = floor(py * (double)NJ_TTF_ONE + 0.5);
        if (qx < -NJ_TTF_MAX_VAL || qx > NJ_TTF_MAX_VAL ||
            qy < -NJ_TTF_MAX_VAL || qy > NJ_TTF_MAX_VAL) {
            return 0;           /* em size far beyond what we can address */
        }
        t->pts[2 * i] = (nj_s2)qx;
        t->pts[2 * i + 1] = (nj_s2)qy;
    }
    for (i = 0; i < ncont; i++) {
        int last = (int)t->ends[i];
        if (last < first || last >= npts) {
            return 0;
        }
        if (!ttf_walk_contour(t, first, last)) {
            return 0;
        }
        if (t->ncont_out >= NJ_TTF_MAX_CONTOURS) {
            return 0;           /* more contours than the outline can hold */
        }
        t->cend[t->ncont_out++] = (nj_u2)t->gn;
        first = last + 1;
    }
    return 1;
}

static int ttf_geom(nj_ttf *t, int gid, int depth,
                    double a, double b, double c, double d,
                    double dx, double dy);

/* Simple glyph: header, end points, instructions, flags, coordinates. */
static int ttf_geom_simple(nj_ttf *t, nj_u4 goff, nj_u4 glen, int ncont,
                           double a, double b, double c, double d,
                           double dx, double dy)
{
    nj_u4 end_last;
    nj_u4 npts;
    nj_u4 p;
    nj_u4 i;
    nj_u4 ilen;
    long x, y;

    if (ncont > NJ_TTF_MAX_CONTOURS) {
        return 0;
    }
    if (ncont == 0) {
        t->gempty = 1;
        return 1;
    }
    if (10u + 2u * (nj_u4)ncont + 2u > glen) {
        return 0;
    }
    if (!ttf_u2(t, goff + 10u + 2u * ((nj_u4)ncont - 1u), &end_last)) {
        return 0;
    }
    for (i = 0; i < (nj_u4)ncont; i++) {
        nj_u4 e;
        if (!ttf_u2(t, goff + 10u + 2u * i, &e)) {
            return 0;
        }
        t->ends[i] = (nj_u2)e;
        if (i > 0u && e <= (nj_u4)t->ends[i - 1u]) {
            return 0;           /* end points must strictly increase */
        }
    }
    npts = end_last + 1u;
    if (npts > (nj_u4)NJ_TTF_MAX_POINTS) {
        return 0;
    }
    if (npts == 0u) {
        t->gempty = 1;
        return 1;
    }

    p = 10u + 2u * (nj_u4)ncont;
    if (!sl_u2(t, (ttf_slice){ goff, glen }, p, &ilen)) {
        return 0;
    }
    p += 2u;
    if (ilen > glen - p) {
        return 0;               /* instructions past the end of the glyph */
    }
    p += ilen;

    /* Flags, run-length encoded. */
    i = 0;
    while (i < npts) {
        nj_u4 f;
        if (p >= glen) {
            return 0;
        }
        f = (nj_u4)t->data[goff + p];
        p++;
        t->pflags[i++] = (nj_u1)f;
        if (f & 0x08u) {
            nj_u4 rep;
            if (p >= glen) {
                return 0;
            }
            rep = (nj_u4)t->data[goff + p];
            p++;
            while (rep > 0u) {
                if (i >= npts) {
                    return 0;
                }
                t->pflags[i++] = (nj_u1)f;
                rep--;
            }
        }
    }

    /* X coordinates (deltas). */
    x = 0;
    for (i = 0; i < npts; i++) {
        nj_u4 f = t->pflags[i];
        long v = 0;
        if (f & 0x02u) {
            if (p >= glen) {
                return 0;
            }
            v = (long)t->data[goff + p];
            p++;
            if (!(f & 0x10u)) {
                v = -v;
            }
        } else if (!(f & 0x10u)) {
            int s;
            if (!sl_s2(t, (ttf_slice){ goff, glen }, p, &s)) {
                return 0;
            }
            p += 2u;
            v = s;
        }
        x += v;
        if (x < -32768 || x > 32767) {
            return 0;
        }
        t->pts[2 * i] = (nj_s2)x;
    }
    /* Y coordinates. */
    y = 0;
    for (i = 0; i < npts; i++) {
        nj_u4 f = t->pflags[i];
        long v = 0;
        if (f & 0x04u) {
            if (p >= glen) {
                return 0;
            }
            v = (long)t->data[goff + p];
            p++;
            if (!(f & 0x20u)) {
                v = -v;
            }
        } else if (!(f & 0x20u)) {
            int s;
            if (!sl_s2(t, (ttf_slice){ goff, glen }, p, &s)) {
                return 0;
            }
            p += 2u;
            v = s;
        }
        y += v;
        if (y < -32768 || y > 32767) {
            return 0;
        }
        t->pts[2 * i + 1] = (nj_s2)y;
    }

    return ttf_emit_points(t, ncont, (int)npts, a, b, c, d, dx, dy);
}

#define NJ_TTF_ARG_1_AND_2_ARE_WORDS    0x0001u
#define NJ_TTF_ARGS_ARE_XY_VALUES       0x0002u
#define NJ_TTF_WE_HAVE_A_SCALE          0x0008u
#define NJ_TTF_MORE_COMPONENTS          0x0020u
#define NJ_TTF_WE_HAVE_AN_X_AND_Y_SCALE 0x0040u
#define NJ_TTF_WE_HAVE_A_TWO_BY_TWO     0x0080u
#define NJ_TTF_SCALED_COMPONENT_OFFSET  0x0800u

static double ttf_f2dot14(const nj_ttf *t, nj_u4 off, int *ok)
{
    int v;
    if (!ttf_s2(t, off, &v)) {
        *ok = 0;
        return 0.0;
    }
    return (double)v / 16384.0;
}

/* Composite glyph: components, each with its own transform. */
static int ttf_geom_composite(nj_ttf *t, nj_u4 goff, nj_u4 glen,
                              double a, double b, double c, double d,
                              double dx, double dy)
{
    nj_u4 p = 10u;
    int n = 0;
    for (;;) {
        nj_u4 flags, cgid;
        double ca = 1.0, cb = 0.0, cc = 0.0, cd = 1.0;
        double cdx, cdy;
        int ok = 1;

        if (p > glen || 4u > glen - p) {
            return 0;
        }
        if (!ttf_u2(t, goff + p, &flags) || !ttf_u2(t, goff + p + 2u, &cgid)) {
            return 0;
        }
        p += 4u;
        if (!(flags & NJ_TTF_ARGS_ARE_XY_VALUES)) {
            return 0;           /* point matching: not supported, by design */
        }
        if (flags & NJ_TTF_ARG_1_AND_2_ARE_WORDS) {
            int ax, ay;
            if (!sl_s2(t, (ttf_slice){ goff, glen }, p, &ax) ||
                !sl_s2(t, (ttf_slice){ goff, glen }, p + 2u, &ay)) {
                return 0;
            }
            p += 4u;
            cdx = (double)ax;
            cdy = (double)ay;
        } else {
            nj_u4 bx, by;
            if (!sl_u1(t, (ttf_slice){ goff, glen }, p, &bx) ||
                !sl_u1(t, (ttf_slice){ goff, glen }, p + 1u, &by)) {
                return 0;
            }
            p += 2u;
            cdx = (double)(int)(nj_s1)bx;
            cdy = (double)(int)(nj_s1)by;
        }
        if (flags & NJ_TTF_WE_HAVE_A_TWO_BY_TWO) {
            ca = ttf_f2dot14(t, goff + p, &ok);
            cb = ttf_f2dot14(t, goff + p + 2u, &ok);
            cc = ttf_f2dot14(t, goff + p + 4u, &ok);
            cd = ttf_f2dot14(t, goff + p + 6u, &ok);
            p += 8u;
        } else if (flags & NJ_TTF_WE_HAVE_AN_X_AND_Y_SCALE) {
            ca = ttf_f2dot14(t, goff + p, &ok);
            cd = ttf_f2dot14(t, goff + p + 2u, &ok);
            p += 4u;
        } else if (flags & NJ_TTF_WE_HAVE_A_SCALE) {
            ca = cd = ttf_f2dot14(t, goff + p, &ok);
            p += 2u;
        }
        if (!ok || p > glen) {
            return 0;
        }
        if (flags & NJ_TTF_SCALED_COMPONENT_OFFSET) {
            double sx = ca * cdx + cc * cdy;
            double sy = cb * cdx + cd * cdy;
            cdx = sx;
            cdy = sy;
        }
        if (cgid >= (nj_u4)t->num_glyphs) {
            return 0;
        }
        /* Compose: this component's transform, then the caller's. */
        {
            double na = a * ca + c * cb;
            double nb = b * ca + d * cb;
            double nc = a * cc + c * cd;
            double nd = b * cc + d * cd;
            double ndx = a * cdx + c * cdy + dx;
            double ndy = b * cdx + d * cdy + dy;
            if (!ttf_geom(t, (int)cgid, 1, na, nb, nc, nd, ndx, ndy)) {
                return 0;
            }
        }
        if (!(flags & NJ_TTF_MORE_COMPONENTS)) {
            break;
        }
        if (++n >= NJ_TTF_MAX_COMPONENTS) {
            return 0;
        }
    }
    return 1;
}

/* Load a glyph's outline into the vertex array.  Returns 0 if the glyph is
 * malformed or exceeds a limit; the caller then reports "no glyph". */
static int ttf_geom(nj_ttf *t, int gid, int depth,
                    double a, double b, double c, double d,
                    double dx, double dy)
{
    nj_u4 start, end;
    nj_u4 goff, glen;
    int ncont;

    if (depth > NJ_TTF_MAX_DEPTH || t->failed) {
        return 0;
    }
    if (gid < 0 || gid >= t->num_glyphs) {
        return 0;
    }
    if (t->loca_long) {
        if (!sl_u4(t, t->loca, 4u * (nj_u4)gid, &start) ||
            !sl_u4(t, t->loca, 4u * (nj_u4)gid + 4u, &end)) {
            return 0;
        }
    } else {
        nj_u4 s2v, e2v;
        if (!sl_u2(t, t->loca, 2u * (nj_u4)gid, &s2v) ||
            !sl_u2(t, t->loca, 2u * (nj_u4)gid + 2u, &e2v)) {
            return 0;
        }
        start = s2v * 2u;
        end = e2v * 2u;
    }
    if (start == end) {
        t->gempty = 1;          /* zero-length slice: an empty glyph */
        return 1;
    }
    if (start > end || end > t->glyf.len) {
        return 0;
    }
    goff = t->glyf.off + start;
    glen = end - start;
    if (glen < 10u) {
        return 0;
    }
    {
        int nc;
        if (!ttf_s2(t, goff, &nc)) {
            return 0;
        }
        ncont = nc;
    }
    if (ncont >= 0) {
        return ttf_geom_simple(t, goff, glen, ncont, a, b, c, d, dx, dy);
    }
    if (ncont == -1) {
        return ttf_geom_composite(t, goff, glen, a, b, c, d, dx, dy);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Exterior span accumulation and rasterisation                        */
/* ------------------------------------------------------------------ */
/* Floor division by 32 for a signed value (>> on negatives is not portable
 * enough to rely on). */
static nj_s4 ttf_px_floor(nj_s4 q)
{
    return (q >= 0) ? (q / NJ_TTF_ONE)
                    : -(((-q) + NJ_TTF_ONE - 1) / NJ_TTF_ONE);
}

static nj_s4 ttf_px_ceil(nj_s4 q)
{
    return (q >= 0) ? ((q + NJ_TTF_ONE - 1) / NJ_TTF_ONE)
                    : -((-q) / NJ_TTF_ONE);
}

/* Add the horizontal overlap of [xa,xb) with each pixel it touches. */
static void ttf_add_span(nj_u4 *row, int w, nj_s4 xa, nj_s4 xb)
{
    nj_s4 p0, p1, px;
    if (xb <= xa || w <= 0) {
        return;
    }
    p0 = ttf_px_floor(xa);
    p1 = ttf_px_floor(xb - 1);
    if (p0 < 0) {
        p0 = 0;
    }
    if (p1 > (nj_s4)w - 1) {
        p1 = (nj_s4)w - 1;
    }
    for (px = p0; px <= p1; px++) {
        nj_s4 lo = px * NJ_TTF_ONE;
        nj_s4 hi = lo + NJ_TTF_ONE;
        nj_s4 s = (xa > lo) ? xa : lo;
        nj_s4 e = (xb < hi) ? xb : hi;
        if (e > s) {
            row[px] += (nj_u4)(e - s);
        }
    }
}

/*
 * Non-zero winding, 16 sub-scanlines per pixel row, exact horizontal overlap.
 * A crossing is counted for a directed edge when the sub-scanline is in
 * [ymin, ymax), which is what makes horizontal edges, vertices and edges that
 * land exactly on a scanline come out right; the winding is summed left to
 * right and a span is painted wherever it is non-zero, so even-odd shapes,
 * self-intersecting contours and nested contours with opposite winding all
 * fall out of the same code.
 */
static int ttf_raster(nj_ttf *t, nj_u1 *dst, int w, int h)
{
    int r, s, q, i, ci;

    for (r = 0; r < h; r++) {
        for (q = 0; q < w; q++) {
            t->rowacc[q] = 0u;
        }
        for (s = 0; s < NJ_TTF_SUBSCAN; s++) {
            nj_s4 y = ((nj_s4)t->ascent - (nj_s4)r) * NJ_TTF_ONE -
                      (nj_s4)(2 * s + 1);
            int nc = 0;
            int wind;
            nj_s4 prev;

            for (ci = 0; ci < t->ncont_out; ci++) {
                int vfirst = (ci == 0) ? 0 : (int)t->cend[ci - 1];
                int vlast = (int)t->cend[ci];   /* one past the last vertex */
                for (i = vfirst; i < vlast; i++) {
                    const nj_s2 *p0 = &t->verts[2 * i];
                    const nj_s2 *p1 =
                        &t->verts[2 * ((i + 1 == vlast) ? vfirst : i + 1)];
                    nj_s4 y0 = (nj_s4)p0[1];
                    nj_s4 y1 = (nj_s4)p1[1];
                    double x;
                    if (!((y0 <= y && y < y1) || (y1 <= y && y < y0))) {
                        continue;
                    }
                    if (nc >= NJ_TTF_MAX_CROSS) {
                        return 0;   /* pathological outline: reject the glyph */
                    }
                    x = (double)p0[0] +
                        (double)(y - y0) * (double)((nj_s4)p1[0] - (nj_s4)p0[0]) /
                            (double)(y1 - y0);
                    {
                        double rx = floor(x + 0.5);
                        if (rx < -32768.0) { rx = -32768.0; }
                        if (rx > 32767.0) { rx = 32767.0; }
                        t->cross[nc].x = (nj_s2)rx;
                        t->cross[nc].d = (y1 > y0) ? (nj_s1)1 : (nj_s1)-1;
                        nc++;
                    }
                }
            }
            /* Insertion sort by x: a scanline crosses an outline a handful
             * of times, and this is called a few thousand times per glyph. */
            for (i = 1; i < nc; i++) {
                ttf_cross key = t->cross[i];
                int j = i - 1;
                while (j >= 0 && t->cross[j].x > key.x) {
                    t->cross[j + 1] = t->cross[j];
                    j--;
                }
                t->cross[j + 1] = key;
            }
            wind = 0;
            prev = 0;
            for (i = 0; i < nc; i++) {
                if (wind != 0 && (nj_s4)t->cross[i].x > prev) {
                    ttf_add_span(t->rowacc, w, prev, (nj_s4)t->cross[i].x);
                }
                wind += (int)t->cross[i].d;
                prev = (nj_s4)t->cross[i].x;
            }
        }
        for (q = 0; q < w; q++) {
            /* rowacc is in 1/32 px: NJ_TTF_SUBSCAN full pixels = 512 units. */
            nj_u4 cov = (t->rowacc[q] * 255u + 256u) /
                        (nj_u4)(NJ_TTF_SUBSCAN * NJ_TTF_ONE);
            dst[(nj_size)r * (nj_size)w + (nj_size)q] = (nj_u1)cov;
        }
    }
    return 1;
}

/* Synthetic bold: one-pixel horizontal smear, which is what "embolden" means
 * at these sizes.  It moves the ink half a pixel right; the alternative (a
 * smear in both directions) doubles the work for a width the eye cannot see. */
static void ttf_embolden(nj_u1 *dst, int w, int h)
{
    int r, x;
    for (r = 0; r < h; r++) {
        nj_u1 *row = dst + (nj_size)r * (nj_size)w;
        for (x = w - 1; x >= 1; x--) {
            if (row[x - 1] > row[x]) {
                row[x] = row[x - 1];
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The glyph provider                                                  */
/* ------------------------------------------------------------------ */
static void ttf_cache_reset(nj_ttf *t)
{
    t->cache_used = 0;
    t->pool_used = 0u;
}

static const nj_u1 *ttf_glyph_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    nj_ttf *t = (nj_ttf *)ud;
    int gid;
    int i;
    int adv;
    int bw, bh;
    nj_u4 bytes;
    nj_u4 off;
    nj_u1 *dst;

    if (t == NULL || w == NULL || h == NULL) {
        return NULL;
    }
    gid = ttf_map_char(t, ch);
    if (gid <= 0) {
        return NULL;            /* missing: the renderer falls back */
    }
    for (i = 0; i < t->cache_used; i++) {
        if ((int)t->slots[i].gid == gid) {
            *w = (int)t->slots[i].w;
            *h = (int)t->slots[i].h;
            return t->pool + t->slots[i].off;
        }
    }

    t->gn = 0;
    t->ncont_out = 0;
    t->gempty = 0;
    t->failed = 0;
    if (!ttf_geom(t, gid, 0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0) || t->failed) {
        return NULL;
    }
    adv = ttf_adv_px(t, gid);

    if (t->gn == 0) {
        /* A glyph with no outline: whitespace.  Width from hmtx, zero rows. */
        bw = (adv > 0) ? adv : 1;
        bh = 1;
    } else {
        nj_s4 xmax = ttf_px_ceil(t->gxmax);
        nj_s4 below = -ttf_px_floor(t->gymin);
        bw = (adv > (int)xmax) ? adv : (int)xmax;
        if (bw < 1) {
            bw = 1;
        }
        bh = t->ascent + ((below > (nj_s4)t->descent) ? (int)below : t->descent);
        if (bh < 1) {
            bh = 1;
        }
    }
    if (bw > NJ_TTF_MAX_W || bh > NJ_TTF_MAX_H) {
        return NULL;
    }
    bytes = (nj_u4)bw * (nj_u4)bh;
    if (bytes > t->pool_bytes) {
        return NULL;            /* the caller's cache is too small for this */
    }
    if (t->pool_used > t->pool_bytes - bytes) {
        ttf_cache_reset(t);
    }
    off = t->pool_used;
    t->pool_used += bytes;
    dst = t->pool + off;

    if (t->gn == 0) {
        memset(dst, 0, (nj_size)bytes);
    } else {
        if (!ttf_raster(t, dst, bw, bh)) {
            return NULL;
        }
        if (t->style & NJ_FONT_STYLE_BOLD) {
            ttf_embolden(dst, bw, bh);
        }
    }

    if (t->cache_used < t->cache_cap) {
        ttf_slot *s = &t->slots[t->cache_used++];
        s->gid = (nj_u4)gid;
        s->off = off;
        s->w = (nj_u4)bw;
        s->h = (nj_u4)bh;
        s->adv = (nj_u4)adv;
    }
    *w = bw;
    *h = bh;
    return dst;
}

static int ttf_glyph_advance(void *ud, nj_u2 ch)
{
    nj_ttf *t = (nj_ttf *)ud;
    int gid;
    int i;
    if (t == NULL) {
        return 0;
    }
    gid = ttf_map_char(t, ch);
    if (gid <= 0) {
        return t->default_adv;
    }
    for (i = 0; i < t->cache_used; i++) {
        if ((int)t->slots[i].gid == gid) {
            return (int)t->slots[i].adv;
        }
    }
    return ttf_adv_px(t, gid);
}

/* ------------------------------------------------------------------ */
/* Open / configure                                                    */
/* ------------------------------------------------------------------ */
static int ttf_parse_headers(nj_ttf *t)
{
    ttf_slice head, hhea, maxp;
    nj_u4 magic, upem, num_glyphs, num_hmetrics;
    int index_to_loc;
    int asc, desc;

    if (!ttf_find_table(t, NJ_TTF_TAG('h', 'e', 'a', 'd'), &head) ||
        head.len < 54u) {
        return 0;
    }
    if (!ttf_u4v(t, head.off + 12u, &magic) || magic != 0x5F0F3CF5u) {
        return 0;
    }
    if (!ttf_u2(t, head.off + 18u, &upem)) {
        return 0;
    }
    if (upem < 16u || upem > 16384u) {
        return 0;
    }
    if (!ttf_s2(t, head.off + 50u, &index_to_loc)) {
        return 0;
    }
    if (index_to_loc != 0 && index_to_loc != 1) {
        return 0;
    }
    t->upem = (int)upem;
    t->loca_long = (index_to_loc == 1) ? 1 : 0;

    if (!ttf_find_table(t, NJ_TTF_TAG('m', 'a', 'x', 'p'), &maxp) ||
        maxp.len < 6u) {
        return 0;
    }
    if (!ttf_u2(t, maxp.off + 4u, &num_glyphs) || num_glyphs == 0u ||
        num_glyphs > 65535u) {
        return 0;
    }
    t->num_glyphs = (int)num_glyphs;

    if (!ttf_find_table(t, NJ_TTF_TAG('h', 'h', 'e', 'a'), &hhea) ||
        hhea.len < 36u) {
        return 0;
    }
    if (!ttf_u2(t, hhea.off + 34u, &num_hmetrics) || num_hmetrics == 0u ||
        num_hmetrics > num_glyphs) {
        return 0;
    }
    t->num_hmetrics = (int)num_hmetrics;

    if (!ttf_find_table(t, NJ_TTF_TAG('l', 'o', 'c', 'a'), &t->loca)) {
        return 0;
    }
    if (!ttf_find_table(t, NJ_TTF_TAG('g', 'l', 'y', 'f'), &t->glyf)) {
        return 0;
    }
    if (!ttf_find_table(t, NJ_TTF_TAG('h', 'm', 't', 'x'), &t->hmtx)) {
        return 0;
    }
    {
        /* loca must be able to address every glyph, glyf must not be shorter
         * than loca claims (checked per glyph), hmtx must hold every metric. */
        nj_u4 need_loca = (num_glyphs + 1u) * (t->loca_long ? 4u : 2u);
        nj_u4 need_hmtx = 4u * num_hmetrics +
                          2u * (num_glyphs - num_hmetrics);
        if (t->loca.len < need_loca || t->hmtx.len < need_hmtx) {
            return 0;
        }
    }

    t->scale = (double)t->px_size / (double)t->upem;

    /* Line metrics.  hhea is what FreeType reports as the face height, so it
     * leads; OS/2's usWin* only rescues a degenerate hhea. */
    if (!ttf_s2(t, hhea.off + 4u, &asc) || !ttf_s2(t, hhea.off + 6u, &desc)) {
        return 0;
    }
    {
        int gap;
        if (!ttf_s2(t, hhea.off + 8u, &gap)) {
            return 0;
        }
        t->leading = (int)ceil((double)gap * t->scale);
        if (t->leading < 0) {
            t->leading = 0;
        }
    }
    if (ttf_find_table(t, NJ_TTF_TAG('O', 'S', '/', '2'), &t->os2) &&
        t->os2.len >= 78u) {
        nj_u4 win_asc, win_desc;
        if (asc <= 0 && ttf_u2(t, t->os2.off + 74u, &win_asc)) {
            asc = (int)win_asc;
        }
        if (desc <= 0 && ttf_u2(t, t->os2.off + 76u, &win_desc)) {
            desc = -(int)win_desc;
        }
    }
    if (asc <= 0 || desc >= 0) {
        /* Still nonsense: use a conventional 80/20 split of the em. */
        asc = t->px_size * 4 / 5;
        desc = -(t->px_size - asc);
    }
    t->ascent = (int)ceil((double)asc * t->scale);
    t->descent = (int)ceil((double)(-desc) * t->scale);
    /* yMax from head: ink above the hhea ascender must still have a row 0 to
     * land in, or the tops of tall glyphs would be clipped. */
    {
        int ymax = 0;
        if (ttf_s2(t, head.off + 42u, &ymax) && ymax > 0) {
            int need = (int)ceil((double)ymax * t->scale);
            if (need > t->ascent) {
                t->ascent = need;
            }
        }
    }
    if (t->ascent < 1) {
        t->ascent = 1;
    }
    if (t->descent < 0) {
        t->descent = 0;
    }
    t->height = t->ascent + t->descent + t->leading;
    return 1;
}

/*
 * Every loca entry must be inside glyf and non-decreasing.  This is the one
 * piece of glyph data cheap enough to check for the whole font up front, and
 * checking it is what makes a corrupt loca a failed open rather than a failed
 * lookup much later.
 */
static int ttf_check_loca(const nj_ttf *t)
{
    nj_u4 i;
    nj_u4 prev = 0u;
    for (i = 0; i <= (nj_u4)t->num_glyphs; i++) {
        nj_u4 v;
        if (t->loca_long) {
            if (!sl_u4(t, t->loca, 4u * i, &v)) {
                return 0;
            }
        } else {
            nj_u4 v2;
            if (!sl_u2(t, t->loca, 2u * i, &v2)) {
                return 0;
            }
            v = v2 * 2u;
        }
        if (v > t->glyf.len || (i > 0u && v < prev)) {
            return 0;
        }
        prev = v;
    }
    return 1;
}

nj_ttf *nj_ttf_open(void *mem, nj_size mem_bytes,
                    const nj_u1 *data, nj_u4 len, int px_size)
{    nj_ttf *t;
    nj_u4 version;
    nj_size base, cap, slots_bytes, pool_off;
    nj_u1 *p;

    if (mem == NULL || data == NULL || len < 12u || px_size <= 0) {
        return NULL;
    }
    base = ttf_base_bytes();
    if (mem_bytes <= base + (nj_size)NJ_TTF_POOL_PER_GLYPH) {
        return NULL;
    }
    cap = (mem_bytes - base) /
          ((nj_size)sizeof(ttf_slot) + (nj_size)NJ_TTF_POOL_PER_GLYPH);
    if (cap > (nj_size)NJ_TTF_MAX_CACHE) {
        cap = (nj_size)NJ_TTF_MAX_CACHE;
    }
    for (;;) {
        slots_bytes = ttf_align8(cap * (nj_size)sizeof(ttf_slot));
        pool_off = base + slots_bytes;
        if (cap > 0u && pool_off < mem_bytes &&
            mem_bytes - pool_off >= (nj_size)NJ_TTF_POOL_PER_GLYPH) {
            break;
        }
        if (cap == 0u) {
            return NULL;
        }
        cap--;
    }

    t = (nj_ttf *)mem;
    memset(t, 0, sizeof(*t));
    t->data = data;
    t->len = len;
    t->px_size = px_size;
    t->slots = (ttf_slot *)((nj_u1 *)mem + base);
    t->cache_cap = (int)cap;
    t->pool = (nj_u1 *)mem + pool_off;
    t->pool_bytes = mem_bytes - pool_off;
    p = (nj_u1 *)mem + ttf_align8((nj_size)sizeof(nj_ttf));
    t->pts = (nj_s2 *)p;
    p += (nj_size)(NJ_TTF_MAX_POINTS * 4);
    t->pflags = p;
    p += (nj_size)NJ_TTF_MAX_POINTS;
    t->verts = (nj_s2 *)p;
    p += (nj_size)(NJ_TTF_MAX_VERTS * 4);
    t->cross = (ttf_cross *)p;
    p += (nj_size)NJ_TTF_MAX_CROSS * (nj_size)sizeof(ttf_cross);
    t->rowacc = (nj_u4 *)p;

    /* Directory, then the tables we cannot do without. */
    if (!ttf_u4v(t, 0u, &version)) {
        return NULL;
    }
    if (version != 0x00010000u && version != 0x74727565u) {
        return NULL;            /* including 'OTTO': no glyf outlines here */
    }
    if (!ttf_check_directory(t)) {
        return NULL;
    }
    if (!ttf_parse_headers(t)) {
        return NULL;
    }
    if (!ttf_check_loca(t)) {
        return NULL;
    }
    if (!ttf_find_table(t, NJ_TTF_TAG('c', 'm', 'a', 'p'), &t->cmap)) {
        return NULL;
    }
    if (!ttf_parse_cmap(t)) {
        return NULL;
    }
    (void)ttf_find_table(t, NJ_TTF_TAG('n', 'a', 'm', 'e'), &t->name);
    ttf_read_name(t);
    t->default_adv = ttf_default_advance(t);
    (void)ttf_adv_px(t, 0);
    ttf_cache_reset(t);
    return t;
}

int nj_ttf_font(nj_ttf *t, nj_font *out, int style, int size)
{
    if (t == NULL || out == NULL) {
        return 0;
    }
    if (t->style != style) {
        t->style = style;
        ttf_cache_reset(t);     /* cached rows are style-specific */
    }
    memset(out, 0, sizeof(*out));
    out->name = t->namebuf;
    out->height = t->height;
    out->ascent = t->ascent;
    out->descent = t->descent;
    out->leading = t->leading;
    out->style = style;
    out->size = size;
    out->proportional = 1;
    out->first_char = 0u;
    out->last_char = 0u;
    out->glyph_w = t->default_adv;
    out->glyph_h = t->height;
    out->bitmap = NULL;
    out->glyph_lookup = ttf_glyph_lookup;
    out->coverage = 1;
    out->glyph_ud = t;
    out->glyph_advance = ttf_glyph_advance;
    out->fallback = NULL;
    return 1;
}

#else  /* !NJ_ENABLE_TTF */

/* ISO C forbids an empty translation unit, and a build with TrueType off is
 * exactly that. */
typedef int nj_ttf_disabled_;

#endif /* NJ_ENABLE_TTF */
