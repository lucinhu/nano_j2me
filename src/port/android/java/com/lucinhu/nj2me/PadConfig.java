package com.lucinhu.nj2me;

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
        /**
         * A second code that goes down and up with the first (0 = none).
         *
         * <p>The diagonals of 经典手机 press two directions at once, the way a
         * stick moves: a MIDlet that steers with {@code Canvas.getKeyStates()}
         * needs both bits held to walk diagonally.  The digit the same key
         * carries on a handset is in the block to the left, so a game that
         * wants the phone's own 1/3/7/9 still has it.
         */
        int code2;
        float x, y, w, h;

        Key(int code, float x, float y, float w, float h) {
            this(code, 0, x, y, w, h);
        }

        Key(int code, int code2, float x, float y, float w, float h) {
            this.code = code;
            this.code2 = code2;
            this.x = x;
            this.y = y;
            this.w = w;
            this.h = h;
        }

        Key copy() {
            return new Key(code, code2, x, y, w, h);
        }
    }

    static final int PRESET_DPAD = 0;
    static final int PRESET_CUSTOM = 2;
    static final int PRESET_FULL = 3;
    static final int PRESET_LANDSCAPE = 4;
    static final int PRESET_ARCADE = 5;
    static final int PRESET_PHONE = 7;
    static final int PRESET_CLASSIC = 8;
    static final int PRESET_ONEHAND = 9;
    static final int PRESET_COUNT = 8;

    final List<Key> keys = new ArrayList<Key>();
    float opacity = 0.50f;      /* how solid an idle keycap is; the
                                 * default is半透明, the slider in the
                                 * editor owns the rest */
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

    /*
     * Where keycaps may be drawn: the display cutout and the system bars eat
     * into the window on most modern phones.  Fractions, like everything else
     * here, and deliberately not saved with the layout - it is a property of
     * this screen, not of the player's arrangement, and switching to a phone
     * without a notch must not leave the keys huddled in a corner.
     */
    private float safeL = 0f;
    private float safeT = 0f;
    private float safeR = 1f;
    private float safeB = 1f;

    /** A key's drawn (and hit) rectangle, after the safe area has its say. */
    static final class Box {
        float x, y, w, h;
    }

    private final Box scratch = new Box();

    private PadConfig() {
    }

    /**
     * Tells the layout which part of the window is safe to draw in.
     *
     * <p>Fractions of the view: left, top, right and bottom of the usable
     * rectangle.  A nonsense rectangle (empty, or outside the view) is ignored
     * rather than obeyed, because the alternative is every keycap piled in one
     * spot.
     */
    void setSafeArea(float l, float t, float r, float b) {
        if (!(l >= 0f && t >= 0f && r <= 1f && b <= 1f && r - l > 0.05f && b - t > 0.05f)) {
            safeL = 0f;
            safeT = 0f;
            safeR = 1f;
            safeB = 1f;
            return;
        }
        safeL = l;
        safeT = t;
        safeR = r;
        safeB = b;
    }

    float safeLeft() {
        return safeL;
    }

    float safeTop() {
        return safeT;
    }

    float safeRight() {
        return safeR;
    }

    float safeBottom() {
        return safeB;
    }

    /**
     * A preset's coordinates, in units of the screen's SHORT side.
     *
     * <p>A pad built in raw fractions of the window is stretched on a landscape
     * window and squashed on a portrait one: the same 0.14 x 0.06 keycap is
     * 151x115 px on a phone held upright and 269x65 px on the same phone held
     * sideways, which is what "some layouts look crooked, some too big or too
     * small" was about. Every preset below is written in short-side units
     * instead - one unit is min(window width, window height) - so a keycap is
     * as wide as it is tall on any screen in either orientation.
     *
     * <p>u is also the natural unit for a thumb: a 0.11 u cap is 119 px on a
     * 1080p phone whatever way up it is.
     */
    private static final class Grid {
        /** A handset held upright: the box the portrait layouts are drawn in. */
        static final float PORTRAIT_W = 1.0f;
        static final float PORTRAIT_H = 1.78f;
        /** The same handset on its side. */
        static final float LANDSCAPE_W = 1.78f;
        static final float LANDSCAPE_H = 1.0f;

        final int w;
        final int h;
        final float m;
        private final float k;
        private final float oy;

        Grid(int winW, int winH, float designW, float designH) {
            w = (winW > 0) ? winW : 1080;
            h = (winH > 0) ? winH : 1920;
            m = (w < h) ? w : h;
            float kx = xmax() / designW;
            float ky = ymax() / designH;
            k = (kx < ky) ? kx : ky;
            /* The box is pinned to the bottom of the window: a pad belongs
             * under the thumbs, not floating in the middle of the picture. */
            oy = ymax() - designH * k;
        }

        Grid(int winW, int winH) {
            this(winW, winH, PORTRAIT_W, PORTRAIT_H);
        }

        /** How wide the window is in units. */
        float xmax() {
            return w / m;
        }

        /** How tall the window is in units. */
        float ymax() {
            return h / m;
        }

        /** Units from the left edge. */
        float x(float u) {
            return u * k * m / w;
        }

        /** Units from the top edge. */
        float y(float u) {
            return (u * k + oy) * m / h;
        }

        /**
         * Units down from the TOP of the window, ignoring the pad's bottom
         * anchor.
         *
         * <p>The whole block is pinned to the bottom because that is where
         * thumbs are, but a handset's soft keys sit under the earpiece
         * whatever the game does - anchored to the pad they end up floating
         * in the middle of the picture.
         */
        float top(float u) {
            return sy(u);
        }

        /** A width in units, as a fraction of the window's width. */
        float sx(float u) {
            return u * k * m / w;
        }

        /** A height in units, as a fraction of the window's height.  Not the
         *  same number as sx() unless the window is square: that difference is
         *  exactly what keeps a keycap square. */
        float sy(float u) {
            return u * k * m / h;
        }

        /** Units back from the right edge, mirrored so a right-anchored key
         *  keeps its margin whatever the window's width is. */
        float xr(float u) {
            return (xmax() - u * k) * m / w;
        }

        /** Also scaled: the largest key width that still fits the window. */
        float k() {
            return k;
        }

        void add(List<Key> out, int code, float ux, float uy, float uw, float uh) {
            out.add(new Key(code, x(ux), y(uy), sx(uw), sy(uh)));
        }

        /** A key whose right edge sits `uright` units in from the window's
         *  right edge.  Mirroring the margin is what keeps a right-anchored
         *  cluster in place on a window of any width. */
        void addRight(List<Key> out, int code, float uright, float uy,
                      float uw, float uh) {
            out.add(new Key(code, xr(uright + uw), y(uy), sx(uw), sy(uh)));
        }

        void combo(List<Key> out, int code, int code2,
                   float ux, float uy, float uw, float uh) {
            out.add(new Key(code, code2, x(ux), y(uy), sx(uw), sy(uh)));
        }

        void comboRight(List<Key> out, int code, int code2,
                        float uright, float uy, float uw, float uh) {
            out.add(new Key(code, code2, xr(uright + uw), y(uy), sx(uw), sy(uh)));
        }

        /** The four directions of a cross, without the middle key. */
        void cross(List<Key> out, float x0, float y0, float cap, float step) {
            add(out, 19, x0 + step, y0, cap, cap);                  /* 上 */
            add(out, 21, x0, y0 + step, cap, cap);                  /* 左 */
            add(out, 22, x0 + 2 * step, y0 + step, cap, cap);       /* 右 */
            add(out, 20, x0 + step, y0 + 2 * step, cap, cap);       /* 下 */
        }

        /** Three soft keys across the top: left, clear, right. */
        void softRow(List<Key> out, float margin, float y0, float wide, float tall) {
            add(out, 82, margin, y0, wide, tall);
            add(out, 67, (xmax() - wide * k) / 2f, y0, wide, tall);
            addRight(out, 4, margin, y0, wide, tall);
        }
    }

    /** The twelve keys of a handset, in the order a phone had them. */
    private static final int[] DIGITS = {
        8, 9, 10,          /* 1 2 3 */
        11, 12, 13,        /* 4 5 6 */
        14, 15, 16,        /* 7 8 9 */
        17, 7, 18          /* * 0 # */
    };

    /**
     * The layout a game gets before anyone has an opinion.  Everything sits in
     * the bars above and below a 240x320 screen, because that is where there is
     * nothing to cover: a MIDlet's own game area is the last place a control
     * should be drawn.
     */
    static PadConfig defaults() {
        return defaults(1080, 1920);
    }

    static PadConfig defaults(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_DPAD;
        Grid g = new Grid(winW, winH);
        final float cap = 0.115f;      /* ~124 px on a 1080p phone */
        final float step = 0.125f;

        g.cross(p.keys, 0.045f, 1.34f, cap, step);
        g.add(p.keys, 23, 0.66f, 1.44f, 0.30f, 0.20f);     /* 确定 / 火力 */
        g.add(p.keys, 82, 0.045f, 0.045f, 0.34f, 0.10f);   /* 左软键 */
        g.addRight(p.keys, 4, 0.045f, 0.045f, 0.34f, 0.10f);
        return p;
    }

    /**
     * The handset the games were written for: the digits on one side, the whole
     * eight-way cluster on the other.
     *
     * <p>A feature phone had no joystick.  The same twelve keys were the
     * numbers, the directions and the diagonals at once, so a title that walks
     * its hero with 2/4/6/8 and jumps with 5 expects to find all of that on one
     * keypad.  The left block is the twelve keys; the right block is that same
     * keypad read as directions - the diagonals really are 1/3/7/9, which is why
     * they carry those codes twice - with the soft keys along its top row and
     * the fire button in the middle.
     */
    static PadConfig classic() {
        return classic(1080, 1920);
    }

    static PadConfig classic(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_CLASSIC;
        Grid g = new Grid(winW, winH);
        final float cap = 0.135f;
        final float step = 0.143f;
        final float left = 0.012f;
        final float y0 = 1.06f;

        for (int i = 0; i < DIGITS.length; i++) {
            g.add(p.keys, DIGITS[i],
                  left + (i % 3) * step, y0 + (i / 3) * step, cap, cap);
        }

        /* Right block: the soft keys along the top, then the eight-way
         * cluster with the fire button in the middle.
         *
         * The diagonals press BOTH directions at once.  That is what a stick
         * does, and it is what a MIDlet reading Canvas.getKeyStates() needs -
         * with a single digit code it would never see a diagonal walk.  The
         * digits themselves are one block to the left, so a game that asks for
         * the phone's '1' still gets it. */
        final float colL = 2 * step;          /* units in from the right edge */
        g.addRight(p.keys, 82, colL, y0, cap, cap);                    /* L */
        g.addRight(p.keys, 4, 0f, y0, cap, cap);                       /* R */
        g.comboRight(p.keys, 21, 19, colL, y0 + step, cap, cap);       /* ↖ */
        g.addRight(p.keys, 19, step, y0 + step, cap, cap);             /* ↑ */
        g.comboRight(p.keys, 22, 19, 0f, y0 + step, cap, cap);         /* ↗ */
        g.addRight(p.keys, 21, colL, y0 + 2 * step, cap, cap);         /* ← */
        g.addRight(p.keys, 23, step, y0 + 2 * step, cap, cap);         /* ● */
        g.addRight(p.keys, 22, 0f, y0 + 2 * step, cap, cap);           /* → */
        g.comboRight(p.keys, 21, 20, colL, y0 + 3 * step, cap, cap);   /* ↙ */
        g.addRight(p.keys, 20, step, y0 + 3 * step, cap, cap);         /* ↓ */
        g.comboRight(p.keys, 22, 20, 0f, y0 + 3 * step, cap, cap);     /* ↘ */
        return p;
    }

    /** No keys at all: the player is about to place every one of them. */
    static PadConfig blank() {
        PadConfig p = new PadConfig();
        p.preset = PRESET_CUSTOM;
        return p;
    }

    /**
     * Everything a pad can hold, laid out on one grid: soft keys across the
     * top, A/B/C/D above the cross on the left, the twelve digits on the right.
     */
    static PadConfig full() {
        return full(1080, 1920);
    }

    static PadConfig full(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_FULL;
        Grid g = new Grid(winW, winH);
        final float cap = 0.105f;
        final float step = 0.115f;
        final float left = 0.015f;

        g.softRow(p.keys, 0.03f, 0.045f, 0.30f, 0.10f);

        int[] abcd = { 29, 30, 31, 32 };
        for (int i = 0; i < abcd.length; i++) {
            g.add(p.keys, abcd[i], left + i * step, 1.03f, cap, cap);
        }
        g.cross(p.keys, left, 1.18f, cap, step);
        g.add(p.keys, 23, left + step, 1.18f + step, cap, cap);   /* 确定 */

        final float dx = 0.56f;
        final float dy = 1.00f;
        for (int i = 0; i < DIGITS.length; i++) {
            g.add(p.keys, DIGITS[i],
                  dx + (i % 3) * step, dy + (i / 3) * step, cap, cap);
        }
        return p;
    }

    /**
     * Two hands, one screen on its side: the cross under the left thumb, the
     * two game keys and the fire button under the right one.
     */
    static PadConfig landscape() {
        return landscape(1920, 1080);
    }

    static PadConfig landscape(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_LANDSCAPE;
        Grid g = new Grid(winW, winH, Grid.LANDSCAPE_W, Grid.LANDSCAPE_H);
        final float cap = 0.16f;
        final float step = 0.175f;

        g.add(p.keys, 82, 0.04f, 0.05f, 0.24f, 0.10f);
        g.addRight(p.keys, 4, 0.04f, 0.05f, 0.24f, 0.10f);

        g.cross(p.keys, 0.05f, 0.40f, cap, step);
        g.add(p.keys, 23, 0.05f + step, 0.40f + step, cap, cap);   /* 确定 */

        /* The two game keys and the fire button, anchored to the right edge
         * so they keep their margin on a window of any width. */
        g.addRight(p.keys, 30, 0.045f, 0.30f, cap, cap);
        g.addRight(p.keys, 29, 0.045f + cap + 0.02f, 0.30f, cap, cap);
        g.addRight(p.keys, 31, 0.045f, 0.30f + cap + 0.04f, cap * 1.3f, cap);
        return p;
    }

    /** A stick and three big buttons - the arcade cabinet arrangement. */
    static PadConfig arcade() {
        return arcade(1920, 1080);
    }

    static PadConfig arcade(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_ARCADE;
        Grid g = new Grid(winW, winH, Grid.LANDSCAPE_W, Grid.LANDSCAPE_H);
        final float cap = 0.185f;
        final float step = 0.20f;

        /* The two soft keys go under the top edge of the WINDOW: on a phone
         * they are the plates beside the screen, not part of the pad. */
        p.keys.add(new Key(4, g.xr(0.04f + 0.22f), g.top(0.30f), g.sx(0.22f), g.sy(0.09f)));
        p.keys.add(new Key(82, g.x(0.04f), g.top(0.30f), g.sx(0.22f), g.sy(0.09f)));

        g.cross(p.keys, 0.04f, 0.34f, cap, step);
        g.add(p.keys, 23, 0.04f + step, 0.34f + step, cap, cap);

        g.addRight(p.keys, 30, 0.045f, 0.32f, 0.20f, 0.20f);
        g.addRight(p.keys, 29, 0.045f + 0.22f, 0.32f, 0.20f, 0.20f);
        g.addRight(p.keys, 31, 0.045f + 0.10f, 0.58f, 0.24f, 0.24f);
        return p;
    }

    /**
     * A whole handset on screen: the three soft keys along the top, the cross
     * over the twelve digits, both centred on the same grid, the way a Nokia
     * laid its keypad out.
     */
    static PadConfig phone() {
        return phone(1080, 1920);
    }

    static PadConfig phone(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_PHONE;
        Grid g = new Grid(winW, winH);
        final float cap = 0.105f;
        final float step = 0.115f;

        g.softRow(p.keys, 0.03f, 0.045f, 0.30f, 0.10f);

        final float block = 3 * step - (step - cap);
        final float x0 = (g.xmax() - block) / 2f;
        g.cross(p.keys, x0, 0.90f, cap, step);
        g.add(p.keys, 23, x0 + step, 0.90f + step, cap, cap);

        for (int i = 0; i < DIGITS.length; i++) {
            g.add(p.keys, DIGITS[i],
                  x0 + (i % 3) * step, 1.28f + (i / 3) * step, cap, cap);
        }
        return p;
    }

    /**
     * Everything under the right thumb: the cross in the bottom right corner
     * and the fire button just left of it, for a phone held in one hand.
     */
    static PadConfig onehand() {
        return onehand(1080, 1920);
    }

    static PadConfig onehand(int winW, int winH) {
        PadConfig p = new PadConfig();
        p.preset = PRESET_ONEHAND;
        Grid g = new Grid(winW, winH);
        final float cap = 0.12f;
        final float step = 0.13f;

        final float y0 = 1.30f;
        final float col = 0.05f + 2 * step + cap;    /* units in from the right */
        /* the cross, mirrored: 右 column first, then the middle, then 左 */
        g.addRight(p.keys, 19, col - step, y0, cap, cap);              /* 上 */
        g.addRight(p.keys, 22, col - 2 * step, y0 + step, cap, cap);   /* 右 */
        g.addRight(p.keys, 21, col, y0 + step, cap, cap);              /* 左 */
        g.addRight(p.keys, 20, col - step, y0 + 2 * step, cap, cap);   /* 下 */
        g.addRight(p.keys, 23, col + 0.02f + cap, y0 + step, 0.24f, 0.20f); /* 火力 */

        g.addRight(p.keys, 82, 0.38f, 0.045f, 0.33f, 0.10f);
        g.addRight(p.keys, 4, 0.03f, 0.045f, 0.33f, 0.10f);
        return p;
    }

    /** The presets the layout picker offers, in the order it shows them. */
    static final int[] PRESET_IDS = {
        PRESET_DPAD, PRESET_CLASSIC, PRESET_FULL, PRESET_PHONE,
        PRESET_ONEHAND, PRESET_ARCADE, PRESET_LANDSCAPE,
        PRESET_CUSTOM
    };

    /** The layout of a preset, for a window of this shape. */
    static PadConfig byId(int id, int winW, int winH) {
        if (id == PRESET_CLASSIC) {
            return classic(winW, winH);
        }
        if (id == PRESET_FULL) {
            return full(winW, winH);
        }
        if (id == PRESET_LANDSCAPE) {
            return landscape(winW, winH);
        }
        if (id == PRESET_ARCADE) {
            return arcade(winW, winH);
        }
        if (id == PRESET_PHONE) {
            return phone(winW, winH);
        }
        if (id == PRESET_ONEHAND) {
            return onehand(winW, winH);
        }
        if (id == PRESET_CUSTOM) {
            return blank();
        }
        return defaults(winW, winH);
    }

    /** The layout of a preset.  CUSTOM has no keys, by definition. */
    static PadConfig byId(int id) {
        return byId(id, 1080, 1920);
    }

    /** What a preset is for, one line, for the picker. */
    static String presetHint(int id) {
        if (id == PRESET_CLASSIC) {
            return "左数字 · 右八向 + 火力";
        }
        if (id == PRESET_FULL) {
            return "方向 + 数字 + A/B/C/D";
        }
        if (id == PRESET_LANDSCAPE) {
            return "左手方向，右手 A/B/确定";
        }
        if (id == PRESET_ARCADE) {
            return "左方向 + 右侧三个大键";
        }
        if (id == PRESET_PHONE) {
            return "整台手机：软键 + 方向 + 数字";
        }
        if (id == PRESET_ONEHAND) {
            return "全在右下角，一只手能按";
        }
        if (id == PRESET_CUSTOM) {
            return "清空，自己摆";
        }
        return "左下方向 + 右下火力 + 软键";
    }

    static String presetName(int id) {
        if (id == PRESET_CLASSIC) {
            return "经典手机";
        }
        if (id == PRESET_FULL) {
            return "全按键";
        }
        if (id == PRESET_LANDSCAPE) {
            return "横屏双手";
        }
        if (id == PRESET_ARCADE) {
            return "街机双键";
        }
        if (id == PRESET_PHONE) {
            return "手机键盘";
        }
        if (id == PRESET_ONEHAND) {
            return "单手右下";
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
        SharedPreferences sp = pref(ctx);
        PadConfig p = parse(sp.getString("keys", null));
        if (p == null) {
            String named = defaultLayout(ctx);
            if (named != null) {
                p = loadLayout(ctx, named);
            }
        }
        if (p == null) {
            return defaults();
        }
        p.opacity = sp.getFloat("opacity", p.opacity);
        p.grid = sp.getBoolean("grid", p.grid);
        p.snap = sp.getBoolean("snap", p.snap);
        p.haptics = sp.getBoolean("haptics", p.haptics);
        p.preset = sp.getInt("preset", PRESET_CUSTOM);
        return p;
    }

    /** The whole arrangement as one string, ready for a preference. */
    String blob() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < keys.size(); i++) {
            Key k = keys.get(i);
            sb.append(k.code).append(',').append(fmt(k.x)).append(',').append(fmt(k.y))
              .append(',').append(fmt(k.w)).append(',').append(fmt(k.h));
            if (k.code2 != 0) {
                /* written only when it is used: a pad with no combos keeps the
                 * old five-field record, so nothing else has to learn a new
                 * format */
                sb.append(',').append(k.code2);
            }
            sb.append(';');
        }
        return sb.toString();
    }

    /** Reads back what blob() wrote; null when there is nothing stored. */
    private static PadConfig parse(String blob) {
        if (blob == null || blob.length() == 0) {
            return null;
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
            if (f.length != 5 && f.length != 6) {
                continue;
            }
            try {
                p.keys.add(new Key(Integer.parseInt(f[0]),
                                   f.length == 6 ? Integer.parseInt(f[5]) : 0,
                                   Float.parseFloat(f[1]), Float.parseFloat(f[2]),
                                   Float.parseFloat(f[3]), Float.parseFloat(f[4])));
            } catch (NumberFormatException e) {
                /* a hand-edited preference is not worth crashing over */
            }
        }
        if (p.keys.isEmpty()) {
            return null;
        }
        upgradeClassicDiagonals(p.keys);
        return p;
    }

    /** The keyboard flags that are shared by every layout. */
    private static SharedPreferences.Editor saveFlags(SharedPreferences.Editor e,
                                                      PadConfig cfg) {
        return e.putFloat("opacity", cfg.opacity)
                .putBoolean("grid", cfg.grid)
                .putBoolean("snap", cfg.snap)
                .putBoolean("haptics", cfg.haptics)
                .putInt("preset", cfg.preset);
    }

    /** Writes this arrangement as the default the next game starts from. */
    void save(Context ctx) {
        saveFlags(ctx.getSharedPreferences("pad", Context.MODE_PRIVATE).edit()
                  .putString("keys", blob()), this)
              .apply();
    }

    /**
     * The layout a game should open with: the one the player arranged for this
     * jar, or the default when this game has none.
     */
    static PadConfig forGame(Context ctx, String jar) {
        PadConfig own = parse(pref(ctx).getString(gameKey(jar), null));
        if (own == null) {
            return load(ctx);
        }
        own.opacity = pref(ctx).getFloat("opacity", own.opacity);
        own.grid = pref(ctx).getBoolean("grid", own.grid);
        own.snap = pref(ctx).getBoolean("snap", own.snap);
        own.haptics = pref(ctx).getBoolean("haptics", own.haptics);
        own.preset = pref(ctx).getInt("preset", PRESET_CUSTOM);
        return own;
    }

    /** Remembers this arrangement for one game only.  The default stays put. */
    static void saveForGame(Context ctx, String jar, PadConfig cfg) {
        SharedPreferences.Editor e = pref(ctx).edit();
        if (jar != null && jar.length() > 0) {
            e.putString(gameKey(jar), cfg.blob());
        }
        saveFlags(e, cfg);
        e.apply();
    }

    static void forgetGame(Context ctx, String jar) {
        if (jar != null && jar.length() > 0) {
            pref(ctx).edit().remove(gameKey(jar)).apply();
        }
    }

    private static String gameKey(String jar) {
        return "game:" + jar;
    }

    private static SharedPreferences pref(Context ctx) {
        return ctx.getSharedPreferences("pad", Context.MODE_PRIVATE);
    }

    /** The names of the layouts the player saved for reuse. */
    static java.util.List<String> layoutNames(Context ctx) {
        java.util.List<String> out = new ArrayList<String>();
        for (String k : pref(ctx).getAll().keySet()) {
            if (k.startsWith("layout:") && k.length() > 7) {
                out.add(k.substring(7));
            }
        }
        java.util.Collections.sort(out);
        return out;
    }

    /** Keeps this arrangement under a name the player can pick again. */
    static void saveLayout(Context ctx, String name, PadConfig cfg) {
        if (name == null || name.trim().length() == 0) {
            return;
        }
        pref(ctx).edit().putString("layout:" + name.trim(), cfg.blob()).apply();
    }

    static PadConfig loadLayout(Context ctx, String name) {
        return parse(pref(ctx).getString("layout:" + name, null));
    }

    static void deleteLayout(Context ctx, String name) {
        pref(ctx).edit().remove("layout:" + name).apply();
    }

    /** Which named layout a game with no arrangement of its own should use. */
    static String defaultLayout(Context ctx) {
        return pref(ctx).getString("defaultLayout", null);
    }

    static void setDefaultLayout(Context ctx, String name) {
        SharedPreferences.Editor e = pref(ctx).edit();
        if (name == null) {
            e.remove("defaultLayout");
        } else {
            e.putString("defaultLayout", name);
        }
        e.apply();
    }

    /**
     * Where a keycap actually goes: its own rectangle, moved just far enough to
     * clear the cutout and the system bars.
     *
     * <p>Moved, never resized, and never written back: a key that ends up under
     * the notch on this phone is still where the player put it on the phone
     * they laid it out on.
     */
    void rectOf(Key k, Box out) {
        out.w = k.w;
        out.h = k.h;
        out.x = fit(k.x, k.w, safeL, safeR);
        out.y = fit(k.y, k.h, safeT, safeB);
    }

    private static float fit(float v, float size, float lo, float hi) {
        if (size >= hi - lo) {
            return lo;
        }
        if (v < lo) {
            return lo;
        }
        return v + size > hi ? hi - size : v;
    }

    /** Moves a key's stored position inside the safe area (the editor's drag). */
    void clampInto(Key k) {
        k.x = fit(k.x, k.w, safeL, safeR);
        k.y = fit(k.y, k.h, safeT, safeB);
    }

    /** Key under a normalized point, topmost first (the last one drawn wins). */
    Key hit(float nx, float ny) {
        for (int i = keys.size() - 1; i >= 0; i--) {
            Key k = keys.get(i);
            rectOf(k, scratch);
            if (nx >= scratch.x && nx <= scratch.x + scratch.w
                    && ny >= scratch.y && ny <= scratch.y + scratch.h) {
                return k;
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

    /**
     * 经典手机 first shipped its diagonals as the digit codes a handset puts on
     * those keys (8/10/14/16 = 1/3/7/9).  They press two directions at once
     * now, so a pad saved before that change is rewritten in place: the
     * geometry the player arranged is kept, only the meaning of the four
     * corner keys changes.
     *
     * <p>The digit block on the left is left alone - it is the same four codes
     * and it is the reason the diagonals can afford to stop being digits.  The
     * two blocks are told apart by x: classic() puts the right one at 0.506.
     */
    static void upgradeClassicDiagonals(List<Key> keys) {
        for (int i = 0; i < keys.size(); i++) {
            Key k = keys.get(i);
            if (k.x < 0.5f || k.code2 != 0) {
                continue;
            }
            switch (k.code) {
                case 8:                                     /* ↖ */
                    k.code = 21;
                    k.code2 = 19;
                    break;
                case 10:                                    /* ↗ */
                    k.code = 22;
                    k.code2 = 19;
                    break;
                case 14:                                    /* ↙ */
                    k.code = 21;
                    k.code2 = 20;
                    break;
                case 16:                                    /* ↘ */
                    k.code = 22;
                    k.code2 = 20;
                    break;
                default:
                    break;
            }
        }
    }

    private static String fmt(float v) {
        return String.format(java.util.Locale.US, "%.4f", v);
    }
}
