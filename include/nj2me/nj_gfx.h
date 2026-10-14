/*
 * nano_j2me - nj_gfx.h
 *
 * Software rasterizer.  Everything MIDP Graphics needs, implemented over a
 * plain linear pixel buffer, with a translate + clip state exactly like
 * javax.microedition.lcdui.Graphics.
 *
 * Pixel formats: NJ_PIXFMT_RGB565 (default, embedded panels),
 *                NJ_PIXFMT_RGB888, NJ_PIXFMT_ARGB8888.
 *
 * All drawing honours: color (ARGB8888 with alpha), translate, clip,
 * blend mode (SRC_OVER / NONE) and the stroke style for drawRect/drawArc/
 * drawRoundRect.
 */
#ifndef NJ_GFX_H
#define NJ_GFX_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Surface                                                             */
/* ------------------------------------------------------------------ */
typedef struct nj_surface {
    void *pixels;     /* linear buffer, stride bytes per row */
    int   width;
    int   height;
    int   stride;     /* bytes per row; may be != width*bpp */
    int   format;     /* NJ_PIXFMT_* */
    int   bpp;        /* bytes per pixel, cached */
} nj_surface;

/* ------------------------------------------------------------------ */
/* Stroke / blend styles                                               */
/* ------------------------------------------------------------------ */
#define NJ_STROKE_SOLID  0
#define NJ_STROKE_DOTTED 1

#define NJ_BLEND_NONE    0   /* opaque copy  */
#define NJ_BLEND_SRC_OVER 1  /* alpha blend  */

/* ------------------------------------------------------------------ */
/* Font                                                                */
/* ------------------------------------------------------------------ */
/*
 * A bitmap font.  Glyphs are 1 bit per pixel, row-major, MSB first, one byte
 * per 8 horizontal pixels:  row_bytes = (glyph_w + 7) / 8.
 *
 * `glyph_lookup` optionally overrides the fixed-pitch bitmap for characters
 * outside [first_char, last_char] - this is how the CJK provider hooks in.
 */
typedef struct nj_font {
    const char *name;
    int   height;        /* line height = ascent + descent (+ leading) */
    int   ascent;
    int   descent;
    int   leading;
    int   style;         /* NJ_FONT_STYLE_*  */
    int   size;          /* NJ_FONT_SIZE_*   */
    int   proportional;
    nj_u2 first_char;
    nj_u2 last_char;
    int   glyph_w;       /* fixed cell width  */
    int   glyph_h;
    const nj_u1 *bitmap; /* (last-first+1) * glyph_h * row_bytes */

    /*
     * Optional glyph provider, for anything the fixed table cannot hold: a wide
     * face, a CJK font, or a TrueType face rasterised on demand.
     *
     * The rows it returns are 1bpp (MSB first, (w+7)/8 bytes per row) unless
     * `coverage` is set, in which case they are one byte per pixel of coverage
     * and the renderer blends.  Coverage exists because thresholding an
     * antialiased rasteriser deletes thin strokes: a hinted-away stem is two
     * columns at about 50%, and a 1bpp cut through it loses the stem entirely
     * from "1", "l", "i" and "t".
     */
    const nj_u1 *(*glyph_lookup)(void *ud, nj_u2 ch, int *w, int *h);
    int   coverage;      /* 0 = 1bpp rows, 1 = 8bpp coverage rows */
    void *glyph_ud;
    /* Optional advance metric override (proportional wide glyphs). */
    int (*glyph_advance)(void *ud, nj_u2 ch);

    /* Font consulted for characters outside [first_char, last_char] and for
     * glyphs the fixed cell cannot represent (CJK).  Chainable: the fallback
     * may itself have a fallback.  NULL = no fallback. */
    const struct nj_font *fallback;
} nj_font;

#define NJ_FONT_STYLE_PLAIN  0
#define NJ_FONT_STYLE_BOLD   1
#define NJ_FONT_STYLE_ITALIC 2
#define NJ_FONT_STYLE_UNDERLINED 4

