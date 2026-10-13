/*
 * nano_j2me - tests/test_detect.c
 *
 * When may a port believe the box it detected?
 *
 * The Android port crops the picture to the region a MIDlet paints ("fit the
 * picture" / 适配画面) and offers that region as the game's resolution.  Both
 * are wrong the moment the painted region is not the canvas: a title that
 * leaves its status bar alone, a menu that only touches a corner or a game
 * with a black background and a small HUD all produce a box that is real and
 * is not a screen.  nj_detect_snap() is the rule that keeps those out of the
 * player's settings, and this is the table of what it must decide.
 *
 * Usage: test_detect
 */
#include "nj2me/nj.h"

#include <stdio.h>

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

/* Detect a box and report what the rule made of it. */
static int snap(int bx, int by, int bw, int bh, int cw, int ch,
                int *x, int *y, int *w, int *h)
{
    *x = *y = *w = *h = -1;
    return nj_detect_snap(bx, by, bw, bh, cw, ch, x, y, w, h);
}

static void test_a_full_canvas_needs_no_snapping(void)
{
    int x, y, w, h;

    /* 240x320 game in a 240x320 canvas: the box IS the canvas. */
    CHECK(snap(0, 0, 240, 320, 240, 320, &x, &y, &w, &h) == 1,
          "a full canvas is published");
    CHECK(x == 0 && y == 0 && w == 240 && h == 320,
          "and it is the canvas itself");

    /* Nine tenths counts as the whole screen - that is the line the port
     * already uses to stop detecting. */
    CHECK(snap(0, 0, 240, 300, 240, 320, &x, &y, &w, &h) == 1,
          "nine tenths of the canvas is the canvas");
    CHECK(w == 240 && h == 320 && x == 0 && y == 0,
          "and is not snapped to a smaller screen");
}

static void test_the_status_bar_case(void)
{
    int x, y, w, h;

    /* A 240x320 title that never paints its last fifteen rows.  This is the
     * box that used to be adopted as a resolution. */
    CHECK(snap(0, 0, 240, 305, 240, 320, &x, &y, &w, &h) == 1,
          "240x305 is recognised as a 240x320 screen");
    CHECK(w == 240 && h == 320, "and the missing rows come back");
    CHECK(x == 0 && y == 0, "the box stays where it was painted");

    /* The same title painted two rows down: the screen keeps the offset. */
    CHECK(snap(0, 2, 240, 305, 240, 320, &x, &y, &w, &h) == 1,
          "an offset box is still recognised");
    CHECK(x == 0 && y == 0 && w == 240 && h == 320,
          "and is centred on what was painted");
}

static void test_a_smaller_screen_inside_a_big_canvas(void)
{
    int x, y, w, h;

    /* The classic case this feature exists for: a 128x128 title started on a
     * 240x320 canvas paints a 128x128 corner. */
    CHECK(snap(0, 0, 128, 128, 240, 320, &x, &y, &w, &h) == 1,
          "128x128 in a big canvas is a screen");
    CHECK(w == 128 && h == 128 && x == 0 && y == 0,
          "and the picture is cropped to it");

    /* Centred content: the crop follows the paint. */
    CHECK(snap(56, 96, 128, 128, 240, 320, &x, &y, &w, &h) == 1,
          "a centred 128x128 box is a screen");
    CHECK(x == 56 && y == 96 && w == 128 && h == 128,
          "and is not moved");

    /* A 176x208 title: the most common small screen in the corpus. */
    CHECK(snap(10, 20, 176, 208, 240, 320, &x, &y, &w, &h) == 1,
          "176x208 is a screen");
    CHECK(w == 176 && h == 208, "and its size is adopted");
    CHECK(x == 10 && y == 20, "with the detected offset kept");
}

