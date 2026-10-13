package com.lucinhu.nj2me;

import java.util.ArrayList;
import java.util.List;

/**
 * The keypad's press semantics, off the device.
 *
 * <p>A thumb can only check so much: whether a key goes down with the touch
 * that lands on it, whether sliding off lets it go, and what a second finger
 * does are all decisions in {@link PadPress}.  This runs them on a desktop JVM
 * so a change that breaks one is caught before it reaches a phone.
 */
public final class PadPressTest {

    /* Two keycaps side by side: "down" (Android 20) on the left, "left" (21). */
    private static final int DOWN = 20;
    private static final int LEFT = 21;
    private static final float NX_DOWN = 0.15f;
    private static final float NX_LEFT = 0.45f;
    private static final float NY = 0.85f;
    private static final float NX_EMPTY = 0.75f;

    private PadPressTest() {
    }

    /** Records what the game would have been told. */
    private static final class Rec implements PadPress.Sink {
        private final List<String> events = new ArrayList<String>();

        @Override
        public void onKey(int androidKeyCode, boolean down) {
            events.add((down ? "key+" : "key-") + androidKeyCode);
        }

        @Override
        public void onTouch(int pointerId, int phase, float nx, float ny) {
            events.add("t" + pointerId + "." + phase);
        }

        /** Everything since the last take, in the order it happened. */
        String take() {
            String s = events.toString();
            events.clear();
            return s;
        }
    }

    private static PadConfig cfg() {
        PadConfig c = PadConfig.blank();
        c.keys.add(new PadConfig.Key(DOWN, 0.10f, 0.80f, 0.20f, 0.10f));
        c.keys.add(new PadConfig.Key(LEFT, 0.40f, 0.80f, 0.20f, 0.10f));
        return c;
    }

