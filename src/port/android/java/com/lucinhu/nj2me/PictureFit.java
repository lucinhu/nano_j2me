package com.lucinhu.nj2me;

/**
 * Where the game picture lands in a window.
 *
 * <p>This is the arithmetic the port does every frame in
 * {@code nj_port_android.c}'s presenter, repeated here so the editor can draw
 * the picture the way the game will paint it - and so a player can put a key
 * next to the picture instead of on top of it.  The two must agree; the unit
 * test next to this file pins the numbers the port was measured to produce on
 * a 1080x1920 window (4x, 960x1280, sixty pixels of pan either way).
 *
 * <p>Pure Java on purpose: no Android type is named here, so the class runs
 * under the host JVM in the off-device test harness.
 */
final class PictureFit {

    /*
     * How the picture is fitted to the window.  The constants live here rather
     * than in GameActivity so that this file stays free of Android types and
     * can be exercised by the off-device test harness; GameActivity's VIEW_*
     * aliases point at them, so there is still one set of numbers.
     */
    /** Largest whole-number scale that fits: sharpest, but leaves bars. */
    static final int MODE_INT = 0;
    /**
     * Scaled fractionally to the largest size that still fits whole: the bars
     * shrink but never vanish, because a 240x320 canvas and a 1080x1920 window
     * do not share an aspect ratio and cropping is not on offer.
     */
    static final int MODE_FILL = 1;
    /** Integer fit plus the player's own zoom and offset. */
    static final int MODE_CUSTOM = 2;
    /** Only the box the port found the MIDlet painting in. */
    static final int MODE_AUTO = 3;

    /** The picture is drawn at :srcW x :srcH scaled to :w x :h at (:x, :y). */
    final int srcW;
    final int srcH;
    final float x;
    final float y;
    final float w;
    final float h;

    private PictureFit(int srcW, int srcH, float x, float y, float w, float h) {
        this.srcW = srcW;
        this.srcH = srcH;
        this.x = x;
        this.y = y;
        this.w = w;
        this.h = h;
    }

    /**
     * @param mode one of the MODE_* constants above (GameActivity's VIEW_*)
     * @param detW detected canvas width, or 0 when nothing has been measured
     */
    static PictureFit of(int winW, int winH, int screenW, int screenH, int mode,
                         float zoom, float panX, float panY, int detW, int detH) {
        int sw = screenW > 0 ? screenW : 1;
        int sh = screenH > 0 ? screenH : 1;
        /* "适配画面" paints only the box the port found the MIDlet drawing in,
         * which is what a game that leaves a band of dead pixels wants; with
         * nothing measured it is the whole canvas.  The editor has no box
         * position to work with (only its size), so it previews it centred. */
        if (mode == MODE_AUTO && detW >= 16 && detH >= 16
                && (long) detW * detH < (long) sw * sh * 9 / 10) {
            sw = detW;
            sh = detH;
        }
        float scale;
        if (mode == MODE_FILL) {
            scale = Math.min(winW / (float) sw, winH / (float) sh);
        } else {
            int fit = (int) Math.min(winW / (float) sw, winH / (float) sh);
            scale = fit < 1 ? 1f : fit;
        }
        if (zoom > 0.05f) {
            scale *= zoom;
        }
        float dw = sw * scale;
        float dh = sh * scale;
        return new PictureFit(sw, sh,
                (winW - dw) / 2f + panX, (winH - dh) / 2f + panY, dw, dh);
    }

    /**
     * How far the picture may be pushed before an edge would leave the window.
     *
     * <p>A picture that fits has {@code (window - drawn) / 2} of slack on each
     * side, one that overflows has {@code (drawn - window) / 2}: the same
     * number either way, measured from the centre.  This is the formula the
     * port publishes through {@code nativeViewLimits}.
     */
    static float maxPan(int winLen, float drawnLen) {
        return (winLen > drawnLen ? winLen - drawnLen : drawnLen - winLen) / 2f;
    }

    static float clampPan(float pan, float max) {
        if (pan > max) {
            return max;
        }
        return pan < -max ? -max : pan;
    }
}
