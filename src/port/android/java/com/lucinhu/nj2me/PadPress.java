package com.lucinhu.nj2me;

/**
 * What a finger on the keypad means: which MIDP key it holds, and when that
 * key is released.
 *
 * <p>The rule the player expects is "the key is down while my finger is on it",
 * and every part of it is a decision this class makes on its own:
 *
 * <ul>
 *   <li>A finger that lands on a keycap presses it in the same event that
 *       delivers the touch.  Nothing waits for the finger to lift: a keypad
 *       that fires on release is a keypad that fires late, and a game that
 *       reads a direction only after the thumb has moved on has already
 *       missed the turn.</li>
 *   <li>A finger that slides off the keycap releases it, and a finger that
 *       slides onto a neighbour releases the old key and presses the new one.
 *       The MIDlet sees exactly the key that is under the finger.</li>
 *   <li>A finger that slides from empty screen onto a keycap presses it, and
 *       the game's pointer is released at that moment - one finger is either
 *       steering the game or working the pad, never both at once.</li>
 *   <li>Two fingers on one keycap are still one MIDlet key press, and it stays
 *       down until the last of them lifts.  A duplicate press is a menu that
 *       skips a line; an early release is a jump that ends in the air.</li>
 *   <li>Lifting the finger, the event stream being cancelled (a call, an
 *       activity switch) or the control bar opening releases everything.</li>
 * </ul>
 *
 * <p>Everything here is in normalized view coordinates (0..1) and every
 * dependency is plain Java, so the semantics above are tested on the desktop
 * (tools/check_android_java.sh) instead of only by thumb.
 */
final class PadPress {

    /** Where the decisions go: the game's key queue and the game's pointer. */
    interface Sink {
        /** A MIDP key (by Android key code) went down or came up. */
        void onKey(int androidKeyCode, boolean down);

        /** A pointer the pad is not using: phase 0 down, 1 move, 2 up. */
        void onTouch(int pointerId, int phase, float nx, float ny);
    }

    /*
     * Sixteen is more fingers than a handset has, and fixed arrays keep this
     * class free of every collection Android would make it depend on.
     */
    private static final int MAX_POINTERS = 16;

    private PadConfig cfg;
    private Sink sink;

    private final int[] ids = new int[MAX_POINTERS];
    private final int[] codes = new int[MAX_POINTERS];   /* 0 while free */
    private final int[] codes2 = new int[MAX_POINTERS];  /* the combo half */
    private final float[] lastX = new float[MAX_POINTERS];
    private final float[] lastY = new float[MAX_POINTERS];
    private int count;

    /* The one pointer the game owns.  A MIDlet sees a single pointer, and the
     * pad must not hand it two. */
    private int gameId = -1;

    PadPress(PadConfig cfg, Sink sink) {
        this.cfg = cfg;
        this.sink = sink;
    }

    void setConfig(PadConfig cfg) {
        this.cfg = cfg;
    }

    void setSink(Sink sink) {
        this.sink = sink;
    }

    /** A finger landed.  True when it landed on a keycap (the pad took it). */
    boolean down(int id, float nx, float ny) {
        PadConfig.Key k = keyAt(nx, ny);
        if (k != null) {
            return pressCode(id, k.code, k.code2, nx, ny);
        }
        /* Every finger is tracked, even the ones holding nothing: a finger the
         * game never saw may still slide onto a keycap later. */
        if (!occupy(id, 0, nx, ny)) {
            return false;
        }
        if (gameId < 0) {
            gameId = id;
            touch(id, 0, nx, ny);
        }
        return false;
    }

    /** A finger travelled.  Releases and presses follow the keycaps under it. */
    void move(int id, float nx, float ny) {
        int at = indexOf(id);
        if (at < 0) {
            return;
        }
        remember(at, nx, ny);
        PadConfig.Key k = keyAt(nx, ny);
        if (codes[at] != 0) {
            if (k != null && (k.code != codes[at] || k.code2 != codes2[at])) {
                releaseCode(at);
                pressCode(id, k.code, k.code2, nx, ny);
            } else if (k == null) {
                releaseCode(at);
            }
            return;
        }
        if (id == gameId) {
            if (k != null) {
                /* Slid from the game's picture onto a key: the game gives the
                 * pointer up, because from here the finger is steering the pad. */
                touch(id, 2, nx, ny);
                gameId = -1;
                pressCode(id, k.code, k.code2, nx, ny);
            } else {
                touch(id, 1, nx, ny);
            }
        } else if (k != null) {
            /* A second finger, one the game never saw: it can still work the
             * pad without disturbing the pointer the game is following. */
            pressCode(id, k.code, k.code2, nx, ny);
        }
    }

    /** A finger lifted.  Works the same whether or not it is still on the key. */
    void up(int id, float nx, float ny) {
        int at = indexOf(id);
        if (at < 0) {
            return;
        }
        remember(at, nx, ny);
        if (codes[at] != 0) {
            releaseCode(at);
        } else if (id == gameId) {
            touch(id, 2, nx, ny);
            gameId = -1;
        }
        release(at);
    }