#define NJ_FONT_SIZE_SMALL   8
#define NJ_FONT_SIZE_MEDIUM  0
#define NJ_FONT_SIZE_LARGE   16

/*
 * Font registry.
 *
 * The runtime holds no mutable global font state, so the *caller* (the VM)
 * owns an nj_fontset.  It contains mutable nj_font instances built from the
 * immutable bitmap tables in .rodata; the fallback pointer can therefore be
 * installed per-instance without touching shared data.
 *
 *   nj_fontset fs;
 *   nj_fontset_init(&fs);
 *   nj_fontset_set_fallback(&fs, my_cjk_font);
 *   const nj_font *f = nj_fontset_get(&fs, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
 */
/*
 * The variants each size has in a font set: plain and bold, and nothing else.
 * A MIDP font also has ITALIC and UNDERLINED, but those are synthesised by the
 * renderer, so they do not get a slot of their own.
 *
 * This is the stride of the slot array, which makes it the one number
 * nj_fontset_get() has to agree on with everything that fills the slots
 * (nj_font_ttf.c).  Two files computing a slot index from a private copy of it
 * is a bug waiting for the day one copy changes: the writer and the reader
 * would disagree about which face a size maps to, and the wrong font would be
 * handed out with no error anywhere.
 */
#define NJ_FONT_SLOT_STYLES 2

#define NJ_FONTSET_SLOTS (3 * NJ_FONT_SLOT_STYLES)

typedef struct nj_fontset {
    nj_font fonts[NJ_FONTSET_SLOTS];
    int     initialized;
    int     count;
} nj_fontset;

void          nj_fontset_init(nj_fontset *fs);
const nj_font *nj_fontset_get(nj_fontset *fs, int size, int style);
void          nj_fontset_set_fallback(nj_fontset *fs, const nj_font *fb);

/*
 * Install a fallback per slot, for a port whose glyphs live outside the
 * runtime: the platform's text API, a system font, a server.
 *
 * `fonts` is indexed by size_index(size) * NJ_FONT_SLOT_STYLES + bold, so the
 * caller can give the large face a bigger provider cell than the small one -
 * one shared fallback cannot, because it is never told which size asked.
 * A NULL entry leaves that slot alone, and a count below NJ_FONTSET_SLOTS
 * only fills the first `count` slots.
 *
 * Install this before any TrueType face: nj_fontset_use_ttf() rebuilds the
 * slots and gives each one its own fallback chain.
 */
void          nj_fontset_set_fallback_slots(nj_fontset *fs,
                                            const nj_font *const *fonts,
                                            int count);

/*
 * Install the port's faces as the slots themselves, rather than behind them.
 *
 * A fallback only answers for characters the built-in cell cannot draw.  A
 * platform that draws Latin better than the bundled bitmap can - and should
 * draw every character - needs to be the slot: same indexing, same NULL and
 * count rules, but the built-in face of that slot is replaced, so both the
 * advances and the ink come from the port.
 *
 * The slot keeps whatever fallback it already had, so a face installed here
 * can still leave a chain behind it.  The face's own metrics - height, ascent,
 * descent - become the slot's, so install before the MIDlet builds a screen.
 */
void          nj_fontset_set_primary_slots(nj_fontset *fs,
                                           const nj_font *const *fonts,
                                           int count);

/* The immutable bitmap table behind a given (size, style) pair.  Exposed so a
 * port can build its own nj_font if it wants a different registry lifetime.
 * Never NULL. */
const nj_font *nj_font_builtin(int size, int style);

