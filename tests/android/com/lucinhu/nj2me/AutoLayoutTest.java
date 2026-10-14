package com.lucinhu.nj2me;

/**
 * The first-run picture fit, off the device.
 *
 * <p>The question a player asks is "is the hero behind my thumb", so every
 * check asks it the same way: take the numbers {@link AutoLayout} would hand to
 * {@code nativeSetView}, run them through the arithmetic the presenter in
 * {@code nj_port_android.c} does every frame, and look at where the picture
 * lands.  It must not sit under a single keycap, and it must not be smaller
 * than the band allows.
 */
public final class AutoLayoutTest {

    /** Portrait and landscape phones, a small handset, a tall modern screen. */
    private static final int[][] WINDOWS = {
        {1080, 1920}, {1920, 1080}, {720, 1280}, {1440, 3200},
    };

    private static final int[] PRESETS = {
        PadConfig.PRESET_DPAD,
        PadConfig.PRESET_CLASSIC,
        PadConfig.PRESET_PHONE,
        PadConfig.PRESET_ARCADE,
        PadConfig.PRESET_ONEHAND,
        PadConfig.PRESET_LANDSCAPE,
        PadConfig.PRESET_FULL,
    };

    private AutoLayoutTest() {
    }

    public static void main(String[] args) {
        PadConfig classic = PadConfig.byId(PadConfig.PRESET_CLASSIC, 1080, 1920);
        AutoLayout.Fit f = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f,
                classic.keys);
        Check.section("the 经典手机 pad gets a whole-number picture above it");
        Check.isTrue("a fit comes out", f != null);
        Check.eq("three times, not the four the window would take", f.scale, 3);
        Check.eq("asked for as a zoom on the four-times fit",
                Math.round(f.zoom * 100f), 75);
        Check.eq("in custom mode, which is the one that multiplies",
                f.mode, PictureFit.MODE_CUSTOM);
        Check.isTrue("and nothing on the pad covers the picture",
                clear(f, 1080, 1920, 240, 320, classic, 0f, 0f, 1f, 1f));

