/*
 * nano_j2me - PlatformImage.java
 *
 * The platform's image decoders, behind the port's codec provider.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The runtime ships its own PNG and JPEG decoders and they are what every
 * desktop build uses, but Android's are the ones with the silicon behind them:
 * libjpeg-turbo in the framework, a hardware WebP path on many SoCs, and the
 * GIF and BMP readers the runtime does not have at all.  A sprite sheet is the
 * largest thing a J2ME game decodes, so this is where the port hands the bytes
 * over and takes the pixels back.
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Two statics, called on the VM thread: info() answers {width, height,
 * hasAlpha} without decoding anything, and decode() turns the same bytes into
 * R,G,B,A bytes.  Both return null when the platform declines - which is not
 * an error, it is the runtime's cue to fall back to its own decoder - and
 * neither throws, because a game JAR is untrusted input and a Java exception
 * crossing JNI would take the VM down.
 *
 * The pixel layout decode() promises: exactly w*h*4 bytes, in R,G,B,A order,
 * one byte per channel, top row first, no padding, no scaling.  Alpha is
 * straight (un-premultiplied): the platform stores it premultiplied, and
 * getPixels() un-premultiplies on the way out, so a half-transparent pixel
 * keeps its colour.  A fully transparent pixel may come back as 0x00000000,
 * because multiplying by zero is lossy and no decoder can undo it.
 *
 * Because this touches no per-app state other than the context the port is
 * required to hand it, nothing here outlives the process: no Activity is
 * pinned and no run-time cache is kept.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

final class PlatformImage {

    private static final String TAG = "nj2me";

    /*
     * The runtime allocates w*h*4 bytes for whatever decode() returns, so an
     * image past this is refused before either side allocates anything.
     * 4096x4096 is 64 MiB of pixels: already more than the heap a low-end
     * device gives an app, and more than any J2ME title ever decoded at once.
     */
    private static final long MAX_PIXELS = 4096L * 4096L;

    /* See logOnce(). */
    private static final int LOG_ONCE_MAX = 8;
    private static final Set<String> LOGGED =
            Collections.synchronizedSet(new HashSet<String>());

    /*
     * The application context, never an Activity: a static field that held an
     * Activity would keep a destroyed screen (and its whole view tree) alive
     * for the life of the process.  BitmapFactory itself is stateless - the
     * field is what makes init() meaningful rather than decorative, and
     * refusing to work before init() turns a forgotten call into one clear
     * log line instead of a half-wired port.
     */
    private static Context appContext;

    private PlatformImage() {
    }

    /* Called once from the Activity, before the VM can ask for an image. */
    static void init(Context context) {
        try {
            if (context != null) {
                appContext = context.getApplicationContext();
            }
        } catch (Throwable ignored) {
            /* nothing to bind */
        }
    }

    /**
     * One image's shape: {@code {width, height, hasAlpha}}.
     *
     * <p>Read with {@code inJustDecodeBounds}, so the platform parses the
     * header and allocates nothing - the runtime probes every image before it
     * commits to it, and probing must not cost a decode.
     *
     * <p>null when the bytes are not an image this platform can decode, or
     * when the image is past {@link #MAX_PIXELS}.
     */
    static int[] info(byte[] data) {
        try {
            if (!ready()) {
                return null;
            }
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(data, 0, data.length, opts);
            int w = opts.outWidth;
            int h = opts.outHeight;
            if (w <= 0 || h <= 0) {
                logOnce("probe:" + opts.outMimeType,
                        "the platform cannot decode these " + data.length
                                + " bytes (" + (opts.outMimeType == null
                                ? "unrecognised" : opts.outMimeType) + ")");
                return null;
            }
            if ((long) w * h > MAX_PIXELS) {
                logOnce("probe:huge", "refusing " + w + "x" + h + ": the runtime"
                        + " allocates 4 bytes a pixel and the port caps that at "
                        + MAX_PIXELS);
                return null;
            }
            return new int[]{w, h, alphaInHeader(data, opts.outMimeType)};
        } catch (Throwable t) {
            logOnce("probe:" + t.getClass().getName(), "probe failed (" + t + ")");
            return null;
        }
    }

    /**
     * Decode to w*h*4 bytes in R,G,B,A order, or null.
     *
     * <p>{@code w} and {@code h} are the size the caller got from
     * {@link #info}; they are verified against the decode, so a caller that
     * mixed two images up gets null rather than a buffer it will read past the
     * end of.
     */
    static byte[] decode(byte[] data, int w, int h) {
        try {
            if (!ready() || w <= 0 || h <= 0) {
                return null;
            }
            if ((long) w * h > MAX_PIXELS) {
                logOnce("probe:huge", "refusing " + w + "x" + h + ": the runtime"
                        + " allocates 4 bytes a pixel and the port caps that at "
                        + MAX_PIXELS);
                return null;
            }
            BitmapFactory.Options opts = new BitmapFactory.Options();
            /*
             * ARGB_8888 explicitly, not the default: it is the config whose
             * alpha channel survives, and naming it also keeps the platform
             * from handing back a HARDWARE bitmap, whose pixels cannot be read
             * at all.
             */
            opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
            /* No density scaling.  A byte array has no density, but saying so
             * keeps the promise that what is decoded is what was probed. */
            opts.inScaled = false;
            Bitmap bm = BitmapFactory.decodeByteArray(data, 0, data.length, opts);
            if (bm == null) {
                logOnce("decode:null", "the platform declined " + data.length
                        + " bytes it had described as " + w + "x" + h);
                return null;
            }
            try {
                if (bm.getWidth() != w || bm.getHeight() != h) {
                    logOnce("decode:size", "info() said " + w + "x" + h
                            + " but the decode is " + bm.getWidth() + "x"
                            + bm.getHeight());
                    return null;
                }
                /*
                 * getPixels() rather than copyPixelsToBuffer(): it is on every
                 * API level, it takes the destination stride explicitly (so a
                 * bitmap whose rows are padded cannot silently shift the
                 * image), and it hands back the colour and alpha of a pixel in
                 * one int, which is what the R,G,B,A pack below needs.  A
                 * buffer copy would be a second layout to reason about for no
                 * gain here.
                 */
                int[] argb = new int[w * h];
                bm.getPixels(argb, 0, w, 0, 0, w, h);
                byte[] out = new byte[w * h * 4];
                int p = 0;
                for (int i = 0; i < argb.length; i++) {
                    int c = argb[i];
                    out[p++] = (byte) (c >> 16);    /* R */
                    out[p++] = (byte) (c >> 8);     /* G */
                    out[p++] = (byte) c;            /* B */
                    out[p++] = (byte) (c >>> 24);   /* A */
                }
                return out;
            } finally {
                try {
                    bm.recycle();
                } catch (Throwable ignored) {
                    /* a bitmap that is already gone is not a failure */
                }
            }
        } catch (Throwable t) {
            logOnce("decode:" + t.getClass().getName(), "decode failed (" + t + ")");
            return null;
        }
    }

    /*
     * Is the port wired up?  Called before anything else so a forgotten
     * init() is one log line, not a game that silently has no images.
     */
    private static boolean ready() {
        if (appContext != null) {
            return true;
        }
        logOnce("no-init", "PlatformImage.init() was never called; refusing to"
                + " decode until the Activity hands over an application context");
        return false;
    }

    /*
     * A game that asks for a broken image asks again every frame - a menu
     * redraws, an animation timer fires, Map.load retries - so a failure is
     * logged once per kind, never once per call.  The set is capped because a
     * hostile JAR must not be able to grow a static field one distinct failure
     * at a time.
     */
    private static void logOnce(String kind, String detail) {
        synchronized (LOGGED) {
            if (LOGGED.size() >= LOG_ONCE_MAX || !LOGGED.add(kind)) {
                return;
            }
        }
        Log.w(TAG, "image: " + detail);
    }

    /*
     * "Does this image carry alpha?" - answered from the header, because with
     * inJustDecodeBounds nothing is decoded and the only Options field that
     * would answer it (outConfig) exists from API 26.  So the formats where
     * the question is cheap are read exactly, and the rest answer "yes":
     * claiming alpha costs the runtime two bytes a pixel, while missing it
     * silently drops a sprite's transparency, and only one of those two
     * mistakes is visible to a player.
     */
    private static int alphaInHeader(byte[] d, String mime) {
        if ("image/png".equals(mime)) {
            return pngAlpha(d, d.length);
        }
        if ("image/webp".equals(mime)) {
            return webpAlpha(d, d.length);
        }
        if ("image/jpeg".equals(mime)) {
            return 0;   /* never: JPEG has no alpha plane */
        }
        if ("image/bmp".equals(mime) || "image/x-ms-bmp".equals(mime)) {
            /* 32 bits a pixel with an explicit alpha mask; plain 32-bit
             * BI_RGB ignores its fourth byte, and every other depth has no
             * alpha at all. */
            if (d.length >= 32) {
                int bits = (d[28] & 0xff) | ((d[29] & 0xff) << 8);
                int comp = (d[30] & 0xff) | ((d[31] & 0xff) << 8);
                return (bits == 32 && (comp == 3 || comp == 6)) ? 1 : 0;
            }
            return 0;
        }
        /* GIF (transparency is per frame, so it is a whole-file question, not
         * a header one) and anything else the platform named: claim it. */
        return 1;
    }

    /*
     * PNG: IHDR's colour type says whether there is an alpha plane (4 =
     * grey+alpha, 6 = RGB+alpha), and an indexed image (3) has one only when a
     * tRNS chunk precedes the pixels - which is how the paletted sprites a J2ME
     * game ships carry their transparency.  0 and 2 have none.
     */
    private static int pngAlpha(byte[] d, int n) {
        if (n < 33) {
            return 1;   /* truncated; the decoder will refuse it anyway */
        }
        int type = d[25] & 0xff;
        if (type == 4 || type == 6) {
            return 1;
        }
        if (type != 3) {
            return 0;
        }
        int p = 8;      /* first chunk, after the 8-byte signature */
        while (p + 8 <= n) {
            long len = ((long) (d[p] & 0xff) << 24) | ((d[p + 1] & 0xff) << 16)
                    | ((d[p + 2] & 0xff) << 8) | (d[p + 3] & 0xff);
            if (isChunk(d, p + 4, 't', 'R', 'N', 'S')) {
                return 1;
            }
            if (isChunk(d, p + 4, 'I', 'D', 'A', 'T')) {
                return 0;   /* no tRNS before the pixels: opaque */
            }
            long next = (long) p + 12 + len;    /* length, type, data, CRC */
            if (next <= p || next > n) {
                return 1;   /* malformed; claim alpha, the decoder will judge */
            }
            p = (int) next;
        }
        return 1;
    }

    /*
     * WebP: the RIFF container names the payload.  VP8X carries an alpha flag
     * in its first byte, VP8L one in the lossless header, and a plain lossy
     * "VP8 " chunk has no alpha channel at all.  The mask is 0x10 in both - the
     * container numbers bits from the most significant, the lossless bitstream
     * from the least.
     */
    private static int webpAlpha(byte[] d, int n) {
        if (n < 25) {
            return 1;   /* cannot tell; claim it */
        }
        if (isChunk(d, 12, 'V', 'P', '8', 'X')) {
            return (d[20] & 0x10) != 0 ? 1 : 0;
        }
        if (isChunk(d, 12, 'V', 'P', '8', 'L')) {
            return (d[24] & 0x10) != 0 ? 1 : 0;
        }
        return 0;
    }

    /* A four-character chunk tag at off; the caller has already bounded off. */
    private static boolean isChunk(byte[] d, int off, char c0, char c1, char c2,
                                   char c3) {
        return d[off] == c0 && d[off + 1] == c1 && d[off + 2] == c2
                && d[off + 3] == c3;
    }
}
