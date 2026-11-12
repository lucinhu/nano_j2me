package com.lucinhu.nj2me;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

/** BitmapFactory existed at API 1; MediaCodec/modern image APIs are not referenced. */
final class PlatformImage {
    private static final long MAX_PIXELS = 2048L * 2048L;
    private PlatformImage() { }

    static int[] info(byte[] data) {
        if (data == null) { return null; }
        try {
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(data, 0, data.length, options);
            int width = options.outWidth, height = options.outHeight;
            if (width <= 0 || height <= 0 || (long)width * height > MAX_PIXELS) { return null; }
            // JPEG is opaque. Other formats conservatively keep their alpha channel.
            boolean jpeg = data.length > 2 && (data[0] & 255) == 255 && (data[1] & 255) == 216;
            return new int[]{width, height, jpeg ? 0 : 1};
        } catch (RuntimeException e) { return null; }
        catch (OutOfMemoryError e) { return null; }
    }

    /** Top-down, tightly packed, straight-alpha R,G,B,A bytes. */
    static byte[] decode(byte[] data, int width, int height) {
        if (data == null || width <= 0 || height <= 0 || (long)width * height > MAX_PIXELS) { return null; }
        Bitmap bitmap = null;
        try {
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inPreferredConfig = Bitmap.Config.ARGB_8888;
            options.inScaled = false;
            bitmap = BitmapFactory.decodeByteArray(data, 0, data.length, options);
            if (bitmap == null || bitmap.getWidth() != width || bitmap.getHeight() != height) { return null; }
            int[] argb = new int[width * height];
            bitmap.getPixels(argb, 0, width, 0, 0, width, height);
            byte[] rgba = new byte[argb.length * 4];
            for (int i = 0, p = 0; i < argb.length; i++) {
                int pixel = argb[i];
                rgba[p++] = (byte)(pixel >> 16);
                rgba[p++] = (byte)(pixel >> 8);
                rgba[p++] = (byte)pixel;
                rgba[p++] = (byte)(pixel >>> 24);
            }
            return rgba;
        } catch (RuntimeException e) { return null; }
        catch (OutOfMemoryError e) { return null; }
        finally { if (bitmap != null) { bitmap.recycle(); } }
    }
}
