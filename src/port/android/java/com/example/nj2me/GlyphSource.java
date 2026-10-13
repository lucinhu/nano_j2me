package com.example.nj2me;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.util.LruCache;

/**
 * The platform's glyphs, handed to the runtime one character at a time.
 *
 * <p>The runtime's own wide-glyph table is a bitmap face compiled into the
 * library - 7445 GB2312 characters at two sizes, about 480 KB per ABI. Android
 * already has those glyphs in a font the runtime cannot read (the system CJK
 * faces are CFF2/OTTO, and the TrueType path in the runtime reads glyf
 * outlines), so the port rasterises them here instead and the build ships no
 * glyph table at all.
 *
 * <p>Called from the VM thread through JNI, once per character per size: the
 * cache is what keeps a screen of Chinese text from crossing the JNI boundary
 * every frame. It is also why nothing here may allocate per call - the bitmap
 * is created once per character and the coverage bytes are what gets kept.
 *
 * <p>Coverage, not a thresholded bitmap: at 12 px a CJK stroke is often two
 * columns at partial intensity, and cutting it at 50% deletes the stroke.
 */
final class GlyphSource {

    private GlyphSource() {
    }

    /** Glyphs kept: a game's vocabulary is small, and each entry is tiny. */
    private static final int CACHE_GLYPHS = 1024;

    private static final LruCache<Integer, byte[]> CACHE =
            new LruCache<Integer, byte[]>(CACHE_GLYPHS) {
                @Override
                protected int sizeOf(Integer key, byte[] value) {
                    return 1;
                }
            };

    /** One Paint for the run: creating one per glyph is a per-frame allocation. */
    private static Paint paint;

    private static Paint paintFor(int px) {
        Paint p = paint;
        if (p == null) {
            p = new Paint(Paint.ANTI_ALIAS_FLAG);
            /*
             * "sans-serif" rather than a named family: the platform resolves it
             * to the device's own UI font, which is the whole point - a Xiaomi
             * phone draws MiSans, a Pixel draws Roboto, and a phone with a
             * Chinese locale draws the Chinese face without this code knowing
             * any of their names.
             */
            p.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            p.setColor(Color.WHITE);
            p.setSubpixelText(false);
            paint = p;
        }
        if (p.getTextSize() != (float) px) {
            p.setTextSize((float) px);
        }
        return p;
    }

    /**
     * Coverage rows for one character: {@code {w, h, w*h bytes}} with one byte
     * per pixel, 0 = empty and 255 = solid.
     *
     * <p>Returns null when the character cannot be drawn, which the runtime
     * reads as "this face has no glyph" and answers with a blank cell.
     */
    static byte[] render(int cp, int px) {
        if (px <= 0 || px > 32) {
            return null;
        }
        Integer key = Integer.valueOf((cp << 8) | px);
        byte[] hit = CACHE.get(key);
        if (hit != null) {
            return hit;
        }
        byte[] made = rasterise(cp, px);
        if (made != null) {
            CACHE.put(key, made);
        }
        return made;
    }

    private static byte[] rasterise(int cp, int px) {
        if (!Character.isValidCodePoint(cp)) {
            return null;
        }
        String s = new String(Character.toChars(cp));
        Paint p = paintFor(px);

        int w = (int) Math.ceil(p.measureText(s));
        if (w <= 0) {
            w = px;
        }
        if (w > 16) {
            w = 16;
        }
        int h = px;

        /*
         * Centre the ink in the cell vertically: the runtime draws a glyph so
         * that its cell sits on the text baseline, and a face whose ink hangs
         * off the bottom of its cell would look like a broken font.
         */
        Rect bounds = new Rect();
        p.getTextBounds(s, 0, s.length(), bounds);
        float baseline = (h - bounds.height()) / 2.0f - bounds.top;

        Bitmap bm = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bm);
        c.drawText(s, 0f, baseline, p);

        int[] argb = new int[w * h];
        bm.getPixels(argb, 0, w, 0, 0, w, h);
        bm.recycle();

        byte[] out = new byte[2 + w * h];
        out[0] = (byte) w;
        out[1] = (byte) h;
        for (int i = 0; i < argb.length; i++) {
            out[2 + i] = (byte) ((argb[i] >>> 24) & 0xff);
        }
        return out;
    }
}
