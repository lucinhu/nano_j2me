package com.lucinhu.nj2me;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Vibrator;
import android.view.MotionEvent;
import android.view.View;

/**
 * The floating keypad: draws the layout, turns taps into key events, and is
 * also its own editor.
 *
 * <p>One view for both jobs is not laziness.  The editor has to hit-test exactly
 * the way play does, or a key would sit somewhere the finger cannot reach; the
 * only difference between the two modes is what a drag does to the key under it.
 *
 * <p>Touches that do not land on a key are handed to the game instead of being
 * swallowed, which is what makes the keypad "floating" rather than a mode you
 * are stuck in: the MIDlet keeps getting its pointer events in the empty parts
 * of the screen.
 */
class PadView extends View {

    /** What the pad does with events it does not consume. */
    interface Sink {
        void onPadKey(int androidKeyCode, boolean down);

        void onGameTouch(int pointerId, int phase, float x, float y);

        /**
         * The player is moving the picture itself, in 画面调整 mode: multiply the
         * zoom by {@code zoomFactor} and move it by {@code dx}/{@code dy} window
         * pixels.  One finger drags, two fingers pinch - the same gestures as a
         * photo viewer, because that is what this is.
         */
        void onViewGesture(float zoomFactor, float dx, float dy);
    }

    /** Only the editor cares about these; play mode leaves it null. */
    interface EditListener {
        void onKeyLongPress(PadConfig.Key key);

        void onEmptyLongPress(float nx, float ny);
    }

    private PadConfig cfg;
    private final Paint fill = new Paint();
    private final Paint stroke = new Paint();
    private final Paint text = new Paint();
    private final Paint icon = new Paint();
    private final RectF rect = new RectF();
    private final RectF inner = new RectF();
    private final Path shape = new Path();
    private final Paint guide = new Paint();
    private final PadConfig.Box box = new PadConfig.Box();

    /*
     * The window edges the display cutout and the system bars occupy, in
     * pixels, as the layout last heard them.  Kept in pixels rather than
     * fractions because they arrive from the window insets, and converted on
     * every size change: a rotation swaps width and height and the fractions
     * have to follow.
     */
    private int insetL, insetT, insetR, insetB;

    private Sink sink;
    private EditListener editListener;
    private boolean editing;
    private boolean previewOpacity;
    private boolean barShowing;      /* the game's control bar owns the screen */
    private boolean adjusting;      /* empty space moves the picture, not the game */
    private float adjustX, adjustY; /* last finger position while dragging it */
    private float adjustSpread;     /* two-finger distance while pinching it */
    private PadConfig.Key selected;

    /*
     * Which key each active finger holds, when a key is released, and which
     * finger the game owns: all of that is PadPress, which has no Android
     * dependency so the rules can be tested off the device.  This view is the
     * translation layer - pixels to fractions, events to calls - and the
     * editor.
     */
    private PadPress padPress;

    /* PadPress works in fractions of the view; the game wants pixels. */
    private final PadPress.Sink padSink = new PadPress.Sink() {
        @Override
        public void onKey(int androidKeyCode, boolean down) {
            if (down) {
                buzz();
            }
            if (sink != null) {
                sink.onPadKey(androidKeyCode, down);
            }
        }

        @Override
        public void onTouch(int pointerId, int phase, float nx, float ny) {
            if (sink != null) {
                sink.onGameTouch(pointerId, phase, nx * getWidth(), ny * getHeight());
            }
        }
    };

    private float dragDx, dragDy;
    private float pinchStartDist;
    private float pinchStartW, pinchStartH;
    private float pinchStartX, pinchStartY;
    private Runnable longPress;
    private float pressX, pressY;
    private boolean longFired;

