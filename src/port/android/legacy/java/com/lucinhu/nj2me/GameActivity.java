package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.Vibrator;
import android.util.Log;
import android.util.SparseIntArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.nio.ShortBuffer;
import java.util.HashMap;
import java.util.HashSet;

/** API 5 frontend. All VM destruction belongs to the runner, never the UI thread. */
public final class GameActivity extends Activity {
    private static String loadError;
    static {
        try { System.loadLibrary("nj2me"); }
        catch (LinkageError e) { loadError = e.toString(); }
    }

    private static volatile WeakReference<GameActivity> current = new WeakReference<GameActivity>(null);
    private static Vibrator vibrator;

    static boolean hasRunningGame() { return current.get() != null; }
    private final Object lock = new Object();
    private final HashMap<Integer, Integer> held = new HashMap<Integer, Integer>();
    private final HashSet<Integer> hardware = new HashSet<Integer>();
    private long handle;
    private boolean resumed, closing, quitting;
    private Thread runner, saver;
    private LegacyAudio audio;
    private FrameView frame;
    private Keypad pad;
    private File stateFile;
    private boolean keypadVisible;
    private AlertDialog panel;

    private static native int nativeLegacyVersion();
    private static native long nativeCreate(Surface nativeSurface, String jarPath,
            int width, int height, int heapMB, String rmsDir, String fsDir);
    private static native void nativeRun(long handle);
    private static native void nativeKey(long handle, int keyCode, boolean down);
    private static native void nativeTouch(long handle, int id, int phase, float x, float y);
    private static native void nativeSetActive(long handle, boolean active);
    /* Surface remains in the ABI; the legacy frontend always passes null. */
    private static native void nativeSetSurface(long handle, Surface nativeSurface);
    private static native void nativeSetRestore(long handle, String path);
    private static native int nativeSaveState(long handle, String path);
    private static native void nativeQuit(long handle);
    private static native void nativeDestroy(long handle);
    static native int nativeRenderAudio(long handle, short[] samples, int frames);

    /** JNI callback on the VM thread. Pixels are tightly packed, top-down RGB565. */
    public static void blitFrame(short[] pixels, int width, int height) {
        GameActivity activity = current.get();
        if (activity != null && activity.frame != null) {
            activity.frame.blit(pixels, width, height);
        }
    }

