package com.lucinhu.nj2me;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.zip.CRC32;

/**
 * Where the .jar files live and what we remember about each one.
 *
 * <p>Everything is under the app's private files directory, which is the only
 * place on a modern Android that needs no permission and no storage framework
 * dance.  A jar arrives either through the document picker (copied in) or
 * through {@code adb push} (already there); both end up in the same folder, so
 * the list shows both and the tools below can seed a device without the UI.
 *
 * <p>Per-game settings live in a small sidecar text file rather than in
 * SharedPreferences, so that deleting a jar takes its settings with it and
 * copying the folder to another phone carries them along.
 */
final class JarStore {

    static final String DIR = "jars";
    static final int DEF_W = 240;
    static final int DEF_H = 320;
    static final int DEF_HEAP_MB = 8;

    /** Screen sizes worth offering before asking for a custom one. */
    static final int[][] PRESETS = {
        { 128, 128 }, { 128, 160 }, { 176, 208 }, { 176, 220 }, { 208, 208 },
        { 240, 320 }, { 320, 240 }, { 360, 640 }, { 480, 800 }
    };

    static final class Entry {
        File jar;
        String title;
        /*
         * True when the title is the player's own, rather than the MIDlet's
         * name read out of the jar.  Only that kind is written to the sidecar:
         * a derived title must stay derived, or re-importing a renamed jar
         * would keep showing the previous manifest's name for ever.
         */
        boolean titleSet;
        int w = DEF_W;
        int h = DEF_H;
        int heapMB = DEF_HEAP_MB;
        /** Game speed in percent of real time: 100 is real time. */
        int speed = 100;
        long size;
        long touched;

        /*
         * How the picture sat in the window the last time this game ran, and
         * what the port measured it to paint.  Remembered per game because they
         * are properties of the game, not of the app: a 128x128 title wants the
         * screen filled, a 240x320 one does not.
         */
        int viewMode;
        float zoom = 1.0f;
        float panX, panY;
        int detW, detH;

        String sizeText() {
            if (size >= 1024 * 1024) {
                return String.format(java.util.Locale.US, "%.1f MB", size / 1048576.0);
            }
            return (size / 1024) + " KB";
        }

        String screenText() {
            return w + "x" + h + " · " + heapMB + "MB";
        }

        /** What the port measured the MIDlet to paint, or null if never run. */
        String detText() {
            return detW > 0 && detH > 0 ? detW + "x" + detH : null;
        }

        boolean isCustomView() {
            return viewMode == 2;
        }
    }

    private JarStore() {
    }

    /**
     * The jar folder.  External app storage when the device has it, because that
     * is the one directory a user can reach with a cable and one adb command
     * ("adb push game.jar /sdcard/Android/data/<pkg>/files/jars/") without the
     * app asking for a single permission.  Internal storage is the fallback for
     * devices with no external volume.
     */
    static File dir(Context ctx) {
        File base = ctx.getExternalFilesDir(null);
        if (base == null) {
            base = ctx.getFilesDir();
        }
        File d = new File(base, DIR);
        if (!d.isDirectory()) {
            d.mkdirs();
        }
        return d;
    }

    /** Directory the MIDlet sees as its file:// root, and where RMS goes. */
    static File fsRoot(Context ctx) {
        File d = new File(ctx.getFilesDir(), "fs");
        d.mkdirs();
        return d;
    }

    static File rmsRoot(Context ctx) {
        File d = new File(ctx.getFilesDir(), "rms");
        d.mkdirs();
        return d;
    }

    static List<Entry> list(Context ctx) {
        File[] files = dir(ctx).listFiles();
        List<Entry> out = new ArrayList<Entry>();
        if (files == null) {
            return out;
        }
        for (int i = 0; i < files.length; i++) {
            String name = files[i].getName();
            if (!files[i].isFile() || !name.toLowerCase(java.util.Locale.US).endsWith(".jar")) {
                continue;
            }
            out.add(read(ctx, files[i]));
        }
        Collections.sort(out, new Comparator<Entry>() {
            @Override
            public int compare(Entry a, Entry b) {
                return b.touched == a.touched ? 0 : (b.touched > a.touched ? 1 : -1);
            }
        });
        return out;
    }

