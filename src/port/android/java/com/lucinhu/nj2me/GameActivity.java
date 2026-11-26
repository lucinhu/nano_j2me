package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
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
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.EditText;
import android.graphics.Typeface;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.Locale;
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
    private boolean surfaceAttached;
    private int surfaceGlPresent;
    private PadView pad;
    private PadConfig padConfig;
    private TextView status;
    private TextView graphicsPanelStatus;
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
    private int presentFilter;    /* 0 off, 2 scanline, 3 LCD, 4 CRT: 画面滤镜 */
    private int glPresent;        /* 0 software, 1 GL window present */
    private int glPresentMode;    /* 0 nearest, 2 scanline, 3 LCD, 4 CRT, 5 FSR1, 6 bicubic */
    private int glPresentScale;   /* 0 for automatic fit, otherwise an integer scale cap */
    private static final float DEFAULT_GL_SHARP = 0.87f;
    private float glPresentSharp = DEFAULT_GL_SHARP;
    private int rewindSeconds;    /* 0 off, 1/2/5: in-memory rewind window */
    private int insetL, insetT, insetR, insetB;

    /*
     * Audio focus.  A game that plays sound has to ask the phone for the
     * output instead of talking over whatever is already playing, and it has
     * to go quiet when another app takes it away - a call, a video, a music
     * player.  The port owns both the MIDI Players and the mixer, so the
     * request is made here on the main thread and a loss is answered by
     * pausing the MIDI Players this app started.  The port's own mixer
     * follows onPause()/onResume() as it always has; a focus loss that does
     * not pause the game must not silently stop the MIDlet's music forever,
     * so the listener resumes on AUDIOFOCUS_GAIN when the activity is still
     * resumed.
     */
    private AudioManager audioManager;
    private boolean audioFocusHeld;
    private boolean resumed;
    private final AudioManager.OnAudioFocusChangeListener audioFocusListener =
            new AudioManager.OnAudioFocusChangeListener() {
        @Override
        public void onAudioFocusChange(int change) {
            if (change == AudioManager.AUDIOFOCUS_GAIN) {
                if (resumed) {
                    MidiPlayer.resumeAll();
                }
                Log.i(TAG, "audio focus: gained");
                return;
            }
            /* LOSS, LOSS_TRANSIENT and LOSS_TRANSIENT_CAN_DUCK: the port
             * cannot duck a MIDI Player, so it pauses.  The game keeps its
             * frames - a notification blip is not a reason to freeze it. */
            MidiPlayer.pauseAll();
            Log.i(TAG, "audio focus: paused (change=" + change + ")");
        }
    };

    /*
     * Hardware keys the player is holding.  The pad's own keys live in
     * PadPress; these are the codes dispatchKeyEvent forwards to the MIDlet,
     * and the one thing they need that PadPress does not do for them is a
     * release when the key stream goes away: a dialog takes the keyboard, a
     * paused activity gets no ACTION_UP, and the MIDlet would hold that key
     * down until the player pressed it again.
     */
    private final java.util.HashSet<Integer> heldKeys = new java.util.HashSet<Integer>();

    /*
     * BACK has two jobs on a J2ME phone, and doing them on the same press was
     * the bug: the key is the MIDlet's right soft key (the game's own menu),
     * and on a phone with no other menu key it is also the port's panel handle.
     * A press used to do both - the game opened its menu and the settings
     * dialog came down on top of it.  So the press is decided on key-up: a
     * short press is handed to the MIDlet as one complete soft-key press, a
     * long press opens the panel and sends nothing.  The delay for a short
     * press is the release itself, not a timer, and MIDP has no key-repeat for
     * a press whose press/release arrive together.
     */
    private static final long BACK_LONG_MS = 550;
    private boolean backDown;
    private boolean backLongPressed;
    private final Runnable backLongPress = new Runnable() {
        @Override
        public void run() {
            if (!backDown) {
                return;
            }
            backLongPressed = true;
            Log.i(TAG, "back: long press -> port panel");
            showPanel();
        }
    };

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

    /** The same surface at a new size: rotation, multi-window, display size. */
    private static native void nativeSetWindowSize(long handle, int width,
                                                   int height);

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

    /**
     * 呈现滤镜：0 关闭，2 扫描线，3 LCD 网格，4 CRT。设置界面写入，
     * 呈现循环下一帧应用；只作用于窗口缓冲，不影响模拟器帧/存档/截图。
     */
    private static native void nativeSetPresentFilter(long handle, int mode);
    private static native void nativeSetGlPresent(long handle, int on, int mode,
                                                   int scale, float sharp);
    private static native void nativeSetRewind(long handle, int intervalFrames,
                                               int slots);
    private static native void nativeRewindStep(long handle);

    /**
     * Frames per second to aim for: 30 or 60, 0 for uncapped, -1 for the
     * port's own default.  Like the speed word, it is applied by the run
     * thread at the top of its next frame.
     */
    private static native void nativeSetFrameCap(long handle, int cap);

    /** Launch choice only: 0 software, 1 experimental GLES2. Never changed mid-run. */
    private static native boolean nativeFsr2Built();
    private static native boolean nativeVulkanBuilt();
    private static native void nativeSetHd(long handle, int scale, int ssaa, boolean subdivide, boolean fsr2, boolean vulkan, int assetFilter);
    private static native void nativeSetGraphicsMode(long handle, int mode);
    private static native boolean nativeGraphicsSupported();

    /** [state, fills, blits, rasters, declines, requested], for this run only. */
    private static native long[] nativeGraphicsInfo(long handle);

    /** Also used by the library before a game exists. */
    static boolean graphicsSupported() {
        try {
            return nativeGraphicsSupported();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

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

    /*
     * Cheat list.  The panel writes the game's cheat file and asks the run
     * thread to clear, re-read and re-apply it at the next frame boundary; the
     * answer is the number of parsed codes (negative = reload failed).
     */
    private static native int nativeCheatReload(long handle, String path);
    private static native int nativeCheatSearch(long handle, long value,
                                                int width, long[] out);
    private static native int nativeCheatNarrow(long handle, long[] candidates,
                                                int count, long value,
                                                int width, long[] out);
    private static native int nativeCheatRead(long handle, long[] offsets,
                                             int count, int width, long[] out);

    /*
     * Change search ("控制变量法"): record the arena as a baseline, let the
     * game change a value, then keep the addresses whose value changed, stayed
     * equal, grew or shrank.  The last call writes one value once instead of
     * freezing it every frame.
     */
    private static native int nativeCheatSnapshot(long handle);
    private static native int nativeCheatChanged(long handle, int width,
                                                 int mode, long[] out);
    private static native int nativeCheatFilterChanged(long handle,
                                                       long[] candidates,
                                                       int count, int width,
                                                       int mode, long[] out);
    private static native int nativeCheatWriteOnce(long handle, long offset,
                                                   long value, int width);

    /*
     * Candidates kept from the last value search, the byte width they were
     * searched with, and whether a search/narrow request is in flight.  The
     * panel talks to the run thread through the same lock the other natives
     * use, so the array is only touched while no request is pending.
     */
    private final long[] cheatCandidates = new long[256];
    private int cheatCandidateCount;
    private int cheatSearchWidth = 4;
    private boolean cheatBaselineSet;

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
        if (heapMB <= 0) {
            /* Automatic: the launcher stores 0 for "let the port decide", and
             * the decision is made here so a changed phone (or Android's own
             * memory pressure) is reflected on the next launch. */
            heapMB = JarStore.autoHeapMB(this);
            Log.i(TAG, "heap: automatic, " + heapMB + " MB for this phone");
        } else {
            Log.i(TAG, "heap: " + heapMB + " MB (chosen)");
        }
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
        installGameSurface();

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
        resumed = true;
        requestAudioFocus();
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
            if (handle != 0 && surfaceAttached) {
                nativeSetActive(handle, true);
            }
        }
    }

    @Override
    protected void onPause() {
        publishIme();
        imePaused = true;
        resumed = false;
        hideIme();
        /* Let go of everything the player is holding before the VM parks: an
         * activity that is going away gets no matching ACTION_UP, so a key
         * held through Home would stay down in the MIDlet and walk the player
         * off a cliff when they came back.  PadPress's keys and pointer are
         * released by the pad; the physical-keyboard codes are this class's. */
        releaseHeldKeys();
        pad.releaseAllKeys();
        /* Park the VM before the surface goes away: a MIDlet that keeps ticking
         * behind a locked screen burns battery and gains nothing. */
        saveView();
        synchronized (lock) {
            if (handle != 0) {
                nativeSetActive(handle, false);
            }
        }
        abandonAudioFocus();
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
        /* The hardware BACK key is handled in dispatchKeyEvent (short press =
         * the MIDlet's right soft key, long press = the panel).  This remains
         * for the paths the port does not consume itself - a framework
         * accessibility action, a gesture the OEM routes around the key stream:
         * not a hard exit, still the panel, where 退出 is one tap away. */
        showPanel();
    }

    /* ------------------------------------------------------------ audio focus */

    /*
     * Legacy requestAudioFocus() on purpose: it is the call that exists on
     * every API level this port supports (minSdk 21).  The AudioFocusRequest
     * object would be tidier on 26+, but it also means a second code path
     * that only runs on half the phones this runs on.  The framework's
     * automatic ducking of a legacy stream while another app's notification
     * plays is exactly what a game wants; only the pause/resume decisions are
     * this class's to make.
     */
    @SuppressWarnings("deprecation")
    private void requestAudioFocus() {
        if (audioFocusHeld) {
            return;
        }
        if (audioManager == null) {
            audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        }
        if (audioManager == null) {
            return;
        }
        int r = audioManager.requestAudioFocus(audioFocusListener,
                AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        audioFocusHeld = r == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        Log.i(TAG, "audio focus: " + (audioFocusHeld ? "granted" : "denied"));
    }

    private void abandonAudioFocus() {
        if (!audioFocusHeld) {
            return;
        }
        if (audioManager != null) {
            audioManager.abandonAudioFocus(audioFocusListener);
        }
        audioFocusHeld = false;
        Log.i(TAG, "audio focus: abandoned");
    }

    /*
     * Send the ups for every code still held, and forget them.  Called from
     * onPause() and when the panel opens: both take the key stream away, and
     * the MIDlet has no way to learn the player let go.
     */
    private void releaseHeldKeys() {
        if (heldKeys.isEmpty()) {
            return;
        }
        synchronized (lock) {
            if (handle != 0) {
                for (Integer code : heldKeys) {
                    nativeKey(handle, code, false);
                }
            }
        }
        Log.i(TAG, "keys: released " + heldKeys.size() + " held key(s)");
        heldKeys.clear();
    }

    /* --------------------------------------------------------------- surface */

    private void installGameSurface() {
        surfaceGlPresent = glPresent;
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        root.addView(surfaceView, 0, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        synchronized (lock) {
            if (holder != surfaceView.getHolder() || finishing || (handle == 0 && runThread != null)) {
                return;
            }
            surfaceAttached = true;
            if (handle == 0) {
                surfaceGlPresent = glPresent;
                handle = nativeCreate(holder.getSurface(), jarPath, screenW, screenH,
                                      heapMB, JarStore.rmsRoot(this).getAbsolutePath(),
                                      JarStore.fsRoot(this).getAbsolutePath());
                if (handle == 0) {
                    Log.e(TAG, "port creation failed");
                    toastThenFinish();
                    return;
                }
                SharedPreferences hd = getSharedPreferences("nano_j2me", MODE_PRIVATE);
                /* Stored preferences may predate the menu pairing rule: FSR2
                 * only carries valid temporal data beside a 2x or 3x HD
                 * scale, so repair the pair here instead of silently
                 * rendering without temporal history. */
                int hdScale = hd.getInt("hd_scale", 1);
                boolean fsr2 = hd.getInt("hd_fsr2", 0) != 0;
                if (fsr2 && hdScale != 2 && hdScale != 3) {
                    hdScale = 2;
                    hd.edit().putInt("hd_scale", hdScale).apply();
                }
                nativeSetHd(handle, hdScale, hd.getInt("hd_ssaa", 1),
                        hd.getInt("hd_subdivide", 0) != 0, fsr2,
                        hd.getInt("hd_vulkan", 0) != 0, hd.getInt("hd_smooth_assets", 0));
                nativeSetGraphicsMode(handle,
                        graphicsSupported() && Settings.gles2Experimental(this) ? 1 : 0);
                nativeSetSpeed(handle, speed);
                applyNativeDisplaySettingsLocked();
                /* A new MIDlet: its own vocabulary is what the subset row will
                 * keep, so the previous game's stops being recorded here. */
                GlyphSource.beginRun();
                if (statePath != null) {
                    /* Read on the VM thread before the MIDlet starts: a
                     * restored VM resumes mid-method, so there is nothing to
                     * start. */
                    nativeSetRestore(handle, statePath);
                }
                runThread = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        runVm();
                    }
                }, "nj2me-vm");
                applyView();
                nativeSetActive(handle, resumed);
                runThread.start();
            } else {
                nativeSetSurface(handle, holder.getSurface());
                nativeSetActive(handle, resumed);
            }
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        /* Rotation, a multi-window drag and a display-size change all keep the
         * surface and change its size.  The port asked the buffer queue for a
         * size when it started, so it cannot see this on its own: tell it, and
         * it re-asks for buffers and re-fits the picture to the new window. */
        synchronized (lock) {
            if (holder == surfaceView.getHolder() && surfaceAttached && handle != 0) {
                nativeSetWindowSize(handle, width, height);
            }
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        synchronized (lock) {
            if (holder != surfaceView.getHolder()) return;
            surfaceAttached = false;
            if (handle != 0) {
                nativeSetActive(handle, false);
                nativeSetSurface(handle, null);
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
        if (code == KeyEvent.KEYCODE_BACK) {
            /*
             * The one key the port plays both sides of.  Short press: the
             * MIDlet's right soft key, delivered as a complete press/release
             * pair now (the press was held while we were still deciding).
             * Long press: the port panel, no key to the game.  Never both.
             */
            if (down) {
                if (ev.getRepeatCount() == 0) {
                    backDown = true;
                    backLongPressed = false;
                    ui.postDelayed(backLongPress, BACK_LONG_MS);
                    Log.i(TAG, "back: down");
                }
            } else {
                backDown = false;
                ui.removeCallbacks(backLongPress);
                if (backLongPressed) {
                    Log.i(TAG, "back: up after long press, panel only");
                } else {
                    synchronized (lock) {
                        if (handle != 0) {
                            nativeKey(handle, KeyEvent.KEYCODE_BACK, true);
                            nativeKey(handle, KeyEvent.KEYCODE_BACK, false);
                        }
                    }
                    Log.i(TAG, "back: short press -> MIDP soft-right");
                }
            }
            return true;
        }
        if (down && ev.getRepeatCount() > 0) {
            return true;        /* MIDP has no key repeat; a held key is held */
        }
        /* Remember what this class forwards: when the stream is taken away by
         * the panel or by the activity pausing, releaseHeldKeys() sends the
         * ACTION_UP the framework is not going to deliver. */
        if (down) {
            heldKeys.add(code);
        } else {
            heldKeys.remove(code);
        }
        synchronized (lock) {
            if (handle != 0) {
                nativeKey(handle, code, down);
            }
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

    /* The caller holds lock and has a live handle. */
    private void applyNativeDisplaySettingsLocked() {
        nativeSetFilter(handle, pictureAA);
        nativeSetPresentFilter(handle, presentFilter);
        applyGlPresentSettings();
        applyRewindSetting();
        nativeSetFrameCap(handle, Settings.frameCap(this));
    }

    private void applyGlPresentSettings() {
        synchronized (lock) {
            if (handle != 0 && graphicsSupported()) {
                boolean replace = surfaceGlPresent != glPresent;
                if (replace) {
                    // ANativeWindow_lock connects a CPU producer for the lifetime of its
                    // Surface. EGL needs a fresh BufferQueue when the producer changes.
                    nativeSetSurface(handle, null);
                    surfaceAttached = false;
                    surfaceView.getHolder().removeCallback(this);
                    root.removeView(surfaceView);
                }
                nativeSetGlPresent(handle, glPresent, glPresentMode, glPresentScale, glPresentSharp);
                if (replace) installGameSurface();
            }
        }
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
        SharedPreferences prefs = getSharedPreferences("nano_j2me", MODE_PRIVATE);
        presentFilter = prefs.getInt("present_filter", 0);
        if (presentFilter == 1 || presentFilter < 0 || presentFilter > 4) {
            presentFilter = 0;
        }
        glPresent = prefs.getInt("gl_present", 0) != 0 ? 1 : 0;
        glPresentMode = prefs.getInt("gl_present_mode", 0);
        if (glPresentMode != 2 && glPresentMode != 3 && glPresentMode != 4
                && glPresentMode != 5 && glPresentMode != 6) {
            glPresentMode = 0;
        }
        glPresentSharp = prefs.getFloat("gl_present_sharp", DEFAULT_GL_SHARP);
        if (!(glPresentSharp >= 0.05f && glPresentSharp <= 1.0f)) {
            glPresentSharp = DEFAULT_GL_SHARP;
        }
        glPresentScale = prefs.getInt("gl_present_scale", 0);
        if (glPresentScale < 2 || glPresentScale > PictureFit.MAX_RENDER_SCALE) {
            glPresentScale = 0;
        }
        rewindSeconds = Settings.rewindSeconds(this);
        synchronized (lock) {
            if (handle != 0) {
                applyNativeDisplaySettingsLocked();
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
        /* The dialog takes the key and touch streams with it, so anything the
         * player is holding must be released now: no release event is coming
         * through a modal window, and the MIDlet would keep that key down (or
         * that pointer pressed) after the panel is dismissed. */
        releaseHeldKeys();
        pad.releaseAllKeys();
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, jarTitle));
        box.addView(Md3.supporting(this, screenW + "x" + screenH
                + (detW > 0 ? " · 画面 " + detW + "x" + detH : "")), Md3.gap(this, 6));

        LinearLayout adjust = Md3.row(this, Md3.IC_TUNE, "调整画面和按键", null, false, null);
        final String fontPath = Settings.fontFile(this);
        int[] glyphs = GlyphSource.stats();
        LinearLayout fontRow = Md3.row(this, Md3.IC_IMPORT, "更换字体…",
                fontPath == null ? "系统字体" : new File(fontPath).getName(), false, null);
        LinearLayout subsetRow = Md3.row(this, Md3.IC_TUNE, "只保留这个游戏用到的字",
                "字形缓存 " + glyphs[0] + " 个 · " + FontKit.size(glyphs[1]), false, null);
        final TextView graphicsDetails = Md3.supporting(this, "");
        graphicsPanelStatus = graphicsDetails;
        updateGraphicsPanel(readGraphicsInfo());
        final File cheatFile = cheatFileFor(jarName);
        final boolean cheatFileExists = cheatFile.isFile();
        LinearLayout cheatRow = Md3.row(this, Md3.IC_CHECK, "作弊码",
                cheatFileExists ? "已启用 · " + cheatFile.getName() : null,
                false, null);
        LinearLayout searchRow = Md3.row(this, Md3.IC_TUNE, "搜索作弊数值",
                null, false, null);
        LinearLayout save = Md3.row(this, Md3.IC_MEMORY, "保存状态", null, false, null);
        LinearLayout quit = Md3.dangerRow(this, Md3.IC_POWER, "退出游戏", null);

        box.addView(Md3.header(this, "画面"));
        box.addView(adjust, Md3.gap(this, 12));
        box.addView(Md3.divider(this));
        box.addView(new Md3.Setting(this, "状态信息", DisplayControls.OFF_ON,
                status.getVisibility() == View.VISIBLE ? 1 : 0, new Md3.Pick() {
            @Override public void set(int i) {
                status.setVisibility(i == 1 ? View.VISIBLE : View.GONE);
                refreshStatus();
            }
        }));
        box.addView(Md3.divider(this));
        box.addView(DisplayControls.control(this, DisplayControls.Option.PICTURE, new Runnable() {
            @Override public void run() { applyDisplaySettings(); }
        }));
        box.addView(Md3.divider(this));
        box.addView(filterSetting());
        box.addView(Md3.divider(this));
        box.addView(glPresentSetting());
        box.addView(Md3.divider(this));
        box.addView(glPresentScaleSetting());
        box.addView(glPresentSharpSetting());
        addHdChoice(box, "高清渲染倍率", "hd_scale", 1, scaleLabels(false), true);
        addHdChoice(box, "高清素材采样", "hd_smooth_assets", 0,
                new String[]{"原始像素", "柔和平滑", "像素画清晰"}, true);
        addHdChoice(box, "超采样抗锯齿", "hd_ssaa", 1, new String[]{"关闭", "四采样"}, true);
        addHdChoice(box, "3D 面片细分", "hd_subdivide", 0, new String[]{"关闭", "开启"}, true);
        addHdChoice(box, "Vulkan 3D 面片渲染", "hd_vulkan", 0,
                new String[]{"关闭", "请求启用（设备不支持时回退）"}, nativeVulkanBuilt());
        addHdChoice(box, "FSR2 时域放大", "hd_fsr2", 0,
                new String[]{"关闭", "请求启用（需设备支持）"}, nativeFsr2Built());
        box.addView(Md3.divider(this));
        box.addView(DisplayControls.control(this, DisplayControls.Option.FPS, new Runnable() {
            @Override public void run() { applyDisplaySettings(); }
        }));
        box.addView(Md3.header(this, "文字"));
        box.addView(DisplayControls.control(this, DisplayControls.Option.FONT, new Runnable() {
            @Override public void run() { applyDisplaySettings(); }
        }));
        box.addView(Md3.divider(this));
        box.addView(fontRow);
        box.addView(Md3.divider(this));
        box.addView(subsetRow);
        box.addView(Md3.header(this, "游戏"));
        box.addView(speedSetting());
        box.addView(Md3.divider(this));
        box.addView(DisplayControls.control(this, DisplayControls.Option.GLES, new Runnable() {
            @Override public void run() { applyDisplaySettings(); }
        }));
        box.addView(graphicsDetails, Md3.gap(this, 6));
        box.addView(Md3.header(this, "存档"));
        box.addView(save);
        box.addView(Md3.divider(this));
        box.addView(rewindSetting());
        box.addView(Md3.header(this, "高级"));
        box.addView(cheatRow);
        box.addView(Md3.divider(this));
        box.addView(searchRow);
        box.addView(Md3.divider(this));
        box.addView(new Md3.Setting(this, "菜单按钮", DisplayControls.OFF_ON, padConfig.menuVisible ? 1 : 0, new Md3.Pick() {
            @Override public void set(int i) {
                padConfig.menuVisible = i == 1;
                positionHandle();
                PadConfig.saveForGame(GameActivity.this, jarName, padConfig);
            }
        }));
        box.addView(quit, Md3.gap(this, 12));

        final AlertDialog dlg = Md3.show(new AlertDialog.Builder(this)
                .setView(Md3.scroller(this, box))
                .setNegativeButton("关闭", null), this);

        dlg.setOnDismissListener(new android.content.DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(android.content.DialogInterface dialog) {
                if (graphicsPanelStatus == graphicsDetails) {
                    graphicsPanelStatus = null;
                }
            }
        });
        cheatRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                cheatDialog(cheatFile);
            }
        });
        searchRow.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                cheatSearchDialog(cheatCandidateCount > 0);
            }
        });
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
        save.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dlg.dismiss();
                saveSlotDialog();
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
    /** Choose the slot to write; an existing one asks before it is replaced. */
    private String glPresentText() {
        if (glPresent == 0) {
            return "关闭（软件呈现）";
        }
        switch (glPresentMode) {
            case 2: return "扫描线（GL）";
            case 3: return "LCD 网格（GL）";
            case 4: return "CRT（GL）";
            case 5: return "FSR1（GL）";
            case 6: return "双三次（GL）";
            default: return "最近邻（GL）";
        }
    }

    private static String[] scaleLabels(boolean autoFirst) {
        String[] labels = new String[PictureFit.MAX_RENDER_SCALE];
        for (int i = 0; i < labels.length; ++i) labels[i] = (i + 1) + "×";
        if (autoFirst) labels[0] = "自动（填满画面）";
        return labels;
    }

    private void addHdChoice(LinearLayout box, final String title, final String key,
                             final int base, final String[] labels, boolean available) {
        final SharedPreferences prefs = getSharedPreferences("nano_j2me", MODE_PRIVATE);
        final int current = Math.max(0, Math.min(labels.length - 1, prefs.getInt(key, base) - base));
        Md3.Setting row = new Md3.Setting(this, title + "（下次启动）", labels, current, new Md3.Pick() {
            @Override public void set(int which) {
                int value = which + base;
                SharedPreferences.Editor edit = prefs.edit().putInt(key, value);
                /* AMD FSR2 2.2 takes at most three source pixels per output
                 * axis, so temporal upscaling can only run beside a 2x or 3x
                 * HD scale. Keep the pair consistent instead of accepting a
                 * combination that silently renders without temporal history. */
                if ("hd_fsr2".equals(key) && which != 0) {
                    int scale = prefs.getInt("hd_scale", 1);
                    if (scale != 2 && scale != 3) {
                        edit.putInt("hd_scale", 2);
                        ((Md3.Setting)box.findViewWithTag("hd_scale")).select(1);
                    }
                }
                if ("hd_scale".equals(key) && (value < 2 || value > 3) &&
                        prefs.getInt("hd_fsr2", 0) != 0) {
                    edit.putInt("hd_fsr2", 0);
                    ((Md3.Setting)box.findViewWithTag("hd_fsr2")).select(0);
                    toast("FSR2 时域放大需要 2× 或 3× 高清渲染，已关闭 FSR2");
                }
                edit.apply();
            }
        });
        row.setTag(key);
        row.setEnabled(available);
        if (!available) row.setText(title + "：此安装包未含此后端");
        box.addView(row);
    }

    private View glPresentScaleSetting() {
        final String[] labels = scaleLabels(true);
        int checked = glPresentScale == 0 ? 0 : glPresentScale - 1;
        Md3.Setting row = new Md3.Setting(this, "GL 放大", labels, checked, new Md3.Pick() {
                    @Override
                    public void set(int which) {
                        glPresentScale = which == 0 ? 0 : which + 1;
                        getSharedPreferences("nano_j2me", MODE_PRIVATE).edit()
                                .putInt("gl_present_scale", glPresentScale).apply();
                        applyGlPresentSettings();
                    }
                });
        row.setEnabled(graphicsSupported());
        return row;
    }

    private String glPresentSharpText() {
        if (Math.abs(glPresentSharp - 0.30f) < 0.01f) {
            return "0.30（柔和）";
        }
        if (Math.abs(glPresentSharp - 0.55f) < 0.01f) {
            return "0.55（中等）";
        }
        if (Math.abs(glPresentSharp - 1.00f) < 0.01f) {
            return "1.00（锐利）";
        }
        return "0.87（默认）";
    }

    private View glPresentSharpSetting() {
        final float[] values = { 0.30f, 0.55f, 0.87f, 1.00f };
        final String[] names = { "0.30（柔和，光晕最少）", "0.55（中等）",
                "0.87（默认，AMD 参考）", "1.00（最锐，光晕更强）" };
        int selected = 2;
        for (int i = 0; i < values.length; i++) {
            if (Math.abs(values[i] - glPresentSharp) < 0.01f) {
                selected = i;
            }
        }
        Md3.Setting row = new Md3.Setting(this, "FSR1 锐度", names, selected, new Md3.Pick() {
                    @Override
                    public void set(int which) {
                        glPresentSharp = values[which];
                        getSharedPreferences("nano_j2me", MODE_PRIVATE).edit()
                                .putFloat("gl_present_sharp", glPresentSharp).apply();
                        applyGlPresentSettings();
                        toast("FSR1 锐度：" + glPresentSharpText());
                    }
                });
        row.setEnabled(graphicsSupported());
        return row;
    }

    private View glPresentSetting() {
        final int[] modes = { -1, 0, 2, 3, 4, 5, 6 };
        final String[] labels = { "关闭（软件呈现）", "最近邻（GL）", "扫描线（GL）",
                "LCD 网格（GL）", "CRT（GL）", "FSR1（GL）", "双三次（GL）" };
        final String[] hints = {
                "保持现有软件呈现；完全不占用 GPU 呈现资源",
                "GPU 把原始帧放大到屏幕，像素清晰、开销低；与软件呈现逐像素一致",
                "GPU 呈现时叠加扫描线；只作用于窗口显示",
                "GPU 呈现时叠加 LCD 网格；只作用于窗口显示",
                "GPU 呈现时叠加 CRT 扫描线/荫罩效果；只作用于窗口显示",
                "放大时用立方核重建原始像素并锐化，等大时保留原像素，缩小时按覆盖面积采样",
                "立方核插值、不做锐化；比 FSR1 柔和，无锐化光晕" };
        int checked = 0;
        for (int i = 0; i < modes.length; i++) {
            if (modes[i] == (glPresent != 0 ? glPresentMode : -1)) {
                checked = i;
            }
        }
        Md3.Setting row = new Md3.Setting(this, "GL 呈现", labels, checked, new Md3.Pick() {
                    @Override
                    public void set(int which) {
                        if (modes[which] < 0) {
                            glPresent = 0;
                            glPresentMode = 0;
                        } else {
                            glPresent = 1;
                            glPresentMode = modes[which];
                        }
                        getSharedPreferences("nano_j2me", MODE_PRIVATE).edit()
                                .putInt("gl_present", glPresent)
                                .putInt("gl_present_mode", glPresentMode).apply();
                        applyGlPresentSettings();
                        toast("GL 呈现：" + glPresentText());
                    }
                });
        row.setEnabled(graphicsSupported());
        return row;
    }

    private void saveSlotDialog() {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, "保存状态"));
        box.addView(Md3.supporting(this,
                "每个槽位单独保存；覆盖前会再确认。读取在游戏卡片的「读取存档」里。"),
                Md3.gap(this, 6));
        for (int slot = 0; slot <= JarStore.SLOT_MAX; slot++) {
            final int s = slot;
            String when = JarStore.slotTimeText(this, jarName, s);
            box.addView(Md3.row(this, Md3.IC_MEMORY, JarStore.slotName(s),
                    "空".equals(when) ? "空，点击保存" : "已保存 " + when + "，点击覆盖",
                    false, new View.OnClickListener() {
                        @Override
                        public void onClick(View v) {
                            confirmSave(s);
                        }
                    }));
        }
        Md3.show(new AlertDialog.Builder(this)
                .setView(Md3.scroller(this, box))
                .setNegativeButton("关闭", null), this);
    }

    private void confirmSave(final int slot) {
        if (!JarStore.hasSlot(this, jarName, slot)) {
            saveState(JarStore.slotFile(this, jarName, slot), slot);
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("覆盖" + JarStore.slotName(slot) + "？")
                .setMessage("这个槽位里的进度会被替换，不能撤销。")
                .setNegativeButton("取消", null)
                .setPositiveButton("覆盖", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        saveState(JarStore.slotFile(GameActivity.this, jarName, slot), slot);
                    }
                })
                .show();
    }

    private void saveState(java.io.File file, int slot) {
        int rc;
        String path = file.getAbsolutePath();
        synchronized (lock) {
            if (handle == 0) {
                toast("游戏还没跑起来，稍等一下再存");
                return;
            }
            rc = nativeSaveState(handle, path);
        }
        if (rc == 0) {
            toast("已保存到" + JarStore.slotName(slot) + "，在游戏卡片里读取");
        } else if (rc == -8) {
            /* NJ_ERR_STATE: the frame boundary landed while the MIDlet thread
             * was parked inside a native call (serviceRepaints-driven loops),
             * which a snapshot cannot encode.  Retrying does not help for
             * those titles, so say what is going on instead of a bare error. */
            toast("这次没能保存：游戏线程正忙，这类游戏暂不支持中途存档");
        } else {
            toast("这次没能保存（错误 " + rc + "）");
        }
    }

    /**
     * The cheat file for this game: `<jar dir>/cheats/<jar stem>.txt`, the
     * first path the port resolves at startup, so what the player edits here is
     * what the next launch reads and what the live reload below re-reads.  The
     * format is the runtime's arena-offset cheat: one code per line, '#' starts
     * a comment line.
     */
    private File cheatFileFor(String jar) {
        String stem = jar == null || jar.length() == 0 ? "game" : jar;
        if (stem.regionMatches(true, stem.length() - 4, ".jar", 0, 4)) {
            stem = stem.substring(0, stem.length() - 4);
        }
        /*
         * Next to the jar in its "cheats" folder: that is the first place the
         * port looks for a cheat file, so what the editor saves is what the
         * next launch reads, and a file pushed with adb to the same folder is
         * picked up by this row too.
         */
        return new File(new File(JarStore.dir(this), "cheats"), stem + ".txt");
    }

    /**
     * Edit the game's cheat list.  Saving writes the file and asks the running
     * VM to reload it: the same "arena offset : hex bytes" lines the libretro
     * core and the CLI accept (see docs/notes/2026-11-19-android-cheat-editor.md).
     */
    private void cheatDialog(final File file) {
        final EditText input = new EditText(this);
        input.setGravity(Gravity.TOP | Gravity.START);
        input.setTypeface(Typeface.MONOSPACE);
        input.setHint("每行一条：<竞技场偏移>:<十六进制字节>\n例如 1A2B0:05\n# 以 # 开头是注释");
        input.setMinLines(6);
        input.setText(readTextFile(file));
        new AlertDialog.Builder(this)
                .setTitle("作弊码 · " + jarTitle)
                .setView(Md3.scroller(this, input))
                .setPositiveButton("保存并重新加载",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                saveCheats(file, input.getText().toString());
                            }
                        })
                .setNeutralButton("清空",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                saveCheats(file, "");
                            }
                        })
                .setNegativeButton("取消", null)
                .show();
    }

    private void saveCheats(File file, String text) {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            toast("建不了作弊码目录");
            return;
        }
        try {
            OutputStreamWriter w = new OutputStreamWriter(
                    new FileOutputStream(file), "UTF-8");
            try {
                w.write(text);
            } finally {
                w.close();
            }
        } catch (java.io.IOException e) {
            toast("写不了作弊码文件：" + e.getMessage());
            return;
        }
        int n;
        synchronized (lock) {
            n = handle != 0 ? nativeCheatReload(handle, file.getPath()) : -1;
        }
        if (n >= 0) {
            toast(n == 0 ? "作弊码已清空" : "已重新加载 " + n + " 条作弊码");
        } else {
            toast("文件已保存，但这次没能重新加载");
        }
    }

    /*
     * ---- cheat value search ------------------------------------------
     *
     * A front-end style value search: the player types the number they can see
     * in the game (gold, HP, ...), narrows the list by typing the new number
     * after something changes, and finally taps an offset to append the code
     * for it to the cheat file and reload it.  The arena is never scanned on
     * the UI thread: the run thread does it between frames through
     * nj_vm_cheat_find()/nj_vm_cheat_filter() (see docs/notes/2026-11-19-cheat-search.md).
     */

    /** The cheat file's little-endian spelling of a value: 0x1234 -> "3412". */
    private static String cheatCodeHex(long value, int width) {
        StringBuilder b = new StringBuilder(width * 2);
        for (int i = 0; i < width; i++) {
            b.append(String.format(Locale.ROOT, "%02X",
                    (int) ((value >> (8 * i)) & 0xff)));
        }
        return b.toString();
    }

    private static String cheatHexOffset(long offset) {
        return "0x" + Long.toHexString(offset).toUpperCase(Locale.ROOT);
    }

    /** The player's number as decimal ("120") or hex ("0x78"); null if bad. */
    private static Long parseCheatValue(String text, int width) {
        String t = text == null ? "" : text.trim();
        if (t.isEmpty()) {
            return null;
        }
        long v;
        try {
            if (t.startsWith("0x") || t.startsWith("0X")) {
                v = Long.parseLong(t.substring(2), 16);
            } else {
                v = Long.parseLong(t);
            }
        } catch (NumberFormatException e) {
            return null;
        }
        if (v < 0) {
            return null;
        }
        if (width == 1 && v > 0xFFL) {
            return null;
        }
        if (width == 2 && v > 0xFFFFL) {
            return null;
        }
        if (width == 4 && v > 0xFFFFFFFFL) {
            return null;
        }
        return v;
    }

    /** The whole text file, or "" when it does not exist yet. */
    private static String readTextFile(File file) {
        StringBuilder sb = new StringBuilder();
        if (file.isFile()) {
            try {
                InputStreamReader r = new InputStreamReader(
                        new FileInputStream(file), "UTF-8");
                try {
                    char[] buf = new char[1024];
                    int n;
                    while ((n = r.read(buf)) > 0) {
                        sb.append(buf, 0, n);
                    }
                } finally {
                    r.close();
                }
            } catch (java.io.IOException e) {
                sb.append("# 读取失败: ").append(e.getMessage()).append('\n');
            }
        }
        return sb.toString();
    }

    private void cheatSearchDialog(final boolean narrow) {
        final boolean narrowing = narrow && cheatCandidateCount > 0;
        final EditText input = new EditText(this);
        input.setTypeface(Typeface.MONOSPACE);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setHint("当前数值：十进制（如 120）或 0x 开头的十六进制");
        final RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.HORIZONTAL);
        final int[] widths = {1, 2, 4};
        final String[] labels = {"1 字节", "2 字节", "4 字节"};
        for (int i = 0; i < widths.length; i++) {
            RadioButton rb = new RadioButton(this);
            rb.setText(labels[i]);
            rb.setId(2300 + widths[i]);
            rb.setChecked(widths[i] == cheatSearchWidth);
            group.addView(rb);
        }
        LinearLayout box2 = new LinearLayout(this);
        box2.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(20 * getResources().getDisplayMetrics().density);
        box2.setPadding(pad, pad / 2, pad, 0);
        box2.addView(input);
        if (!narrowing) {
            box2.addView(group);
        }
        new AlertDialog.Builder(this)
                .setTitle(narrowing ? "缩小候选范围" : "搜索作弊数值")
                .setMessage(narrowing
                        ? "上次找到 " + cheatCandidateCount
                                + " 个候选。输入游戏里现在显示的数值继续缩小："
                        : "输入游戏里能看到的数值（金币、血量等数字）")
                .setView(box2)
                .setPositiveButton(narrowing ? "继续缩小" : "搜索",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                int width = cheatSearchWidth;
                                if (!narrowing) {
                                    int checked = group.getCheckedRadioButtonId();
                                    width = 4;
                                    for (int i = 0; i < widths.length; i++) {
                                        if (2300 + widths[i] == checked) {
                                            width = widths[i];
                                        }
                                    }
                                }
                                Long value = parseCheatValue(
                                        input.getText().toString(), width);
                                if (value == null) {
                                    toast("数值格式不对，或超出 " + width
                                            + " 字节范围");
                                    return;
                                }
                                runCheatSearch(value, width, narrowing);
                            }
                        })
                .setNeutralButton(narrowing ? "重新搜索" : "变化搜索",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                if (narrowing) {
                                    cheatCandidateCount = 0;
                                    cheatSearchDialog(false);
                                } else {
                                    cheatChangeDialog();
                                }
                            }
                        })
                .show();
    }

    /**
     * Change search: record a baseline, let the game change the value, then
     * filter the candidate list by "变了 / 没变 / 变大 / 变小".  This is the
     * control-variable method: the address that keeps matching the observed
     * behaviour is the one the game uses.
     */
    private void cheatChangeDialog() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(20 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);

        TextView hint = new TextView(this);
        hint.setText(cheatBaselineSet
                ? "基准已记录。回游戏里让数值变化，再点下面的条件筛选；"
                  + "命中后会以当前内存作为新基准继续缩小。"
                : "第 1 步：点“记录基准”。回游戏里让数值变化（打怪、买东西），"
                  + "再点“变了”或“变大/变小”筛选。");
        box.addView(hint);

        final RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.HORIZONTAL);
        final int[] widths = {1, 2, 4};
        final String[] widthLabels = {"1 字节", "2 字节", "4 字节"};
        for (int i = 0; i < widths.length; i++) {
            RadioButton rb = new RadioButton(this);
            rb.setText(widthLabels[i]);
            rb.setId(2400 + widths[i]);
            rb.setChecked(widths[i] == cheatSearchWidth);
            group.addView(rb);
        }
        box.addView(group);

        final String[] labels = {"记录基准（重设）", "变了", "没变", "变大", "变小"};
        final int[] modes = {-1, 0, 1, 2, 3};
        final AlertDialog[] holder = new AlertDialog[1];
        for (int i = 0; i < labels.length; i++) {
            final int mode = modes[i];
            Button b = new Button(this);
            b.setText(labels[i]);
            b.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    int width = cheatSearchWidth;
                    int checked = group.getCheckedRadioButtonId();
                    for (int k = 0; k < widths.length; k++) {
                        if (2400 + widths[k] == checked) {
                            width = widths[k];
                        }
                    }
                    if (mode < 0) {
                        recordCheatBaseline();
                    } else {
                        runCheatChangeFilter(width, mode);
                    }
                    if (holder[0] != null) {
                        holder[0].dismiss();
                    }
                }
            });
            box.addView(b);
        }
        holder[0] = new AlertDialog.Builder(this)
                .setTitle("变化搜索（控制变量法）")
                .setView(box)
                .setNegativeButton("关闭", null)
                .show();
    }

    private void recordCheatBaseline() {
        int n;
        synchronized (lock) {
            if (handle == 0) {
                toast("游戏还没跑起来，稍等一下再搜");
                return;
            }
            n = nativeCheatSnapshot(handle);
        }
        if (n <= 0) {
            toast("没能记录基准（游戏可能还没跑起来）");
            return;
        }
        cheatBaselineSet = true;
        cheatCandidateCount = 0;
        toast("基准已记录（" + n + " 字节）。回游戏改变数值后再点变化条件");
    }

    private void runCheatChangeFilter(int width, int mode) {
        int n;
        synchronized (lock) {
            if (handle == 0) {
                toast("游戏还没跑起来，稍等一下再搜");
                return;
            }
            if (!cheatBaselineSet) {
                toast("先点“记录基准”，再去游戏里改变数值");
                return;
            }
            if (cheatCandidateCount > 0) {
                n = nativeCheatFilterChanged(handle, cheatCandidates,
                        cheatCandidateCount, width, mode, cheatCandidates);
            } else {
                n = nativeCheatChanged(handle, width, mode, cheatCandidates);
            }
        }
        if (n < 0) {
            toast("这次没能筛选（游戏可能还没跑起来）");
            return;
        }
        cheatSearchWidth = width;
        cheatCandidateCount = Math.min(n, cheatCandidates.length);
        if (n == 0) {
            toast("没有符合条件的地址：再让数值变化一次，或重设基准");
            return;
        }
        if (n > cheatCandidates.length) {
            toast("命中 " + n + " 个地址，只保留前 " + cheatCandidates.length + " 个");
        }
        /* Rebase so the next pass compares against the state we just kept. */
        synchronized (lock) {
            if (nativeCheatSnapshot(handle) > 0) {
                cheatBaselineSet = true;
            }
        }
        showCheatCandidates(0L, width);
    }

    private void runCheatSearch(long value, int width, boolean narrow) {
        int n;
        synchronized (lock) {
            if (handle == 0) {
                toast("游戏还没跑起来，稍等一下再搜");
                return;
            }
            n = narrow
                    ? nativeCheatNarrow(handle, cheatCandidates,
                            cheatCandidateCount, value, width, cheatCandidates)
                    : nativeCheatSearch(handle, value, width, cheatCandidates);
        }
        if (n < 0) {
            toast("这次没能搜索（游戏可能还没跑起来）");
            return;
        }
        cheatSearchWidth = width;
        cheatCandidateCount = Math.min(n, cheatCandidates.length);
        if (n == 0) {
            toast("没有找到匹配的数值：换一个数值，或先让游戏显示那个数字再搜");
            return;
        }
        if (n > cheatCandidates.length) {
            toast("命中 " + n + " 个地址，只保留前 " + cheatCandidates.length
                    + " 个");
        }
        showCheatCandidates(value, width);
    }

    /** The value at one candidate offset, formatted for the candidate list. */
    private String cheatValueText(long value, int width) {
        if (value == 0xFFFFFFFFL && width < 4) {
            return "?";                         /* unreadable */
        }
        long mask = width >= 4 ? 0xFFFFFFFFL : (1L << (8 * width)) - 1L;
        long v = value & mask;
        return v + " (0x" + Long.toHexString(v).toUpperCase(Locale.ROOT) + ")";
    }

    /**
     * Read the current arena value of every candidate, so the list can show
     * which addresses still hold a plausible number.  Returns null on failure.
     */
    private long[] cheatReadCandidateValues() {
        if (cheatCandidateCount <= 0) {
            return new long[0];
        }
        long[] values = new long[cheatCandidateCount];
        int n;
        synchronized (lock) {
            n = nativeCheatRead(handle, cheatCandidates, cheatCandidateCount,
                                cheatSearchWidth, values);
        }
        if (n < 0) {
            return null;
        }
        return values;
    }

    private void showCheatCandidates(final long value, final int width) {
        final long[] live = cheatReadCandidateValues();
        final int shown = Math.min(cheatCandidateCount, 30);
        final String[] items = new String[shown];
        for (int i = 0; i < shown; i++) {
            String text = cheatHexOffset(cheatCandidates[i]);
            if (live != null && i < live.length) {
                text += "  = " + cheatValueText(live[i], width);
            }
            items[i] = text;
        }
        new AlertDialog.Builder(this)
                .setTitle("候选地址 · 共 " + cheatCandidateCount + " 个"
                        + (cheatCandidateCount > shown ? "（显示前 " + shown + "）" : ""))
                .setItems(items, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        final long current = (live != null && which < live.length)
                                ? live[which] : value;
                        final long offset = cheatCandidates[which];
                        new AlertDialog.Builder(GameActivity.this)
                                .setTitle("怎么改这个地址？")
                                .setItems(new String[] {
                                        "持续修改（写成作弊码，每帧保持）",
                                        "单次修改（只写一次，游戏可覆盖）"},
                                        new android.content.DialogInterface.OnClickListener() {
                                            @Override
                                            public void onClick(
                                                    android.content.DialogInterface d2,
                                                    int which2) {
                                                if (which2 == 0) {
                                                    cheatWriteCode(offset, current, width);
                                                } else {
                                                    int r;
                                                    synchronized (lock) {
                                                        r = nativeCheatWriteOnce(handle,
                                                                offset, current, width);
                                                    }
                                                    toast(r == 1
                                                            ? "已写入一次（游戏之后可以改回去）"
                                                            : "这次没能写入");
                                                }
                                            }
                                        })
                                .setNegativeButton("取消", null)
                                .show();
                    }
                })
                .setPositiveButton("缩小范围",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                cheatSearchDialog(true);
                            }
                        })
                .setNeutralButton("重新搜索",
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d,
                                                int which) {
                                cheatCandidateCount = 0;
                                cheatSearchDialog(false);
                            }
                        })
                .setNegativeButton("取消", null)
                .show();
    }

    /** Append `<offset>:<bytes>` for this game and reload the cheat file. */
    private void cheatWriteCode(long offset, long value, int width) {
        File file = cheatFileFor(jarName);
        String offsetHex = Long.toHexString(offset).toUpperCase(Locale.ROOT);
        String code = offsetHex + ":" + cheatCodeHex(value, width);
        StringBuilder text = new StringBuilder();
        boolean replaced = false;
        for (String line : readTextFile(file).split("\n")) {
            String trimmed = line.trim();
            if (trimmed.isEmpty()) {
                continue;
            }
            int colon = trimmed.indexOf(':');
            String lineOffset = colon > 0 ? trimmed.substring(0, colon).trim() : "";
            if (lineOffset.equalsIgnoreCase(offsetHex)) {
                /* Keep one line per address: re-picking an offset rewrites it
                 * instead of stacking duplicates that fight for the same
                 * bytes.  Later duplicates of the same address are dropped. */
                if (!replaced) {
                    text.append(code).append('\n');
                    replaced = true;
                }
            } else {
                text.append(trimmed).append('\n');
            }
        }
        if (!replaced) {
            text.append(code).append('\n');
        }
        saveCheats(file, text.toString());
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
    private View speedSetting() {
        final int[] percents = { 50, 75, 100, 150, 200, 300, 400, 600, 800 };
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
        Md3.Setting row = new Md3.Setting(this, "游戏速度", labels, checked, new Md3.Pick() {
                    @Override
                    public void set(int which) {
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
        return row;
    }

    /** Tell the port which rewind window this install wants (0 = off). */
    private void applyRewindSetting() {
        if (handle == 0) {
            return;
        }
        /* One snapshot every 0.5 s; slots follow the requested window so the
         * ring covers it before the 256 MiB cap steps in. */
        nativeSetRewind(handle, rewindSeconds > 0 ? 30 : 0,
                        rewindSeconds > 0 ? rewindSeconds * 2 : 0);
    }

    /*
     * Rewind chooser: an in-memory ring of full state snapshots kept by the
     * native present loop (docs/REWIND.md).  Off by default because a snapshot
     * copies the whole arena; 1/2/5 s pick the window, and the last entry
     * steps back immediately.
     */
    private View rewindSetting() {
        final int[] values = { 0, 1, 2, 5 };
        final String[] labels = { "关闭（不占内存）", "1 秒", "2 秒", "5 秒", "倒带一步（立刻回退）" };
        final String[] hints = {
                "不保留任何快照，默认",
                "每 0.5 秒一张快照，可回退 1 秒",
                "可回退 2 秒",
                "可回退 5 秒；大堆游戏可能先触到 256 MiB 上限",
                "立即载入最近一张快照"
        };
        int checked = 0;
        for (int i = 0; i < values.length; i++) {
            if (values[i] == rewindSeconds) {
                checked = i;
            }
        }
        Md3.Setting row = new Md3.Setting(this, "倒带", labels, checked, new Md3.Pick() {
                    @Override
                    public void set(int which) {
                        if (which == 4) {
                            synchronized (lock) {
                                if (handle != 0) {
                                    nativeRewindStep(handle);
                                }
                            }
                            toast("倒带一步");
                            return;
                        }
                        rewindSeconds = values[which];
                        Settings.setRewindSeconds(GameActivity.this, rewindSeconds);
                        synchronized (lock) {
                            applyRewindSetting();
                        }
                        Log.i(TAG, "rewind: window " + rewindSeconds + " s");
                        toast("倒带 " + Settings.rewindText(rewindSeconds));
                    }
                });
        return row;
    }

    /*
     * Picture filter: display effects on the window buffer only.  The game
     * framebuffer, save states, screenshots and the libretro core never see
     * them, which is why this is a present-path setting and not a Graphics
     * one.  Stored per install, applied on the next frame.
     */
    private View filterSetting() {
        final String[] labels = { "关闭（像素原样）", "扫描线", "LCD 网格", "CRT" };
        final int[] modes = { 0, 2, 3, 4 };
        final String[] hints = {
                "窗口原样显示，默认",
                "每隔一行压暗，模拟老电视扫描线",
                "每隔三行/列画暗线，模拟 LCD 像素栅格",
                "更重的扫描线 + 栅格，接近 CRT 观感"
        };
        int checked = 0;
        for (int i = 0; i < modes.length; i++) {
            if (presentFilter == modes[i]) {
                checked = i;
            }
        }
        Md3.Setting row = new Md3.Setting(this, "画面滤镜", labels, checked, new Md3.Pick() {
                    @Override
                    public void set(int which) {
                        presentFilter = modes[which];
                        getSharedPreferences("nano_j2me", MODE_PRIVATE).edit()
                                .putInt("present_filter", presentFilter).apply();
                        synchronized (lock) {
                            if (handle != 0) {
                                nativeSetPresentFilter(handle, presentFilter);
                            }
                        }
                        toast("画面滤镜 " + labels[which]);
                    }
                });
        return row;
    }




    /** The same lifetime lock as nativeDestroy; the returned array is a CPU snapshot. */
    private long[] readGraphicsInfo() {
        synchronized (lock) {
            return handle != 0 ? nativeGraphicsInfo(handle) : null;
        }
    }

    private static String graphicsStateText(long[] info) {
        if (info == null || info.length < 6) {
            return "绘制状态暂不可用";
        }
        switch ((int) info[0]) {
            case 0:
                return "软件绘制（本局未启用 GLES2）";
            case 1:
                return "GLES2 等待启动";
            case 2:
                return info[1] > 0 || info[2] > 0 || info[3] > 0
                        || (info.length >= 8 && info[6] > 0)
                        ? "GLES2 已就绪，已执行 GPU 绘制"
                        : "GLES2 已就绪，等待支持的绘制";
            case 3:
                return "GLES2 出错，已回退软件";
            case 4:
                return "本局运行已结束";
            case 5:
                return "此安装包未含 GLES2，使用软件绘制";
            default:
                return "绘制状态暂不可用";
        }
    }

    private void updateGraphicsPanel(long[] info) {
        if (graphicsPanelStatus == null) {
            return;
        }
        String detail = "本局状态：" + graphicsStateText(info);
        if (info != null && info.length >= 9 && info[8] > 0) {
            detail += "\n实际渲染倍率 " + info[8] + "×";
        }
        if (info != null && info.length >= 6) {
            detail += "\n本局启动选择：" + (info[5] == 1 ? "硬件加速（GLES2）" : "软件绘制")
                    + "\nGPU 填充 " + info[1] + " · 图像 " + info[2] + " · 栅格 " + info[3]
                    + "\n转交软件 " + info[4] + " 次";
            if (info.length >= 8) {
                detail += "\nGPU 三角形 " + info[6] + " · 三维批次 " + info[7];
            }
        }
        graphicsPanelStatus.setText(detail);
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
        long[] graphics = readGraphicsInfo();
        updateGraphicsPanel(graphics);
        status.setText(jarTitle + "  " + screenW + "x" + screenH + "\n" + s
                + "\n" + graphicsStateText(graphics));
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