/*
 * Render through a TrueType face.
 *
 * `data`/`len` is the whole font file and must stay valid for as long as the
 * font set uses it - the faces borrow it and do not copy it, so a caller
 * holding the file in a heap block must keep that block alive.  `alloc`
 * supplies each face's memory including its glyph cache; the emulator passes
 * the VM's host allocator, so every block is released by nj_vm_destroy() and
 * nothing here needs a matching teardown.  Two emulator instances in one
 * process see nothing of each other's faces.
 *
 * Every (size, style) slot is rebuilt.  Metrics come from the bitmap face each
 * slot replaces rather than from the font, because MIDlet layout is written
 * against Font.getHeight(): a drop-in face reporting a different line height
 * would silently re-flow every screen.  The bitmap face stays as the fallback,
 * so a character the supplied font has no glyph for still renders.
 *
 * Returns the number of slots rebuilt (NJ_FONTSET_SLOTS on success), or 0 if
 * the font cannot be parsed or an allocation fails.  On 0 the font set is left
 * exactly as it was - a partly upgraded font set is a state no caller can
 * reason about, and a corrupt file should not be able to produce one.  A build
 * with NJ_ENABLE_TTF=0 always returns 0 and keeps its bitmap faces.
 */
typedef void *(*nj_font_alloc_fn)(void *ud, nj_size n);

int nj_fontset_use_ttf(nj_fontset *fs, const nj_u1 *data, nj_u4 len,
                       nj_font_alloc_fn alloc, void *ud);

/* Metrics. */
int nj_font_char_width(const nj_font *f, nj_u2 ch);
int nj_font_string_width(const nj_font *f, const nj_u2 *s, int len);
int nj_font_chars_that_fit(const nj_font *f, const nj_u2 *s, int len, int avail);

/* ------------------------------------------------------------------ */
/* Graphics state                                                      */
/* ------------------------------------------------------------------ */
typedef struct nj_gfx {
    nj_surface *surf;
    nj_u4 color;       /* current color, 0xAARRGGBB */
    int   translate_x;
    int   translate_y;
    int   clip_x, clip_y, clip_w, clip_h;  /* in surface space */
    int   stroke;
    int   blend;
    const nj_font *font;
    nj_u4 xor_color;   /* setXORMode target */
    int   use_xor;
} nj_gfx;

/* Initialize `g` for `surf`; clip becomes the whole surface, color black,
 * font = MEDIUM PLAIN. */
void nj_gfx_init(nj_gfx *g, nj_surface *surf);

/* Coordinate helpers: user -> device. */
int nj_gfx_dev_x(const nj_gfx *g, int x);
int nj_gfx_dev_y(const nj_gfx *g, int y);

/* ------------------------------------------------------------------ */
/* Clipping / transform                                                */
/* ------------------------------------------------------------------ */
void nj_gfx_set_clip(nj_gfx *g, int x, int y, int w, int h);
void nj_gfx_clip_rect(nj_gfx *g, int x, int y, int w, int h);
void nj_gfx_translate(nj_gfx *g, int dx, int dy);
void nj_gfx_get_translate(const nj_gfx *g, int *dx, int *dy);
int  nj_gfx_get_clip_x(const nj_gfx *g);
int  nj_gfx_get_clip_y(const nj_gfx *g);
int  nj_gfx_get_clip_w(const nj_gfx *g);
int  nj_gfx_get_clip_h(const nj_gfx *g);

/* ------------------------------------------------------------------ */
/* Color / style                                                       */
/* ------------------------------------------------------------------ */
void nj_gfx_set_color(nj_gfx *g, nj_u4 argb);
nj_u4 nj_gfx_get_color(const nj_gfx *g);
void nj_gfx_set_color_rgb(nj_gfx *g, int r, int g_, int b);
void nj_gfx_set_alpha(nj_gfx *g, int alpha0_255);
int  nj_gfx_get_alpha(const nj_gfx *g);
void nj_gfx_set_blend(nj_gfx *g, int mode);
void nj_gfx_set_stroke(nj_gfx *g, int stroke);
void nj_gfx_set_font(nj_gfx *g, const nj_font *f);
void nj_gfx_set_xor(nj_gfx *g, nj_u4 argb);
void nj_gfx_clear_xor(nj_gfx *g);

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */
void nj_gfx_clear(nj_gfx *g, nj_u4 argb);
void nj_gfx_pixel(nj_gfx *g, int x, int y);
void nj_gfx_line(nj_gfx *g, int x1, int y1, int x2, int y2);
void nj_gfx_rect(nj_gfx *g, int x, int y, int w, int h);           /* outline */
void nj_gfx_fill_rect(nj_gfx *g, int x, int y, int w, int h);
void nj_gfx_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw, int rh);
void nj_gfx_fill_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw, int rh);
void nj_gfx_arc(nj_gfx *g, int x, int y, int w, int h, int start, int extent);
void nj_gfx_fill_arc(nj_gfx *g, int x, int y, int w, int h, int start, int extent);
void nj_gfx_triangle(nj_gfx *g, int x1, int y1, int x2, int y2, int x3, int y3, int filled);

