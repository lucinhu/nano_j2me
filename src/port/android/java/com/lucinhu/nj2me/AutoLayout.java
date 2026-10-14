package com.lucinhu.nj2me;

import java.util.List;

/**
 * Where the picture goes the first time a game runs, so the pad is not drawn
 * on top of it.
 *
 * <p>Out of the box the port centres the canvas and the player drops a keypad
 * over it.  A 240x320 game on a 1080x1920 window is scaled 4x to 960x1280 and
 * the d-pad sits on the bottom-left corner of that picture: the two things a
 * game needs to show - the hero and the score - are exactly where the thumb
 * is.  This class looks at the keys that are installed, finds the largest
 * rectangle of the window no keycap covers, fits the biggest whole-number
 * scale of the canvas inside it and asks for the pan that centres the picture
 * there.  The player keeps every override: it is applied only while a game has
 * no saved picture setting of its own.
 *
 * <p>The union of the keys is not the answer: a pad that hugs the corners has
 * a union spanning the whole window, while the usable band is the hole in the
 * middle.  So the band is found by splitting the window at every key edge and
 * taking the tallest free gap in each strip.
 *
 * <p>Pure Java on purpose: no Android type is named here, so the class runs
 * under the host JVM in the off-device test harness.  The arithmetic it feeds
 * is {@link PictureFit}'s, which is in turn the arithmetic
 * {@code nj_port_android.c}'s presenter does every frame.
 */
final class AutoLayout {

    /** What the game should tell the port: the same four numbers as nativeSetView. */
    static final class Fit {
        /** {@link PictureFit#MODE_INT} or {@link PictureFit#MODE_CUSTOM}. */
        final int mode;
        final float zoom;
        final float panX;
        final float panY;
        /** The whole-number scale it came out at, for the log and the tests. */
        final int scale;

        Fit(int mode, float zoom, float panX, float panY, int scale) {
            this.mode = mode;
            this.zoom = zoom;
            this.panX = panX;
            this.panY = panY;
            this.scale = scale;
        }
    }

    /** A rectangle of the window, in pixels. */
    private static final class Band {
        float x, y, w, h;
    }

    private AutoLayout() {
    }

    /**
     * The first-run fit, or null when the window or the canvas makes no sense.
     *
     * @param winW      window width in pixels
     * @param winH      window height in pixels
     * @param screenW   the MIDlet's canvas width
     * @param screenH   the MIDlet's canvas height
     * @param safeL     left edge of the inset-safe area, as a fraction
     * @param safeT     top edge of the inset-safe area, as a fraction
     * @param safeR     right edge of the inset-safe area, as a fraction
     * @param safeB     bottom edge of the inset-safe area, as a fraction
     * @param keys      the installed keypad, in normalized window coordinates
     */
    static Fit fit(int winW, int winH, int screenW, int screenH,
                   float safeL, float safeT, float safeR, float safeB,
                   List<PadConfig.Key> keys) {
        if (winW <= 0 || winH <= 0 || screenW <= 0 || screenH <= 0) {
            return null;
        }
        Band band = freeBand(winW, winH, screenW, screenH, safeL, safeT, safeR, safeB, keys);
        /*
         * A whole-number scale only: a fractional one turns every sprite edge
         * into a blur, and the picture is the one thing on screen that must
         * stay crisp.
         */
        int scale = (int) Math.min(band.w / screenW, band.h / screenH);
        if (scale < 1) {
            /*
             * A pad can cover everything - 全按键 on a small screen does.  The
             * picture still has to be somewhere, so it gets one pixel per
             * pixel, centred in the band it was given; the keys win that
             * argument, and the player can move the picture or the pad.
             */
            scale = 1;
        }
        int base = Math.max(1, Math.min(winW / screenW, winH / screenH));
        float zoom = (float) scale / (float) base;
        int mode = (scale == base) ? PictureFit.MODE_INT : PictureFit.MODE_CUSTOM;

        float dw = screenW * (float) scale;
        float dh = screenH * (float) scale;
        /*
         * Centred in the free band: the port draws the picture at
         * (window - drawn)/2 + pan, so the pan is how far the band's centre
         * sits from the window's.  Then clamped the way the port clamps it,
         * which only bites for a band that hugs an edge - and there it is what
         * stops the picture hanging off the screen.
         */
        float panX = band.x + band.w / 2f - winW / 2f;
        float panY = band.y + band.h / 2f - winH / 2f;
        panX = PictureFit.clampPan(panX, PictureFit.maxPan(winW, dw));
        panY = PictureFit.clampPan(panY, PictureFit.maxPan(winH, dh));
        return new Fit(mode, zoom, panX, panY, scale);
    }

