package com.example.nj2me;

import android.app.Activity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The whole Android side of nano_j2me.
 *
 * Note what is NOT here: no key-code translation (the port owns that mapping
 * so it stays in one place), no frame loop, no rendering.  This class copies
 * the game out of assets on first launch - RecordStore needs a writable home -
 * hands the Surface to the native side, and forwards input.
 *
 * The native context is carried in {@link #handle} rather than in a C global,
 * which is what lets the user back out of a game and start another one without
 * restarting the process.
 */
public class Nj2meActivity extends Activity implements SurfaceHolder.Callback {

    private static final String JAR_ASSET = "game.jar";

    private SurfaceView view;
    private long handle;

    private native long nativeCreate(Surface surface, String jarPath);
    private native void nativeRun(long handle);
    private native void nativeKey(long handle, int keyCode, boolean down);
    private native void nativePointer(long handle, int x, int y, boolean down);
    private native void nativeQuit(long handle);
    private native void nativeDestroy(long handle);

    static {
        System.loadLibrary("nj2me");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                             WindowManager.LayoutParams.FLAG_FULLSCREEN);
        view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        // Let the D-pad drive the game even without a touch screen.
        view.setFocusableInTouchMode(true);
        view.requestFocus();
        setContentView(view);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        String path = extractJar();
        handle = nativeCreate(holder.getSurface(), path);
        if (handle != 0) {
            nativeRun(handle);
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        // Nothing to do: the port re-locks the window per frame and picks up
        // the new geometry, so rotation and split-screen just work.
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        release();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (handle != 0) {
            nativeKey(handle, keyCode, true);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (handle != 0) {
            nativeKey(handle, keyCode, false);
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (handle == 0) {
            return super.onTouchEvent(event);
        }
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                nativePointer(handle, (int) event.getX(), (int) event.getY(), true);
                break;
            case MotionEvent.ACTION_UP:
                nativePointer(handle, (int) event.getX(), (int) event.getY(), false);
                break;
            default:
                break;
        }
        return true;
    }

    @Override
    protected void onDestroy() {
        release();
        super.onDestroy();
    }

    private void release() {
        if (handle != 0) {
            nativeQuit(handle);      // makes the frame loop exit
            nativeDestroy(handle);   // joins it, then tears the VM down
            handle = 0;
        }
    }

    /** Copy assets/game.jar to filesDir once, so RMS has somewhere to write. */
    private String extractJar() {
        File out = new File(getFilesDir(), JAR_ASSET);
        if (out.exists() && out.length() > 0) {
            return out.getAbsolutePath();
        }
        try (InputStream in = getAssets().open(JAR_ASSET);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
        } catch (Exception e) {
            // Fall through: the native side will report "cannot read the JAR".
            return out.getAbsolutePath();
        }
        return out.getAbsolutePath();
    }
}