    /**
     * Everything comes up: the control bar opened, the activity is pausing, or
     * the event stream was cancelled mid-press.  A key left down here is a
     * MIDlet that walks into a wall until the next tap.
     */
    void cancel() {
        /* Release every code that is down exactly once, combos included: the
         * first finger holding it sends the key-up, the others are just
         * fingers.  A key the pad fails to release is a key stuck down in the
         * MIDlet, which is worse than a stray press. */
        for (int i = 0; i < count; i++) {
            for (int half = 0; half < 2; half++) {
                int code = half == 0 ? codes[i] : codes2[i];
                if (code == 0) {
                    continue;
                }
                boolean first = true;
                for (int j = 0; j < i; j++) {
                    if (codes[j] == code || codes2[j] == code) {
                        first = false;
                        break;
                    }
                }
                if (first) {
                    key(code, false);
                }
            }
        }
        if (gameId >= 0) {
            touch(gameId, 2, 0f, 0f);
            gameId = -1;
        }
        count = 0;
    }

    /** Drop the game's pointer without telling anyone: the view mode changed. */
    void forgetGame() {
        gameId = -1;
    }

    /** True while the pad is holding this Android key code down. */
    boolean isPressed(int code) {
        for (int i = 0; i < count; i++) {
            if (codes[i] == code || codes2[i] == code) {
                return true;
            }
        }
        return false;
    }

    /** True only for the exact cap: ← must not light up when ↑ is held. */
    boolean isPressed(int code, int code2) {
        for (int i = 0; i < count; i++) {
            if (codes[i] == code && codes2[i] == code2) {
                return true;
            }
        }
        return false;
    }

    /** How many fingers the pad is currently holding keys with. */
    int heldCount() {
        int n = 0;
        for (int i = 0; i < count; i++) {
            if (codes[i] != 0) {
                n++;
            }
        }
        return n;
    }

    /**
     * Press a keycap with this finger, telling the game only the first time.
     *
     * <p>False when there was no room to follow the finger: a press the pad
     * cannot track is a press it could never release, so it is not sent.
     */
    private boolean pressCode(int id, int code, int code2, float nx, float ny) {
        /* "fresh" is per code, not per finger: a finger landing on the ↖
         * diagonal while another thumb already holds ← must not send a second
         * key-down for ←, or the MIDlet sees a key it never got a key-up for. */
        boolean fresh = !isPressed(code);
        boolean fresh2 = code2 != 0 && !isPressed(code2);
        if (!occupy(id, code, nx, ny)) {
            return false;
        }
        int at = indexOf(id);
        if (at >= 0) {
            codes2[at] = code2;
        }
        if (fresh) {
            key(code, true);
        }
        if (fresh2) {
            key(code2, true);
        }
        return true;
    }

    /** Let this slot's key go, unless another finger is still holding it. */
    private void releaseCode(int at) {
        int code = codes[at];
        int code2 = codes2[at];
        codes[at] = 0;
        codes2[at] = 0;
        if (code != 0 && !isPressed(code)) {
            key(code, false);
        }
        if (code2 != 0 && !isPressed(code2)) {
            key(code2, false);
        }
    }

    private PadConfig.Key keyAt(float nx, float ny) {
        return cfg == null ? null : cfg.hit(nx, ny);
    }

    private int indexOf(int id) {
        for (int i = 0; i < count; i++) {
            if (ids[i] == id) {
                return i;
            }
        }
        return -1;
    }

    /** Remembers a finger, or reports that the table is full. */
    private boolean occupy(int id, int code, float nx, float ny) {
        int at = indexOf(id);
        if (at >= 0) {
            codes[at] = code;
            codes2[at] = 0;
            remember(at, nx, ny);
            return true;
        }
        if (count >= MAX_POINTERS) {
            return false;
        }
        ids[count] = id;
        codes[count] = code;
        codes2[count] = 0;
        lastX[count] = nx;
        lastY[count] = ny;
        count++;
        return true;
    }

    private void remember(int at, float nx, float ny) {
        lastX[at] = nx;
        lastY[at] = ny;
    }
    private void release(int at) {
        count--;
        for (int i = at; i < count; i++) {
            ids[i] = ids[i + 1];
            codes[i] = codes[i + 1];
            codes2[i] = codes2[i + 1];
            lastX[i] = lastX[i + 1];
            lastY[i] = lastY[i + 1];
        }
        codes[count] = 0;
        codes2[count] = 0;
    }

    private void key(int code, boolean down) {
        if (sink != null) {
            sink.onKey(code, down);
        }
    }

    private void touch(int id, int phase, float nx, float ny) {
        if (sink != null) {
            sink.onTouch(id, phase, nx, ny);
        }
    }
}
