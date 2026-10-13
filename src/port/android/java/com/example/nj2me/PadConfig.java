package com.example.nj2me;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

/**
 * The on-screen keypad layout: which keys, where, how big.
 *
 * <p>Positions are fractions of the view (0..1), not pixels and not dp.  A phone
 * in one hand and a tablet on a stand have nothing in common in pixels, but
 * "bottom-left corner" means the same thing on both, and that is what a player
 * is actually placing.  The cost is that a layout tuned on a tall phone looks
 * slightly different on a short one, which is exactly what the editor is for.
 */
final class PadConfig {

    /** One keycap.  x/y is the top-left corner, w/h the size, all 0..1. */
    static final class Key {
        int code;
        float x, y, w, h;

        Key(int code, float x, float y, float w, float h) {
            this.code = code;
            this.x = x;
            this.y = y;
            this.w = w;
            this.h = h;
        }

        Key copy() {
            return new Key(code, x, y, w, h);
        }

        boolean contains(float nx, float ny) {
            return nx >= x && nx <= x + w && ny >= y && ny <= y + h;
        }
    }

    static final int PRESET_DPAD = 0;
    static final int PRESET_NUMERIC = 1;
    static final int PRESET_CUSTOM = 2;
    static final int PRESET_FULL = 3;

    final List<Key> keys = new ArrayList<Key>();
    float opacity = 0.30f;      /* how solid an idle keycap is */
    boolean grid = true;        /* guide lines in the editor */
    boolean snap = true;        /* round dragged keys to the grid */
    boolean haptics = true;     /* short buzz on a key press */

    /*
     * Which built-in layout this came from, for the editor's label.  It is a
     * record of the last preset the player applied, nothing more: as soon as a
     * key is dragged it is CUSTOM, and claiming otherwise would be a lie the
     * player could see.
     */
    int preset = PRESET_DPAD;

    static final float GRID_STEP = 0.01f;

    private PadConfig() {
    }

    /**
     * The layout a game gets before anyone has an opinion.  Everything sits in
     * the bars above and below a 240x320 screen, because that is where there is
     * nothing to cover: a MIDlet's own game area is the last place a control
     * should be drawn.
     */
    static PadConfig defaults() {
        PadConfig p = new PadConfig();
        p.keys.add(new Key(19, 0.100f, 0.795f, 0.140f, 0.060f));   /* 上 */
        p.keys.add(new Key(21, 0.020f, 0.860f, 0.140f, 0.060f));   /* 左 */
        p.keys.add(new Key(22, 0.180f, 0.860f, 0.140f, 0.060f));   /* 右 */
        p.keys.add(new Key(20, 0.100f, 0.925f, 0.140f, 0.060f));   /* 下 */
        p.keys.add(new Key(23, 0.700f, 0.860f, 0.260f, 0.115f));   /* 确定 */
        /* The soft keys clear the top edge by more than they look like they
         * need to: the game's control bar lives there until it times out, and a
         * keycap underneath a button is a keycap the player cannot press. */
        p.keys.add(new Key(82, 0.020f, 0.080f, 0.200f, 0.055f));   /* 左软键 */
        p.keys.add(new Key(4,  0.780f, 0.080f, 0.200f, 0.055f));   /* 右软键 */
        return p;
    }

    /**
     * The other layout a phone game can want: twelve number keys.
     *
     * <p>Plenty of titles - puzzle games, dial pads, anything ported from a
     * feature phone - use the digit keys and nothing else, and a D-pad plus a
     * fire button gives them nothing to press.  Three columns and four rows in
     * the bottom-right corner, laid out in the order the keypad of a phone has
     * them, because that is the shape a hand already knows.
     *
     * <p>The fractions are wider than they are tall on purpose: the view is
     * taller than it is wide, so equal fractions would be tall thin caps.
     */
    static PadConfig numeric() {
        PadConfig p = new PadConfig();
        p.preset = PRESET_NUMERIC;
        int[] codes = {
            8, 9, 10,          /* 1 2 3 */
            11, 12, 13,        /* 4 5 6 */
            14, 15, 16,        /* 7 8 9 */
            17, 7, 18          /* * 0 # */
        };
        float x0 = 0.63f, y0 = 0.640f;
        float w = 0.110f, h = 0.055f, gapx = 0.010f, gapy = 0.010f;
        for (int i = 0; i < codes.length; i++) {
            int col = i % 3;
            int row = i / 3;
            p.keys.add(new Key(codes[i],
                               x0 + col * (w + gapx),
                               y0 + row * (h + gapy),
                               w, h));
        }
        return p;
    }

    /** No keys at all, for a layout built by hand. */
    static PadConfig blank() {
        PadConfig p = new PadConfig();
        p.preset = PRESET_CUSTOM;
        return p;
    }