static void test_a_box_that_is_not_a_screen_is_refused(void)
{
    int x, y, w, h;

    /* A menu that paints a corner. */
    CHECK(snap(0, 0, 40, 20, 240, 320, &x, &y, &w, &h) == 0,
          "a corner is not a screen");
    /* Something between two sizes, close to neither. */
    CHECK(snap(0, 0, 200, 100, 240, 320, &x, &y, &w, &h) == 0,
          "200x100 matches no screen");
    /* Twelve percent short of 240x320: refused, because a wrong crop is worse
     * than no crop - the player can still pick 铺满 or set the size by hand. */
    CHECK(snap(0, 0, 240, 280, 240, 320, &x, &y, &w, &h) == 0,
          "240x280 is too far from 240x320 to guess");
    /* Two pixels outside the tolerance of 176x208, then inside it.  This is
     * the same rule measured on a screen the 90%-of-canvas case cannot reach. */
    CHECK(snap(0, 0, 176, 191, 240, 320, &x, &y, &w, &h) == 0,
          "176x191 is refused");
    CHECK(snap(0, 0, 176, 193, 240, 320, &x, &y, &w, &h) == 1,
          "176x193 is inside the tolerance");
    CHECK(w == 176 && h == 208, "and it is 176x208");
    CHECK(snap(0, 0, 15, 15, 240, 320, &x, &y, &w, &h) == 0,
          "a box under sixteen pixels is not a screen");
}

static void test_clamping_and_nonsense(void)
{
    int x, y, w, h;

    /* Painted one pixel per side more than the canvas: the canvas wins. */
    CHECK(snap(-1, -1, 242, 322, 240, 320, &x, &y, &w, &h) == 1,
          "a box larger than the canvas is clamped");
    CHECK(x == 0 && y == 0 && w == 240 && h == 320,
          "and becomes the whole canvas");

    /* A landscape canvas keeps its own size. */
    CHECK(snap(0, 0, 320, 240, 320, 240, &x, &y, &w, &h) == 1,
          "320x240 in a 320x240 canvas");
    CHECK(w == 320 && h == 240, "is the canvas");

    /* A small screen inside a big landscape canvas. */
    CHECK(snap(20, 10, 176, 208, 352, 416, &x, &y, &w, &h) == 1,
          "176x208 in a 352x416 canvas is a screen");
    CHECK(x == 20 && y == 10 && w == 176 && h == 208,
          "kept where it was painted");

    /* A box that only fits the bottom-right corner: centring must clamp. */
    CHECK(snap(230, 310, 10, 10, 240, 320, &x, &y, &w, &h) == 0,
          "a ten pixel box is refused before any clamping");

    /* Degenerate arguments must not be obeyed. */
    CHECK(snap(0, 0, 128, 128, 0, 0, &x, &y, &w, &h) == 0,
          "an empty canvas is refused");
    CHECK(nj_detect_snap(0, 0, 128, 128, 240, 320, NULL, &y, &w, &h) == 0,
          "a NULL output is refused");
}

static void test_the_nearest_screen_wins(void)
{
    int x, y, w, h;

    /* 126x127 is nearer to 128x128 than to 130x130. */
    CHECK(snap(0, 0, 126, 127, 240, 320, &x, &y, &w, &h) == 1,
          "126x127 is a screen");
    CHECK(w == 128 && h == 128, "and it is 128x128");

    /* 131x129 is nearer to 130x130. */
    CHECK(snap(0, 0, 131, 129, 240, 320, &x, &y, &w, &h) == 1,
          "131x129 is a screen");
    CHECK(w == 130 && h == 130, "and it is 130x130");

    /* A slightly wide 176x208 box: the size is the screen, not the box. */
    CHECK(snap(0, 0, 180, 210, 240, 320, &x, &y, &w, &h) == 1,
          "a hand-drawn border does not change the screen");
    CHECK(w == 176 && h == 208, "176x208 is still what it is");

    /* 101x64 (a real Nokia screen) is exact. */
    CHECK(snap(0, 0, 101, 64, 240, 320, &x, &y, &w, &h) == 1,
          "101x64 is a screen");
    CHECK(w == 101 && h == 64, "and it is adopted as itself");
}

int main(void)
{
    printf("== nj_detect_snap: a detected box is only a canvas when it is one ==\n");
    test_a_full_canvas_needs_no_snapping();
    test_the_status_bar_case();
    test_a_smaller_screen_inside_a_big_canvas();
    test_a_box_that_is_not_a_screen_is_refused();
    test_clamping_and_nonsense();
    test_the_nearest_screen_wins();
    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
