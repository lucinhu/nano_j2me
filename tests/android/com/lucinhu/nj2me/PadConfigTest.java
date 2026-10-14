package com.lucinhu.nj2me;

import java.util.List;

/**
 * The built-in keypad layouts and the safe area, off the device.
 *
 * <p>What is checked here is what a player would notice immediately: a keycap
 * half off the screen or under the notch, two caps on top of each other (only
 * one of them can ever be pressed, so the other is a decoration that looks like
 * a button), and a preset picker whose rows do not all lead somewhere.
 */
public final class PadConfigTest {

    private PadConfigTest() {
    }

    /** Rectangles overlap by more than a rounding error. */
    private static boolean overlap(PadConfig.Key a, PadConfig.Key b) {
        return a.x + 1e-6f < b.x + b.w && b.x + 1e-6f < a.x + a.w
                && a.y + 1e-6f < b.y + b.h && b.y + 1e-6f < a.y + a.h;
    }

    /** Float arithmetic on fractions does not land on the decimal exactly. */
    private static boolean close(float got, float want) {
        return Math.abs(got - want) < 1e-4f;
    }

    private static void wellFormed(String name, PadConfig c) {
        boolean inside = true;
        boolean sized = true;
        boolean distinct = true;
        String overlap = "none";
        List<PadConfig.Key> keys = c.keys;
        for (int i = 0; i < keys.size(); i++) {
            PadConfig.Key k = keys.get(i);
            if (k.x < 0f || k.y < 0f || k.x + k.w > 1f || k.y + k.h > 1f) {
                inside = false;
            }
            if (k.w < 0.02f || k.h < 0.02f) {
                sized = false;
            }
            for (int j = i + 1; j < keys.size(); j++) {
                PadConfig.Key o = keys.get(j);
                /* On a handset the diagonals ARE the digit keys 1/3/7/9, so
                 * the one preset that shows both blocks repeats them on
                 * purpose; everywhere else a repeat is a mistake. */
                if (o.code == k.code && c.preset != PadConfig.PRESET_CLASSIC) {
                    distinct = false;
                }
                if (overlap(k, o)) {
                    overlap = k.code + "/" + o.code;
                }
            }
        }
        Check.isTrue(name + ": keys stay on the screen", inside);
        Check.isTrue(name + ": keys are big enough to hit", sized);
        Check.isTrue(name + ": no key repeats", distinct);
        Check.eq(name + ": no key sits under another", overlap, "none");
        Check.isTrue(name + ": the layout has keys", keys.size() > 0);
    }

    /**
     * Sane on a phone, whichever way up it is.
     *
     * A preset is written in units of the screen's short side, so the same cap
     * must come out the same number of PIXELS on a portrait and a landscape
     * window: that is what stops a pad looking stretched when the phone turns.
     * The old presets were raw window fractions and failed exactly this.
     */
    private static void bothOrientations(int id, String name) {
        int[][] screens = { { 1080, 1920 }, { 1920, 1080 }, { 320, 640 }, { 640, 320 } };
        boolean reachable = true;
        boolean shaped = true;

        for (int[] s : screens) {
            PadConfig c = PadConfig.byId(id, s[0], s[1]);
            for (PadConfig.Key k : c.keys) {
                float px = k.x * s[0];
                float py = k.y * s[1];
                float pw = k.w * s[0];
                float ph = k.h * s[1];
                /* 16 px is the floor: a dense layout (digits plus an
                 * eight-way cluster) on the 320 px screen of an old handset
                 * has to shrink that far, and 16 px is still a target. */
                if (pw < 16f || ph < 16f || px < -0.5f || py < -0.5f
                        || px + pw > s[0] + 0.5f || py + ph > s[1] + 0.5f) {
                    reachable = false;
                }
                /* A keycap is written in short-side units, so its SHAPE is the
                 * same whichever way the phone is held.  Raw window fractions
                 * (which is what these presets used to be) gave a 4:1 cap on
                 * one orientation and a 1:4 cap on the other. */
                float aspect = pw / ph;
                if (aspect < 0.35f || aspect > 3.6f) {
                    shaped = false;
                }
            }
        }
        Check.isTrue(name + ": every key is reachable on portrait, landscape and 320x640", reachable);
        Check.isTrue(name + ": a key keeps its shape whichever way up the phone is", shaped);
    }