    /**
     * The largest rectangle of the safe area that no keycap covers.
     *
     * <p>Every key's left and right edge is a boundary the free space can
     * change at, so the window is cut into strips there; inside one strip the
     * picture of the occupied rows does not change.  The keys are clamped into
     * the safe area first, exactly as {@code PadConfig.rectOf} draws them, so
     * a key the notch pushes inwards blocks where it really is.
     */
    private static Band freeBand(int winW, int winH, int screenW, int screenH,
                                 float safeL, float safeT,
                                 float safeR, float safeB, List<PadConfig.Key> keys) {
        float l = clamp01(safeL) * winW;
        float r = clamp01(safeR) * winW;
        float t = clamp01(safeT) * winH;
        float b = clamp01(safeB) * winH;
        if (r - l < 1f) {
            l = 0f;
            r = winW;
        }
        if (b - t < 1f) {
            t = 0f;
            b = winH;
        }

        Band full = new Band();
        full.x = l;
        full.y = t;
        full.w = r - l;
        full.h = b - t;

        int n = (keys == null) ? 0 : keys.size();
        if (n == 0) {
            return full;
        }
        float[] kx0 = new float[n];
        float[] kx1 = new float[n];
        float[] ky0 = new float[n];
        float[] ky1 = new float[n];
        int m = 0;
        float[] xs = new float[2 * n + 2];
        xs[m++] = l;
        xs[m++] = r;
        for (int i = 0; i < n; i++) {
            PadConfig.Key k = keys.get(i);
            float x0 = k.x * winW;
            float y0 = k.y * winH;
            float kw = k.w * winW;
            float kh = k.h * winH;
            /* Mirrors PadConfig.fit(): a key that does not fit is pinned to
             * the near edge rather than pushed off the far one. */
            x0 = pin(x0, kw, l, r);
            y0 = pin(y0, kh, t, b);
            kx0[i] = x0;
            kx1[i] = x0 + kw;
            ky0[i] = y0;
            ky1[i] = y0 + kh;
            xs[m++] = clamp(x0, l, r);
            xs[m++] = clamp(x0 + kw, l, r);
        }
        sort(xs, m);

        /*
         * The whole safe area is the fallback, not the starting point: with a
         * keypad anywhere on the screen the window as a whole is not free, and
         * measuring it as a candidate would win every comparison a narrower
         * band can win.
         */
        Band best = null;
        for (int i = 0; i < m; i++) {
            for (int j = i + 1; j < m; j++) {
                float x0 = xs[i];
                float x1 = xs[j];
                if (x1 - x0 < 1f) {
                    continue;
                }
                Band cand = new Band();
                cand.x = x0;
                cand.w = x1 - x0;
                tallestGap(x0, x1, t, b, kx0, kx1, ky0, ky1, cand);
                if (best == null || better(cand, best, screenW, screenH)) {
                    best = cand;
                }
            }
        }
        return best == null ? full : best;
    }

    /** True when {@code cand} fits a bigger whole-number scale than {@code best}. */
    private static boolean better(Band cand, Band best, int screenW, int screenH) {
        int a = (int) Math.min(cand.w / screenW, cand.h / screenH);
        int z = (int) Math.min(best.w / screenW, best.h / screenH);
        if (a != z) {
            return a > z;
        }
        return cand.w * cand.h > best.w * best.h;
    }

    /**
     * The tallest stretch of {@code [t, b]} that no key overlapping the strip
     * {@code [x0, x1]} covers.  Writes it into {@code out}.
     */
    private static void tallestGap(float x0, float x1, float t, float b,
                                   float[] kx0, float[] kx1, float[] ky0, float[] ky1,
                                   Band out) {
        int n = kx0.length;
        float[] from = new float[n];
        float[] to = new float[n];
        int c = 0;
        for (int i = 0; i < n; i++) {
            /* Touching is not overlapping: a strip that starts exactly at a
             * key's edge keeps the whole height of that key's row. */
            if (kx0[i] < x1 && kx1[i] > x0 && ky1[i] > t && ky0[i] < b) {
                from[c] = ky0[i] < t ? t : ky0[i];
                to[c] = ky1[i] > b ? b : ky1[i];
                c++;
            }
        }
        for (int i = 1; i < c; i++) {
            float f = from[i];
            float e = to[i];
            int j = i - 1;
            while (j >= 0 && from[j] > f) {
                from[j + 1] = from[j];
                to[j + 1] = to[j];
                j--;
            }
            from[j + 1] = f;
            to[j + 1] = e;
        }
        float y = t;
        float bestY = t;
        float bestH = 0f;
        for (int i = 0; i < c; i++) {
            if (from[i] > y && from[i] - y > bestH) {
                bestH = from[i] - y;
                bestY = y;
            }
            if (to[i] > y) {
                y = to[i];
            }
        }
        if (b - y > bestH) {
            bestH = b - y;
            bestY = y;
        }
        out.y = bestY;
        out.h = bestH;
    }

    /** Pads a key's edge into the safe area the way the editor's clamp does. */
    private static float pin(float v, float size, float lo, float hi) {
        if (size >= hi - lo) {
            return lo;
        }
        if (v < lo) {
            return lo;
        }
        return v + size > hi ? hi - size : v;
    }

    private static float clamp(float v, float lo, float hi) {
        if (v < lo) {
            return lo;
        }
        return v > hi ? hi : v;
    }

    private static float clamp01(float v) {
        if (v < 0f) {
            return 0f;
        }
        return v > 1f ? 1f : v;
    }

    private static void sort(float[] a, int n) {
        for (int i = 1; i < n; i++) {
            float v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = v;
        }
    }
}
