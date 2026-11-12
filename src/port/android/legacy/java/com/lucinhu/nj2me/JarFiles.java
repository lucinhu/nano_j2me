package com.lucinhu.nj2me;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Locale;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.zip.CRC32;

/** Storage deliberately uses only the API 1 files directory and SD card APIs. */
final class JarFiles {
    static final int[][] SIZES = {{128,128}, {128,160}, {176,208}, {176,220},
                                  {240,320}, {320,240}, {360,640}};
    static final int[] HEAPS = {4, 8, 16, 32};
    static final long MAX_JAR_BYTES = 64L * 1024L * 1024L;
    static final long MAX_UNPACKED_BYTES = 256L * 1024L * 1024L;
    static final int MAX_JAR_ENTRIES = 65536;

    private JarFiles() { }

    static SharedPreferences settings(Context context) {
        return context.getSharedPreferences("legacy", Context.MODE_PRIVATE);
    }

    static File directory(File parent, String name) throws IOException {
        File dir = new File(parent, name);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("Cannot create " + dir.getAbsolutePath());
        }
        return dir;
    }

    static File privateJars(Context context) throws IOException {
        return directory(context.getFilesDir(), "jars");
    }

    static File externalJars() {
        return new File(Environment.getExternalStorageDirectory(), "nano_j2me");
    }

    static ArrayList<File> list(Context context) throws IOException {
        ArrayList<File> jars = new ArrayList<File>();
        HashSet<String> seen = new HashSet<String>();
        add(jars, seen, privateJars(context));
        add(jars, seen, externalJars());
        Collections.sort(jars, new Comparator<File>() {
            public int compare(File left, File right) {
                int name = left.getName().compareToIgnoreCase(right.getName());
                return name == 0 ? left.getAbsolutePath().compareTo(right.getAbsolutePath()) : name;
            }
        });
        return jars;
    }

    private static void add(ArrayList<File> jars, HashSet<String> seen, File dir) {
        File[] files = dir.listFiles();
        if (files == null) { return; }
        for (int i = 0; i < files.length; i++) {
            File file = files[i];
            if (file.isFile() && file.canRead()
                    && file.getName().toLowerCase(Locale.US).endsWith(".jar")) {
                String path;
                try { path = file.getCanonicalPath(); }
                catch (IOException e) { path = file.getAbsolutePath(); }
                if (seen.add(path)) { jars.add(file); }
            }
        }
    }

    /** Runs off the UI thread. Publishing is one rename within the private jars directory. */
    static synchronized File importJar(Context context, Uri source) throws IOException {
        String scheme = source == null ? null : source.getScheme();
        if (!"file".equals(scheme) && !"content".equals(scheme)) {
            throw new IOException("只支持 file:// 或 content:// 文件。");
        }
        File temporary = null;
        try {
            File root = privateJars(context);
            ArrayList<File> existing = list(context);
            // Keep exactly this library path so its path-based RMS/state identity survives.
            if ("file".equals(scheme)) {
                if (source.getPath() == null) { throw new IOException("文件路径为空。"); }
                File original = new File(source.getPath()).getCanonicalFile();
                for (File candidate : existing) {
                    if (candidate.getCanonicalFile().equals(original)) {
                        validateJar(candidate);
                        return candidate;
                    }
                }
            }
            // A killed process may leave a hidden staging file; it is never listed as a JAR.
            File[] stale = root.listFiles();
            if (stale != null) {
                for (File file : stale) {
                    if (file.getName().startsWith(".import-") && file.getName().endsWith(".part")) {
                        file.delete();
                    }
                }
            }
            String name = importName(context, source);
            temporary = File.createTempFile(".import-", ".part", root);
            MessageDigest hash = digest();
            InputStream input = context.getContentResolver().openInputStream(source);
            if (input == null) { throw new IOException("无法打开所选文件。"); }
            try {
                FileOutputStream output = new FileOutputStream(temporary);
                try {
                    byte[] buffer = new byte[16384];
                    int count;
                    long copied = 0;
                    while ((count = readChunk(input, buffer)) != -1) {
                        copied += count;
                        if (copied > MAX_JAR_BYTES) { throw new IOException("JAR 文件超过导入上限（64 MiB）。"); }
                        output.write(buffer, 0, count);
                        hash.update(buffer, 0, count);
                    }
                    output.getFD().sync();
                } finally { output.close(); }
            } finally { input.close(); }
            validateJar(temporary);
            byte[] contentHash = hash.digest();
            for (File candidate : existing) {
                if (candidate.length() == temporary.length()) {
                    try {
                        if (MessageDigest.isEqual(contentHash, fileDigest(candidate))) { return candidate; }
                    } catch (IOException ignored) { /* An unreadable old JAR must not block import. */ }
                }
            }
            File destination = new File(root, name);
            String stem = name.substring(0, name.length() - 4);
            for (int suffix = 2; destination.exists(); suffix++) {
                destination = new File(root, stem + " (" + suffix + ").jar");
            }
            if (!temporary.renameTo(destination)) { throw new IOException("无法完成导入，请检查应用存储空间。"); }
            temporary = null;
            return destination;
        } catch (SecurityException e) {
            throw new IOException("没有读取该文件的权限，请重新选择文件，或复制到 SD 卡后导入。");
        } catch (IllegalArgumentException e) {
            throw new IOException("无效的文件地址：" + e.getMessage());
        } finally {
            if (temporary != null) { temporary.delete(); }
        }
    }

    private static int readChunk(InputStream input, byte[] buffer) throws IOException {
        int count = input.read(buffer);
        if (count != 0) { return count; }
        int one = input.read();
        if (one < 0) { return -1; }
        buffer[0] = (byte)one;
        return 1;
    }

    private static MessageDigest digest() throws IOException {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (java.security.NoSuchAlgorithmException e) { throw new IOException("SHA-256 is unavailable"); }
    }

    private static byte[] fileDigest(File file) throws IOException {
        InputStream input = new FileInputStream(file);
        try {
            MessageDigest hash = digest();
            byte[] buffer = new byte[16384];
            int count;
            while ((count = readChunk(input, buffer)) != -1) { hash.update(buffer, 0, count); }
            return hash.digest();
        } finally { input.close(); }
    }

    private static void validateJar(File file) throws IOException {
        if (file.length() > MAX_JAR_BYTES) { throw new IOException("JAR 文件超过导入上限（64 MiB）。"); }
        JarFile jar = new JarFile(file, false);
        try {
            if (jar.size() > MAX_JAR_ENTRIES) { throw new IOException("JAR 条目超过导入上限（65536 个）。"); }
            if (jar.getJarEntry("META-INF/MANIFEST.MF") == null) {
                throw new IOException("不是可用的 JAR：缺少 META-INF/MANIFEST.MF。");
            }
            // Bound inflation before parsing the manifest, which may also be compressed.
            byte[] buffer = new byte[16384];
            long expanded = 0;
            Enumeration<JarEntry> entries = jar.entries();
            while (entries.hasMoreElements()) {
                JarEntry entry = entries.nextElement();
                if (entry.isDirectory()) { continue; }
                InputStream input = jar.getInputStream(entry);
                try {
                    CRC32 crc = new CRC32();
                    long size = 0;
                    int count;
                    while ((count = readChunk(input, buffer)) != -1) {
                        expanded += count;
                        if (expanded > MAX_UNPACKED_BYTES) {
                            throw new IOException("JAR 累计解压数据超过导入上限（256 MiB）。");
                        }
                        crc.update(buffer, 0, count);
                        size += count;
                    }
                    if (size != entry.getSize() || crc.getValue() != entry.getCrc()) {
                        throw new IOException("JAR 数据损坏：" + entry.getName());
                    }
                } finally { input.close(); }
            }
            if (jar.getManifest() == null) { throw new IOException("无法解析 JAR 清单。"); }
        } finally { jar.close(); }
    }

    private static String importName(Context context, Uri source) {
        String name = null;
        if ("content".equals(source.getScheme())) {
            Cursor cursor = null;
            try {
                cursor = context.getContentResolver().query(source,
                        new String[]{"_display_name"}, null, null, null);
                if (cursor != null && cursor.moveToFirst()) {
                    int column = cursor.getColumnIndex("_display_name");
                    if (column >= 0) { name = cursor.getString(column); }
                }
            } catch (RuntimeException ignored) { }
            finally { if (cursor != null) { cursor.close(); } }
        }
        if (name == null || name.length() == 0) { name = source.getLastPathSegment(); }
        if (name == null) { name = "imported"; }
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        name = name.substring(slash + 1);
        if (name.toLowerCase(Locale.US).endsWith(".jar")) { name = name.substring(0, name.length() - 4); }
        StringBuilder safe = new StringBuilder();
        for (int i = 0; i < name.length() && safe.length() < 80; i++) {
            char value = name.charAt(i);
            safe.append(Character.isLetterOrDigit(value) || value == '-' || value == '_' || value == ' '
                    || value == '.' ? value : '_');
        }
        name = safe.toString().trim();
        while (name.startsWith(".")) { name = name.substring(1); }
        return (name.length() == 0 ? "imported" : name) + ".jar";
    }

    /** A path hash avoids collisions between identically named jars in both roots. */
    static File gameDirectory(Context context, File jar) throws IOException {
        try {
            byte[] bytes = jar.getCanonicalPath().getBytes("UTF-8");
            byte[] hash = MessageDigest.getInstance("SHA-1").digest(bytes);
            StringBuilder name = new StringBuilder();
            for (int i = 0; i < hash.length; i++) {
                name.append(Character.forDigit((hash[i] >> 4) & 15, 16));
                name.append(Character.forDigit(hash[i] & 15, 16));
            }
            return directory(directory(context.getFilesDir(), "games"), name.toString());
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("SHA-1 is unavailable");
        }
    }

    static File state(Context context, File jar) throws IOException {
        return new File(gameDirectory(context, jar), "game.state");
    }

    static int sizeIndex(SharedPreferences prefs) {
        int index = prefs.getInt("size", 4);
        return index >= 0 && index < SIZES.length ? index : 4;
    }

    static int heapIndex(SharedPreferences prefs) {
        int index = prefs.getInt("heap", 1);
        return index >= 0 && index < HEAPS.length ? index : 1;
    }
}
