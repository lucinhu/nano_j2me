/*
 * nano_j2me - nj_ttf.h
 *
 * A TrueType (glyf) rasteriser that fits in a caller-supplied block of memory.
 *
 * This is the whole public surface.  The face is opaque, it never allocates,
 * it holds no static state, and it borrows the font bytes rather than copying
 * them, so a caller that wants to drop the face just forgets the block (and
 * keeps the file bytes alive for as long as the nj_font it produced lives).
 *
 * Typical use:
 *
 *     nj_size  bytes = nj_ttf_sizeof(128);
 *     void    *mem   = alloc(ud, bytes);
 *     nj_ttf  *face  = nj_ttf_open(mem, bytes, file, file_len, 16);
 *     nj_font  font;
 *
 *     if (face != NULL && nj_ttf_font(face, &font, NJ_FONT_STYLE_PLAIN,
 *                                     NJ_FONT_SIZE_MEDIUM)) {
 *         nj_gfx_set_font(&g, &font);
 *     }
 *
 * Rasterised rows are 8bpp coverage (nj_font.coverage = 1), one byte per
 * pixel, `w` bytes per row, `h` rows, top row first, the baseline `ascent`
 * pixels below the top of row 0.  The returned pointer stays valid until the
 * next call on the same face, which is exactly as long as nj_text.c uses it.
 *
 * Missing and empty glyphs, in one place:
 *
 *   - a character with no cmap entry, and a character whose cmap entry points
 *     at glyph 0, both return NULL from glyph_lookup.  The caller's fallback
 *     chain then runs (and glyph_advance still reports the right width), which
 *     is friendlier than painting a box - and this rasteriser never invents a
 *     box of its own;
 *   - a glyph with no contours (space and friends) is a real glyph: it returns
 *     w = its advance, h = 1, and one blank row, so the caller can tell
 *     "whitespace" from "missing".
 *
 * Metrics come from head.unitsPerEm, hhea and hmtx, with ascent raised to
 * head.yMax when the tallest ink would otherwise be clipped.  See nj_ttf.c for
 * what else is and is not implemented.
 */
#ifndef NJ_TTF_H
#define NJ_TTF_H

#include "nj2me/nj_gfx.h"
#include "nj2me/nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nj_ttf nj_ttf;

/*
 * Bytes a face needs, including a `cache_glyphs`-entry rasterisation cache.
 *
 * The cache is a bump arena: when it is full it is dropped and refilled, so
 * `cache_glyphs` sizes the arena rather than limiting any one glyph.  A larger
 * number is pure speed.  0 is treated as 1.
 */
nj_size nj_ttf_sizeof(int cache_glyphs);

/*
 * Parse `data` (borrowed - the caller keeps it alive as long as the face) into
 * `mem` (mem_bytes >= nj_ttf_sizeof(...)).  Returns NULL on any malformed
 * font, and never reads outside data[0..len).  `px_size` is the em size in
 * pixels; the face renders at that size for its whole life.
 *
 * `mem` may be uninitialised and may be reused: every byte of face state,
 * cache included, is written here.
 */
nj_ttf *nj_ttf_open(void *mem, nj_size mem_bytes,
                    const nj_u1 *data, nj_u4 len, int px_size);

/*
 * Configure `out` so it renders through `t`.  Returns 0 on failure (NULL
 * arguments), 1 on success.
 *
 * `style` is NJ_FONT_STYLE_*; BOLD and ITALIC are synthesised by the
 * rasteriser (a one-pixel horizontal smear and a 12-degree shear), not by
 * substituting a different face.  `size` is the caller's NJ_FONT_SIZE_*
 * constant and is only recorded, not interpreted.
 *
 * Changing the style between calls drops the glyph cache, so the same face
 * can back a plain and a bold nj_font - at the cost of re-rasterising on
 * every switch.  Build one face per style if that matters.
 *
 * On success `out` gets: glyph_lookup, glyph_advance, glyph_ud = t,
 * coverage = 1, proportional = 1, name (from the font's name table, or
 * "TrueType"), size (echoed), style, and
 *     ascent  = ceil(max(hhea.ascender, head.yMax) * px_size / unitsPerEm)
 *     descent = ceil(-hhea.descender * px_size / unitsPerEm)
 *     height  = ascent + descent + (OS/2 line gap, 0 without OS/2)
 * The metrics belong to the em size the face was opened with; `size` here does
 * not rescale them.
 */
int nj_ttf_font(nj_ttf *t, nj_font *out, int style, int size);

#ifdef __cplusplus
}
#endif

#endif /* NJ_TTF_H */
