package com.lucinhu.nj2me;

import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.jar.Attributes;
import java.util.jar.JarFile;
import java.util.jar.Manifest;

/**
 * The name a MIDlet calls itself, read out of the jar's own manifest.
 *
 * <p>This is the name on the game's title bar, in the launcher and in every
 * dialog about it, so it has to be the MIDlet's name and not the file's: a
 * player who copies {@code metal_slug_4_en.jar} onto the phone wants to see
 * "Metal Slug 4" in the list, exactly as the handset that shipped the game did.
 *
 * <p>No Android types here on purpose.  Manifest naming is the part of the
 * launcher that is worth testing off the device, and the desktop test harness
 * runs this class directly.
 *
 * <p>Nothing throws: a jar that is not a zip, or whose manifest no parser
 * survives, is a jar whose name is unknown, and the caller falls back to the
 * file name.
 */
final class JarName {

    /*
     * A jar's manifest is a few hundred bytes, but the launcher asks for every
     * row on every scroll frame.  Keyed by path:size:mtime so that a re-import
     * or an overwrite misses.
     */
    private static final int MAX_CACHE = 256;
    private static final HashMap<String, String> CACHE = new HashMap<String, String>();

    private JarName() {
    }

    /** The MIDlet's own name, or null when the manifest does not give one. */
    static String of(File jar) {
        if (jar == null) {
            return null;
        }
        String k = jar.getPath() + ':' + jar.length() + ':' + jar.lastModified();
        synchronized (CACHE) {
            if (CACHE.containsKey(k)) {
                return CACHE.get(k);
            }
        }
        String name = read(jar);
        synchronized (CACHE) {
            if (CACHE.size() >= MAX_CACHE) {
                CACHE.clear();
            }
            CACHE.put(k, name);
        }
        return name;
    }

    private static String read(File jar) {
        try {
            JarFile jf = new JarFile(jar);
            try {
                Manifest mf = jf.getManifest();
                return mf == null ? null : from(mf.getMainAttributes());
            } finally {
                jf.close();
            }
        } catch (IOException | RuntimeException e) {
            /* not a zip, a manifest no parser survives, or a rejected signer */
            return null;
        }
    }

    /**
     * The name out of a manifest's main attributes.
     *
     * <p>{@code MIDlet-1} is where a well-formed jar puts it, and where this
     * port reads the icon path from as well.  The wild is less tidy: some jars
     * name the suite with {@code MIDlet-Name} and put the class in
     * {@code MIDlet-1}, and a handful have no {@code MIDlet-1} at all and
     * describe the MIDlet as {@code MIDlet-2}.  All three mean the same thing
     * to a player, so all three are read here.
     */
    static String from(Attributes at) {
        if (at == null) {
            return null;
        }
        String n = clean(first(at.getValue("MIDlet-1")));
        if (n != null) {
            return n;
        }
        n = clean(at.getValue("MIDlet-Name"));
        if (n != null) {
            return n;
        }
        for (int i = 2; i <= 9; i++) {
            n = clean(first(at.getValue("MIDlet-" + i)));
            if (n != null) {
                return n;
            }
        }
        return null;
    }

    /**
     * The name a launcher row should show.
     *
     * <p>Order: what the player typed, then what the jar calls itself, then the
     * file's name.  A player who renames a game means it, so their name wins
     * over the manifest forever after.
     */
    static String chooseTitle(String base, String own, String saved,
                              boolean haveFlag, boolean savedFlag) {
        return renamed(base, saved, haveFlag, savedFlag) ? saved
             : (own != null ? own : base);
    }

    /**
     * Whether the sidecar's {@code title=} is the player's own name.
     *
     * <p>Sidecars written before names were derived from the jar carry the file
     * name in {@code title=}, and adopting that as a rename would freeze the
     * very thing this fixes.  So an older sidecar, one with no
     * {@code titleset} line, counts as a rename only when its title differs
     * from the file's name.
     */
    static boolean renamed(String base, String saved, boolean haveFlag, boolean savedFlag) {
        if (saved == null) {
            return false;
        }
        return haveFlag ? savedFlag : !saved.equals(base);
    }

    /** "Name, icon.png, Class" -> "Name". */
    private static String first(String v) {
        if (v == null) {
            return null;
        }
        int comma = v.indexOf(',');
        return comma < 0 ? v : v.substring(0, comma);
    }

    private static String clean(String s) {
        if (s == null) {
            return null;
        }
        String t = s.trim();
        if (t.length() >= 2 && (t.charAt(0) == '"' && t.charAt(t.length() - 1) == '"')) {
            t = t.substring(1, t.length() - 1).trim();
        }
        if (t.length() == 0 || t.equalsIgnoreCase("null") || t.equals("-")) {
            return null;
        }
        return t;
    }
}
