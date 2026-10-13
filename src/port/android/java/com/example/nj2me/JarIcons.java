package com.example.nj2me;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.LruCache;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.jar.JarFile;
import java.util.jar.Manifest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * A game's name and icon, read out of the game's own jar.
 *
 * <p>Jars misbehave in the wild - 7 of the 395 in this corpus throw from
 * JarFile - so every path here degrades to a drawn letter rather than throwing.
 */
final class JarIcons {

    /* 64 is the whole corpus; a launcher must not cache without bound. */
    private static final int MAX_INFO = 64;
    /* Real icons are 16x16..64x64, so a bigger entry is a game resource. */
    private static final long MAX_ICON = 512 * 1024;
    private static final int CACHE_BYTES = 4 * 1024 * 1024;

    /* {midletName, iconPath} by path:size:mtime.  A null value records a jar
     * already tried, so a broken one is not re-parsed on every scroll frame. */
    private static final HashMap<String, String[]> INFO = new HashMap<String, String[]>();

    /* Icons, which a list scroll asks for over and over. */
    private static final LruCache<String, Bitmap> ICONS =
            new LruCache<String, Bitmap>(CACHE_BYTES) {
                @Override
                protected int sizeOf(String k, Bitmap v) {
                    return v.getByteCount();
                }
            };

    private JarIcons() {
    }

    /** {midletName, iconPath} from MIDlet-1, or null when the jar has no usable manifest. */
    static String[] info(File jar) {
        if (jar == null) {
            return null;
        }
        /* The parse runs under the lock on purpose: the list binds on one
         * thread, and this is the thing that must not repeat per frame. */
        synchronized (INFO) {
            String k = id(jar);
            String[] got = INFO.get(k);
            if (!INFO.containsKey(k)) {
                got = midlet1(jar);
                if (INFO.size() >= MAX_INFO) {
                    INFO.clear();
                }
                INFO.put(k, got);
            }
            return got == null ? null : new String[] { got[0], got[1] };
        }
    }

    private static String[] midlet1(File jar) {
        try {
            JarFile jf = new JarFile(jar);
            try {
                Manifest mf = jf.getManifest();
                String v = mf == null ? null : mf.getMainAttributes().getValue("MIDlet-1");
                String[] f = v == null ? new String[0] : v.split(",");
                String name = f.length > 0 ? f[0].trim() : "";
                String icon = f.length > 1 ? f[1].trim() : "";
                return new String[] { name.length() > 0 ? name : null,
                                      icon.length() > 0 ? icon : null };
            } finally {
                jf.close();
            }
        } catch (IOException | RuntimeException e) {
            /* not a zip, a manifest no parser survives, or a rejected signer */
            return null;
        }
    }

    /** The icon decoded at a size that fits {@code px}, or null when the jar has no readable icon. */
    static Bitmap load(File jar, String iconPath, int px) {
        if (jar == null || iconPath == null || px <= 0) {
            return null;
        }
        String k = "I|" + id(jar) + '|' + iconPath + '|' + px;
        Bitmap bm = ICONS.get(k);
        if (bm != null) {
            return bm;
        }
        try {
            ZipFile zf = new ZipFile(jar);
            try {
                ZipEntry ze = find(zf, iconPath);
                if (ze == null || ze.isDirectory() || ze.getSize() > MAX_ICON) {
                    return null;
                }
                byte[] buf = new byte[(int) (ze.getSize() > 0 ? ze.getSize() : MAX_ICON)];
                InputStream in = zf.getInputStream(ze);
                int n = 0;
                try {
                    int got;
                    while (n < buf.length && (got = in.read(buf, n, buf.length - n)) > 0) {
                        n += got;
                    }
                } finally {
                    in.close();
                }
                bm = BitmapFactory.decodeByteArray(buf, 0, n);
                if (bm == null) {
                    return null;   /* an icon variant this decoder will not take */
                }
                int w = bm.getWidth();
                int h = bm.getHeight();
                if (w > px || h > px) {
                    int big = Math.max(w, h);
                    Bitmap small = Bitmap.createScaledBitmap(bm, Math.max(1, w * px / big),
                            Math.max(1, h * px / big), true);
                    bm.recycle();
                    bm = small;
                }
                /*
                 * Density after scaling, so it draws at the asked size and not
                 * the PNG's. The display's own dpi rather than DENSITY_DEFAULT:
                 * px is already a device-pixel count (dp(48)), so a 160 dpi tag
                 * would make the ImageView rescale a bitmap that is exactly the
                 * right size already.
                 */
                bm.setDensity(Resources.getSystem().getDisplayMetrics().densityDpi);
            } finally {
                zf.close();
            }
        } catch (IOException | RuntimeException e) {
            return null;   /* odd zips crash the framework's own zip reader */
        }
        ICONS.put(k, bm);
        return bm;
    }

    /* Icons are named inconsistently in the wild: a leading '/' (337 of the 395
     * jars here), another case, or a Windows backslash. */
    private static ZipEntry find(ZipFile zf, String path) {
        ZipEntry ze = zf.getEntry(path);
        String want = path.replace('\\', '/');
        want = want.startsWith("/") ? want.substring(1) : want;
        if (ze != null) {
            return ze;
        }
        Enumeration<? extends ZipEntry> all = zf.entries();
        while (all.hasMoreElements()) {
            ZipEntry e = all.nextElement();
            String got = e.getName().replace('\\', '/');
            got = got.startsWith("/") ? got.substring(1) : got;
            if (got.equalsIgnoreCase(want)) {
                return e;
            }
        }
        return null;
    }

    /**
     * load() when it can, otherwise a drawn placeholder: never null, never throws.
     *
     * <p>No Context: everything comes from the jar and from Paint, so any
     * thread can call this and the caller needs no resources.
     */
    static Bitmap forEntry(JarStore.Entry e, int px) {
        int size = px > 0 ? px : 1;
        String title = e == null ? null : e.title;
        if (e == null || e.jar == null) {
            return letter(title, size);
        }
        try {
            String[] in = info(e.jar);
            Bitmap bm = in == null ? null : load(e.jar, in[1], size);
            return bm != null ? bm : letter(in != null && in[0] != null ? in[0] : title, size);
        } catch (RuntimeException ignored) {
            /* nothing a caller could do; the letter is the fallback */
            return letter(title, size);
        }
    }

    /** A tile with the title's first character; the dark palette suits the UI. */
    static Bitmap letter(String title, int px) {
        int size = px > 0 ? px : 1;
        String t = title == null ? "" : title.trim();
        t = t.length() == 0 ? "?" : t;
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(Color.HSVToColor(new float[] { (t.hashCode() % 360 + 360) % 360, 0.45f, 0.55f }));
        p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(size * 0.45f);
        Paint.FontMetrics fm = p.getFontMetrics();
        Bitmap bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bm);
        float r = size * 0.5f;
        c.drawCircle(r, r, r, p);
        p.setColor(Color.WHITE);
        /* codePointAt: a CJK title gives one whole glyph, never half a surrogate. */
        c.drawText(new String(Character.toChars(t.codePointAt(0))), r,
                r - (fm.ascent + fm.descent) / 2f, p);
        return bm;
    }

    /* The identity of the current bytes: a re-import or an edit must miss. */
    private static String id(File jar) {
        return jar.getPath() + ':' + jar.length() + ':' + jar.lastModified();
    }
}