/* Text.  `s` is UTF-16. */
void nj_gfx_draw_text(nj_gfx *g, const nj_u2 *s, int len, int x, int y, int anchor);
void nj_gfx_draw_char(nj_gfx *g, nj_u2 ch, int x, int y);

/* ------------------------------------------------------------------ */
/* Images / blitting                                                   */
/* ------------------------------------------------------------------ */
/*
 * An image is a PACKED byte buffer with optional per-pixel alpha.
 *
 *   format      an NJ_PIXFMT_* value -- NOT an NJ_CODEC_CAN() capability
 *               bitmask.  RGB565 = 2 bytes/pixel, RGB888 = 3, ARGB8888 = 4;
 *               the row pitch is exactly width * bytes-per-pixel.
 *   pixels      the first pixel; the layout above is what
 *               nj_codec_image_new() allocates (w * h * bpp bytes), so a
 *               decoder may hand over a half-size RGB565 frame instead of
 *               expanding it to ARGB8888 first.
 *   has_alpha   1 when ARGB8888 alpha values in the data vary; RGB565 and
 *               RGB888 have no alpha channel and always blit as opaque.
 *
 * Transparent ARGB8888 pixels leave the destination untouched (ROP PAINT) and
 * partially transparent ones are composited; see nj_gfx_draw_image_rop().
 */
typedef struct nj_image {
    int   width;
    int   height;
    int   format;      /* NJ_PIXFMT_* : packed layout, see above */
    nj_u4 *pixels;     /* row-major, bpp bytes per pixel, stride == width */
    int   has_alpha;
} nj_image;

void nj_gfx_draw_image(nj_gfx *g, const nj_image *img,
                       int dx, int dy, int sx, int sy, int sw, int sh);
void nj_gfx_draw_image_rop(nj_gfx *g, const nj_image *img, int dx, int dy,
                           int sx, int sy, int sw, int sh, int rop,
                           int transform);
void nj_gfx_draw_rgb(nj_gfx *g, const nj_u1 *rgb, int scanlen, int alpha,
                     int dx, int dy, int sx, int sy, int sw, int sh,
                     int rop, int transform);
void nj_gfx_draw_region(nj_gfx *g, const nj_image *src,
                        int sx, int sy, int sw, int sh,
                        int transform, int is_xor, nj_u4 xor_argb,
                        const nj_image *clip_src,
                        int dx, int dy, int rop);

/* Alpha-blended fill of an arbitrary region with coverage 0..255. */
void nj_gfx_blend_pixel(nj_surface *s, int x, int y, nj_u4 argb, int alpha);

/*
 * Draw one pixel of the current colour at `coverage` (0..255), respecting the
 * clip.  Text drawn through a `coverage` font uses this; 255 goes down the
 * ordinary opaque path so a bitmap face is unaffected.
 */
void nj_gfx_blend_coverage(nj_gfx *g, int x, int y, int coverage);

