package com.lucinhu.nj2me;

import java.util.Locale;

/**
 * The arithmetic behind the font settings, with no {@code android.*} in sight.
 *
 * <p>Everything here is a decision that can be checked off a device: how many
 * bytes a cached glyph costs, which files the platform will accept as a font,
 * what a picked file is copied to, and how a byte count is written down.  The
 * parts that need a {@link android.graphics.Typeface} live in
 * {@link GlyphSource}; the parts that need a phone live in the activities.
 *
 * <p>Kept separate for the same reason {@link PictureFit} is: the host test
 * harness runs pure Java, so the rules the player can break - a font file that
 * is not a font, a subset that reports numbers nobody can reproduce - are
 * tested without an emulator.
 */
final class FontKit {

    /**
     * Code points one run remembers for the subset.
     *
     * <p>A game that draws more than this is not a game whose vocabulary can be
     * trimmed meaningfully; the cap keeps the trace itself from being the
     * memory problem it exists to avoid.
     */
    static final int MAX_USED = 8192;

    private FontKit() {
    }

    /** Bytes one cached glyph costs: two header bytes and one per pixel. */
    static int glyphBytes(int w, int h) {
        return (w > 0 && h > 0) ? 2 + w * h : 0;
    }

    /** True when the name looks like a font the platform can load. */
    static boolean looksLikeFont(String name) {
        String n = name == null ? "" : name.toLowerCase(Locale.US);
        return n.endsWith(".ttf") || n.endsWith(".otf") || n.endsWith(".ttc");
    }

    /**
     * The name a picked font is copied to.
     *
     * <p>Deliberately not the player's file name: the copy lives in the app's
     * own directory and is opened by a path this code builds, so the name has
     * to be one this code would write.  The extension is kept, because the
     * platform's font loader looks at it.
     */
    static String storeName(String displayName) {
        String n = displayName == null ? "" : displayName.toLowerCase(Locale.US);
        int dot = n.lastIndexOf('.');
        String ext = (dot >= 0 && looksLikeFont(n.substring(dot)))
                ? n.substring(dot) : ".ttf";
        return "custom" + ext;
    }

    /**
     * A byte count a player can read: bytes, then KB, then MB.
     *
     * <p>One decimal for KB and MB, because the difference the subset switch
     * makes is often a fraction of a kilobyte and "0 KB" would look like it did
     * nothing.
     */
    static String size(long bytes) {
        if (bytes < 1024) {
            return bytes + " B";
        }
        if (bytes < 1024L * 1024L) {
            return String.format(Locale.US, "%.1f KB", bytes / 1024.0);
        }
        return String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    /**
     * The key a glyph is cached under.
     *
     * <p>It carries the style and the anti-aliasing switch as well as the
     * character and the size: a bold glyph and a plain one are different ink,
     * and so are a smoothed one and a hard-edged one.  (The runtime's own
     * native cache keys on character and cell only; that cache is refilled from
     * this one, so the two must be invalidated together - which is why the
     * settings rows clear both.)
     */
    static int cacheKey(int cp, int px, boolean bold, boolean antiAlias) {
        return (cp << 8) | ((px & 0x3f) << 2) | (bold ? 2 : 0) | (antiAlias ? 1 : 0);
    }

    /** The code point a {@link #cacheKey} was built from. */
    static int cacheCodePoint(int key) {
        return key >>> 8;
    }
}