    public static void vibrateDevice(int millis) {
        if (vibrator != null && millis > 0) {
            try { vibrator.vibrate(Math.min(millis, 5000)); }
            catch (RuntimeException ignored) { }
        }
    }

    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        if (loadError != null) { fail("无法加载旧版 libnj2me.so：\n" + loadError); return; }
        try {
            if (nativeLegacyVersion() != 1) {
                fail("此 APK 需要支持 API5 bitmap 回调的 legacy native 库。"); return;
            }
        } catch (LinkageError e) {
            fail("此 APK 混入了现代 native 库；请使用 legacy 构建（需要 nativeLegacyVersion / blitFrame）。");
            return;
        }
        GameActivity previous = current.get();
        if (previous != null && previous != this) {
            fail("上一游戏仍在关闭，请返回游戏库稍后重试。"); return;
        }
        String path = getIntent().getStringExtra("jarpath");
        if (path == null) { path = getIntent().getStringExtra("jar"); }
        if (path == null) { fail("未指定 JAR 文件。"); return; }
        File jar = new File(path);
        if (!jar.isFile() || !jar.canRead()) { fail("无法读取 JAR：" + path); return; }
        SharedPreferences prefs = JarFiles.settings(this);
        int[] size = JarFiles.SIZES[JarFiles.sizeIndex(prefs)];
        keypadVisible = prefs.getBoolean("keypad", true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);
        Button menu = new Button(this);
        menu.setText("菜单 / 存档 / 返回游戏库");
        menu.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) { showPanel(); }
        });
        root.addView(menu, new LinearLayout.LayoutParams(-1, dp(40)));
        frame = new FrameView(this);
        root.addView(frame, new LinearLayout.LayoutParams(-1, 0, 1));
        pad = new Keypad(this);
        root.addView(pad, new LinearLayout.LayoutParams(-1, dp(160)));
        pad.setVisibility(keypadVisible ? View.VISIBLE : View.GONE);
        setContentView(root);
        vibrator = (Vibrator)getApplicationContext().getSystemService(Context.VIBRATOR_SERVICE);
        try {
            File gameDir = JarFiles.gameDirectory(this, jar);
            stateFile = new File(gameDir, "game.state");
            String rms = JarFiles.directory(gameDir, "rms").getAbsolutePath();
            String fs = JarFiles.directory(gameDir, "fs").getAbsolutePath();
            handle = nativeCreate(null, jar.getAbsolutePath(), size[0], size[1],
                    JarFiles.HEAPS[JarFiles.heapIndex(prefs)], rms, fs);
            if (handle == 0) { fail("无法创建游戏，可能内存不足。"); return; }
            nativeSetActive(handle, false);
            if (getIntent().getBooleanExtra("restore", false) && stateFile.isFile()) {
                nativeSetRestore(handle, stateFile.getAbsolutePath());
            }
            if (prefs.getBoolean("sound", true)) {
                try { audio = new LegacyAudio(handle); }
                catch (RuntimeException e) {
                    Log.w("nj2me", "AudioTrack unavailable", e);
                    Toast.makeText(this, "此设备无法开启 PCM 声音，继续静音运行。", Toast.LENGTH_LONG).show();
                }
            }
        } catch (IOException e) {
            fail(e.getMessage());
            return;
        } catch (LinkageError e) {
            fail("旧版 JNI 接口不完整：" + e.getMessage());
            return;
        }
        runner = new Thread(new Runnable() {
            public void run() { runGame(); }
        }, "nj2me-runner");
        current = new WeakReference<GameActivity>(this);
        runner.start();
    }

    private int dp(int value) {
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void runGame() {
        try { nativeRun(handle); }
        catch (final LinkageError e) {
            Log.e("nj2me", "Game JNI failed", e);
            runOnUiThread(new Runnable() {
                public void run() { Toast.makeText(GameActivity.this, e.toString(), Toast.LENGTH_LONG).show(); }
            });
        } finally {
            Thread save;
            synchronized (lock) { closing = true; save = saver; }
            if (audio != null) { audio.requestStop(); }
            join(save);
            if (audio != null) { audio.awaitStop(); }
            synchronized (lock) {
                if (handle != 0) { nativeDestroy(handle); handle = 0; }
            }
            if (current.get() == GameActivity.this) { current = new WeakReference<GameActivity>(null); }
            runOnUiThread(new Runnable() {
                public void run() {
                    if (panel != null) { panel.dismiss(); panel = null; }
                    frame.dispose();
                    finish();
                }
            });
        }
    }

    private static void join(Thread thread) {
        if (thread == null) { return; }
        boolean interrupted = false;
        for (;;) {
            try { thread.join(); break; }
            catch (InterruptedException e) { interrupted = true; }
        }
        if (interrupted) { Thread.currentThread().interrupt(); }
    }

    protected void onResume() {
        super.onResume();
        resumed = true;
        synchronized (lock) { if (handle != 0 && !closing && !quitting) { nativeSetActive(handle, true); } }
        if (audio != null && !quitting) { audio.setActive(true); }
    }

    protected void onPause() {
        resumed = false;
        releaseInputs();
        if (audio != null) { audio.setActive(false); }
        synchronized (lock) { if (handle != 0 && !closing) { nativeSetActive(handle, false); } }
        super.onPause();
    }

    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) { releaseInputs(); }
    }

    protected void onDestroy() {
        if (panel != null) { panel.dismiss(); panel = null; }
        requestQuit();
        if (runner == null) {
            if (audio != null) { audio.requestStop(); audio.awaitStop(); }
            // A failed initialization has no VM worker; PCM has now also ended.
            synchronized (lock) {
                if (handle != 0) { nativeDestroy(handle); handle = 0; }
            }
            if (current.get() == this) { current = new WeakReference<GameActivity>(null); }
            if (frame != null) { frame.dispose(); }
        }
        super.onDestroy();
    }

    private void fail(String message) {
        if (panel != null) { panel.dismiss(); }
        panel = new AlertDialog.Builder(this).setTitle("无法启动游戏").setMessage(message)
            .setCancelable(false).setPositiveButton("返回", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int which) { finish(); }
            }).create();
        panel.show();
    }

    private void requestQuit() {
        quitting = true;
        releaseInputs();
        if (audio != null) { audio.requestStop(); }
        synchronized (lock) {
            if (handle != 0 && !closing) {
                // Wake a background VM so its queued quit event is consumed.
                nativeSetActive(handle, true);
                nativeQuit(handle);
            }
        }
    }

    private void showPanel() {
        synchronized (lock) { if (handle == 0 || closing || quitting) { return; } }
        if (panel != null && panel.isShowing()) { return; }
        releaseInputs();
        panel = new AlertDialog.Builder(this).setTitle("游戏菜单")
            .setItems(new String[]{"继续游戏", "保存即时存档", keypadVisible ? "隐藏键盘" : "显示键盘", "退出并返回游戏库"},
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) {
                        panel = null;
                        if (which == 1) { saveState(); }
                        else if (which == 2) {
                            keypadVisible = !keypadVisible;
                            pad.setVisibility(keypadVisible ? View.VISIBLE : View.GONE);
                        } else if (which == 3) {
                            requestQuit();
                            // Keep this Activity until the worker finishes native teardown.
                            Toast.makeText(GameActivity.this, "正在退出…", Toast.LENGTH_SHORT).show();
                        }
                    }
                }).setNegativeButton("取消", null).create();
        panel.show();
    }

    private void saveState() {
        synchronized (lock) {
            if (handle == 0 || closing || quitting || saver != null && saver.isAlive()) { return; }
            final long owner = handle;
            final String path = stateFile.getAbsolutePath();
            saver = new Thread(new Runnable() {
                public void run() {
                    final int result = nativeSaveState(owner, path);
                    runOnUiThread(new Runnable() {
                        public void run() {
                            if (!isFinishing()) {
                                Toast.makeText(GameActivity.this, result == 0 ? "即时存档已保存" : "即时存档失败（" + result + "），请用游戏内存档。", Toast.LENGTH_LONG).show();
                            }
                        }
                    });
                }
            }, "nj2me-save");
            saver.start();
        }
    }

    private void press(int code) {
        synchronized (lock) {
            if (handle == 0 || closing || quitting) { return; }
            Integer count = held.get(Integer.valueOf(code));
            held.put(Integer.valueOf(code), Integer.valueOf(count == null ? 1 : count.intValue() + 1));
            if (count == null) { nativeKey(handle, code, true); }
        }
    }

    private void release(int code) {
        synchronized (lock) {
            Integer count = held.get(Integer.valueOf(code));
            if (count == null) { return; }
            if (count.intValue() > 1) { held.put(Integer.valueOf(code), Integer.valueOf(count.intValue() - 1)); }
            else {
                held.remove(Integer.valueOf(code));
                if (handle != 0 && !closing) { nativeKey(handle, code, false); }
            }
        }
    }

    private void releaseInputs() {
        if (pad != null) { pad.cancel(); }
        if (frame != null) { frame.cancelTouch(); }
        synchronized (lock) {
            if (handle != 0 && !closing) {
                for (Integer code : held.keySet()) { nativeKey(handle, code.intValue(), false); }
            }
            held.clear();
            hardware.clear();
        }
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (panel != null && panel.isShowing()) { return super.dispatchKeyEvent(event); }
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_MENU) {
            if (event.getAction() == KeyEvent.ACTION_UP) { showPanel(); }
            return true;
        }
        boolean gameKey = code == KeyEvent.KEYCODE_BACK || code >= 7 && code <= 23
                || code >= 29 && code <= 54 || code == 62 || code == 66 || code == 67;
        if (!gameKey || frame == null || !resumed) { return super.dispatchKeyEvent(event); }
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (hardware.add(Integer.valueOf(code))) { press(code); }
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            if (hardware.remove(Integer.valueOf(code))) { release(code); }
        }
        return true;
    }

    private void touch(int id, int phase, float x, float y) {
        synchronized (lock) {
            if (handle != 0 && !closing && !quitting) { nativeTouch(handle, id, phase, x, y); }
        }
    }

    private final class FrameView extends View {
        private final Object pixelsLock = new Object();
        private final Paint paint = new Paint();
        private final RectF destination = new RectF();
        private Bitmap bitmap;
        private ShortBuffer staging;
        private int touchId = -1;
        private float lastX, lastY;
        private boolean disposed;

        FrameView(Context context) { super(context); paint.setFilterBitmap(false); }

        void blit(short[] pixels, int width, int height) {
            if (pixels == null || width <= 0 || height <= 0 || width > 2048 || height > 2048
                    || (long)width * height > pixels.length) { return; }
            synchronized (pixelsLock) {
                if (disposed) { return; }
                try {
                    if (bitmap == null || bitmap.getWidth() != width || bitmap.getHeight() != height) {
                        if (bitmap != null) { bitmap.recycle(); bitmap = null; }
                        staging = null;
                        bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565);
                        staging = ShortBuffer.allocate(bitmap.getRowBytes() / 2 * height);
                    }
                    // RGB565 rows may be padded to four bytes, especially for odd widths.
                    staging.clear();
                    int stride = bitmap.getRowBytes() / 2;
                    for (int y = 0; y < height; y++) {
                        staging.position(y * stride);
                        staging.put(pixels, y * width, width);
                    }
                    staging.rewind();
                    bitmap.copyPixelsFromBuffer(staging);
                } catch (RuntimeException e) { Log.e("nj2me", "Frame copy failed", e); }
                catch (OutOfMemoryError e) { Log.e("nj2me", "Frame allocation failed", e); }
            }
            postInvalidate();
        }

        private void fit() {
            if (bitmap == null) { destination.setEmpty(); return; }
            float scale = Math.min((float)getWidth() / bitmap.getWidth(), (float)getHeight() / bitmap.getHeight());
            float width = bitmap.getWidth() * scale;
            float height = bitmap.getHeight() * scale;
            float left = (getWidth() - width) * 0.5f;
            float top = (getHeight() - height) * 0.5f;
            destination.set(left, top, left + width, top + height);
        }

        protected void onDraw(Canvas canvas) {
            canvas.drawColor(Color.BLACK);
            synchronized (pixelsLock) {
                if (bitmap != null && !disposed) {
                    fit();
                    canvas.drawBitmap(bitmap, null, destination, paint);
                }
            }
        }

        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getAction() & 255;
            int index = (event.getAction() >> 8) & 255; // getActionIndex is API 8.
            int phase;
            if (action == MotionEvent.ACTION_DOWN) {
                synchronized (pixelsLock) {
                    fit();
                    if (bitmap == null || !destination.contains(event.getX(), event.getY())) { return false; }
                }
                touchId = event.getPointerId(0);
                index = 0;
                phase = 0;
            } else if (action == MotionEvent.ACTION_MOVE) {
                index = event.findPointerIndex(touchId);
                if (index < 0) { return true; }
                phase = 1;
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                if (event.getPointerId(index) != touchId) { return true; }
                phase = 2;
            } else if (action == MotionEvent.ACTION_CANCEL) { cancelTouch(); return true; }
            else { return true; }
            synchronized (pixelsLock) {
                fit();
                if (bitmap == null || destination.width() <= 0 || destination.height() <= 0) { return true; }
                lastX = Math.max(0, Math.min(bitmap.getWidth() - 1,
                        (event.getX(index) - destination.left) * bitmap.getWidth() / destination.width()));
                lastY = Math.max(0, Math.min(bitmap.getHeight() - 1,
                        (event.getY(index) - destination.top) * bitmap.getHeight() / destination.height()));
            }
            touch(touchId, phase, lastX, lastY);
            if (phase == 2) { touchId = -1; }
            return true;
        }

        void cancelTouch() {
            if (touchId >= 0) { touch(touchId, 3, lastX, lastY); touchId = -1; }
        }

        void dispose() {
            synchronized (pixelsLock) {
                disposed = true;
                if (bitmap != null) { bitmap.recycle(); bitmap = null; }
                staging = null;
            }
        }
    }

    /** Four rows with independent pointer ownership allow direction + fire chords. */
    private final class Keypad extends View {
        private final String[] labels = {"左软", "↑", "右软", "1", "2", "3",
            "←", "OK", "→", "4", "5", "6", "*", "↓", "#", "7", "8", "9",
            "菜单", "删除", "隐藏", "*", "0", "#"};
        private final int[] codes = {82, 19, 4, 8, 9, 10, 21, 23, 22, 11, 12, 13,
            17, 20, 18, 14, 15, 16, -1, 67, -2, 17, 7, 18};
        private final SparseIntArray pointers = new SparseIntArray();
        private final Paint paint = new Paint();
        private final RectF cell = new RectF();

        Keypad(Context context) { super(context); paint.setAntiAlias(true); }

        protected void onDraw(Canvas canvas) {
            canvas.drawColor(0xff202020);
            float width = getWidth() / 6.0f, height = getHeight() / 4.0f;
            paint.setTextSize(Math.min(dp(16), width / 3));
            paint.setTextAlign(Paint.Align.CENTER);
            for (int i = 0; i < codes.length; i++) {
                boolean down = false;
                for (int p = 0; p < pointers.size(); p++) { if (pointers.valueAt(p) == i) { down = true; break; } }
                float x = (i % 6) * width, y = (i / 6) * height;
                cell.set(x + 1, y + 1, x + width - 1, y + height - 1);
                paint.setColor(down ? 0xff5078a0 : 0xff404040);
                canvas.drawRect(cell, paint);
                paint.setColor(Color.WHITE);
                canvas.drawText(labels[i], x + width * 0.5f,
                        y + (height - paint.ascent() - paint.descent()) * 0.5f, paint);
            }
        }

        private int button(float x, float y) {
            if (x < 0 || y < 0 || x >= getWidth() || y >= getHeight()) { return -1; }
            return Math.min(3, (int)(y * 4 / getHeight())) * 6 + Math.min(5, (int)(x * 6 / getWidth()));
        }

        private void down(int id, int index) {
            if (index < 0 || codes[index] < 0) { return; }
            pointers.put(id, index);
            press(codes[index]);
        }

        private void up(int id) {
            int offset = pointers.indexOfKey(id);
            if (offset >= 0) {
                int index = pointers.valueAt(offset);
                pointers.delete(id);
                release(codes[index]);
            }
        }

        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getAction() & 255;
            int index = (event.getAction() >> 8) & 255;
            int id = event.getPointerId(index < event.getPointerCount() ? index : 0);
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                int hit = button(event.getX(index), event.getY(index));
                if (hit >= 0 && codes[hit] == -1) { showPanel(); }
                else if (hit >= 0 && codes[hit] == -2) { cancel(); keypadVisible = false; setVisibility(View.GONE); }
                else { down(id, hit); }
            } else if (action == MotionEvent.ACTION_MOVE) {
                for (int p = 0; p < event.getPointerCount(); p++) {
                    id = event.getPointerId(p);
                    int hit = button(event.getX(p), event.getY(p));
                    int offset = pointers.indexOfKey(id);
                    if (offset >= 0 && pointers.valueAt(offset) != hit) {
                        up(id);
                        down(id, hit);
                    }
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) { up(id); }
            else if (action == MotionEvent.ACTION_CANCEL) { cancel(); }
            invalidate();
            return true;
        }

        void cancel() {
            for (int i = 0; i < pointers.size(); i++) { release(codes[pointers.valueAt(i)]); }
            pointers.clear();
            invalidate();
        }
    }
}
