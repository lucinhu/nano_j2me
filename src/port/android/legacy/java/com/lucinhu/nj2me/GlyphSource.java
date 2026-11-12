package com.lucinhu.nj2me;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import java.util.LinkedHashMap;
import java.util.Map;

/** Same JNI coverage contract as the modern frontend, without API 12 LruCache. */
final class GlyphSource {
    private static final int CAPACITY = 256;
    private static final LinkedHashMap<Long, byte[]> CACHE =
        new LinkedHashMap<Long, byte[]>(CAPACITY, 0.75f, true) {
            protected boolean removeEldestEntry(Map.Entry<Long, byte[]> entry) {
                return size() > CAPACITY;
            }
        };
    private static final Paint PAINT = new Paint();

    private GlyphSource() { }

    /** Returns {width, height, width*height alpha bytes}, or null on failure. */
    static synchronized byte[] render(int codePoint, int pixels, int bold, int ascent) {
        if (pixels <= 0 || pixels > 32 || codePoint < 0 || codePoint > 0x10ffff) { return null; }
        long bits = ((long)codePoint << 16) | ((long)pixels << 10)
                | ((bold != 0 ? 1L : 0L) << 9) | (ascent & 511);
        Long key = Long.valueOf(bits);
        byte[] hit = CACHE.get(key);
        if (hit != null) { return hit; }
        Bitmap bitmap = null;
        try {
            String text = new String(Character.toChars(codePoint));
            PAINT.setColor(Color.WHITE);
            PAINT.setAntiAlias(true);
            PAINT.setSubpixelText(false);
            PAINT.setTextSize(pixels);
            PAINT.setTypeface(bold != 0 ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            int width = Math.max(1, Math.min(16, (int)Math.ceil(PAINT.measureText(text))));
            bitmap = Bitmap.createBitmap(width, pixels, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            canvas.drawText(text, 0, ascent > 0 ? ascent : pixels * 0.75f, PAINT);
            int[] argb = new int[width * pixels];
            bitmap.getPixels(argb, 0, width, 0, 0, width, pixels);
            byte[] coverage = new byte[2 + argb.length];
            coverage[0] = (byte)width;
            coverage[1] = (byte)pixels;
            for (int i = 0; i < argb.length; i++) { coverage[i + 2] = (byte)(argb[i] >>> 24); }
            CACHE.put(key, coverage);
            return coverage;
        } catch (RuntimeException e) { return null; }
        catch (OutOfMemoryError e) { CACHE.clear(); return null; }
        finally { if (bitmap != null) { bitmap.recycle(); } }
    }
}
