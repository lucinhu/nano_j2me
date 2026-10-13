/*
 * nano_j2me - nj_detect.c
 *
 * Snapping the region a MIDlet paints to a screen a MIDlet would ask for.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * The Android port learns a game's real canvas by accumulating the pixels that
 * change between frames (android_detect in src/port/android/nj_port_android.c)
 * and offering that box as the picture to show ("fit the picture" / 适配画面).
 * The union is honest, but it is not always a canvas:
 *
 *   - a title that leaves the bottom status bar alone gives a box that is a
 *     few rows short of the screen it was written for;
 *   - a menu that only ever paints a corner gives a box that is a corner;
 *   - a game with a black background and a small HUD gives a small box even
 *     though it means to use the whole screen.
 *
 * Cropping the picture to such a box zooms the game into a strip of itself,
 * and "use this as the resolution" would write a size like 240x305 into the
 * game's settings - a canvas no MIDlet is laid out for.
 *
 * So a detected box is only published when it IS a screen size somebody makes:
 * the nearest entry in a table of feature-phone canvases, within a sixteenth
 * in each dimension, centred on what was detected.  A box that matches nothing
 * is refused, and the caller keeps watching instead of cropping.
 *
 * The table is deliberately small - real MIDP screens, portrait and landscape
 * - because a bigger table is a bigger chance of snapping a corner to a screen
 * it is not.  The caller can always set the canvas by hand.
 */

#include "nj_internal.h"

typedef struct {
    nj_u2 w;
    nj_u2 h;
} nj_detect_size;

/* Screens the corpus is laid out for, plus the landscape halves of the common
 * ones: a box is compared against these and nothing else. */
static const nj_detect_size NJ_DETECT_SIZES[] = {
    {  96,  65 },   /* the smallest MIDP screen in the corpus */
    {  96,  96 },
    { 101,  64 },
    { 128,  96 },
    { 128, 128 },
    { 128, 160 },
    { 130, 130 },
    { 132, 176 },
    { 160, 128 },
    { 176, 208 },
    { 176, 220 },
    { 208, 208 },
    { 240, 260 },
    { 240, 320 },
    { 320, 240 },
    { 352, 416 },
    { 360, 640 },
    { 480, 800 },
    { 640, 480 },
};

#define NJ_DETECT_COUNT \
    ((int)(sizeof(NJ_DETECT_SIZES) / sizeof(NJ_DETECT_SIZES[0])))

/* A box smaller than this is a corner, not a screen. */
#define NJ_DETECT_MIN 16

/* Relative distance allowed to the nearest screen: 1/16 in each dimension,
 * and never less than four pixels, so a small screen is not held to a
 * precision its own drawing does not have. */
static int nj_detect_tol(int size)
{
    int t = size / 16 + 3;

    return (t < 4) ? 4 : t;
}

int nj_detect_snap(int bx, int by, int bw, int bh, int cw, int ch,
                   int *ox, int *oy, int *ow, int *oh)
{
    int i;
    int best = -1;
    int best_score = 0;
    int w;
    int h;

    if (!ox || !oy || !ow || !oh || cw <= 0 || ch <= 0) {
        return 0;
    }
    if (bw < NJ_DETECT_MIN || bh < NJ_DETECT_MIN) {
        return 0;
    }
    if (bw > cw) {
        bw = cw;
    }
    if (bh > ch) {
        bh = ch;
    }
    /*
     * A box that is the canvas (or as good as) needs no snapping and no
     * cropping: nine tenths is the line the caller already uses to decide that
     * a title paints its whole screen.
     */
    if ((long)bw * (long)bh >= (long)cw * (long)ch / 10L * 9L) {
        w = cw;
        h = ch;
    } else {
        for (i = 0; i < NJ_DETECT_COUNT; i++) {
            int sw = (int)NJ_DETECT_SIZES[i].w;
            int sh = (int)NJ_DETECT_SIZES[i].h;
            int dw;
            int dh;
            int score;

            /* A screen bigger than the canvas is not this game's screen. */
            if (sw > cw || sh > ch) {
                continue;
            }
            dw = (sw > bw) ? sw - bw : bw - sw;
            dh = (sh > bh) ? sh - bh : bh - sh;
            if (dw > nj_detect_tol(sw) || dh > nj_detect_tol(sh)) {
                continue;
            }
            /* Prefer the closest match, measured in percent of the screen so
             * a 96x96 entry is not beaten by a 480x800 one that is further
             * away in pixels but closer in proportion. */
            score = dw * 1000 / sw + dh * 1000 / sh;
            if (best < 0 || score < best_score) {
                best = i;
                best_score = score;
            }
        }
        if (best < 0) {
            /*
             * Nothing in the table is close.  Refusing is the point: the caller
             * keeps its raw union and keeps watching, and the player keeps a
             * picture that fills the screen instead of a crop of a corner.
             */
            return 0;
        }
        w = (int)NJ_DETECT_SIZES[best].w;
        h = (int)NJ_DETECT_SIZES[best].h;
    }

    *ow = w;
    *oh = h;
    *ox = bx + bw / 2 - w / 2;
    *oy = by + bh / 2 - h / 2;
    if (*ox > cw - w) {
        *ox = cw - w;
    }
    if (*oy > ch - h) {
        *oy = ch - h;
    }
    if (*ox < 0) {
        *ox = 0;
    }
    if (*oy < 0) {
        *oy = 0;
    }
    return 1;
}