    PadView(Context ctx, PadConfig cfg) {
        super(ctx);
        this.cfg = cfg;
        this.padPress = new PadPress(cfg, padSink);
        setFocusable(false);
        fill.setAntiAlias(true);
        stroke.setAntiAlias(true);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f);
        text.setAntiAlias(true);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        icon.setAntiAlias(true);
    }

    void setConfig(PadConfig cfg) {
        this.cfg = cfg;
        padPress.setConfig(cfg);
        applySafeArea();
        invalidate();
    }

    /**
     * The edges of the window the display cutout and the system bars own.
     *
     * <p>Keycaps are moved clear of them rather than squashed into what is
     * left: a player who laid their pad out on a phone without a notch should
     * not find the D-pad rearranged on one that has it.
     */
    void setSafeAreaPx(int l, int t, int r, int b) {
        insetL = Math.max(0, l);
        insetT = Math.max(0, t);
        insetR = Math.max(0, r);
        insetB = Math.max(0, b);
        applySafeArea();
        invalidate();
    }

    private void applySafeArea() {
        if (cfg == null) {
            return;
        }
        int w = getWidth();
        int h = getHeight();
        if (w <= 0 || h <= 0) {
            return;
        }
        cfg.setSafeArea(insetL / (float) w, insetT / (float) h,
                        (w - insetR) / (float) w, (h - insetB) / (float) h);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        super.onSizeChanged(w, h, oldW, oldH);
        applySafeArea();
    }

    void setSink(Sink sink) {
        this.sink = sink;
    }

    void setEditListener(EditListener l) {
        this.editListener = l;
    }

    /**
     * Draw the caps the way the game will, for as long as the editor's
     * transparency slider is held.  Editing otherwise paints them solid: a key
     * you cannot see is a key you cannot place.
     */
    void setPreviewOpacity(boolean on) {
        previewOpacity = on;
        invalidate();
    }

    void setEditing(boolean editing) {
        this.editing = editing;
        invalidate();
    }

    /** True while the game's own control bar is up: the pad steps aside. */
    void setBarShowing(boolean showing) {
        barShowing = showing;
        if (showing) {
            releaseAllKeys();
        }
    }

    /**
     * 画面调整: a finger on empty space now moves the picture instead of reaching
     * the game.  Keycaps keep working, because the player is adjusting the view
     * *of the game* and needs to see what the game does while they do it.
     */
    void setAdjusting(boolean a) {
        adjusting = a;
        adjustSpread = 0f;
        if (!a) {
            padPress.forgetGame();
        }
        invalidate();
    }

    boolean isAdjusting() {
        return adjusting;
    }

    PadConfig.Key selectedKey() {
        return selected;
    }

    private void buzz() {
        if (!cfg.haptics || editing) {
            return;
        }
        Vibrator v = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
        if (v != null && v.hasVibrator()) {
            try {
                v.vibrate(12);
            } catch (SecurityException e) {
                /* No android.permission.VIBRATE (or it was revoked): silence the
                   buzz, never the game - this runs inside dispatchTouchEvent. */
                cfg.haptics = false;
            } catch (RuntimeException e) {
                cfg.haptics = false;
            }
        }
    }

    private void releaseAll() {
        padPress.cancel();
        selected = null;
        invalidate();
    }

    /**
     * Let go of everything the pad is holding.
     *
     * <p>Called when the game goes to the background: a key the player was
     * holding as the activity paused would otherwise stay down in the MIDlet
     * until they touched the pad again.
     */
    void releaseAllKeys() {
        releaseAll();
    }

    private PadConfig.Key keyAt(float px, float py) {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return null;
        }
        return cfg.hit(px / getWidth(), py / getHeight());
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        if (barShowing) {
            /* The game's control bar is up.  Consume rather than fall through:
             * passing the event on would deliver a phantom tap to the MIDlet
             * from under the bar the player is aiming at. */
            return true;
        }
        int action = ev.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = ev.getActionIndex();
                int id = ev.getPointerId(idx);
                float x = ev.getX(idx);
                float y = ev.getY(idx);
                PadConfig.Key k = keyAt(x, y);
                if (adjusting && !editing && k == null) {
                    /* Empty space is the picture itself: remember where the
                     * finger is and move it from there, and if this is the
                     * second finger, remember the pinch distance. */
                    adjustX = x;
                    adjustY = y;
                    if (ev.getPointerCount() >= 2) {
                        adjustSpread = spread(ev);
                    }
                    return true;
                }
                if (editing) {
                    selected = k;
                    longFired = false;
                    pressX = x;
                    pressY = y;
                    if (k != null) {
                        dragDx = k.x - x / getWidth();
                        dragDy = k.y - y / getHeight();
                        if (ev.getPointerCount() >= 2) {
                            startPinch(ev, k);
                        }
                    } else {
                        /* Empty space is the picture, not only a place to add a
                         * key: dragging here moves the game picture and a pinch
                         * scales it, which is what lets one screen place the
                         * keys and the picture against each other.  The long
                         * press that adds a key stays armed and is cancelled as
                         * soon as the finger travels. */
                        adjustX = x;
                        adjustY = y;
                        if (ev.getPointerCount() >= 2) {
                            adjustSpread = spread(ev);
                        }
                    }
                    armLongPress(k, x / getWidth(), y / getHeight());
                    invalidate();
                    return true;
                }
                /* A keycap is pressed by the event that delivers the touch, not
                 * by the one that lifts the finger; empty space is the game's. */
                padPress.down(id, x / getWidth(), y / getHeight());
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (editing) {
                    if (movedPastSlop(ev)) {
                        disarmLongPress();
                    }
                    if (selected != null && !longFired) {
                        if (ev.getPointerCount() >= 2) {
                            pinch(ev, selected);
                        } else {
                            moveTo(selected, ev.getX(0) / getWidth() + dragDx,
                                            ev.getY(0) / getHeight() + dragDy);
                        }
                        invalidate();
                    } else if (selected == null && !longFired && sink != null) {
                        pictureGesture(ev);
                    }
                    return true;
                }
                for (int i = 0; i < ev.getPointerCount(); i++) {
                    padPress.move(ev.getPointerId(i), ev.getX(i) / getWidth(),
                                  ev.getY(i) / getHeight());
                }
                invalidate();
                if (adjusting && !editing && sink != null) {
                    pictureGesture(ev);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = ev.getActionIndex();
                int id = ev.getPointerId(idx);
                if (editing) {
                    disarmLongPress();
                    if (ev.getPointerCount() <= 2) {
                        pinchStartDist = 0f;
                        adjustSpread = 0f;
                    }
                    return true;
                }
                if (adjusting) {
                    adjustSpread = 0f;
                }
                padPress.up(id, ev.getX(idx) / getWidth(), ev.getY(idx) / getHeight());
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                disarmLongPress();
                releaseAll();
                return true;
            }
            default:
                return true;
        }
    }

    /**
     * Feed the picture the finger's travel (or the spread between two of them).
     *
     * <p>Deltas, not positions: the sink accumulates them, so a pan is the sum
     * of the moves and a pinch is the ratio between two spreads.
     */
    private void pictureGesture(MotionEvent ev) {
        if (ev.getPointerCount() >= 2) {
            float d = spread(ev);
            if (adjustSpread > 1f && d > 1f) {
                sink.onViewGesture(d / adjustSpread, 0f, 0f);
                adjustSpread = d;
            }
        } else {
            sink.onViewGesture(1f, ev.getX(0) - adjustX, ev.getY(0) - adjustY);
            adjustX = ev.getX(0);
            adjustY = ev.getY(0);
        }
    }

    private void startPinch(MotionEvent ev, PadConfig.Key k) {
        pinchStartDist = spread(ev);
        pinchStartW = k.w;
        pinchStartH = k.h;
        pinchStartX = k.x;
        pinchStartY = k.y;
    }

    private void pinch(MotionEvent ev, PadConfig.Key k) {
        if (pinchStartDist <= 0f) {
            startPinch(ev, k);
            return;
        }
        float scale = spread(ev) / pinchStartDist;
        if (scale < 0.25f) {
            scale = 0.25f;
        }
        if (scale > 4f) {
            scale = 4f;
        }
        float w = clamp(pinchStartW * scale, 0.03f, 1f);
        float h = clamp(pinchStartH * scale, 0.02f, 1f);
        /* Keep the centre put while resizing: the finger is on the middle of
         * the key, and a key that also slides around while pinching is a key
         * nobody can place. */
        float cx = pinchStartX + pinchStartW / 2f;
        float cy = pinchStartY + pinchStartH / 2f;
        k.w = w;
        k.h = h;
        moveTo(k, cx - w / 2f, cy - h / 2f);
    }

    private static float spread(MotionEvent ev) {
        if (ev.getPointerCount() < 2) {
            return 0f;
        }
        float dx = ev.getX(0) - ev.getX(1);
        float dy = ev.getY(0) - ev.getY(1);
        return (float) Math.sqrt(dx * dx + dy * dy);
    }

    private void moveTo(PadConfig.Key k, float nx, float ny) {
        if (cfg.snap) {
            nx = Math.round(nx / PadConfig.GRID_STEP) * PadConfig.GRID_STEP;
            ny = Math.round(ny / PadConfig.GRID_STEP) * PadConfig.GRID_STEP;
        }
        k.x = nx;
        k.y = ny;
        /* The editor shows the same rectangle the player will press, so a key
         * cannot be dragged to a place it would only be drawn in. */
        cfg.clampInto(k);
    }

    /**
     * A long press in the editor means "do something to this key", and it has to
     * be cancelled the moment the finger travels: otherwise every drag would end
     * with a menu on screen.
     */
    private void armLongPress(final PadConfig.Key k, final float nx, final float ny) {
        disarmLongPress();
        longPress = new Runnable() {
            @Override
            public void run() {
                longFired = true;
                if (editListener == null) {
                    return;
                }
                if (k != null) {
                    editListener.onKeyLongPress(k);
                } else {
                    editListener.onEmptyLongPress(nx, ny);
                }
            }
        };
        postDelayed(longPress, 500);
    }

    private void disarmLongPress() {
        if (longPress != null) {
            removeCallbacks(longPress);
            longPress = null;
        }
    }

    private boolean movedPastSlop(MotionEvent ev) {
        float slop = 12f * getResources().getDisplayMetrics().density;
        return Math.abs(ev.getX(0) - pressX) > slop
            || Math.abs(ev.getY(0) - pressY) > slop;
    }

    private static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        int w = getWidth();
        int h = getHeight();
        if (w <= 0 || h <= 0 || cfg == null) {
            return;
        }
        if (editing) {
            drawEditorGuides(c, w, h);
        }
        int base = (editing && !previewOpacity) ? 255 : (int) (cfg.opacity * 255f);
        text.setTextSize(Math.max(14f, Math.min(w, h) * 0.022f));
        for (int i = 0; i < cfg.keys.size(); i++) {
            PadConfig.Key k = cfg.keys.get(i);
            /* Every key under a finger lights up, not just the last one
             * pressed: with two thumbs on a D-pad the player has to be able to
             * see both. */
            boolean down = selected == k || padPress.isPressed(k.code, k.code2);
            cfg.rectOf(k, box);
            rect.set(box.x * w, box.y * h, (box.x + box.w) * w, (box.y + box.h) * h);
            float r = Math.min(rect.width(), rect.height()) * 0.22f;

            fill.setColor(down ? Color.argb(235, 255, 214, 64)
                               : Color.argb(base, 250, 250, 250));
            c.drawRoundRect(rect, r, r, fill);

            stroke.setColor(down ? Color.argb(255, 90, 70, 0)
                                 : Color.argb(Math.min(255, base + 90), 40, 40, 40));
            c.drawRoundRect(rect, r, r, stroke);

            if (editing && k == selected) {
                stroke.setColor(Color.argb(255, 0, 140, 255));
                stroke.setStrokeWidth(4f);
                c.drawRoundRect(rect, r, r, stroke);
                stroke.setStrokeWidth(2f);
            }

            int ink = Math.min(255, base + 150);
            float cx = rect.centerX();
            float cy = rect.centerY();
            float s = Math.min(rect.width(), rect.height()) * 0.30f;
            icon.setColor(Color.argb(ink, 20, 20, 20));
            text.setColor(Color.argb(ink, 20, 20, 20));

            if (!drawKeyIcon(c, k.code, k.code2, cx, cy, s)) {
                Paint.FontMetrics fm = text.getFontMetrics();
                float ty = cy - (fm.ascent + fm.descent) / 2f;
                c.drawText(KeyCatalog.labelOf(k.code), cx, ty, text);
            }

            /*
             * The editor still needs the name: an arrow and a fire button are
             * obvious, but A/B/C/D are not, and this is the screen where the
             * player decides which key is which.
             */
            if (editing && k == selected) {
                text.setTextSize(Math.max(12f, Math.min(w, h) * 0.018f));
                Paint.FontMetrics fm = text.getFontMetrics();
                c.drawText(label(k), cx,
                           rect.bottom + (fm.descent - fm.ascent), text);
                text.setTextSize(Math.max(14f, Math.min(w, h) * 0.022f));
            }
        }
    }

    private static boolean isArrow(int code) {
        return code == 19 || code == 20 || code == 21 || code == 22;
    }

    private static float dirX(int code) {
        return code == 21 ? -1f : (code == 22 ? 1f : 0f);
    }

    private static float dirY(int code) {
        return code == 19 ? -1f : (code == 20 ? 1f : 0f);
    }

    /** One triangle pointed along (dx, dy): a straight arrow, or a diagonal. */
    private boolean drawArrow(Canvas c, float cx, float cy, float s, float dx, float dy,
                              float halfWidth) {
        float len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len < 0.5f) {
            return false;
        }
        dx /= len;
        dy /= len;
        float bx = cx - dx * s * 0.62f;
        float by = cy - dy * s * 0.62f;
        shape.reset();
        shape.moveTo(cx + dx * s * 1.22f, cy + dy * s * 1.22f);
        shape.lineTo(bx - dy * s * halfWidth, by + dx * s * halfWidth);
        shape.lineTo(bx + dy * s * halfWidth, by - dx * s * halfWidth);
        shape.close();
        icon.setStyle(Paint.Style.FILL);
        c.drawPath(shape, icon);
        return true;
    }

    /** What the editor prints under a selected cap: 左上, not 左. */
    private static String label(PadConfig.Key k) {
        if (isArrow(k.code) && isArrow(k.code2)) {
            return (dirX(k.code) < 0 ? "左" : "右") + (dirY(k.code) < 0 ? "上" : "下");
        }
        return KeyCatalog.labelOf(k.code);
    }

    /*
     * Keys are shapes, not words.
     *
     * A pad lettered 上/下/左/右/确定 is a pad written in one language, and it
     * leans on the runtime's bitmap faces having the characters: they have no
     * arrow glyphs at all, so a triangle drawn here is both sharper and the
     * same picture on every phone.  Digits, *, # and A-D stay text - they are
     * their own icon, and translating them would be the bug.
     */
    private boolean drawKeyIcon(Canvas c, int code, int code2, float cx, float cy, float s) {
        if (isArrow(code)) {
            /*
             * A diagonal cap presses two directions, so it draws the arrow
             * between them - one triangle pointed along the sum, which is also
             * how the four straight arrows are drawn.
             */
            float dx = dirX(code) + dirX(code2);
            float dy = dirY(code) + dirY(code2);
            /* a wide wedge at 45 degrees reads as a sideways triangle; a narrow
               one reads as the corner it is */
            return drawArrow(c, cx, cy, s, dx, dy, (code2 != 0) ? 0.52f : 0.78f);
        }
        switch (code) {
            case 23:                                   /* fire: 23, and its aliases */
            case 66:
            case 62:
                icon.setStyle(Paint.Style.FILL);
                c.drawCircle(cx, cy, s * 0.85f, icon);
                return true;
            case 67:                                   /* clear */
                icon.setStyle(Paint.Style.STROKE);
                icon.setStrokeWidth(Math.max(2f, s * 0.34f));
                c.drawLine(cx - s * 0.85f, cy - s * 0.85f,
                           cx + s * 0.85f, cy + s * 0.85f, icon);
                c.drawLine(cx + s * 0.85f, cy - s * 0.85f,
                           cx - s * 0.85f, cy + s * 0.85f, icon);
                return true;
            case 82:                                   /* soft keys: the plate
                                                        * on that edge, filled */
            case 4: {
                float hw = s * 0.95f;
                float hh = s * 0.62f;
                float r = hh * 0.45f;
                icon.setStyle(Paint.Style.STROKE);
                icon.setStrokeWidth(Math.max(2f, s * 0.20f));
                inner.set(cx - hw, cy - hh, cx + hw, cy + hh);
                c.drawRoundRect(inner, r, r, icon);
                icon.setStyle(Paint.Style.FILL);
                if (code == 82) {
                    inner.set(cx - hw, cy - hh, cx - hw * 0.20f, cy + hh);
                } else {
                    inner.set(cx + hw * 0.20f, cy - hh, cx + hw, cy + hh);
                }
                c.drawRoundRect(inner, r, r, icon);
                return true;
            }
            default:
                return false;
        }
    }

    private void drawEditorGuides(Canvas c, int w, int h) {
        if (cfg.grid) {
            Paint g = guide;
            g.setColor(Color.argb(60, 0, 160, 255));
            g.setStrokeWidth(1f);
            g.setStyle(Paint.Style.STROKE);
            for (float f = PadConfig.GRID_STEP; f < 1f; f += PadConfig.GRID_STEP) {
                c.drawLine(f * w, 0, f * w, h, g);
                c.drawLine(0, f * h, w, f * h, g);
            }
        }
        /*
         * Where the notch and the system bars are, drawn whether or not the
         * grid is on: a player who cannot see the cutout has no way to
         * understand why a key refuses to go any further left.
         */
        float l = cfg.safeLeft() * w;
        float t = cfg.safeTop() * h;
        float r = cfg.safeRight() * w;
        float b = cfg.safeBottom() * h;
        if (l > 0.5f || t > 0.5f || r < w - 0.5f || b < h - 0.5f) {
            Paint s = guide;
            s.setStyle(Paint.Style.FILL);
            s.setColor(Color.argb(40, 255, 120, 0));
            if (t > 0.5f) {
                c.drawRect(0f, 0f, w, t, s);
            }
            if (b < h - 0.5f) {
                c.drawRect(0f, b, w, h, s);
            }
            if (l > 0.5f) {
                c.drawRect(0f, t, l, b, s);
            }
            if (r < w - 0.5f) {
                c.drawRect(r, t, w, b, s);
            }
            s.setStyle(Paint.Style.STROKE);
            s.setStrokeWidth(2f);
            s.setColor(Color.argb(120, 255, 170, 0));
            c.drawRect(l, t, r, b, s);
        }
    }
}
