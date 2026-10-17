package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.media.AudioManager;
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
import android.widget.EditText;
import android.text.Editable;
import android.text.TextWatcher;
import android.text.InputType;
import android.text.InputFilter;
import android.text.method.PasswordTransformationMethod;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputMethodManager;
import java.lang.ref.WeakReference;

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
    private TextView status;
    private TextView handleButton;
    private FrameLayout root;      /* the whole window: the handle is placed in it */
    private static volatile WeakReference<GameActivity> textActivity = new WeakReference<>(null);
    private ImeEditor imeEditor;
    private boolean imeApplying, imeActive, imePaused;
    private int imeSession, imeSequence, imeSentCursor;
    private String imeSentText = "";
    private TextSnapshot imeSnapshot;
    private boolean imeShowPending, imeRestartPending;
    private final Runnable imeShow = new Runnable() {
        @Override public void run() {
            if (!imeShowPending || !imeActive || imePaused || finishing || imeEditor == null) { return; }
            /* A visible editor can acquire view focus before its window has
             * input focus. Keep the request until onWindowFocusChanged retries. */
            if (imeEditor.getWindowToken() == null || !imeEditor.hasWindowFocus()) { return; }
            if (!imeEditor.requestFocus()) { return; }
            InputMethodManager manager = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if (manager == null) { return; }
            if (imeRestartPending) {
                manager.restartInput(imeEditor);
                imeRestartPending = false;
            }
            imeShowPending = !manager.showSoftInput(imeEditor, InputMethodManager.SHOW_IMPLICIT);
        }
    };
    private final Runnable imePublish = new Runnable() {
        @Override public void run() { publishIme(); }
    };

    private String jarPath = "";   /* absolute: what the port opens */
    private String jarName = "";   /* file name: the JarStore key */
    private String jarTitle = "";
    private String statePath;      /* the snapshot to restore, or null */
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
    private int maxPanX, maxPanY;   /* how far the pan may go, from the port */
    private boolean viewDirty;      /* worth writing back to the sidecar */
    private boolean detSaved;       /* the measured resolution is already stored */
    private int detW, detH;
    private int detLastW, detLastH, detStable;

    /* 沉浸模式: the player's setting, plus where the notch and bars are. */
    private boolean immersive;
    private int speed = 100;      /* game speed, percent of real time */
    private int pictureAA;        /* 0 nearest, 1 bilinear: 画面反锯齿 */
    private int insetL, insetT, insetR, insetB;

    private final Handler ui = new Handler(Looper.getMainLooper());
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
    private static native boolean nativeText(long handle, int session, int sequence,
                                              String text, int cursor);

    private static native void nativeTouch(long handle, int id, int phase,
                                           float x, float y);

    private static native void nativeSetActive(long handle, boolean active);

    private static native void nativeSetSurface(long handle, Surface surface);

    private static native String nativeStats(long handle);

    private static native void nativeSetView(long handle, int mode, float zoom,
                                             float dx, float dy);

    private static native int[] nativeDetect(long handle);
    private static native int[] nativeViewLimits(long handle);

    /**
     * Game speed in percent of real time.  The port applies it on its next
     * frame; the VM is never touched from this thread.
     */
    private static native void nativeSetSpeed(long handle, int percent);

    /**
     * Picture anti-aliasing: 1 mixes the neighbours when the picture is scaled
     * by a fraction (铺满, 自定义), 0 is the solid-block nearest neighbour every
     * port has always drawn.  Like the speed word above, it is applied by the
     * present loop on its next frame.
     */
    private static native void nativeSetFilter(long handle, int linear);

    /** Drop the glyphs the port has cached (new typeface or anti-aliasing). */
    private static native void nativeGlyphReset(long handle);

    /*
     * State save/restore.  The VM runs on its own thread, so the UI thread only
     * ever asks: nativeSetRestore() is read once, before the MIDlet starts,
     * and nativeSaveState() leaves a request that the VM thread performs at its
     * next frame boundary and then waits for - a frame is a few milliseconds,
     * and an answer that says "saved" when nothing was written is worse than a
     * slow one.
     */
    private static native void nativeSetRestore(long handle, String path);
    private static native int nativeSaveState(long handle, String path);

    private static native void nativeQuit(long handle);

    private static native void nativeDestroy(long handle);

    /** Android key codes the pad may offer; used by the keypad editor. */
    static native String[] nativeKeyTable();

    /** Called from the VM thread (via JNI) when a MIDlet asks for vibration. */
    public static void vibrateDevice(int ms) {
        Vibrator v = vibrator;
        if (v != null && v.hasVibrator()) {
            try {
                v.vibrate(ms);
            } catch (SecurityException e) {
                /* a MIDlet may ask for vibration the app is not allowed to give;
                   the request is dropped rather than thrown back at the VM */
            } catch (RuntimeException e) {
            }
        }
    }

    /* Static because JNI reaches it without an instance, and the application
     * context because a static field must not pin an Activity. */
    private static Vibrator vibrator;

    /* ------------------------------------------------------------- lifecycle */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        textActivity = new WeakReference<>(this);
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
        /* HTTP for javax.microedition.io.HttpConnection.  It needs no Context
         * of its own (HttpURLConnection is checked against the manifest's
         * INTERNET permission when the socket opens); the call is here so a
         * missing bridge shows up in the log at start-up rather than at the
         * first request. */
        PlatformHttp.init(getApplicationContext());
        /* JSR 256 sensors.  Optional in the same way: without it the C hook
         * answers "no sensors", so findSensors() comes back empty instead of
         * the package being reported missing. */
        PlatformSensor.init(getApplicationContext());
        /* Stream sockets, for SocketConnection.  Optional in the same way:
         * without it Connector.open("socket://...") refuses by name. */
        PlatformSocket.init(getApplicationContext());
        /* Bluetooth discovery, for javax.bluetooth.  The emulator has no
         * adapter, so this one mostly answers "no radio" - which is exactly
         * the answer the runtime turns into BluetoothStateException. */
        PlatformBluetooth.init(getApplicationContext());
        /* The Material 3 palette, before the first view is built: the dialogs
         * this screen opens are the framework's, and they have to agree with
         * the colours the library used. */
        Md3.init(this);
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
        statePath = it.getStringExtra("state");
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

        padConfig = PadConfig.forGame(this, jarName);
        JarStore.Entry se = JarStore.byName(this, jarName);
        if (se != null && se.speed > 0) {
            speed = se.speed;
        }
        /*
         * The player's two per-game switches.  They are statics on the
         * platform bridges because that is where the MIDlet's connections end
         * up, and they are set here - before the VM exists - so no game can
         * get a connection or a sensor the settings deny.
         */
        boolean net = (se == null) || se.net != 0;
        PlatformHttp.setAllowed(net);
        PlatformSocket.setAllowed(net);
        PlatformSensor.setMode(se == null ? 0 : se.sensor);

        /*
         * The player's display settings, applied before the VM exists: the
         * glyph source is read from the VM thread on the very first frame of
         * text, so the font and its anti-aliasing have to be in place by then.
         */
        applyDisplaySettings();

        FrameLayout root = new FrameLayout(this);
        this.root = root;
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        pad = new PadView(this, padConfig);
        pad.setSink(this);
        root.addView(pad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        status = new TextView(this);
        status.setTextColor(Md3.OVERLAY_MUTED);
        status.setTextSize(11f);
        status.setBackground(Md3.round(this, Md3.OVERLAY, 12f));
        status.setPadding(dp(10), dp(6), dp(10), dp(6));
        status.setVisibility(View.GONE);
        FrameLayout.LayoutParams slp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        slp.gravity = Gravity.BOTTOM | Gravity.START;
        slp.setMargins(dp(6), 0, 0, dp(6));
        root.addView(status, slp);

        handleButton = new TextView(this);
        handleButton.setText("≡");
        Md3.overlayHandle(handleButton, this);
        FrameLayout.LayoutParams hlp = new FrameLayout.LayoutParams(dp(40), dp(40));
        /* Where it sits comes from the pad config, not from gravity: the player
         * moves it in the editor and hides it from the panel. */
        hlp.gravity = Gravity.TOP | Gravity.START;
        root.addView(handleButton, hlp);
        handleButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showPanel();
            }
        });

        imeEditor = new ImeEditor(this);
        imeEditor.setVisibility(View.GONE);
        imeEditor.setTextColor(Color.BLACK);
        imeEditor.setBackgroundColor(Color.WHITE);
        imeEditor.setHint("输入文字");
        FrameLayout.LayoutParams imeLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM);
        root.addView(imeEditor, imeLayout);
        imeEditor.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) { }
            @Override public void afterTextChanged(Editable value) { scheduleIme(); }
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
        positionHandle();
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
        if (status != null) {
            status.setPadding(dp(8) + insetL, dp(4), dp(8) + insetR, dp(4) + insetB);
        }
        if (handleButton != null) {
            positionHandle();
        }
    }

    /**
     * Puts the ≡ handle where the player left it, out of the cutout and on the
     * window.
     *
     * <p>The position is stored as a fraction of the window, so it means the
     * same thing after a rotation; the size is in dp, so the clamp has to be
     * done in pixels.  Hiding it is a config flag, which is why this is also
     * what the panel's 显示菜单按钮 row calls: the handle can never be hidden
     * without a way back.
     */
    private void positionHandle() {
        if (handleButton == null || padConfig == null) {
            return;
        }
        handleButton.setVisibility(padConfig.menuVisible ? View.VISIBLE : View.GONE);
        int w = windowW();
        int h = windowH();
        if (w <= 0 || h <= 0) {
            return;
        }
        int size = dp(Math.round(PadConfig.MENU_DP));
        int maxX = Math.max(0, w - insetR - size);
        int maxY = Math.max(0, h - insetB - size);
        int x = Math.round(padConfig.menuX * w);
        int y = Math.round(padConfig.menuY * h);
        x = Math.min(Math.max(x, Math.min(insetL, maxX)), maxX);
        y = Math.min(Math.max(y, Math.min(insetT, maxY)), maxY);
        FrameLayout.LayoutParams lp =
            (FrameLayout.LayoutParams) handleButton.getLayoutParams();
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.leftMargin = x;
        lp.topMargin = y;
        lp.rightMargin = 0;
        lp.bottomMargin = 0;
        handleButton.setLayoutParams(lp);
    }

    /** The window in pixels: the laid-out root, or the display before that. */
    private int windowW() {
        if (root != null && root.getWidth() > 0) {
            return root.getWidth();
        }
        return getResources().getDisplayMetrics().widthPixels;
    }

    private int windowH() {
        if (root != null && root.getHeight() > 0) {
            return root.getHeight();
        }
        return getResources().getDisplayMetrics().heightPixels;
    }

    /**
     * The first run of a game gets a picture that is not under the pad.
     *
     * <p>Only the first: as soon as the player has a picture setting of their
     * own - moved it, zoomed it, or saved anything from the editor - that is
     * what runs, and this makes no difference at all.  The result is not
     * written to the sidecar on purpose: it is a property of this window and
     * this pad, and the next launch works it out again.
     */
    private void autoLayoutIfFresh() {
        JarStore.Entry e = (jarName != null && jarName.length() > 0)
                ? JarStore.byName(this, jarName) : null;
        if (e != null && e.viewSet) {
            return;
        }
        int w = windowW();
        int h = windowH();
        AutoLayout.Fit fit = AutoLayout.fit(w, h, screenW, screenH,
                insetL / (float) w, insetT / (float) h,
                (w - insetR) / (float) w, (h - insetB) / (float) h,
                padConfig.keys);
        if (fit == null) {
            return;
        }
        viewMode = fit.mode;
        viewZoom = fit.zoom;
        viewPanX = fit.panX;
        viewPanY = fit.panY;
        if (e != null) {
            /* Publish the numbers we are about to run with, so the editor
             * previews the same picture the game shows and saving from it does
             * not snap the picture back to the centred fit.  viewSet stays
             * false: this is still the app's choice, so the next launch may
             * work it out again - for a pad the player has since edited, for
             * instance - right up to the moment they move or save a picture. */
            e.viewMode = viewMode;
            e.zoom = viewZoom;
            e.panX = viewPanX;
            e.panY = viewPanY;
            JarStore.write(this, e);
        }
        if (fit.scale > 1) {
            Log.i(TAG, "picture: " + fit.scale + "x in the pad-free band ("
                    + Math.round(fit.panX) + "," + Math.round(fit.panY) + ")");
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        imePaused = false;
        if (imeActive && imeEditor != null) { requestIme(true); }
        applyFullscreen();
        /* The editor may have changed the layout or moved the picture while
         * we were away - it is the one screen where both are set up. */
        padConfig.replaceWith(PadConfig.forGame(this, jarName));
        pad.setConfig(padConfig);
        positionHandle();
        reloadView();
        autoLayoutIfFresh();
        applyDisplaySettings();
        MidiPlayer.resumeAll();
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
        publishIme();
        imePaused = true;
        hideIme();
        /* Park the VM before the surface goes away: a MIDlet that keeps ticking
         * behind a locked screen burns battery and gains nothing. */
        saveView();
        synchronized (lock) {
            if (handle != 0) {
                nativeSetActive(handle, false);
            }
        }
        /* The port pauses its mixer; a MIDI Player is a MediaPlayer and has to
         * be paused here too, or the music follows the player into the library. */
        MidiPlayer.pauseAll();
        ui.removeCallbacks(statusTick);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        finishing = true;
        hideIme();
        if (textActivity.get() == this) { textActivity = new WeakReference<>(null); }
        /* The VM is gone by now, but a Player it left started is not: the shell
         * closes every MediaPlayer it opened, or the music outlives the game. */
        MidiPlayer.closeAll();
        ui.removeCallbacks(statusTick);
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
         * key press has already gone to the game.  Back opens the panel, where
         * 退出 is one deliberate tap away. */
        showPanel();
    }

    /* --------------------------------------------------------------- surface */

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        synchronized (lock) {
            if (handle == 0) {
                handle = nativeCreate(holder.getSurface(), jarPath, screenW, screenH,
                                      heapMB, JarStore.rmsRoot(this).getAbsolutePath(),
                                      JarStore.fsRoot(this).getAbsolutePath());
            if (handle != 0) {
                nativeSetSpeed(handle, speed);
                nativeSetFilter(handle, pictureAA);
                /* A new MIDlet: its own vocabulary is what the subset row will
                 * keep, so the previous game's stops being recorded here. */
                GlyphSource.beginRun();
                if (statePath != null) {
                    /* Read on the VM thread before the MIDlet starts: a
                     * restored VM resumes mid-method, so there is nothing to
                     * start. */
                    nativeSetRestore(handle, statePath);
                }
            }
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
        /* The VM has been torn down; anything it left playing is ours to stop. */
        MidiPlayer.closeAll();
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

    /* Native text is exchanged as Java UTF-16, never JNI modified UTF-8. */
    private static final class TextSnapshot {
        final long owner;
        final boolean active, multiline;
        final int session, sequence, constraints, max, cursor;
        final String text;
        TextSnapshot(long owner, boolean active, int session, int sequence, int constraints,
                     boolean multiline, String text, int max, int cursor) {
            this.owner = owner; this.active = active; this.session = session;
            this.sequence = sequence; this.constraints = constraints; this.multiline = multiline;
            this.text = text; this.max = max; this.cursor = cursor;
        }
    }

    /** JNI worker callback: all widget and keyboard work stays on the UI thread. */
    public static void updateNativeText(long owner, boolean active, int session, int sequence,
            int constraints, boolean multiline, String text, int max, int cursor) {
        final GameActivity activity = textActivity.get();
        if (activity == null) { return; }
        final TextSnapshot snapshot = new TextSnapshot(owner, active, session, sequence,
                constraints, multiline, text, max, cursor);
        activity.ui.post(new Runnable() {
            @Override public void run() {
                synchronized (activity.lock) {
                    if (activity.handle != snapshot.owner || activity.finishing) { return; }
                }
                activity.applyIme(snapshot);
            }
        });
    }

    private final class ImeEditor extends EditText {
        ImeEditor(Context context) { super(context); }
        @Override protected void onSelectionChanged(int start, int end) {
            super.onSelectionChanged(start, end);
            scheduleIme();
        }
        @Override public InputConnection onCreateInputConnection(EditorInfo info) {
            InputConnection connection = super.onCreateInputConnection(info);
            if (connection == null) { return null; }
            return new InputConnectionWrapper(connection, false) {
                @Override public boolean finishComposingText() {
                    boolean result = super.finishComposingText(); scheduleIme(); return result;
                }
                @Override public boolean commitText(CharSequence text, int cursor) {
                    boolean result = super.commitText(text, cursor); scheduleIme(); return result;
                }
                @Override public boolean endBatchEdit() {
                    boolean result = super.endBatchEdit(); scheduleIme(); return result;
                }
            };
        }
    }

    private void scheduleIme() {
        if (imeApplying || !imeActive || imePaused || finishing) { return; }
        ui.removeCallbacks(imePublish);
        ui.post(imePublish);
    }

    private void publishIme() {
        if (!imeActive || imePaused || imeApplying || imeEditor == null || finishing) { return; }
        Editable editable = imeEditor.getText();
        if (BaseInputConnection.getComposingSpanStart(editable) >= 0 ||
                BaseInputConnection.getComposingSpanEnd(editable) >= 0) { return; }
        String text = editable.toString();
        int cursor = Math.max(0, imeEditor.getSelectionEnd());
        if (text.equals(imeSentText) && cursor == imeSentCursor) { return; }
        boolean accepted = false;
        synchronized (lock) {
            if (handle != 0) {
                accepted = nativeText(handle, imeSession, imeSequence + 1, text, cursor);
            }
        }
        if (accepted) {
            ++imeSequence; imeSentText = text; imeSentCursor = cursor;
        } else if (!finishing && handle != 0) {
            /* Queue pressure is temporary; keep the latest complete edit. */
            ui.postDelayed(imePublish, 32);
        }
    }

    private void requestIme(boolean restart) {
        if (!imeActive || imePaused || finishing || imeEditor == null) { return; }
        imeShowPending = true;
        imeRestartPending |= restart;
        imeEditor.setVisibility(View.VISIBLE);
        imeEditor.requestFocus();
        ui.removeCallbacks(imeShow);
        /* Run after visibility/focus changes reach the view root. */
        imeEditor.post(imeShow);
    }

    private void hideIme() {
        imeShowPending = imeRestartPending = false;
        ui.removeCallbacks(imeShow);
        ui.removeCallbacks(imePublish);
        if (imeEditor == null) { return; }
        InputMethodManager manager = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
        if (manager != null) { manager.hideSoftInputFromWindow(imeEditor.getWindowToken(), 0); }
        imeEditor.clearFocus();
        imeEditor.setVisibility(View.GONE);
    }

    private void applyIme(TextSnapshot snapshot) {
        boolean newSession = imeSession != snapshot.session;
        imeSnapshot = snapshot;
        imeActive = snapshot.active;
        if (!imeActive || imePaused) { hideIme(); return; }
        if (!newSession && (snapshot.sequence < imeSequence ||
                !imeEditor.getText().toString().equals(imeSentText) ||
                imeEditor.getSelectionEnd() != imeSentCursor)) { return; }
        imeApplying = true;
        try {
            if (newSession) {
                ui.removeCallbacks(imePublish);
                imeSession = snapshot.session;
                imeSequence = snapshot.sequence;
                int kind = snapshot.constraints & 0xffff;
                boolean password = (snapshot.constraints & 0x10000) != 0;
                int type = InputType.TYPE_CLASS_TEXT;
                if (kind == 2) { type = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED; }
                else if (kind == 5) { type = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED | InputType.TYPE_NUMBER_FLAG_DECIMAL; }
                else if (kind == 3) { type = InputType.TYPE_CLASS_PHONE; }
                else if (kind == 1) { type |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS; }
                else if (kind == 4) { type |= InputType.TYPE_TEXT_VARIATION_URI; }
                else if (snapshot.multiline) { type |= InputType.TYPE_TEXT_FLAG_MULTI_LINE; }
                if (password || (snapshot.constraints & (0x80000 | 0x40000)) != 0) {
                    type |= InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
                }
                if (kind == 0 && (snapshot.constraints & 0x100000) != 0) { type |= InputType.TYPE_TEXT_FLAG_CAP_WORDS; }
                if (kind == 0 && (snapshot.constraints & 0x200000) != 0) { type |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES; }
                imeEditor.setInputType(type);
                imeEditor.setSingleLine(!snapshot.multiline || kind != 0);
                imeEditor.setTransformationMethod(password ? PasswordTransformationMethod.getInstance() : null);
                imeEditor.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN |
                        (snapshot.multiline ? EditorInfo.IME_ACTION_NONE : EditorInfo.IME_ACTION_DONE));
                imeEditor.setFilters(new InputFilter[] { new InputFilter.LengthFilter(Math.max(1, snapshot.max)) });
            }
            /* Do not discard OS composing spans when a delayed acknowledgement
             * contains exactly the text already present in the editor. */
            if (!imeEditor.getText().toString().equals(snapshot.text)) { imeEditor.setText(snapshot.text); }
            int cursor = Math.max(0, Math.min(snapshot.cursor, imeEditor.length()));
            if (imeEditor.getSelectionEnd() != cursor) { imeEditor.setSelection(cursor); }
            imeSentText = snapshot.text; imeSentCursor = cursor;
            imeEditor.setVisibility(View.VISIBLE);
            imeEditor.requestFocus();
            if (newSession) { requestIme(true); }
        } finally { imeApplying = false; }
    }

    /* ---------------------------------------------------------------- input */

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        int code = ev.getKeyCode();
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN;
        /*
         * The hardware rocker stays the system's.  The pad can now carry 音量+
         * / 音量- (PadConfig.full(), or any preset the player edits), and those
         * two go to the MIDlet like every other pad key - but a physical key
         * that the framework, the player's muscle memory and every other app
         * agree means "the phone's volume" is not a key a J2ME title gets to
         * swallow.  A game that wants its own volume has the pad keys, which
         * is exactly what item 4 of this round asked for; the documented
         * decision is that the rocker is never forwarded.
         */
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN
                || code == KeyEvent.KEYCODE_VOLUME_MUTE) {
            AudioManager am = (AudioManager) getSystemService(AUDIO_SERVICE);
            if (am == null) {
                return false;       /* let the framework do it the usual way */
            }
            int dir = code == KeyEvent.KEYCODE_VOLUME_UP ? AudioManager.ADJUST_RAISE
                    : (code == KeyEvent.KEYCODE_VOLUME_DOWN ? AudioManager.ADJUST_LOWER
                    : AudioManager.ADJUST_TOGGLE_MUTE);
            am.adjustStreamVolume(AudioManager.STREAM_MUSIC, dir, AudioManager.FLAG_SHOW_UI);
            return true;
        }
        if (imeActive && !imePaused && imeEditor != null && imeEditor.hasFocus()) {
            if (code == KeyEvent.KEYCODE_BACK) {
                if (!down) {
                    InputMethodManager manager = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
                    if (manager != null) { manager.hideSoftInputFromWindow(imeEditor.getWindowToken(), 0); }
                }
                return true;
            }
            if (ev.getUnicodeChar() != 0 || code == KeyEvent.KEYCODE_DEL ||
                    code == KeyEvent.KEYCODE_FORWARD_DEL || code == KeyEvent.KEYCODE_ENTER ||
                    (code >= KeyEvent.KEYCODE_DPAD_UP && code <= KeyEvent.KEYCODE_DPAD_CENTER) ||
                    code == KeyEvent.KEYCODE_MOVE_HOME || code == KeyEvent.KEYCODE_MOVE_END ||
                    ev.getAction() == KeyEvent.ACTION_MULTIPLE) {
                return super.dispatchKeyEvent(ev);
            }
        }
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

    /**
     * Pick up a picture the editor moved, without restarting the game.
     *
     * <p>The port keeps the geometry the player chose, so a change here is
     * applied on the next frame rather than at the next launch: walking back
     * from 调整 shows the new position immediately.
     */
    private void reloadView() {
        JarStore.Entry e = JarStore.byName(this, jarName);
        if (e == null) {
            return;
        }
        viewMode = e.viewMode;
        if (e.zoom > 0.05f) {
            viewZoom = e.zoom;
        }
        viewPanX = e.panX;
        viewPanY = e.panY;
        if (e.detW > 0) {
            detW = e.detW;
            detH = e.detH;
        }
    }

    private void applyView() {
        synchronized (lock) {
            if (handle != 0) {
                clampPan();
                nativeSetView(handle, viewMode, viewZoom, viewPanX, viewPanY);
            }
        }
    }

    /*
     * The frame loop clamps the picture to the panel; clamping the pan it was
     * given as well keeps 偏移 in the 画面 dialog honest, and stops a long drag
     * from winding up an offset the player would have to unwind before the
     * picture moved back.  Before the first frame there are no limits to clamp
     * against, so a pan restored from the launcher survives.
     *
     * Caller holds `lock`.
     */
    private void clampPan() {
        int[] lim = nativeViewLimits(handle);
        if (lim == null || lim.length < 2) {
            return;
        }
        maxPanX = lim[0];
        maxPanY = lim[1];
        if (viewPanX > maxPanX) {
            viewPanX = maxPanX;
        } else if (viewPanX < -maxPanX) {
            viewPanX = -maxPanX;
        }
        if (viewPanY > maxPanY) {
            viewPanY = maxPanY;
        } else if (viewPanY < -maxPanY) {
            viewPanY = -maxPanY;
        }
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
        /* The player moved it: from here on the first-run fit keeps out. */
        e.viewSet = true;
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

    /**
     * Push the player's display settings into the two places that need them.
     *
     * <p>The font ones go to {@link GlyphSource}, which the VM thread calls
     * through JNI - set here (before the MIDlet starts) and again in
     * {@code onResume}, because the player can change them from the panel and
     * come back.  The picture filter goes to the native port, which reads it in
     * its present loop.
     */
    private void applyDisplaySettings() {
        /*
         * The Java cache is emptied inside these two when something changed;
         * the port's own cache is keyed without the typeface or the
         * anti-aliasing, so it has to be dropped too or the old glyphs keep
         * being drawn.
         */
        GlyphSource.setAntiAlias(Settings.fontAntiAlias(this));
        GlyphSource.setFontFile(Settings.fontFile(this));
        pictureAA = Settings.pictureAntiAlias(this) ? 1 : 0;
        synchronized (lock) {
            if (handle != 0) {
                nativeSetFilter(handle, pictureAA);
                nativeGlyphReset(handle);
            }
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != FontPicker.REQUEST) {
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        String path = FontPicker.install(this, data.getData());
        applyDisplaySettings();
        Log.i(TAG, path != null ? "font: using " + path : "font: not a font file");
        toast(path != null ? "字体已更换" : "这个文件不是字体，还是用系统字体");
    }

    /* ----------------------------------------------------------------- panel */

    /**
     * The game's controls, as a panel instead of a strip across the top.
     *
     * <p>A bar that is always there covers the MIDlet's own title row, and
     * while it is up it swallows every pad touch - which is how the first
     * seconds of a game went missing.  The panel appears when the ≡ handle is
     * tapped, leaves nothing on screen when it is dismissed, and the game
     * underneath keeps running.
     */
    /**
     * The in-game panel, opened by the handle in the corner or by Back.  It is
     * a Material 3 card rather than a strip across the top: the picture keeps
     * the whole screen, nothing is covered while the player plays, and 退出 is
     * one deliberate tap inside a dialog instead of a button next to the game.
     */
    private void showPanel() {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, jarTitle));
        box.addView(Md3.supporting(this, screenW + "x" + screenH
                + (detW > 0 ? " · 画面 " + detW + "x" + detH : "")), Md3.gap(this, 6));

        final boolean shown = status.getVisibility() == View.VISIBLE;
        LinearLayout adjust = Md3.row(this, Md3.IC_TUNE, "调整画面和按键",
                "拖动画面、摆放按键，都在这一页", false, null);
        LinearLayout toggle = Md3.row(this, Md3.IC_INFO,
                shown ? "隐藏状态信息" : "显示状态信息",
                shown ? "收起帧率、缩放这些数字" : "看帧率、缩放和测到的画面大小", false, null);
        LinearLayout fast = Md3.row(this, Md3.IC_PLAY, "游戏速度",
                speedText() + " · 慢动作到三倍速", false, null);
        LinearLayout menuRow = Md3.row(this, Md3.IC_KEYS,
                padConfig.menuVisible ? "隐藏菜单按钮" : "显示菜单按钮",
                padConfig.menuVisible
                        ? "把角上的 ≡ 收起来；按返回键还能打开这个面板"
                        : "把角上的 ≡ 放回来", false, null);
        /*
         * 文字 and 画面 anti-aliasing, and the font.  The switches are the
         * player's, not this game's, so they are the same rows the library's
         * menu offers - and they change what the VM thread reads, so they are
         * written through Settings and re-applied here.
         */
        final boolean fontAA = Settings.fontAntiAlias(this);
        final boolean picAA = Settings.pictureAntiAlias(this);
        final String fontPath = Settings.fontFile(this);
        int[] glyphs = GlyphSource.stats();
        LinearLayout fontAaRow = Md3.row(this, Md3.IC_KEYS,
                fontAA ? "关闭文字反锯齿" : "打开文字反锯齿",
                fontAA ? "文字边缘现在是平滑的；关掉是掌机的硬边像素"
                       : "文字边缘现在没有平滑；打开更接近现在的手机",
                fontAA, null);
        LinearLayout fontRow = Md3.row(this, Md3.IC_IMPORT, "更换字体…",
                fontPath == null ? "MIDlet 的文字现在由系统字体绘制；点这里挑一个字体文件"
                                 : "当前：" + new java.io.File(fontPath).getName()
                                   + "（长按换回系统字体）",
                false, null);
        LinearLayout subsetRow = Md3.row(this, Md3.IC_TUNE, "只保留这个游戏用到的字",
                "字形缓存 " + glyphs[0] + " 个 · " + FontKit.size(glyphs[1]), false, null);
        LinearLayout picAaRow = Md3.row(this, Md3.IC_SCREEN,
                picAA ? "关闭画面反锯齿" : "打开画面反锯齿",
                picAA ? "画面现在会做平滑；关掉是原来的方块像素"
                      : "画面缩放时是方块锯齿；铺满和自定义时打开更平滑",
                picAA, null);
        LinearLayout save = Md3.row(this, Md3.IC_MEMORY, "保存状态",
                "把这一局的进度存下来，回到游戏卡片里选「恢复进度」", false, null);
        LinearLayout quit = Md3.dangerRow(this, Md3.IC_POWER, "退出游戏", null);

        box.addView(adjust, Md3.gap(this, 12));
        box.addView(Md3.divider(this));
        box.addView(toggle);
        box.addView(Md3.divider(this));
        box.addView(fast);
        box.addView(Md3.divider(this));
        box.addView(fontAaRow);
        box.addView(Md3.divider(this));
        box.addView(fontRow);
        box.addView(Md3.divider(this));
        box.addView(subsetRow);
        box.addView(Md3.divider(this));
        box.addView(picAaRow);
        box.addView(Md3.divider(this));
        box.addView(menuRow);
        box.addView(Md3.divider(this));
        box.addView(save);
        box.addView(Md3.divider(this));
        box.addView(quit);

        final AlertDialog dlg = Md3.show(new AlertDialog.Builder(this)
                .setView(Md3.scroller(this, box))
                .setNegativeButton("关闭", null), this);

        adjust.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                /* One screen for the picture and the keys, so the player can
                 * see where the game sits while placing the controls - and so
                 * that the answer to "where did my keys go" is one tap, not
                 * two dialogs in a different activity. */
                Intent it = new Intent(GameActivity.this, PadEditActivity.class);
                it.putExtra("jar", jarName);
                it.putExtra("w", screenW);
                it.putExtra("h", screenH);
                startActivity(it);
            }
        });
        toggle.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                status.setVisibility(shown ? View.GONE : View.VISIBLE);
                refreshStatus();
                dlg.dismiss();
            }
        });
        fast.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                speedDialog();
            }
        });
        fontAaRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Settings.setFontAntiAlias(GameActivity.this, !fontAA);
                applyDisplaySettings();
                Log.i(TAG, "font: anti-alias " + (fontAA ? "off" : "on"));
                dlg.dismiss();
                toast(!fontAA ? "文字反锯齿已打开" : "文字反锯齿已关闭");
            }
        });
        fontRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                FontPicker.pick(GameActivity.this);
            }
        });
        /* Long-press is the way back: the row's hint says so, and a reset that
         * needs no confirmation cannot lose anything - the picked file is a
         * copy, and the player still has the original. */
        fontRow.setOnLongClickListener(new View.OnLongClickListener() {
            @Override
            public boolean onLongClick(View v) {
                if (Settings.fontFile(GameActivity.this) == null) {
                    return false;
                }
                FontPicker.reset(GameActivity.this);
                applyDisplaySettings();
                Log.i(TAG, "font: back to the system typeface");
                dlg.dismiss();
                toast("已换回系统字体");
                return true;
            }
        });
        subsetRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                /* What the run has actually drawn, and what dropping the rest
                 * of the cache bought - reported, not promised. */
                int[] r = GlyphSource.trimToUsed();
                Log.i(TAG, "font: subset " + r[1] + " -> " + r[2] + " glyphs, "
                        + r[3] + " -> " + r[4] + " bytes");
                toast("字形缓存 " + r[1] + " → " + r[2] + " 个 · "
                        + FontKit.size(r[3]) + " → " + FontKit.size(r[4]));
                dlg.dismiss();
            }
        });
        picAaRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Settings.setPictureAntiAlias(GameActivity.this, !picAA);
                applyDisplaySettings();
                Log.i(TAG, "picture: anti-alias " + (picAA ? "off" : "on"));
                dlg.dismiss();
                toast(!picAA ? "画面反锯齿已打开" : "画面反锯齿已关闭");
            }
        });
        menuRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                /* Hidden is a setting, not a one-way door: the panel is open
                 * right now, and Back opens it again whenever the handle is
                 * gone. */
                padConfig.menuVisible = !padConfig.menuVisible;
                positionHandle();
                PadConfig.saveForGame(GameActivity.this, jarName, padConfig);
                dlg.dismiss();
                toast(padConfig.menuVisible ? "菜单按钮已显示" : "菜单按钮已隐藏，按返回键打开面板");
            }
        });
        save.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                saveState();
            }
        });
        quit.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                synchronized (lock) {
                    if (handle != 0) {
                        nativeQuit(handle);
                    }
                }
            }
        });
    }

    /**
     * Asks the VM thread for a snapshot and reports what actually happened.
     * A snapshot is only possible between frames and outside a native call -
     * the port refuses the rest - so this can fail, and saying so beats a
     * toast that promises a file nobody wrote.
     */
    private void saveState() {
        int rc;
        String path = JarStore.stateFile(this, jarName).getAbsolutePath();
        synchronized (lock) {
            if (handle == 0) {
                toast("游戏还没跑起来，稍等一下再存");
                return;
            }
            rc = nativeSaveState(handle, path);
        }
        if (rc == 0) {
            toast("已保存进度，下次在游戏卡片里选「恢复进度」");
        } else {
            toast("这次没能保存（错误 " + rc + "）");
        }
    }

    /** "1.0x" for the current speed, for a row's hint. */
    private String speedText() {
        return (speed / 100f) + "x";
    }

    /**
     * Pick the game speed.
     *
     * <p>This is the MIDlet's clock, not a frame-skip: Thread.sleep, timers,
     * Object.wait and the frame pacer all run off one clock in the runtime, so
     * a game that is merely slow (or one whose animations drag) speeds up as a
     * whole.
     */
    private void speedDialog() {
        final int[] percents = { 50, 75, 100, 150, 200, 300 };
        String[] labels = new String[percents.length];
        String[] hints = new String[percents.length];
        int checked = 2;
        for (int i = 0; i < percents.length; i++) {
            labels[i] = (percents[i] / 100f) + "x";
            hints[i] = percents[i] == 100 ? "和手机上的原速一样"
                    : (percents[i] < 100 ? "慢下来，看剧情或过场"
                                         : "时间是 MIDlet 的，不是画面的");
            if (percents[i] == speed) {
                checked = i;
            }
        }
        Md3.chooser(this, Md3.IC_PLAY, "游戏速度", "当前 " + speedText(),
                labels, hints, checked, new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        speed = percents[which];
                        synchronized (lock) {
                            if (handle != 0) {
                                nativeSetSpeed(handle, speed);
                            }
                        }
                        JarStore.Entry e = JarStore.byName(GameActivity.this, jarName);
                        if (e != null) {
                            e.speed = speed;
                            JarStore.write(GameActivity.this, e);
                        }
                        toast("游戏速度 " + speedText());
                    }
                });
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
            if (imeShowPending) {
                ui.removeCallbacks(imeShow);
                ui.post(imeShow);
            }
        }
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