        Check.section("so does the pad a player meets first");
        PadConfig dpad = PadConfig.byId(PadConfig.PRESET_DPAD, 1080, 1920);
        f = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f, dpad.keys);
        Check.isTrue("a fit comes out", f != null);
        Check.isTrue("the picture is clear of every cap",
                clear(f, 1080, 1920, 240, 320, dpad, 0f, 0f, 1f, 1f));
        Check.isTrue("and it is not shrunk to nothing", f.scale >= 3);

        Check.section("every preset on every window is answered");
        for (int p = 0; p < PRESETS.length; p++) {
            for (int w = 0; w < WINDOWS.length; w++) {
                int winW = WINDOWS[w][0];
                int winH = WINDOWS[w][1];
                PadConfig cfg = PadConfig.byId(PRESETS[p], winW, winH);
                AutoLayout.Fit fit = AutoLayout.fit(winW, winH, 240, 320,
                        0f, 0f, 1f, 1f, cfg.keys);
                String name = PadConfig.presetName(PRESETS[p]) + " on "
                        + winW + "x" + winH;
                Check.isTrue(name + ": a fit comes out", fit != null);
                if (fit == null) {
                    continue;
                }
                Check.isTrue(name + ": at least one pixel per pixel", fit.scale >= 1);
                Check.isTrue(name + ": the scale is whole",
                        Math.abs(fit.zoom * base(winW, winH, 240, 320) - fit.scale) < 0.02f);
                /*
                 * 全按键 is the one layout that covers the window on purpose:
                 * there the keys win and the picture is pinned at 1:1 in
                 * whatever hole is left.  Every other preset has to leave the
                 * picture alone, which is the whole point of the fit.
                 */
                if (PRESETS[p] != PadConfig.PRESET_FULL) {
                    Check.isTrue(name + ": no cap covers the picture",
                            clear(fit, winW, winH, 240, 320, cfg, 0f, 0f, 1f, 1f));
                }
            }
        }

        Check.section("an empty pad just fills the window");
        PadConfig empty = PadConfig.blank();
        f = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f, empty.keys);
        Check.eq("the biggest whole-number fit", f.scale, 4);
        Check.eq("which is what the port does anyway", f.mode, PictureFit.MODE_INT);
        Check.eq("no zoom", Math.round(f.zoom * 100f), 100);
        Check.eq("and no pan", Math.round(f.panX) + "," + Math.round(f.panY), "0,0");

        Check.section("the notch squeezes the picture, not the pad");
        PadConfig notched = PadConfig.byId(PadConfig.PRESET_CLASSIC, 1080, 1920);
        notched.setSafeArea(0f, 0f, 1f, 0.92f);
        f = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 0.92f, notched.keys);
        Check.isTrue("a fit comes out", f != null);
        Check.isTrue("and it stays above the unsafe strip",
                clear(f, 1080, 1920, 240, 320, notched, 0f, 0f, 1f, 0.92f));
        Check.isTrue("the picture ends before the inset does",
                drawn(f, 1080, 1920, 240, 320)[1]
                        + drawn(f, 1080, 1920, 240, 320)[3] <= 1920 * 0.92f + 0.5f);

        Check.section("a window with no room still answers");
        PadConfig jammed = PadConfig.byId(PadConfig.PRESET_FULL, 320, 480);
        f = AutoLayout.fit(320, 480, 240, 320, 0f, 0f, 1f, 1f, jammed.keys);
        Check.isTrue("a fit comes out", f != null);
        Check.eq("the picture keeps one pixel per pixel", f.scale, 1);
        Check.isTrue("and it is inside the window",
                drawn(f, 320, 480, 240, 320)[0] >= -0.5f
                        && drawn(f, 320, 480, 240, 320)[0]
                                + drawn(f, 320, 480, 240, 320)[2] <= 320.5f);

        Check.section("nonsense in, nothing out");
        Check.eq("no window, no fit",
                AutoLayout.fit(0, 0, 240, 320, 0f, 0f, 1f, 1f, classic.keys), null);
        Check.eq("no canvas, no fit",
                AutoLayout.fit(1080, 1920, 0, 0, 0f, 0f, 1f, 1f, classic.keys), null);
        Check.eq("and no keys is not an error",
                AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f, null) != null,
                true);

        Check.section("the same window gives the same answer twice");
        AutoLayout.Fit a = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f,
                classic.keys);
        AutoLayout.Fit b = AutoLayout.fit(1080, 1920, 240, 320, 0f, 0f, 1f, 1f,
                classic.keys);
        Check.eq("same mode", a.mode, b.mode);
        Check.eq("same zoom", Float.toString(a.zoom), Float.toString(b.zoom));
        Check.eq("same pan", a.panX + "," + a.panY, b.panX + "," + b.panY);

        Check.done();
    }

    /**
     * The picture the port would draw, in pixels: {@code mode}, {@code zoom},
     * {@code panX}, {@code panY} through the presenter's arithmetic.
     *
     * <p>Written out rather than shared with the port because the port is C and
     * the point of the test is to pin the contract between them; if the two
     * ever drift, this is the line that says so.
     */
    private static float[] drawn(AutoLayout.Fit f, int winW, int winH, int sw, int sh) {
        int fit = base(winW, winH, sw, sh);
        float scale = (f.mode == PictureFit.MODE_CUSTOM) ? fit * f.zoom : fit;
        float dw = sw * scale;
        float dh = sh * scale;
        return new float[] {(winW - dw) / 2f + f.panX, (winH - dh) / 2f + f.panY, dw, dh};
    }

    /** The port's integer fit: whole-number, at least 1. */
    private static int base(int winW, int winH, int sw, int sh) {
        return Math.max(1, Math.min(winW / sw, winH / sh));
    }

    /** True when no keycap of {@code cfg} overlaps the picture it would draw. */
    private static boolean clear(AutoLayout.Fit f, int winW, int winH, int sw, int sh,
                                 PadConfig cfg, float safeL, float safeT,
                                 float safeR, float safeB) {
        float[] pic = drawn(f, winW, winH, sw, sh);
        PadConfig.Box box = new PadConfig.Box();
        for (int i = 0; i < cfg.keys.size(); i++) {
            cfg.rectOf(cfg.keys.get(i), box);
            float kx = box.x * winW;
            float ky = box.y * winH;
            float kw = box.w * winW;
            float kh = box.h * winH;
            if (pic[0] < kx + kw && pic[0] + pic[2] > kx
                    && pic[1] < ky + kh && pic[1] + pic[3] > ky) {
                return false;
            }
        }
        return true;
    }
}