/* ------------------------------------------------------------------ */
/* Surface helpers                                                     */
/* ------------------------------------------------------------------ */
void  nj_surface_fill(nj_surface *s, nj_u4 argb);
nj_u4 nj_surface_get(const nj_surface *s, int x, int y);
void  nj_surface_set(nj_surface *s, int x, int y, nj_u4 argb);
nj_u4 nj_surface_format_color(int format, nj_u4 argb);

/* ------------------------------------------------------------------ */
/* Anchor constants (mirror javax.microedition.lcdui.Graphics)         */
/* ------------------------------------------------------------------ */
/*
 * These are the MIDP 2.0 Graphics anchor values, and they are deliberately the
 * same numbers the Java constants carry: a MIDlet's anchor travels from
 * Graphics.LEFT through the native call straight into nj_gfx, so any other
 * numbering here would silently re-interpret it (LEFT meaning HCENTER, say)
 * rather than fail.  Keep them in step with the seeds in nj_lib_lcdui.c and
 * with tools/midp_stubs.py, which javac inlines into the MIDlet.
 *
 * HOW WE KNOW.  javac inlines `static final int` constants into the caller, so
 * every compiled MIDlet carries MIDP's real numbers.  Scanning 12476 classes
 * from the 395 titles in tests/games/ and collecting the constant that precedes
 * every drawString/drawImage/drawRegion call gives:
 *
 *     20  x11342   TOP|LEFT        the canonical text anchor
 *      0  x 8640   "identical to TOP|LEFT" per the javadoc
 *     17  x 4569   TOP|HCENTER
 *      3  x 2482   HCENTER|VCENTER (images)
 *     36  x 1506   BOTTOM|LEFT
 *     33  x 1361   BOTTOM|HCENTER
 *     40  x  859   BOTTOM|RIGHT   (soft-key labels)
 *     24  x  772   TOP|RIGHT
 *     68  x   19   BASELINE|LEFT
 *
 * Every one of those is a legal, sensible MIDP anchor under this numbering and
 * several are illegal under the one that reads LEFT as 1: 20 would then be
 * RIGHT|VCENTER, which MIDP forbids for text, yet it is the commonest anchor in
 * the whole corpus.  So HCENTER is 1, VCENTER is 2, LEFT is 4, TOP is 16.
 *
 * A commit once "corrected" these to LEFT 1 / HCENTER 2 / RIGHT 4 / TOP 8 and
 * added a translation at the MIDP boundary to compensate.  Both halves were
 * wrong and the translation could not save it: MIDlets inlined the real values
 * while the validator rejected them (anchor 3 became LEFT|HCENTER,
 * "contradictory"), so titles died with IllegalArgumentException instead of
 * drawing.  This numbering is the contract; do not "fix" it again without
 * re-running that scan.
 *
 * NOTE for callers: nj_gfx treats "no vertical bit set" as BASELINE, because a
 * text blitter needs some default.  MIDP instead defines a missing vertical
 * component as TOP, so the MIDP layer spells TOP out explicitly -- see
 * midp_anchor() in src/midp/nj_lib_lcdui.c.
 */
#define NJ_ANCHOR_HCENTER 1
#define NJ_ANCHOR_VCENTER 2
#define NJ_ANCHOR_LEFT    4
#define NJ_ANCHOR_RIGHT   8
#define NJ_ANCHOR_TOP     16
#define NJ_ANCHOR_BOTTOM  32

/* ROP constants, mirroring Graphics SET/PAINT. */
#define NJ_ROP_SET   0
#define NJ_ROP_PAINT 1

/* Sprite transforms, mirroring javax.microedition.lcdui.game.Sprite. */
#define NJ_TRANS_NONE 0
#define NJ_TRANS_ROT90 5
#define NJ_TRANS_ROT180 3
#define NJ_TRANS_ROT270 6
#define NJ_TRANS_MIRROR 2
#define NJ_TRANS_MIRROR_ROT90 7
#define NJ_TRANS_MIRROR_ROT180 1
#define NJ_TRANS_MIRROR_ROT270 4

#ifdef __cplusplus
}
#endif

#endif /* NJ_GFX_H */
