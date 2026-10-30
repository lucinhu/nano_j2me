package com.lucinhu.nj2me;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * The display settings that belong to the player and not to a game.
 *
 * <p>Three things a player sets once and expects everywhere: whether text is
 * smoothed, whether the picture is smoothed, and which font the MIDlet's own
 * text is drawn with.  They live in one preferences file rather than in a
 * {@link PadConfig} or in a {@link JarStore.Entry}, because they are not about
 * one game - a font the player installed is the font they want in every game.
 *
 * <p>Kept as plain getters and setters over {@link SharedPreferences} so the
 * two activities that show them (the library's menu and the game panel) cannot
 * disagree about a key name.
 */
final class Settings {

    private static final String PREFS = "display";

    private static final String FONT_AA = "fontAntiAlias";
    private static final String PICTURE_AA = "pictureAntiAlias";
    private static final String FONT_FILE = "fontFile";
    private static final String FRAME_CAP = "frameCap";

    private Settings() {
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /**
     * Font anti-aliasing, on by default.
     *
     * <p>On is what the port has always done - the platform rasteriser was
     * asked for coverage, and coverage is what makes a 12 px Chinese stroke
     * readable.  The switch exists for players who want the hard pixel look of
     * the handsets this runtime copies.
     */
    static boolean fontAntiAlias(Context ctx) {
        return prefs(ctx).getBoolean(FONT_AA, true);
    }

    static void setFontAntiAlias(Context ctx, boolean on) {
        prefs(ctx).edit().putBoolean(FONT_AA, on).apply();
    }

    /**
     * Picture anti-aliasing, off by default.
     *
     * <p>Off is nearest neighbour: the logical screen scaled up in solid
     * blocks, which is what every port in this tree has always drawn and what a
     * J2ME game's own pixel art assumes.  On mixes neighbours at a fractional
     * scale, which is what 铺满 and a custom zoom ask for.
     */
    static boolean pictureAntiAlias(Context ctx) {
        return prefs(ctx).getBoolean(PICTURE_AA, false);
    }

    static void setPictureAntiAlias(Context ctx, boolean on) {
        prefs(ctx).edit().putBoolean(PICTURE_AA, on).apply();
    }

    /**
     * Frames per second the port aims for, or -1 for "the port's own default".
     *
     * -1 is the status quo: the native port caps a phone at 30, and a title
     * that wants 60 on a phone that can draw it is the case this setting
     * exists for.  Applies to the next game - the port is told at launch and
     * again whenever the game panel changes it.
     */
    static int frameCap(Context ctx) {
        return prefs(ctx).getInt(FRAME_CAP, -1);
    }

    static void setFrameCap(Context ctx, int cap) {
        prefs(ctx).edit().putInt(FRAME_CAP, cap).apply();
    }

    /** The cap as a row title reads it. */
    static String frameCapText(int cap) {
        if (cap < 0) {
            return "30 帧（默认）";
        }
        return cap == 0 ? "不限制" : cap + " 帧";
    }

    /** The next value in the cycle: default -> 60 -> uncapped -> default. */
    static int nextFrameCap(int cap) {
        if (cap < 0) {
            return 60;
        }
        return cap == 0 ? -1 : 0;
    }

    /**
     * The absolute path of the font file the player picked, or null for the
     * phone's own UI face.
     */
    static String fontFile(Context ctx) {
        return prefs(ctx).getString(FONT_FILE, null);
    }

    static void setFontFile(Context ctx, String path) {
        SharedPreferences.Editor ed = prefs(ctx).edit();
        if (path == null) {
            ed.remove(FONT_FILE);
        } else {
            ed.putString(FONT_FILE, path);
        }
        ed.apply();
    }
}