    /**
     * Every key the port can deliver, on one screen.
     *
     * <p>For the titles that use the whole handset: a D-pad in one corner, the
     * twelve digits in the other, the soft keys and Clear along the top, and
     * A/B/C/D down the side. Nothing is left to guess about which key a game
     * wants, and the player can drag away the ones it does not use - which is
     * why this is a preset rather than a mode.
     *
     * <p>It covers the bottom of the game area by design: a full keypad and an
     * unobstructed screen cannot both be had, and a game that needs every key
     * needs them more than it needs the last two rows of pixels.
     */
    static PadConfig full() {
        PadConfig p = new PadConfig();
        p.preset = PRESET_FULL;

        /* soft keys and Clear along the top, clear of the control bar */
        p.keys.add(new Key(82, 0.020f, 0.080f, 0.200f, 0.050f));   /* 左软键 */
        p.keys.add(new Key(67, 0.400f, 0.080f, 0.200f, 0.050f));   /* 清除 */
        p.keys.add(new Key(4,  0.780f, 0.080f, 0.200f, 0.050f));   /* 右软键 */

        /* A/B/C/D */
        float aw = 0.088f;
        int[] game = { 29, 30, 31, 32 };
        for (int i = 0; i < game.length; i++) {
            p.keys.add(new Key(game[i], 0.020f + i * (aw + 0.005f), 0.700f,
                               aw, 0.045f));
        }

        /* D-pad and fire */
        p.keys.add(new Key(21, 0.020f, 0.752f, 0.120f, 0.048f));   /* 左 */
        p.keys.add(new Key(19, 0.145f, 0.752f, 0.120f, 0.048f));   /* 上 */
        p.keys.add(new Key(22, 0.270f, 0.752f, 0.120f, 0.048f));   /* 右 */
        p.keys.add(new Key(20, 0.145f, 0.805f, 0.120f, 0.048f));   /* 下 */
        p.keys.add(new Key(23, 0.020f, 0.858f, 0.370f, 0.080f));   /* 确定 */

        /* twelve digits, in the order a phone has them */
        int[] digits = {
            8, 9, 10,
            11, 12, 13,
            14, 15, 16,
            17, 7, 18
        };
        float x0 = 0.560f, y0 = 0.700f;
        float dw = 0.120f, dh = 0.048f, gx = 0.020f, gy = 0.010f;
        for (int i = 0; i < digits.length; i++) {
            p.keys.add(new Key(digits[i],
                               x0 + (i % 3) * (dw + gx),
                               y0 + (i / 3) * (dh + gy),
                               dw, dh));
        }
        return p;
    }

    static String presetName(int id) {
        if (id == PRESET_NUMERIC) {
            return "数字键盘";
        }
        if (id == PRESET_FULL) {
            return "全按键";
        }
        if (id == PRESET_CUSTOM) {
            return "自定义";
        }
        return "方向盘";
    }

    /** Switching preset throws the old keys away: applying one and keeping both
     * would leave the previous layout under the new one. */
    void applyPreset(PadConfig presetCfg) {
        keys.clear();
        for (int i = 0; i < presetCfg.keys.size(); i++) {
            keys.add(presetCfg.keys.get(i).copy());
        }
        preset = presetCfg.preset;
    }

    static PadConfig load(Context ctx) {
        SharedPreferences sp = ctx.getSharedPreferences("pad", Context.MODE_PRIVATE);
        String blob = sp.getString("keys", null);
        if (blob == null) {
            return defaults();
        }
        PadConfig p = new PadConfig();
        int i = 0;
        while (i < blob.length()) {
            int end = blob.indexOf(';', i);
            if (end < 0) {
                end = blob.length();
            }
            String rec = blob.substring(i, end);
            i = end + 1;
            String[] f = rec.split(",");
            if (f.length != 5) {
                continue;
            }
            try {
                p.keys.add(new Key(Integer.parseInt(f[0]),
                                   Float.parseFloat(f[1]), Float.parseFloat(f[2]),
                                   Float.parseFloat(f[3]), Float.parseFloat(f[4])));
            } catch (NumberFormatException e) {
                /* a hand-edited preference is not worth crashing over */
            }
        }
        if (p.keys.isEmpty()) {
            return defaults();
        }
        p.opacity = sp.getFloat("opacity", p.opacity);
        p.grid = sp.getBoolean("grid", p.grid);
        p.snap = sp.getBoolean("snap", p.snap);
        p.haptics = sp.getBoolean("haptics", p.haptics);
        p.preset = sp.getInt("preset", PRESET_CUSTOM);
        return p;
    }

    void save(Context ctx) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < keys.size(); i++) {
            Key k = keys.get(i);
            sb.append(k.code).append(',').append(fmt(k.x)).append(',').append(fmt(k.y))
              .append(',').append(fmt(k.w)).append(',').append(fmt(k.h)).append(';');
        }
        ctx.getSharedPreferences("pad", Context.MODE_PRIVATE).edit()
           .putString("keys", sb.toString())
           .putFloat("opacity", opacity)
           .putBoolean("grid", grid)
           .putBoolean("snap", snap)
           .putBoolean("haptics", haptics)
           .putInt("preset", preset)
           .apply();
    }

    /** Key under a normalized point, topmost first (the last one drawn wins). */
    Key hit(float nx, float ny) {
        for (int i = keys.size() - 1; i >= 0; i--) {
            if (keys.get(i).contains(nx, ny)) {
                return keys.get(i);
            }
        }
        return null;
    }

    PadConfig copy() {
        PadConfig p = new PadConfig();
        for (int i = 0; i < keys.size(); i++) {
            p.keys.add(keys.get(i).copy());
        }
        p.opacity = opacity;
        p.grid = grid;
        p.snap = snap;
        p.haptics = haptics;
        p.preset = preset;
        return p;
    }

    void replaceWith(PadConfig other) {
        keys.clear();
        for (int i = 0; i < other.keys.size(); i++) {
            keys.add(other.keys.get(i).copy());
        }
        opacity = other.opacity;
        grid = other.grid;
        snap = other.snap;
        haptics = other.haptics;
        preset = other.preset;
    }

    private static String fmt(float v) {
        return String.format(java.util.Locale.US, "%.4f", v);
    }
}
