package com.lucinhu.nj2me;

/**
 * Pins {@link PictureFit} to the geometry the port was measured to produce.
 *
 * <p>The interesting cases are the two windows this app was actually tested on
 * (1080x1920 and 320x640) with a 240x320 MIDlet: the port painted a 960x1280
 * picture with sixty pixels of pan either way on the first, and a 240x320 one
 * with forty and a hundred and sixty on the second.  If the editor's preview
 * drifts from those numbers, the player is placing keys against a picture that
 * is not there.
 */
public final class PictureFitTest {

    public static void main(String[] args) {
        Check.section("integer fit on the two test windows");
        PictureFit a = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_INT,
                1f, 0f, 0f, 0, 0);
        Check.eq("1080x1920 width", a.w, 960f);
        Check.eq("1080x1920 height", a.h, 1280f);
        Check.eq("1080x1920 x", a.x, 60f);
        Check.eq("1080x1920 y", a.y, 320f);
        Check.eq("1080x1920 maxpan x", PictureFit.maxPan(1080, a.w), 60f);
        Check.eq("1080x1920 maxpan y", PictureFit.maxPan(1920, a.h), 320f);

        PictureFit b = PictureFit.of(320, 640, 240, 320, PictureFit.MODE_INT,
                1f, 0f, 0f, 0, 0);
        Check.eq("320x640 width", b.w, 240f);
        Check.eq("320x640 height", b.h, 320f);
        Check.eq("320x640 x", b.x, 40f);
        Check.eq("320x640 y", b.y, 160f);
        Check.eq("320x640 maxpan x", PictureFit.maxPan(320, b.w), 40f);
        Check.eq("320x640 maxpan y", PictureFit.maxPan(640, b.h), 160f);

        Check.section("a source bigger than the window is shown at 1:1, not squashed");
        PictureFit tiny = PictureFit.of(100, 100, 240, 320, PictureFit.MODE_INT,
                1f, 0f, 0f, 0, 0);
        Check.eq("fit floor is one", tiny.w, 240f);

        Check.section("fill takes the fractional scale that still fits");
        PictureFit f = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_FILL,
                1f, 0f, 0f, 0, 0);
        /* 1080/240 = 4.5 beats 1920/320 = 6, so the width is the one that ends
         * flush and the bars move to the top and bottom - the same choice the
         * port makes, because cropping the game to reach the edges is worse */
        Check.eq("fill uses the width ratio", f.w, 1080f);
        Check.eq("fill height follows", f.h, 1440f);
        Check.eq("fill is not an integer multiple", f.w / 240f, 4.5f);
        Check.isTrue("fill is bigger than the integer fit",
                f.w > PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_INT,
                        1f, 0f, 0f, 0, 0).w);

        Check.section("zoom multiplies whichever fit was chosen");
        PictureFit z = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_INT,
                0.5f, 0f, 0f, 0, 0);
        Check.eq("half of four", z.w, 480f);
        Check.eq("half of four, height", z.h, 640f);
        /* the port ignores a zoom at or below 0.05: it is a rounding of 1.0 */
        PictureFit zz = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_INT,
                0.01f, 0f, 0f, 0, 0);
        Check.eq("a nonsense zoom is unity", zz.w, 960f);

        Check.section("pan moves the picture and is clamped to the slack");
        PictureFit p = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_INT,
                1f, 400f, -400f, 0, 0);
        Check.eq("pan is applied", p.x, 460f);
        Check.eq("pan clamps to sixty",
                PictureFit.clampPan(400f, PictureFit.maxPan(1080, p.w)), 60f);
        Check.eq("pan clamps both ways",
                PictureFit.clampPan(-400f, PictureFit.maxPan(1080, p.w)), -60f);
        Check.eq("a smaller pan passes through", PictureFit.clampPan(20f, 60f), 20f);
        /* an overflowing picture is pulled back by its own overflow instead */
        Check.eq("overflowing slack", PictureFit.maxPan(1080, 1920f), 420f);

        Check.section("auto paints only a measured box that is worth cropping to");
        /* 240x280 is 87% of the canvas, under the port's nine-tenths rule */
        PictureFit det = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_AUTO,
                1f, 0f, 0f, 240, 280);
        Check.eq("auto source width", det.srcW, 240);
        Check.eq("auto source height", det.srcH, 280);
        Check.eq("auto keeps the whole-number fit", det.w, 960f);
        Check.eq("auto height follows the box", det.h, 1120f);
        Check.isTrue("auto centres the box", det.y > 320f);
        /* the port ignores a box that is (nearly) the whole canvas: 240x305 is
         * 94% of it, and cropping fifteen rows off a 320-line game helps nobody */
        PictureFit full = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_AUTO,
                1f, 0f, 0f, 240, 305);
        Check.eq("a full-canvas detection is ignored", full.srcW, 240);
        Check.eq("and so is its height", full.srcH, 320);
        PictureFit small = PictureFit.of(1080, 1920, 240, 320, PictureFit.MODE_AUTO,
                1f, 0f, 0f, 8, 8);
        Check.eq("a junk detection is ignored", small.srcW, 240);

        Check.done();
    }
}
