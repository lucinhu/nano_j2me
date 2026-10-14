package com.lucinhu.nj2me;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.util.LruCache;

import java.util.Map;

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
 * columns at partial intensity, and cutting it at 50% deletes the stroke.  The
 * player can turn that smoothing off (item 3 of the font round): the switch is
 * part of the cache key, so a hard-edged glyph and a smoothed one are two
 * different entries rather than a repaint of each other.
 *
 * <p>English is measured here too.  Drawing ascii already came from the
 * platform (the rasteriser asks the provider before the bundled table), but
 * <em>measuring</em> did not: the runtime charges every character inside a
 * face's fixed cell that cell's width before it asks anyone
 * ({@code char_width_depth} in {@code nj_font.c}), so English ink was the
 * phone's while English layout was a bitmap table's.  The port empties the
 * copied cell ({@code android_glyph_init} in {@code nj_port_android.c}), which
 * makes the platform's own advance the answer to both questions.
 *
 * <p>Every code point drawn is also remembered, so a game's own vocabulary can
 * be measured and the rest of the cache dropped ({@link #trimToUsed()}): that
 * is what "只保留游戏用到的字" means for a face that lives in RAM.
 */
final class GlyphSource {

    private GlyphSource() {
    }

    /** Glyphs kept: a game's vocabulary is small, and each entry is tiny. */
    private static final int CACHE_GLYPHS = 1024;

    /*
     * One lock for the cache, the run trace and the faces.
     *
     * render() runs on the VM thread, while the settings rows and the subset
     * row run on the UI thread - and both of them change what a cache entry
     * means.  The lock is only taken on a miss (an LruCache hit is a plain
     * get), so a screen of cached text never contends for it.
     */
    private static final Object LOCK = new Object();

    private static final LruCache<Integer, byte[]> CACHE =
            new LruCache<Integer, byte[]>(CACHE_GLYPHS) {
                @Override
                protected int sizeOf(Integer key, byte[] value) {
                    return 1;
                }
            };

    /** Code points the MIDlet drew since the last {@link #beginRun()}. */
    private static final java.util.HashSet<Integer> USED = new java.util.HashSet<Integer>();

    /** One Paint for the run: creating one per glyph is a per-frame allocation. */
    private static Paint paint;

    /** The player's font file, or null for the phone's own UI face. */
    private static String fontPath;
    private static Typeface plainFace;
    private static Typeface boldFace;

    /** Font anti-aliasing: the UI thread writes it, the VM thread reads it. */
    private static volatile boolean antiAlias = true;

    /**
     * Whether glyphs are smoothed.
     *
     * <p>The runtime's own bundled faces are 1-bit bitmaps; this switch is
     * about the platform rasteriser that draws everything now.  Off gives the
     * hard pixel edges some players want at 12 px, on gives the strokes back.
     */
    static void setAntiAlias(boolean on) {
        synchronized (LOCK) {
            if (antiAlias == on) {
                return;
            }
            antiAlias = on;
            CACHE.evictAll();
        }
    }

    static boolean antiAlias() {
        return antiAlias;
    }

    /**
     * Draw MIDlet text with the player's font file, or the system face for
     * null.
     *
     * <p>The file is copied into the app's own directory by the settings row
     * (a content:// the picker returns would not survive a restart), and a file
     * the platform refuses is not fatal: the system face stays.
     */
    static void setFontFile(String path) {
        synchronized (LOCK) {
            if (path == null ? fontPath == null : path.equals(fontPath)) {
                return;
            }
            fontPath = path;
            plainFace = null;
            boldFace = null;
            CACHE.evictAll();
        }
    }

    static String fontFile() {
        return fontPath;
    }

    private static Typeface face(boolean bold) {
        synchronized (LOCK) {
            if (plainFace == null) {
                Typeface base = null;
                if (fontPath != null) {
                    try {
                        base = Typeface.createFromFile(fontPath);
                    } catch (RuntimeException e) {
                        base = null;    /* not a font this platform can read */
                    }
                }
                if (base == null) {
                    /*
                     * "sans-serif" rather than a named family: the platform
                     * resolves it to the device's own UI font, which is the
                     * whole point - a Xiaomi phone draws MiSans, a Pixel draws
                     * Roboto, and a phone with a Chinese locale draws the
                     * Chinese face without this code knowing any of their
                     * names.
                     */
                    base = Typeface.create("sans-serif", Typeface.NORMAL);
                }
                plainFace = base;
                boldFace = Typeface.create(base, Typeface.BOLD);
            }
            return bold ? boldFace : plainFace;
        }
    }

    private static Paint paintFor(int px, boolean bold) {
        Paint p = paint;
        if (p == null) {
            p = new Paint();
            p.setColor(Color.WHITE);
            p.setSubpixelText(false);
            paint = p;
        }
        if (p.getTextSize() != (float) px) {
            p.setTextSize((float) px);
        }
        if (p.isAntiAlias() != antiAlias) {
            p.setAntiAlias(antiAlias);
        }
        Typeface want = face(bold);
        if (p.getTypeface() != want) {
            p.setTypeface(want);
        }
        return p;
    }

    /**
     * Coverage rows for one character: {@code {w, h, w*h bytes}} with one byte
     * per pixel, 0 = empty and 255 = solid.
     *
     * <p>Returns null when the character cannot be drawn, which the runtime
     * reads as "this face has no glyph" and answers with a blank cell.
     *
     * @param bold non-zero when the style is bold; the runtime's own faces
     *             answer bold from a second table, so the platform has to be
     *             told rather than left to fake it
     */
    static byte[] render(int cp, int px, int bold) {
        if (px <= 0 || px > 32) {
            return null;
        }
        boolean b = bold != 0;
        boolean aa = antiAlias;
        Integer key = Integer.valueOf(FontKit.cacheKey(cp, px, b, aa));
        synchronized (LOCK) {
            byte[] hit = CACHE.get(key);
            if (hit != null) {
                return hit;
            }
            byte[] made = rasterise(cp, px, b, aa);
            if (made != null) {
                CACHE.put(key, made);
                if (USED.size() < FontKit.MAX_USED) {
                    USED.add(Integer.valueOf(cp));
                }
            }
            return made;
        }
    }

    private static byte[] rasterise(int cp, int px, boolean bold, boolean aa) {
        if (!Character.isValidCodePoint(cp)) {
            return null;
        }
        String s = new String(Character.toChars(cp));
        Paint p = paintFor(px, bold);
        if (p.isAntiAlias() != aa) {
            p.setAntiAlias(aa);
        }

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

        byte[] out = new byte[FontKit.glyphBytes(w, h)];
        out[0] = (byte) w;
        out[1] = (byte) h;
        for (int i = 0; i < argb.length; i++) {
            out[2 + i] = (byte) ((argb[i] >>> 24) & 0xff);
        }
        return out;
    }

    /** Forget the previous game's vocabulary: called when a MIDlet starts. */
    static void beginRun() {
        synchronized (LOCK) {
            USED.clear();
        }
    }

    /** {@code {entries, bytes}} of the glyph cache, for the settings row. */
    static int[] stats() {
        synchronized (LOCK) {
            Map<Integer, byte[]> snap = CACHE.snapshot();
            int bytes = 0;
            for (byte[] v : snap.values()) {
                bytes += v.length;
            }
            return new int[] { snap.size(), bytes };
        }
    }

    /**
     * Drop every cached glyph this run did not draw, and report what that did.
     *
     * <p>{@code {removed, entriesBefore, entriesAfter, bytesBefore,
     * bytesAfter}} - the numbers the row shows, so a player can see that the
     * subset happened instead of trusting it.
     *
     * <p>The native face has a second, smaller cache of its own (96 slots in
     * {@code nj_port_android.c}); it holds the last characters drawn, so it
     * refills from here and needs no separate trim.
     */
    static int[] trimToUsed() {
        synchronized (LOCK) {
            Map<Integer, byte[]> snap = CACHE.snapshot();
            int before = snap.size();
            int bytesBefore = 0;
            int bytesAfter = 0;
            int removed = 0;
            for (Map.Entry<Integer, byte[]> e : snap.entrySet()) {
                byte[] v = e.getValue();
                bytesBefore += v.length;
                if (USED.contains(Integer.valueOf(FontKit.cacheCodePoint(e.getKey().intValue())))) {
                    bytesAfter += v.length;
                    continue;
                }
                CACHE.remove(e.getKey());
                removed++;
            }
            USED.clear();
            return new int[] { removed, before, before - removed, bytesBefore, bytesAfter };
        }
    }
}