    static Entry read(Context ctx, File jar) {
        Entry e = new Entry();
        e.jar = jar;
        e.size = jar.length();
        e.touched = playedAt(jar);
        String file = jar.getName();
        String base = file.toLowerCase(java.util.Locale.US).endsWith(".jar")
                    ? file.substring(0, file.length() - 4) : file;
        /*
         * The name a game is shown under comes from the game: a MIDlet declares
         * its own name in the manifest, and a list of file names is a list of
         * strings somebody typed into a download folder.  The file's name is
         * the fallback for the jars that declare nothing, and it stays on the
         * row's second line either way.
         */
        String own = JarIcons.displayName(jar);
        File cfg = cfgOf(jar);
        String savedTitle = null;
        boolean savedFlag = false;
        boolean haveFlag = false;
        if (cfg.isFile()) {
            try {
                List<String> lines = readLines(cfg);
                for (int i = 0; i < lines.size(); i++) {
                    String line = lines.get(i);
                    int eq = line.indexOf('=');
                    if (eq <= 0) {
                        continue;
                    }
                    String k = line.substring(0, eq).trim();
                    String v = line.substring(eq + 1).trim();
                    if (k.equals("title")) {
                        savedTitle = v.length() > 0 ? v : savedTitle;
                    } else if (k.equals("titleset")) {
                        haveFlag = true;
                        savedFlag = v.equals("1") || v.equalsIgnoreCase("true");
                    } else if (k.equals("w")) {
                        e.w = parseInt(v, DEF_W);
                    } else if (k.equals("h")) {
                        e.h = parseInt(v, DEF_H);
                    } else if (k.equals("heap")) {
                        e.heapMB = parseInt(v, DEF_HEAP_MB);
                    } else if (k.equals("speed")) {
                        e.speed = parseInt(v, 100);
                    } else if (k.equals("view")) {
                        e.viewMode = parseInt(v, 0);
                    } else if (k.equals("zoom")) {
                        e.zoom = parseFloat(v, 1.0f);
                    } else if (k.equals("panx")) {
                        e.panX = parseFloat(v, 0f);
                    } else if (k.equals("pany")) {
                        e.panY = parseFloat(v, 0f);
                    } else if (k.equals("detw")) {
                        e.detW = parseInt(v, 0);
                    } else if (k.equals("deth")) {
                        e.detH = parseInt(v, 0);
                    }
                }
                /*
                 * Which name wins, and whether the sidecar's title is the
                 * player's own or a stale copy of the file name, is decided in
                 * JarName so it can be tested off the device.
                 */
            } catch (IOException ioe) {
                /* keep the defaults; a broken sidecar must not hide the game */
            }
        }
        e.title = JarName.chooseTitle(base, own, savedTitle, haveFlag, savedFlag);
        e.titleSet = JarName.renamed(base, savedTitle, haveFlag, savedFlag);
        return e;
    }

    static void write(Context ctx, Entry e) {
        File cfg = cfgOf(e.jar);
        try {
            OutputStream os = new FileOutputStream(cfg);
            /* title= only when the player named it: the MIDlet's own name is
             * read from the jar every time, so a re-import cannot inherit a
             * stale one. */
            String text = (e.titleSet ? "title=" + e.title + "\ntitleset=1\n" : "")
                        + "w=" + e.w + "\nh=" + e.h
                        + "\nheap=" + e.heapMB + "\nview=" + e.viewMode
                        + "\nspeed=" + e.speed
                        + "\nzoom=" + e.zoom + "\npanx=" + e.panX
                        + "\npany=" + e.panY + "\ndetw=" + e.detW
                        + "\ndeth=" + e.detH + "\n";
            os.write(text.getBytes("UTF-8"));
            os.close();
        } catch (IOException ioe) {
            /* the game still runs with defaults on the next launch */
        }
    }

    /**
     * Remember what the port measured this game to paint, and - the first time
     * it is known - adopt it as the screen size.
     *
     * <p>Adopting it is what makes a 128x128 title stop being a 240x320 canvas
     * with a small picture on it: the launcher could not know the real size
     * before, because 6 of the 395 jars in this project's corpus declare one.
     * Only fills in a size that is still the default, so a size the player
     * chose by hand is never overwritten by a measurement.
     */
    static void noteDetected(Context ctx, String jarName, int w, int h) {
        if (jarName == null || w < 16 || h < 16) {
            return;
        }
        File jar = new File(dir(ctx), jarName);
        if (!jar.isFile()) {
            return;
        }
        Entry e = read(ctx, jar);
        if (e.detW == w && e.detH == h) {
            return;
        }
        e.detW = w;
        e.detH = h;
        if (e.w == DEF_W && e.h == DEF_H) {
            e.w = w;
            e.h = h;
        }
        write(ctx, e);
    }

    static void delete(Entry e) {
        e.jar.delete();
        cfgOf(e.jar).delete();
    }

