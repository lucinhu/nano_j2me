package com.lucinhu.nj2me;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Vibrator;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Hosts one MIDlet: surface, input, keypad overlay, control bar.
 *
 * <p>The VM owns a worker thread and never touches the UI thread, with one
 * exception that is not optional: {@code nativeCreate} needs the
 * {@link Surface}, which only exists on the UI thread.  So creation happens in
 * {@code surfaceCreated}, the run loop starts on its own thread right after, and
 * teardown happens at the end of that thread - the same shape a native app
 * would have, and the reason there is no {@code nativeDestroy} in
 * {@code onDestroy}: the activity must not free memory a running VM is using.
 *
 * <p>Every JNI call goes through {@link #lock}.  Once the run thread has torn
 * the port down, {@link #handle} is zero and the UI thread's calls become no-ops
 * instead of a jump into freed memory.
 */
public class GameActivity extends Activity implements SurfaceHolder.Callback, PadView.Sink {

    private static final String TAG = "nj2me";
    private static final int BAR_TIMEOUT_MS = 7000;

    static {
        try {
            System.loadLibrary("nj2me");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "libnj2me.so did not load: " + e.getMessage());
        }
    }

    private final Object lock = new Object();
    private long handle;
    private Thread runThread;
    private volatile boolean finishing;

    private SurfaceView surfaceView;
    private PadView pad;
    private PadConfig padConfig;
    private LinearLayout bar;
    private TextView status;
    private TextView handleButton;
    private TextView titleView;

    private String jarPath = "";   /* absolute: what the port opens */
    private String jarName = "";   /* file name: the JarStore key */
    private String jarTitle = "";
    private int screenW = JarStore.DEF_W;
    private int screenH = JarStore.DEF_H;
    private int heapMB = JarStore.DEF_HEAP_MB;

    /*
     * How the picture sits in the window.  These mirror nj_android_view_mode in
     * include/nj2me/nj_port.h - the numbers travel over JNI and are written to
     * the jar's sidecar file, so they must not move.
     */
    private static final int VIEW_INT = 0;
    private static final int VIEW_FILL = 1;
    private static final int VIEW_CUSTOM = 2;
    private static final int VIEW_AUTO = 3;

    private int viewMode = VIEW_INT;
    private float viewZoom = 1.0f;
    private float viewPanX, viewPanY;
    private boolean viewDirty;      /* worth writing back to the sidecar */
    private boolean detSaved;       /* the measured resolution is already stored */
    private int detW, detH;
    private int detLastW, detLastH, detStable;

    /*
     * 调整画面 is a state the player turns on, not a view mode: the picture
     * follows the finger while it is on, in whichever fit they chose.  It is
     * deliberately separate from viewMode so that moving a picture never
     * costs the player their crisp integer scaling.
     */
    private boolean movingPicture;

    /* 沉浸模式: the player's setting, plus where the notch and bars are. */
    private boolean immersive;
    private int insetL, insetT, insetR, insetB;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable barHider = new Runnable() {
        @Override
        public void run() {
            showBar(false);
        }
    };
    private final Runnable statusTick = new Runnable() {
        @Override
        public void run() {
            refreshStatus();
            ui.postDelayed(this, 1000);
        }
    };

    /* ------------------------------------------------------------------ JNI */

    private static native long nativeCreate(Surface surface, String jarPath,
                                            int screenW, int screenH, int heapMB,
                                            String rmsDir, String fsDir);

    private static native void nativeRun(long handle);

    private static native void nativeKey(long handle, int keyCode, boolean down);

    private static native void nativeTouch(long handle, int id, int phase,
                                           float x, float y);

    private static native void nativeSetActive(long handle, boolean active);

    private static native void nativeSetSurface(long handle, Surface surface);

    private static native String nativeStats(long handle);

    private static native void nativeSetView(long handle, int mode, float zoom,
                                             float dx, float dy);

    private static native int[] nativeDetect(long handle);

    private static native void nativeQuit(long handle);

    private static native void nativeDestroy(long handle);

    /** Android key codes the pad may offer; used by the keypad editor. */
    static native String[] nativeKeyTable();

    /** Called from the VM thread (via JNI) when a MIDlet asks for vibration. */
    public static void vibrateDevice(int ms) {
        Vibrator v = vibrator;
        if (v != null && v.hasVibrator()) {
            v.vibrate(ms);
        }
    }

    /* Static because JNI reaches it without an instance, and the application
     * context because a static field must not pin an Activity. */
    private static Vibrator vibrator;

    /* ------------------------------------------------------------- lifecycle */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        /*
         * The platform MIDI synthesizer writes the file it is given into the
         * app cache, so it needs a Context before the VM can ask for music.
         * The application context, not this Activity: a static field must not
         * pin an Activity that is being destroyed.
         */
        MidiPlayer.init(getApplicationContext());
        /* The platform codecs.  Both are optional: if the classes are
         * missing or a device cannot decode something, the native side
         * falls back to the runtime's own decoders. */
        PlatformImage.init(getApplicationContext());
        PlatformAudio.init(getApplicationContext());
        Intent it = getIntent();
        /*
         * Two names on purpose. "jar" is the file name, which is what the
         * sidecar settings file and JarStore.byName() are keyed on; "jarpath"
         * is the absolute path, which is what the port fopen()s - a bare name
         * would be resolved against "/" and the game would not start.
         */
        jarName = it.getStringExtra("jar");
        jarPath = it.getStringExtra("jarpath");
        if (jarPath == null) {
            jarPath = jarName;
        }
        jarTitle = it.getStringExtra("title");
        screenW = it.getIntExtra("w", JarStore.DEF_W);
        screenH = it.getIntExtra("h", JarStore.DEF_H);
        heapMB = it.getIntExtra("heap", JarStore.DEF_HEAP_MB);
        viewMode = it.getIntExtra("view", VIEW_INT);
        viewZoom = it.getFloatExtra("zoom", 1.0f);
        viewPanX = it.getFloatExtra("panx", 0f);
        viewPanY = it.getFloatExtra("pany", 0f);
        if (jarName == null || jarPath == null || jarTitle == null) {
            finish();
            return;
        }
        vibrator = (Vibrator) getApplicationContext()
                .getSystemService(Context.VIBRATOR_SERVICE);

        /* A 320x240 game wants the phone on its side; a 240x320 one does not.
         * Reading it off the screen size beats guessing from the jar name. */
        setRequestedOrientation(screenW > screenH
                ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        applyFullscreen();

        padConfig = PadConfig.load(this);

        FrameLayout root = new FrameLayout(this);
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        pad = new PadView(this, padConfig);
        pad.setSink(this);
        root.addView(pad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(11f);
        status.setBackgroundColor(Color.argb(170, 0, 0, 0));
        status.setPadding(dp(8), dp(4), dp(8), dp(4));
        status.setVisibility(View.GONE);
        FrameLayout.LayoutParams slp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        slp.gravity = Gravity.BOTTOM | Gravity.START;
        slp.setMargins(dp(6), 0, 0, dp(6));
        root.addView(status, slp);

        bar = buildBar();
        FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        blp.gravity = Gravity.TOP;
        root.addView(bar, blp);

        handleButton = new TextView(this);
        handleButton.setText("≡");
        handleButton.setTextSize(18f);
        handleButton.setTextColor(Color.WHITE);
        handleButton.setGravity(Gravity.CENTER);
        handleButton.setBackgroundColor(Color.argb(120, 0, 0, 0));
        FrameLayout.LayoutParams hlp = new FrameLayout.LayoutParams(dp(34), dp(34));
        hlp.gravity = Gravity.TOP | Gravity.END;
        root.addView(handleButton, hlp);
        handleButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showBar(bar.getVisibility() != View.VISIBLE);
            }
        });

        setContentView(root);
        /*
         * A notch is a hole in the panel the framework cannot move out of the
         * way, so everything the player has to touch is told where it is: the
         * keycaps are moved clear inside PadView, the control bar, the status
         * overlay and the handle are padded by the same insets.
         */
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                applyInsets(in);
                return in;
            }
        });
        root.requestApplyInsets();
        showBar(true);
    }

    /** Keeps the controls out of the cutout and the system bars. */
    private void applyInsets(WindowInsets in) {
        int[] e = Ui.insets(in, immersive);
        insetL = e[0];
        insetT = e[1];
        insetR = e[2];
        insetB = e[3];
        if (pad != null) {
            pad.setSafeAreaPx(insetL, insetT, insetR, insetB);
        }
        if (bar != null) {
            bar.setPadding(dp(6) + insetL, dp(4) + insetT, dp(40) + insetR, dp(4));
        }
        if (status != null) {
            status.setPadding(dp(8) + insetL, dp(4), dp(8) + insetR, dp(4) + insetB);
        }
        if (handleButton != null) {
            FrameLayout.LayoutParams lp =
                (FrameLayout.LayoutParams) handleButton.getLayoutParams();
            lp.topMargin = insetT;
            lp.rightMargin = insetR;
            handleButton.setLayoutParams(lp);
        }
    }

    /** True while empty space moves the picture instead of reaching the game. */
    private boolean adjustingNow() {
        return movingPicture || viewMode == VIEW_CUSTOM;
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyFullscreen();
        /* The keypad editor may have changed the layout while we were away. */
        padConfig.replaceWith(PadConfig.load(this));
        pad.setConfig(padConfig);
        pad.setAdjusting(adjustingNow());
        applyView();
        ui.removeCallbacks(statusTick);
        ui.post(statusTick);
        synchronized (lock) {
            if (handle != 0) {
                nativeSetActive(handle, true);
            }
        }
    }

    @Override
    protected void onPause() {
        /* Park the VM before the surface goes away: a MIDlet that keeps ticking
         * behind a locked screen burns battery and gains nothing. */
        saveView();
        synchronized (lock) {
            if (handle != 0) {
                nativeSetActive(handle, false);
            }
        }
        ui.removeCallbacks(statusTick);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        finishing = true;
        ui.removeCallbacks(statusTick);
        ui.removeCallbacks(barHider);
        synchronized (lock) {
            if (handle != 0) {
                nativeQuit(handle);     /* the run thread frees the port */
            }
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        /* Not a hard exit: a MIDlet's back button is its right soft key, so the
         * key press has already gone to the game.  Back opens the bar, where
         * 退出 is one deliberate tap away. */
        showBar(true);
    }

    /* --------------------------------------------------------------- surface */

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        synchronized (lock) {
            if (handle == 0) {
                handle = nativeCreate(holder.getSurface(), jarPath, screenW, screenH,
                                      heapMB, JarStore.rmsRoot(this).getAbsolutePath(),
                                      JarStore.fsRoot(this).getAbsolutePath());
                if (handle == 0) {
                    Log.e(TAG, "port creation failed");
                    toastThenFinish();
                    return;
                }
                runThread = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        runVm();
                    }
                }, "nj2me-vm");
                applyView();
                runThread.start();
            } else {
                /* Coming back from the background: the old surface is gone and
                 * the port must be pointed at the new one. */
                nativeSetSurface(handle, holder.getSurface());
                nativeSetActive(handle, true);
            }
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        /* The port reads the window size on every present, so a resize needs no
         * announcement - the letterbox is recomputed the next frame anyway. */
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        synchronized (lock) {
            if (handle != 0) {
                nativeSetActive(handle, false);
            }
        }
    }

    private void runVm() {
        long h;
        synchronized (lock) {
            h = handle;
        }
        int rc;
        try {
            nativeRun(h);
        } catch (Throwable t) {
            Log.e(TAG, "run failed: " + t);
        }
        synchronized (lock) {
            if (handle == h) {
                nativeDestroy(h);
                handle = 0;
            }
        }
        saveView();
        ui.post(new Runnable() {
            @Override
            public void run() {
                if (!finishing) {
                    Log.i(TAG, "MIDlet finished");
                }
                /* The game is over: leave the screen rather than showing a
                 * frozen last frame the player cannot get out of. */
                finish();
            }
        });
    }

    private void toastThenFinish() {
        android.widget.Toast.makeText(this, "无法启动这个 MIDlet（看 logcat 的 nj2me 日志）",
                android.widget.Toast.LENGTH_LONG).show();
        finish();
    }

    /* ---------------------------------------------------------------- input */

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        int code = ev.getKeyCode();
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN;
        if (down && ev.getRepeatCount() > 0) {
            return true;        /* MIDP has no key repeat; a held key is held */
        }
        synchronized (lock) {
            if (handle != 0) {
                nativeKey(handle, code, down);
            }
        }
        if (code == KeyEvent.KEYCODE_BACK) {
            /* Consumed, always: the framework's idea of back is "leave the
             * activity", and a MIDlet is not finished with the screen just
             * because the player pressed its right soft key.  The key itself
             * already reached the game above. */
            if (!down) {
                onBackPressed();
            }
            return true;
        }
        return true;
    }

    @Override
    public void onPadKey(int androidKeyCode, boolean down) {
        synchronized (lock) {
            if (handle != 0) {
                nativeKey(handle, androidKeyCode, down);
            }
        }
    }

    @Override
    public void onGameTouch(int pointerId, int phase, float x, float y) {
        synchronized (lock) {
            if (handle != 0) {
                nativeTouch(handle, pointerId, phase, x, y);
            }
        }
    }

    @Override
    public void onViewGesture(float zoomFactor, float dx, float dy) {
        /* The port works in absolute zoom and absolute pan, the gesture in
         * deltas: accumulating here keeps the native side a pure function of
         * what it is told, which is also what the sidecar file stores.
         *
         * The mode is deliberately left alone: the zoom and the pan apply on
         * top of whichever fit the player chose, so moving a picture never
         * costs them the integer scaling they picked it for. */
        viewZoom *= zoomFactor;
        if (viewZoom < 0.25f) {
            viewZoom = 0.25f;
        }
        if (viewZoom > 8.0f) {
            viewZoom = 8.0f;
        }
        viewPanX += dx;
        viewPanY += dy;
        viewDirty = true;
        applyView();
    }

    /* ---------------------------------------------------------------- view */

    private void applyView() {
        synchronized (lock) {
            if (handle != 0) {
                nativeSetView(handle, viewMode, viewZoom, viewPanX, viewPanY);
            }
        }
    }

    private void setViewMode(int mode) {
        viewMode = mode;
        viewDirty = true;
        if (mode == VIEW_CUSTOM) {
            /* Free adjustment is a mode the player is in, not a setting: the
             * picture follows the finger until they pick a fixed mode again. */
            toast("拖动移动画面，双指缩放；在「画面」里选其他方式结束调整");
        } else {
            movingPicture = false;
        }
        pad.setAdjusting(adjustingNow());
        applyView();
        saveView();
        armBarTimer();
        updateBarTitle();
    }

    /**
     * 移动画面: the picture follows the finger, in whichever fit is in force.
     *
     * <p>Separate from 自由缩放 because a player who wants their 240x320 game
     * shifted off the notch should not have to give up integer scaling to do
     * it - and because a drag that moves the picture is a drag the game does
     * not get, so it has to be something the player asks for.
     */
    private void toggleMovingPicture() {
        movingPicture = !movingPicture;
        pad.setAdjusting(adjustingNow());
        if (movingPicture) {
            toast("拖动移动画面，双指缩放；再按一次「移动画面」结束");
        } else if (viewMode == VIEW_CUSTOM) {
            toast("自由缩放本身就是拖动模式；选其他方式可结束调整");
        }
        updateBarTitle();
        armBarTimer();
    }

    /** Nudges the picture by one step; shared with the 微调 dialog. */
    private void nudge(float dx, float dy) {
        viewPanX += dx;
        viewPanY += dy;
        viewDirty = true;
        applyView();
    }

    private String panText() {
        return Math.round(viewPanX) + "," + Math.round(viewPanY);
    }

    private String zoomText() {
        return (Math.round(viewZoom * 100f) / 100f) + "x";
    }

    private String viewName(int mode) {
        if (mode == VIEW_FILL) {
            return "铺满屏幕";
        }
        if (mode == VIEW_CUSTOM) {
            return "自由缩放";
        }
        if (mode == VIEW_AUTO) {
            return "适配画面";
        }
        return "整数倍缩放";
    }

    private void viewDialog() {
        int[] det = null;
        synchronized (lock) {
            if (handle != 0) {
                det = nativeDetect(handle);
            }
        }
        final int dw = det != null ? det[2] : 0;
        final int dh = det != null ? det[3] : 0;
        final String info = (dw > 0
                ? "游戏画面 " + dw + "x" + dh + "（已测量）· "
                : "还没测出画布，多等几秒再打开 · ")
                + "画布 " + screenW + "x" + screenH
                + "\n" + viewName(viewMode) + " " + zoomText()
                + " · 偏移 " + panText() + (movingPicture ? " · 拖动中" : "");
        final String[] items = {
            "整数倍缩放（默认，像素最清晰）",
            "铺满屏幕（放大到没有黑边）",
            "适配画面（自动裁掉不画的部分）",
            "自由缩放（拖动 / 双指缩放）",
            "移动画面（拖动摆位置，再点一次结束）",
            "微调位置（方向按钮一点点挪）",
            "居中（偏移归零）"
        };
        final int[] modes = { VIEW_INT, VIEW_FILL, VIEW_AUTO, VIEW_CUSTOM, -1, -2, -3 };
        for (int i = 0; i < 4; i++) {
            items[i] = (modes[i] == viewMode ? "\u2713 " : "      ") + items[i];
        }
        items[4] = (movingPicture ? "\u2713 " : "      ") + items[4];
        items[5] = "      " + items[5];
        items[6] = "      " + items[6];
        /*
         * The info line rides in a custom title instead of setMessage(): with a
         * list set as well the platform dialog drops the message on the floor.
         */
        new android.app.AlertDialog.Builder(this)
            .setCustomTitle(dialogTitle("画面 · 当前：" + viewName(viewMode), info))
            .setItems(items, new android.content.DialogInterface.OnClickListener() {
                @Override
                public void onClick(android.content.DialogInterface d, int which) {
                    if (which < 4) {
                        setViewMode(modes[which]);
                    } else if (which == 4) {
                        toggleMovingPicture();
                    } else if (which == 5) {
                        nudgeDialog();
                    } else {
                        viewPanX = 0f;
                        viewPanY = 0f;
                        viewDirty = true;
                        applyView();
                        saveView();
                        toast("画面已居中");
                    }
                }
            })
            .setPositiveButton(dw > 0 ? ("用 " + dw + "x" + dh + " 作为分辨率") : "关闭",
                    new android.content.DialogInterface.OnClickListener() {
                @Override
                public void onClick(android.content.DialogInterface d, int which) {
                    if (dw > 0) {
                        adoptDetected(dw, dh);
                    }
                }
            })
            .setNegativeButton("重置", new android.content.DialogInterface.OnClickListener() {
                @Override
                public void onClick(android.content.DialogInterface d, int which) {
                    viewZoom = 1.0f;
                    viewPanX = 0f;
                    viewPanY = 0f;
                    setViewMode(VIEW_INT);
                }
            })
            .show();
    }

    /**
     * 微调位置: a picture one step away from where the player wants it should
     * not need a drag - and the edges, where a drag is hardest to aim, are
     * exactly where a picture usually has to go to clear a notch.
     */
    private void nudgeDialog() {
        final int step = Math.max(4,
                Math.round(Math.min(pad.getWidth(), pad.getHeight()) * 0.02f));
        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.VERTICAL);
        grid.setPadding(dp(12), dp(8), dp(12), dp(8));
        final String up = "\u2191";
        final String down = "\u2193";
        final String left = "\u2190";
        final String right = "\u2192";
        final String centre = "\u25a0 居中";
        final String[][] rows = {
            { null, up, null },
            { left, centre, right },
            { null, down, null }
        };
        for (String[] row : rows) {
            LinearLayout line = new LinearLayout(this);
            line.setOrientation(LinearLayout.HORIZONTAL);
            for (String label : row) {
                if (label == null) {
                    line.addView(new View(this), new LinearLayout.LayoutParams(0, dp(44), 1f));
                    continue;
                }
                Button b = new Button(this);
                b.setText(label);
                b.setAllCaps(false);
                if (centre.equals(label)) {
                    b.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v) {
                            viewPanX = 0f;
                            viewPanY = 0f;
                            viewDirty = true;
                            applyView();
                            saveView();
                        }
                    });
                } else {
                    final float dx = left.equals(label) ? -step : right.equals(label) ? step : 0f;
                    final float dy = up.equals(label) ? -step : down.equals(label) ? step : 0f;
                    b.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v) {
                            nudge(dx, dy);
                        }
                    });
                }
                line.addView(b, new LinearLayout.LayoutParams(0, dp(44), 1f));
            }
            grid.addView(line);
        }
        new android.app.AlertDialog.Builder(this)
            .setCustomTitle(dialogTitle("微调位置 · 当前偏移 " + panText(),
                    "每按一次移动 " + step + " 像素（" + viewName(viewMode) + " "
                            + zoomText() + "）"))
            .setView(grid)
            .setNegativeButton("关闭", null)
            .show();
    }

    /** A dialog heading with a second, quieter line under it. */
    private View dialogTitle(String title, String info) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(18), dp(24), dp(4));
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextColor(0xFFFFFFFF);
        t.setTextSize(17f);
        box.addView(t);
        TextView s = new TextView(this);
        s.setText(info);
        s.setTextColor(0xFF9AA0A6);
        s.setTextSize(12f);
        s.setPadding(0, dp(6), 0, 0);
        box.addView(s);
        return box;
    }

    /**
     * Adopt what the port measured as this game's screen size.
     *
     * <p>The size cannot change while the game runs - the MIDlet's canvas is
     * created once - so this is remembered for the next launch, and the player
     * is told rather than left wondering why nothing moved.
     */
    private void adoptDetected(int w, int h) {
        JarStore.Entry e = JarStore.byName(this, jarName);
        if (e == null) {
            toast("找不到这个游戏的记录");
            return;
        }
        e.w = w;
        e.h = h;
        e.detW = w;
        e.detH = h;
        JarStore.write(this, e);
        toast("已记住 " + w + "x" + h + "，下次启动生效");
    }

    private void saveView() {
        if (!viewDirty) {
            return;
        }
        JarStore.Entry e = JarStore.byName(this, jarName);
        if (e == null) {
            return;
        }
        e.viewMode = viewMode;
        e.zoom = viewZoom;
        e.panX = viewPanX;
        e.panY = viewPanY;
        if (detW > 0) {
            e.detW = detW;
            e.detH = detH;
        }
        JarStore.write(this, e);
        viewDirty = false;
    }

    private void toast(String s) {
        android.widget.Toast.makeText(this, s, android.widget.Toast.LENGTH_SHORT).show();
    }

    /* ------------------------------------------------------------------- bar */

    private LinearLayout buildBar() {
        LinearLayout b = new LinearLayout(this);
        b.setOrientation(LinearLayout.HORIZONTAL);
        b.setBackgroundColor(Color.argb(200, 16, 16, 16));
        b.setGravity(Gravity.CENTER_VERTICAL);
        b.setPadding(dp(6), dp(4), dp(40), dp(4));

        TextView title = new TextView(this);
        titleView = title;
        updateBarTitle();
        title.setTextColor(Color.WHITE);
        title.setTextSize(13f);
        title.setSingleLine(true);
        title.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        b.addView(title, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        b.addView(barButton("画面", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                viewDialog();
            }
        }));
        b.addView(barButton("按键", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showBar(false);
                startActivity(new Intent(GameActivity.this, PadEditActivity.class));
            }
        }));
        b.addView(barButton("状态", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                status.setVisibility(status.getVisibility() == View.VISIBLE
                        ? View.GONE : View.VISIBLE);
                refreshStatus();
                armBarTimer();
            }
        }));
        b.addView(barButton("退出", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                synchronized (lock) {
                    if (handle != 0) {
                        nativeQuit(handle);
                    }
                }
                showBar(false);
            }
        }));
        return b;
    }

    private TextView barButton(String label, View.OnClickListener click) {
        TextView t = new TextView(this);
        t.setText(label);
        t.setTextColor(Color.WHITE);
        t.setTextSize(13f);
        t.setPadding(dp(10), dp(6), dp(10), dp(6));
        t.setOnClickListener(click);
        return t;
    }

    private void showBar(boolean showing) {
        bar.setVisibility(showing ? View.VISIBLE : View.GONE);
        pad.setBarShowing(showing);
        handleButton.setVisibility(showing ? View.GONE : View.VISIBLE);
        ui.removeCallbacks(barHider);
        if (showing) {
            armBarTimer();
        }
    }

    private void armBarTimer() {
        ui.removeCallbacks(barHider);
        ui.postDelayed(barHider, BAR_TIMEOUT_MS);
    }

    private void refreshStatus() {
        String s;
        int[] det = null;
        synchronized (lock) {
            s = handle != 0 ? nativeStats(handle) : null;
            if (handle != 0) {
                det = nativeDetect(handle);
            }
        }
        if (s == null) {
            s = "starting…";
        }
        if (det != null) {
            detW = det[2];
            detH = det[3];
            s = s + "\ndetected " + detW + "x" + detH + " (frames " + det[4] + ")";
            /*
             * Store it once the box has stopped growing.  One measurement is
             * not enough: a game that reveals its screen a row at a time - a
             * scroll, a wipe, a loading bar - would be caught mid-draw and
             * then keep that smaller canvas for good.  Two polls a second
             * apart with the same box, after two seconds of frames, is a
             * picture that has settled.
             */
            if (detW == detLastW && detH == detLastH) {
                detStable++;
            } else {
                detStable = 0;
                detLastW = detW;
                detLastH = detH;
            }
            if (!detSaved && det[4] >= 60 && detStable >= 1) {
                detSaved = true;
                JarStore.noteDetected(this, jarName, detW, detH);
            }
        }
        status.setText(jarTitle + "  " + screenW + "x" + screenH + "\n" + s);
    }

    /** Shows 调整画面 in the bar while the picture follows the finger. */
    private void updateBarTitle() {
        if (titleView != null) {
            titleView.setText(movingPicture ? jarTitle + " · 调整画面" : jarTitle);
        }
    }

    /**
     * Immersive mode and the notch, both of which the player can turn on.
     *
     * <p>The SYSTEM_UI_FLAG_* family this used to rely on has been a no-op
     * since Android 11 - on the API 36 emulator the clock and the 5G icon sat
     * on top of the MIDlet's own title bar.  API 30's WindowInsetsController is
     * the replacement, so the flags are only the fallback for older devices,
     * and everything that mentions the new API lives in {@link Ui} where a
     * device that predates it never loads the class.
     */
    private void applyFullscreen() {
        immersive = Ui.immersive(this);
        Ui.apply(this, immersive);
    }

    /** A swipe or a dialog can bring the system bars back; take them away again. */
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyFullscreen();
        }
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
