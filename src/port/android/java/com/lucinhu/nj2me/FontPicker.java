package com.lucinhu.nj2me;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Picking a font file, and putting it where the app can open it again.
 *
 * <p>The system picker hands back a {@code content://} that is only valid for
 * as long as the grant lives, and the MIDlet's text is drawn from the VM thread
 * on every frame - including after a restart.  So the picked file is copied
 * into the app's own directory once, and the copy is what everything reads.
 *
 * <p>The name of the copy is not the player's file name: it is built here
 * ({@link FontKit#storeName}) so that the path opened later is one this code
 * wrote, with the extension the platform's font loader looks at.
 */
final class FontPicker {

    /** One request code for both screens that can open the picker. */
    static final int REQUEST = 0x0f01;

    private static final String TAG = "nj2me";

    private FontPicker() {
    }

    /** Ask the system for a font file. */
    static void pick(Activity a) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        /* Fonts are not a MIME type every provider agrees on, so the picker
         * offers everything and FontKit decides. */
        i.setType("*/*");
        try {
            a.startActivityForResult(i, REQUEST);
        } catch (RuntimeException e) {
            Log.w(TAG, "font: no document picker: " + e);
        }
    }

    /** The name the provider reports for a document, or its last path part. */
    static String displayName(Context ctx, Uri uri) {
        String name = null;
        Cursor c = null;
        try {
            c = ctx.getContentResolver().query(uri, null, null, null, null);
            if (c != null && c.moveToFirst()) {
                int col = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (col >= 0 && !c.isNull(col)) {
                    name = c.getString(col);
                }
            }
        } catch (RuntimeException e) {
            name = null;
        } finally {
            if (c != null) {
                try {
                    c.close();
                } catch (RuntimeException e) {
                    /* a provider that refuses to close is not worth a crash */
                }
            }
        }
        return name != null ? name : uri.getLastPathSegment();
    }

    /**
     * Copy the picked document into the app's directory and remember it.
     *
     * @return the absolute path of the copy, or null when the file is not a
     *         font or could not be read - the caller says so and nothing
     *         changes
     */
    static String install(Context ctx, Uri uri) {
        String name = displayName(ctx, uri);
        if (!FontKit.looksLikeFont(name)) {
            return null;
        }
        File out = new File(ctx.getFilesDir(), FontKit.storeName(name));
        InputStream in = null;
        OutputStream os = null;
        try {
            in = ctx.getContentResolver().openInputStream(uri);
            if (in == null) {
                return null;
            }
            os = new FileOutputStream(out);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
            os.flush();
        } catch (Exception e) {
            Log.w(TAG, "font: cannot read " + uri + ": " + e);
            return null;
        } finally {
            close(in);
            close(os);
        }
        Settings.setFontFile(ctx, out.getAbsolutePath());
        return out.getAbsolutePath();
    }

    /** Forget the player's font: the system face comes back. */
    static void reset(Context ctx) {
        String path = Settings.fontFile(ctx);
        if (path != null) {
            File f = new File(path);
            if (f.exists() && !f.delete()) {
                Log.w(TAG, "font: cannot delete " + path);
            }
        }
        Settings.setFontFile(ctx, null);
    }

    private static void close(java.io.Closeable c) {
        if (c != null) {
            try {
                c.close();
            } catch (Exception e) {
                /* nothing useful to do about a close that failed */
            }
        }
    }
}
