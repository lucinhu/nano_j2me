/*
 * tests/tmp_font.c - *** THROWAWAY STAND-IN, NOT PART OF THE RUNTIME ***
 *
 * src/gfx/nj_font.c (and its glyph data) is owned by another agent.  This file
 * provides the metrics and the registry from include/nj2me/nj_gfx.h so that
 * tests/test_lcdui.c can verify the LCDUI layer's font plumbing end to end.
 *
 * Glyphs are synthesized on demand into a scratch buffer that belongs to the
 * fontset (never to a C global), so this file also respects the project's
 * "no mutable static state" rule.
 */
#include "nj2me/nj_gfx.h"

#include <string.h>

#define TMPF_GLYPH_MAX  16

/* Deterministic, purely functional advance metric. */
static int tmpf_advance(const nj_font *f, nj_u2 ch)
{
    int base = f->glyph_w;

    if (!f->proportional) {
        return base;
    }
    if (ch == ' ') {
        return base / 2 + 1;
    }
    if (ch == 'i' || ch == 'l' || ch == 'I' || ch == '!' || ch == '.' ||
        ch == ',' || ch == '\'' || ch == ':' || ch == ';') {
        return base - 3;
    }
    if (ch == 'm' || ch == 'M' || ch == 'W' || ch == 'w' || ch == '@') {
        return base + 4;
    }
    return base;
}

int nj_font_char_width(const nj_font *f, nj_u2 ch)
{
    if (!f) {
        return 0;
    }
    return tmpf_advance(f, ch);
}

int nj_font_string_width(const nj_font *f, const nj_u2 *s, int len)
{
    int i, w = 0;
    if (!f || !s) {
        return 0;
    }
    if (len < 0) {
        len = 0;
        while (s[len] != 0u) { len++; }
    }
    for (i = 0; i < len; i++) {
        w += tmpf_advance(f, s[i]);
    }
    return w;
}

int nj_font_chars_that_fit(const nj_font *f, const nj_u2 *s, int len, int avail)
{
    int i, w = 0;
    if (!f || !s) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        int a = tmpf_advance(f, s[i]);
        if (w + a > avail) {
            break;
        }
        w += a;
    }
    return i;
}

/*
 * Synthesize a blocky glyph.  It is not a real typeface, but the ink pattern
 * depends on the character code so rendered text is visibly non-uniform and a
 * regression that stops drawing glyphs at all is caught.
 *
 * (This is a *test* stub: unlike the runtime it is allowed a file-scope
 * scratch buffer because it is deleted as soon as src/gfx/nj_font.c lands.)
 */
typedef struct tmpf_scratch {
    nj_u1 rows[8];
} tmpf_scratch;

static tmpf_scratch tmpf_scratch_buf;

static const nj_u1 *tmpf_glyph(void *ud, nj_u2 ch, int *w, int *h)
{
    tmpf_scratch *sc = (tmpf_scratch *)ud;
    int x, y;

    if (!sc) {
        sc = &tmpf_scratch_buf;
    }
    *w = 8;
    *h = 8;
    for (y = 0; y < 8; y++) {
        nj_u1 byte = 0u;
        for (x = 0; x < 8; x++) {
            int on;
            if (y == 0 || y == 7) {
                on = ((ch & 1u) != 0u);
            } else {
                on = (((((int)ch * 7) + x * 3 + y * 5) & 3) == 0);
            }
            if (on) {
                byte |= (nj_u1)(0x80u >> x);
            }
        }
        sc->rows[y] = byte;
    }
    return sc->rows;
}

static int tmpf_glyph_advance(void *ud, nj_u2 ch)
{
    (void)ud;
    (void)ch;
    return 0;
}

/* A single immutable table per slot; the fontset copies it. */
static const nj_font tmpf_table[NJ_FONTSET_SLOTS] = {
    { "small-plain",  10, 7, 3, 0, NJ_FONT_STYLE_PLAIN,      NJ_FONT_SIZE_SMALL,  0, 32u, 126u, 5, 8, NULL, NULL, NULL, NULL, NULL },
    { "small-bold",   10, 7, 3, 0, NJ_FONT_STYLE_BOLD,       NJ_FONT_SIZE_SMALL,  0, 32u, 126u, 5, 8, NULL, NULL, NULL, NULL, NULL },
    { "medium-pro",   11, 8, 3, 0, NJ_FONT_STYLE_PLAIN,      NJ_FONT_SIZE_MEDIUM, 1, 32u, 126u, 6, 8, NULL, NULL, NULL, NULL, NULL },
    { "medium-bold",  11, 8, 3, 0, NJ_FONT_STYLE_BOLD,       NJ_FONT_SIZE_MEDIUM, 1, 32u, 126u, 6, 8, NULL, NULL, NULL, NULL, NULL },
    { "large-plain",  16, 12, 4, 0, NJ_FONT_STYLE_PLAIN,     NJ_FONT_SIZE_LARGE,  0, 32u, 126u, 8, 12, NULL, NULL, NULL, NULL, NULL },
    { "large-bold",   16, 12, 4, 0, NJ_FONT_STYLE_BOLD,      NJ_FONT_SIZE_LARGE,  0, 32u, 126u, 8, 12, NULL, NULL, NULL, NULL, NULL }
};

const nj_font *nj_font_builtin(int size, int style)
{
    int idx = 0;

    switch (size) {
    case NJ_FONT_SIZE_SMALL: idx = 0; break;
    case NJ_FONT_SIZE_LARGE: idx = 4; break;
    default:                 idx = 2; break;
    }
    if (style & NJ_FONT_STYLE_BOLD) {
        idx += 1;
    }
    if (idx < 0 || idx >= NJ_FONTSET_SLOTS) {
        idx = 2;
    }
    return &tmpf_table[idx];
}

void nj_fontset_init(nj_fontset *fs)
{
    int i;
    if (!fs) {
        return;
    }
    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        const nj_font *b = nj_font_builtin(
            (i < 2) ? NJ_FONT_SIZE_SMALL
                    : ((i < 4) ? NJ_FONT_SIZE_MEDIUM : NJ_FONT_SIZE_LARGE),
            (i & 1) ? NJ_FONT_STYLE_BOLD : NJ_FONT_STYLE_PLAIN);
        fs->fonts[i] = *b;
        fs->fonts[i].glyph_lookup = tmpf_glyph;
        fs->fonts[i].glyph_advance = tmpf_glyph_advance;
        fs->fonts[i].glyph_ud = &tmpf_scratch_buf;
    }
    fs->initialized = 1;
    fs->count = NJ_FONTSET_SLOTS;
}

void nj_fontset_set_fallback(nj_fontset *fs, const nj_font *fb)
{
    int i;
    if (!fs) {
        return;
    }
    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        fs->fonts[i].fallback = fb;
    }
}

const nj_font *nj_fontset_get(nj_fontset *fs, int size, int style)
{
    unsigned idx;

    if (!fs) {
        return NULL;
    }
    if (!fs->initialized) {
        nj_fontset_init(fs);
    }
    idx = 2166136261u;
    idx ^= (unsigned)size;
    idx *= 16777619u;
    idx ^= (unsigned)style;
    idx *= 16777619u;
    idx ^= idx >> 13;
    return &fs->fonts[idx % (unsigned)NJ_FONTSET_SLOTS];
}
