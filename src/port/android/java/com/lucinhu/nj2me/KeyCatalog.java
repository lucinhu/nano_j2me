package com.lucinhu.nj2me;

import java.util.ArrayList;
import java.util.List;

/**
 * The keys the on-screen pad may emit, and what to call them in Chinese.
 *
 * <p>The list itself comes from the native port ({@code nativeKeyTable}), which
 * builds it from the same table {@code nj_port_android_push_key} translates
 * with.  That is deliberate: a keypad editor that offers a button the port would
 * ignore is worse than one that offers fewer buttons.  The names are ours
 * because the native labels are what fits on a keycap ("Soft L"), not what a
 * Chinese-speaking player reads in a menu.
 */
final class KeyCatalog {

    /** One assignable key: an Android key code, the same the hardware sends. */
    static final class Item {
        final int code;
        final String label;

        Item(int code, String label) {
            this.code = code;
            this.label = label;
        }
    }

    private static List<Item> cached;

    private KeyCatalog() {
    }

    static synchronized List<Item> items() {
        if (cached != null) {
            return cached;
        }
        List<Item> out = new ArrayList<Item>();
        String[] lines = null;
        try {
            lines = GameActivity.nativeKeyTable();
        } catch (UnsatisfiedLinkError e) {
            lines = null;              /* no library: fall back to the basics */
        }
        if (lines != null && lines.length > 0) {
            for (int i = 0; i < lines.length; i++) {
                int colon = lines[i].indexOf(':');
                if (colon <= 0) {
                    continue;
                }
                int code;
                try {
                    code = Integer.parseInt(lines[i].substring(0, colon));
                } catch (NumberFormatException e) {
                    continue;
                }
                String nativeLabel = lines[i].substring(colon + 1);
                out.add(new Item(code, zh(code, nativeLabel)));
            }
            /* Two Android codes map to the same MIDP key (23/66/62 are all the
             * fire button).  Offering three "确定" keys is noise, so keep the
             * first and drop the aliases. */
            for (int i = 0; i < out.size(); i++) {
                for (int j = out.size() - 1; j > i; j--) {
                    if (out.get(j).label.equals(out.get(i).label)) {
                        out.remove(j);
                    }
                }
            }
        }
        if (out.isEmpty()) {
            out.add(new Item(19, "上"));
            out.add(new Item(20, "下"));
            out.add(new Item(21, "左"));
            out.add(new Item(22, "右"));
            out.add(new Item(23, "确定"));
            out.add(new Item(4, "右软键"));
            out.add(new Item(82, "左软键"));
            out.add(new Item(67, "清除"));
        }
        cached = out;
        return cached;
    }

    static String labelOf(int code) {
        List<Item> all = items();
        for (int i = 0; i < all.size(); i++) {
            if (all.get(i).code == code) {
                return all.get(i).label;
            }
        }
        return String.valueOf(code);
    }

    /** Chinese name for a key the port knows; the native label is the floor. */
    private static String zh(int code, String fallback) {
        switch (code) {
            case 19: return "上";
            case 20: return "下";
            case 21: return "左";
            case 22: return "右";
            case 23: return "确定";
            case 66: return "确定";
            case 62: return "确定";
            case 4:  return "右软键";
            case 82: return "左软键";
            case 67: return "清除";
            /* The two the pad can carry that the handset did not have a MIDP
             * code for: they arrive as the platform's own volume key codes. */
            case 24: return "音量+";
            case 25: return "音量-";
            case 17: return "*";
            case 18: return "#";
            default:
                if (code >= 7 && code <= 16) {
                    return String.valueOf(code - 7);
                }
                if (code >= 29 && code <= 32) {
                    return String.valueOf((char) ('A' + code - 29));
                }
                return fallback;
        }
    }
}