    public static void main(String[] args) {
        Check.section("every preset the picker offers is a usable layout");
        for (int id : PadConfig.PRESET_IDS) {
            PadConfig c = PadConfig.byId(id);
            Check.eq("byId(" + id + ") is that preset", c.preset, id);
            Check.isTrue("preset " + id + " has a name", PadConfig.presetName(id).length() > 0);
            Check.isTrue("preset " + id + " has a hint", PadConfig.presetHint(id).length() > 0);
            if (id != PadConfig.PRESET_CUSTOM) {
                wellFormed(PadConfig.presetName(id), c);
                bothOrientations(id, PadConfig.presetName(id));
            }
        }
        Check.eq("清空 really is empty", PadConfig.byId(PadConfig.PRESET_CUSTOM).keys.size(), 0);
        Check.eq("the default layout is the D-pad preset",
                 PadConfig.defaults().preset, PadConfig.PRESET_DPAD);

        Check.section("the topmost key is the one a touch lands on");
        PadConfig c = PadConfig.blank();
        c.keys.add(new PadConfig.Key(19, 0.10f, 0.10f, 0.40f, 0.40f));
        c.keys.add(new PadConfig.Key(20, 0.20f, 0.20f, 0.40f, 0.40f));
        Check.eq("the later key wins", c.hit(0.30f, 0.30f).code, 20);
        Check.eq("outside both is the game's", c.hit(0.95f, 0.95f), null);

        Check.section("a keycap the notch reaches is moved, not lost");
        c = PadConfig.blank();
        c.keys.add(new PadConfig.Key(19, 0.00f, 0.00f, 0.20f, 0.10f));
        c.setSafeArea(0.10f, 0.05f, 0.90f, 0.95f);
        PadConfig.Box box = new PadConfig.Box();
        c.rectOf(c.keys.get(0), box);
        Check.eq("it is pushed clear of the cutout", box.x + "," + box.y, "0.1,0.05");
        Check.eq("its size is untouched", box.w + "x" + box.h, "0.2x0.1");
        Check.eq("a touch where it is drawn presses it", c.hit(0.15f, 0.08f).code, 19);
        Check.eq("a touch where it was stored does not", c.hit(0.05f, 0.02f), null);

        c.keys.add(new PadConfig.Key(20, 0.80f, 0.90f, 0.20f, 0.10f));
        c.rectOf(c.keys.get(1), box);
        Check.isTrue("the right/bottom edge is respected too",
                close(box.x, 0.70f) && close(box.y, 0.85f));
        Check.eq("the stored layout is not rewritten by drawing",
                 c.keys.get(1).x + "," + c.keys.get(1).y, "0.8,0.9");

        c.clampInto(c.keys.get(1));
        Check.isTrue("the editor's drag does rewrite it",
                close(c.keys.get(1).x, 0.70f) && close(c.keys.get(1).y, 0.85f));

        Check.section("a nonsense safe area is ignored, not obeyed");
        c.setSafeArea(0.5f, 0.5f, 0.5f, 0.5f);
        c.rectOf(c.keys.get(0), box);
        Check.eq("an empty rectangle falls back to the whole view",
                 c.safeLeft() + "," + c.safeTop() + "," + c.safeRight() + "," + c.safeBottom(),
                 "0.0,0.0,1.0,1.0");
        Check.eq("and the key is where it was stored", box.x, c.keys.get(0).x);
        c.setSafeArea(0f, 0f, 1.2f, 1f);
        Check.eq("one outside the view falls back too", c.safeRight(), 1.0f);

        Check.section("the classic handset's diagonals are two keys at once");
        PadConfig classic = PadConfig.byId(PadConfig.PRESET_CLASSIC);
        int[][] pairs = { { 21, 19 }, { 22, 19 }, { 21, 20 }, { 22, 20 } };
        for (int[] pair : pairs) {
            boolean found = false;
            for (int i = 0; i < classic.keys.size(); i++) {
                PadConfig.Key k = classic.keys.get(i);
                if (k.code == pair[0] && k.code2 == pair[1]) {
                    found = true;
                }
            }
            Check.isTrue("经典手机 has a " + pair[0] + "+" + pair[1] + " diagonal", found);
        }
        int combos = 0;
        for (int i = 0; i < classic.keys.size(); i++) {
            if (classic.keys.get(i).code2 != 0) {
                combos++;
            }
        }
        Check.eq("exactly the four diagonals are combos", combos, 4);
        PadConfig.Key diag = classic.keys.get(classic.keys.size() - 1);
        Check.eq("a copy keeps the second code", diag.copy().code2, diag.code2);
        Check.eq("a key with no combo copies as one", classic.keys.get(0).copy().code2, 0);
        Check.eq("an untouched keypad is半透明", PadConfig.defaults().opacity, 0.50f);

        Check.section("a saved 经典手机 picks up the two-key diagonals");
        PadConfig old = PadConfig.blank();
        old.keys.add(new PadConfig.Key(8, 0.010f, 0.560f, 0.155f, 0.098f));   /* 1, left */
        old.keys.add(new PadConfig.Key(8, 0.506f, 0.664f, 0.155f, 0.098f));   /* ↖, right */
        old.keys.add(new PadConfig.Key(23, 0.667f, 0.768f, 0.155f, 0.098f));  /* fire */
        PadConfig.upgradeClassicDiagonals(old.keys);
        Check.eq("the digit block keeps its 1", old.keys.get(0).code, 8);
        Check.eq("and stays a single key", old.keys.get(0).code2, 0);
        Check.eq("the corner key becomes left", old.keys.get(1).code, 21);
        Check.eq("plus up", old.keys.get(1).code2, 19);
        Check.eq("the fire button is untouched", old.keys.get(2).code, 23);
        PadConfig.upgradeClassicDiagonals(old.keys);
        Check.eq("running it twice changes nothing", old.keys.get(1).code2, 19);

        Check.section("the first pad is the size of the handset it copies");
        PadConfig first = PadConfig.byId(PadConfig.PRESET_DPAD, 1080, 1920);
        PadConfig handset = PadConfig.byId(PadConfig.PRESET_CLASSIC, 1080, 1920);
        float firstCap = 0f;
        float handsetCap = 0f;
        for (int i = 0; i < first.keys.size(); i++) {
            firstCap = Math.max(firstCap, first.keys.get(i).w);
        }
        for (int i = 0; i < handset.keys.size(); i++) {
            handsetCap = Math.max(handsetCap, handset.keys.get(i).w);
        }
        PadConfig.Key fire = null;
        PadConfig.Key capKey = null;
        for (int i = 0; i < first.keys.size(); i++) {
            PadConfig.Key k = first.keys.get(i);
            if (k.code == 23) {
                fire = k;
            }
            if (k.code == 19) {
                capKey = k;
            }
        }
        /*
         * The rule the player asked for: the pad they meet first is the size
         * of the old phone's keypad, not a screen-filling bar.  The old 确定
         * button was 0.30 x 0.20 u - more than twice a keycap in each
         * direction - so the fire button is checked against the caps too.
         */
        Check.isTrue("a keycap is about a tenth of the screen wide",
                firstCap > 0.09f && firstCap < 0.18f);
        Check.isTrue("it is within a fifth of the handset's keycap",
                firstCap > handsetCap * 0.8f && firstCap < handsetCap * 1.25f);
        Check.isTrue("the fire button is a keycap, not a bar",
                fire != null && capKey != null
                        && Math.abs(fire.w - capKey.w) < 1e-4f
                        && Math.abs(fire.h - capKey.h) < 1e-4f);
        Check.isTrue("and it is square on the screen",
                fire != null && Math.abs(fire.w * 1080f - fire.h * 1920f) < 2f);
        Check.isTrue("nothing on the first pad is wider than the keycap",
                firstCap > 0f && firstCap <= handsetCap * 1.25f);

        Check.section("the menu button has a place and a switch");
        PadConfig pad = PadConfig.defaults();
        Check.eq("a fresh pad shows the menu button", pad.menuVisible, true);
        Check.isTrue("and puts it out of the way",
                pad.menuX > 0.5f && pad.menuX < 1f && pad.menuY >= 0f && pad.menuY < 0.5f);
        pad.menuX = 0.2f;
        pad.menuY = 0.7f;
        pad.menuVisible = false;
        PadConfig back = PadConfig.parse(pad.blob());
        Check.isTrue("the place survives a save",
                back != null && close(back.menuX, 0.2f) && close(back.menuY, 0.7f));
        Check.eq("and so does the switch", back.menuVisible, false);
        PadConfig older = PadConfig.parse("8,0.1,0.6,0.13,0.07;");
        Check.isTrue("a sidecar from before the key still loads", older != null);
        Check.eq("with the button shown", older.menuVisible, true);
        PadConfig copied = pad.copy();
        Check.isTrue("a copy carries the place and the switch",
                close(copied.menuX, 0.2f) && close(copied.menuY, 0.7f)
                        && !copied.menuVisible);
        PadConfig into = PadConfig.blank();
        into.replaceWith(pad);
        Check.isTrue("replaceWith carries them too",
                close(into.menuX, 0.2f) && !into.menuVisible);
        PadConfig insets = PadConfig.defaults();
        insets.setSafeArea(0.10f, 0.05f, 0.95f, 0.90f);
        insets.menuX = 0f;
        insets.menuY = 0f;
        insets.clampMenuInto(0.11f, 0.06f);
        Check.isTrue("the handle cannot be dragged under the notch",
                insets.menuX >= 0.10f - 1e-4f && insets.menuY >= 0.05f - 1e-4f);
        insets.menuX = 1f;
        insets.menuY = 1f;
        insets.clampMenuInto(0.11f, 0.06f);
        Check.isTrue("nor off the bottom-right corner",
                insets.menuX + 0.11f <= 0.95f + 1e-4f
                        && insets.menuY + 0.06f <= 0.90f + 1e-4f);

        Check.section("a fresh layout is the whole view");
        c = PadConfig.blank();
        Check.eq("no notch, no reserved edges",
                 c.safeLeft() + "," + c.safeTop() + "," + c.safeRight() + "," + c.safeBottom(),
                 "0.0,0.0,1.0,1.0");
        Check.eq("and keys do not move", c.hit(0.5f, 0.5f), null);

        Check.done();
    }
}