    /**
     * Copy a document-picker result into the private folder.
     *
     * <p>Returned as an {@link Entry} so the caller can rename it and pick a
     * screen size before the first launch; the name is sanitized because it
     * becomes a file name, a list row and a MIDlet title all at once.
     */
    static Entry importUri(Context ctx, Uri uri) throws IOException {
        String display = uri.getLastPathSegment();
        if (display == null) {
            display = "game.jar";
        }
        int slash = display.lastIndexOf('/');
        if (slash >= 0) {
            display = display.substring(slash + 1);
        }
        String base = display.toLowerCase(java.util.Locale.US).endsWith(".jar")
                    ? display.substring(0, display.length() - 4) : display;
        base = sanitize(base);
        if (base.length() == 0) {
            base = "game";
        }
        InputStream in = ctx.getContentResolver().openInputStream(uri);
        if (in == null) {
            throw new IOException("cannot open " + uri);
        }
        /*
         * Stream into a temporary name first, because whether this is a new
         * game can only be told from the whole file.
         *
         * A jar this app already has is not a second game: it is the same game
         * with "(2)" in its name, a second icon, and a second set of screen and
         * keypad settings.  A file manager that re-sends its intent - or a
         * player who taps the same file twice - must land on the row that is
         * already there.
         */
        File tmp = new File(dir(ctx), "." + base + ".import");
        long size;
        long crc;
        CRC32 c = new CRC32();
        OutputStream out = new FileOutputStream(tmp);
        byte[] buf = new byte[64 * 1024];
        int n;
        try {
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                c.update(buf, 0, n);
            }
        } finally {
            try { in.close(); } catch (IOException ignored) { }
            out.close();
        }
        size = tmp.length();
        crc = c.getValue();
        Entry same = findSame(ctx, size, crc);
        if (same != null) {
            tmp.delete();
            return same;
        }
        File jar = uniqueFile(ctx, base);
        if (!tmp.renameTo(jar)) {
            copyFile(tmp, jar);
            tmp.delete();
        }
        Entry e = read(ctx, jar);
        write(ctx, e);
        return e;
    }

    /**
     * An existing jar with the same bytes, or null.
     *
     * <p>Size first: the CRC is only computed for files that could match, which
     * keeps a list of sixty games from hashing sixty megabytes every import.
     */
    private static Entry findSame(Context ctx, long size, long crc) {
        List<Entry> all = list(ctx);
        for (int i = 0; i < all.size(); i++) {
            File f = all.get(i).jar;
            if (f.length() != size) {
                continue;
            }
            if (crcOf(f) == crc) {
                return all.get(i);
            }
        }
        return null;
    }

    private static long crcOf(File f) {
        CRC32 c = new CRC32();
        InputStream in = null;
        try {
            in = new java.io.FileInputStream(f);
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                c.update(buf, 0, n);
            }
        } catch (IOException ioe) {
            return -1;
        } finally {
            if (in != null) {
                try { in.close(); } catch (IOException ignored) { }
            }
        }
        return c.getValue();
    }

    private static void copyFile(File from, File to) throws IOException {
        InputStream in = new java.io.FileInputStream(from);
        try {
            OutputStream out = new FileOutputStream(to);
            try {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }

    /** A jar under {@code files/jars} that adb pushed, by name. */
    static Entry byName(Context ctx, String name) {
        File f = new File(dir(ctx), name);
        if (!f.isFile()) {
            return null;
        }
        return read(ctx, f);
    }

    /**
     * Mark a game as just played so it sorts to the top of the list.  The list
     * is ordered by this stamp, not by file mtime, because copying a jar in
     * resets mtime and a game the player uses daily should not sink.
     */
    static void touch(Context ctx, Entry e) {
        File stamp = new File(dir(ctx), "." + e.jar.getName() + ".played");
        try {
            if (stamp.exists() || stamp.createNewFile()) {
                stamp.setLastModified(System.currentTimeMillis());
            }
        } catch (IOException ioe) {
            e.jar.setLastModified(System.currentTimeMillis());
        }
    }

    private static long playedAt(File jar) {
        File stamp = new File(jar.getParentFile(), "." + jar.getName() + ".played");
        if (stamp.isFile()) {
            return stamp.lastModified();
        }
        return jar.lastModified();
    }

    private static File cfgOf(File jar) {
        return new File(jar.getParentFile(), jar.getName() + ".cfg");
    }

    private static File uniqueFile(Context ctx, String base) {
        File f = new File(dir(ctx), base + ".jar");
        for (int i = 2; f.exists() && i < 1000; i++) {
            f = new File(dir(ctx), base + " (" + i + ").jar");
        }
        return f;
    }

    private static String sanitize(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length() && sb.length() < 64; i++) {
            char c = s.charAt(i);
            /* '/' and '\0' would break paths; the rest is cosmetic. */
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|' || c < ' ') {
                sb.append('_');
            } else {
                sb.append(c);
            }
        }
        return sb.toString().trim();
    }

    /*
     * Zero and negatives fall back to the default, which is right for every key
     * that uses this: a screen of 0x0 or a heap of 0 MB is a broken sidecar, and
     * the view mode's default is 0 anyway.
     */
    private static int parseInt(String v, int def) {
        try {
            int n = Integer.parseInt(v.trim());
            return n > 0 ? n : def;
        } catch (NumberFormatException e) {
            return def;
        }
    }

    private static float parseFloat(String v, float def) {
        try {
            return Float.parseFloat(v.trim());
        } catch (NumberFormatException e) {
            return def;
        }
    }

    private static List<String> readLines(File f) throws IOException {
        InputStream in = new java.io.FileInputStream(f);
        try {
            byte[] all = new byte[(int) Math.min(f.length(), 4096)];
            int got = in.read(all);
            String text = got > 0 ? new String(all, 0, got, "UTF-8") : "";
            return Arrays.asList(text.split("\n"));
        } finally {
            in.close();
        }
    }
}