    public static void main(String[] args) {
        Check.section("a keycap presses with the touch that lands on it");
        Rec r = new Rec();
        PadPress p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        Check.eq("the touch that lands presses the key", r.take(), "[key+20]");
        Check.isTrue("the pad took the finger", p.isPressed(DOWN));
        p.up(1, NX_DOWN, NY);
        Check.eq("lifting releases it", r.take(), "[key-20]");
        Check.isTrue("nothing is held afterwards", p.heldCount() == 0);

        Check.section("a finger inside a keycap sends nothing");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        r.take();
        p.move(1, NX_DOWN + 0.01f, NY - 0.01f);
        Check.eq("moving within the cap is silent", r.take(), "[]");

        Check.section("sliding off a keycap releases it");
        p.move(1, NX_EMPTY, 0.20f);
        Check.eq("off the cap: the key comes up", r.take(), "[key-20]");
        p.move(1, NX_EMPTY + 0.02f, 0.20f);
        Check.eq("and stays up while the finger is off it", r.take(), "[]");
        p.move(1, NX_DOWN, NY);
        Check.eq("sliding back presses it again", r.take(), "[key+20]");
        p.up(1, NX_DOWN, NY);
        Check.eq("lifting finishes the press", r.take(), "[key-20]");

        Check.section("sliding from one keycap to the next");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        r.take();
        p.move(1, NX_LEFT, NY);
        Check.eq("the old key comes up before the new one goes down",
                 r.take(), "[key-20, key+21]");
        p.up(1, NX_LEFT, NY);
        Check.eq("only the new key is released", r.take(), "[key-21]");

        Check.section("empty screen belongs to the game");
        p = new PadPress(cfg(), r);
        p.down(1, NX_EMPTY, 0.30f);
        Check.eq("a finger on the picture is a game pointer", r.take(), "[t1.0]");
        p.move(1, NX_EMPTY - 0.05f, 0.30f);
        Check.eq("and it keeps moving", r.take(), "[t1.1]");
        p.up(1, NX_EMPTY - 0.05f, 0.30f);
        Check.eq("and is lifted", r.take(), "[t1.2]");

        Check.section("a finger the game never saw can still press a keycap");
        p = new PadPress(cfg(), r);
        p.down(1, NX_EMPTY, 0.30f);
        r.take();
        p.down(2, NX_DOWN, NY);
        Check.eq("the second finger presses", r.take(), "[key+20]");
        p.move(1, NX_EMPTY - 0.05f, 0.30f);
        Check.eq("while the game's pointer keeps moving", r.take(), "[t1.1]");
        p.up(2, NX_DOWN, NY);
        Check.eq("and releasing the second finger lets the key go", r.take(), "[key-20]");

        Check.section("sliding from the picture onto a keycap");
        p = new PadPress(cfg(), r);
        p.down(1, NX_EMPTY, 0.30f);
        r.take();
        p.move(1, NX_DOWN, NY);
        Check.eq("the game's pointer ends and the key goes down",
                 r.take(), "[t1.2, key+20]");
        p.move(1, NX_DOWN + 0.01f, NY);
        Check.eq("no more pointer events", r.take(), "[]");
        p.up(1, NX_DOWN + 0.01f, NY);
        Check.eq("lifting releases the key only", r.take(), "[key-20]");

        Check.section("two fingers, two keycaps");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        p.down(2, NX_LEFT, NY);
        Check.eq("both keys go down", r.take(), "[key+20, key+21]");
        Check.isTrue("both are held", p.isPressed(DOWN) && p.isPressed(LEFT));
        Check.eq("two keys held", p.heldCount(), 2);
        p.up(1, NX_DOWN, NY);
        Check.eq("lifting one releases only it", r.take(), "[key-20]");
        Check.isTrue("the other stays held", p.isPressed(LEFT));
        p.up(2, NX_LEFT, NY);
        Check.eq("lifting the second releases it", r.take(), "[key-21]");

        Check.section("two fingers on one keycap are one press");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        p.down(2, NX_DOWN + 0.02f, NY);
        Check.eq("no duplicate press reaches the MIDlet", r.take(), "[key+20]");
        p.up(1, NX_DOWN, NY);
        Check.eq("the first finger lifting does not release it", r.take(), "[]");
        Check.isTrue("it is still held", p.isPressed(DOWN));
        p.up(2, NX_DOWN + 0.02f, NY);
        Check.eq("the last finger lifting releases it", r.take(), "[key-20]");

        Check.section("a cancelled stream releases everything once");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        p.down(2, NX_DOWN + 0.02f, NY);
        p.down(3, NX_LEFT, NY);
        p.down(4, NX_EMPTY, 0.30f);
        r.take();
        p.cancel();
        Check.eq("each key comes up exactly once, and the pointer ends",
                 r.take(), "[key-20, key-21, t4.2]");
        Check.isTrue("nothing is held", p.heldCount() == 0);
        Check.eq("a second cancel is silent", silent(p, r), "[]");

        Check.section("the game's pointer can be dropped without telling it");
        p = new PadPress(cfg(), r);
        p.down(1, NX_EMPTY, 0.30f);
        r.take();
        p.forgetGame();
        p.move(1, NX_EMPTY - 0.05f, 0.30f);
        p.up(1, NX_EMPTY - 0.05f, 0.30f);
        Check.eq("a view-mode change ends the pointer quietly", r.take(), "[]");

        Check.section("events for fingers the pad never had");
        p = new PadPress(cfg(), r);
        p.move(9, NX_DOWN, NY);
        p.up(9, NX_DOWN, NY);
        Check.eq("moving and lifting an unknown finger does nothing", r.take(), "[]");

        Check.section("more fingers than a handset has");
        p = new PadPress(cfg(), r);
        for (int i = 0; i < 40; i++) {
            p.down(i, NX_EMPTY, 0.30f + i * 0.001f);
        }
        Check.eq("only a handset's worth of fingers are followed", r.take(), "[t0.0]");
        Check.isTrue("nothing is held", p.heldCount() == 0);
        p.cancel();
        r.take();
        p.down(100, NX_DOWN, NY);
        Check.eq("and a key works again once the flood is gone", r.take(), "[key+20]");
        Check.isTrue("with the press tracked", p.isPressed(DOWN));

        Check.section("a key held across a config change");
        p = new PadPress(cfg(), r);
        p.down(1, NX_DOWN, NY);
        r.take();
        p.setConfig(PadConfig.blank());
        p.up(1, NX_DOWN, NY);
        Check.eq("the held key is still released", r.take(), "[key-20]");

        Check.done();
    }

    private static String silent(PadPress p, Rec r) {
        p.cancel();
        return r.take();
    }
}
