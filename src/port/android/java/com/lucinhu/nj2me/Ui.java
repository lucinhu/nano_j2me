package com.lucinhu.nj2me;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

/**
 * Full screen, the display cutout, and the window edges they take away.
 *
 * <p>A J2ME game wants the whole panel, and a modern phone answers with a
 * notch, a punch hole and two gesture bars.  Two separate jobs follow from
 * that: get the system's furniture out of the way (沉浸模式), and keep the
 * controls the player still has to touch - the control bar, its buttons, the
 * pad's keycaps - out of the hole that cannot be moved.
 *
 * <p>The cutout is reported by the framework in window coordinates, which is
 * also what the pad draws in, so it can be handed straight to
 * {@code PadView.setSafeAreaPx()}.
 */
final class Ui {

    /** Player-facing setting: hide the system bars while a game runs. */
    private static final String PREFS = "ui";
    private static final String IMMERSIVE = "immersive";

    /*
     * Which way the last apply() was asked to go.  The insets controller is
     * asynchronous: a run() posted on the way into full screen must not land
     * after the player left it and put the bars back.
     */
    private static boolean wanted = true;

    private Ui() {
    }

    static boolean immersive(Context ctx) {
        return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                  .getBoolean(IMMERSIVE, true);
    }

    static void setImmersive(Context ctx, boolean on) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        p.edit().putBoolean(IMMERSIVE, on).apply();
    }

    /**
     * Lets the window use the panel edge to edge, notch included.
     *
     * <p>Without this the framework letterboxes the window away from the
     * cutout, which on a tall phone means a black stripe down one side.  With
     * it, the window is whole and {@link #insets} says which parts of it are
     * under hardware.
     */
    static void edgeToEdge(Window w) {
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }
    }

    /** Hides the system bars (true) or gives the window back to them (false). */
    static void apply(Activity a, boolean immersive) {
        Window w = a.getWindow();
        edgeToEdge(w);
        if (immersive) {
            w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        } else {
            w.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
        final View decor = w.getDecorView();
        wanted = immersive;
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(!immersive);
            decor.post(new Runnable() {
                @Override
                public void run() {
                    android.view.WindowInsetsController c = decor.getWindowInsetsController();
                    if (c == null) {
                        return;
                    }
                    if (wanted) {
                        c.hide(WindowInsets.Type.statusBars()
                               | WindowInsets.Type.navigationBars());
                        c.setSystemBarsBehavior(
                            android.view.WindowInsetsController
                                .BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    } else {
                        c.show(WindowInsets.Type.statusBars()
                               | WindowInsets.Type.navigationBars());
                    }
                }
            });
        } else {
            int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
            if (immersive) {
                flags |= View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                       | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                       | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                       | View.SYSTEM_UI_FLAG_FULLSCREEN
                       | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            }
            decor.setSystemUiVisibility(flags);
        }
    }

    /**
     * Draws the app's own colour behind the system bars instead of leaving them
     * their default black stripe, the way a modern list screen does: the bars
     * stay visible - the clock, the gesture pill - but the window is laid out
     * behind them.
     *
     * <p>Callers must pad their content with {@link #insets}(win, true): the
     * framework no longer shrinks the window, so its edges are real.
     */
    static void drawBehindBars(Activity a, boolean lightBackground) {
        Window w = a.getWindow();
        edgeToEdge(w);
        w.setStatusBarColor(Color.TRANSPARENT);
        w.setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 29) {
            w.setStatusBarContrastEnforced(false);
            w.setNavigationBarContrastEnforced(false);
        }
        final View decor = w.getDecorView();
        final boolean darkIcons = lightBackground;
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            decor.post(new Runnable() {
                @Override
                public void run() {
                    android.view.WindowInsetsController c = decor.getWindowInsetsController();
                    if (c == null) {
                        return;
                    }
                    int mask = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                             | android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                    c.setSystemBarsAppearance(darkIcons ? mask : 0, mask);
                }
            });
        } else {
            int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                      | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                      | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
            if (darkIcons && Build.VERSION.SDK_INT >= 23) {
                flags |= View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            }
            if (darkIcons && Build.VERSION.SDK_INT >= 26) {
                flags |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            }
            decor.setSystemUiVisibility(flags);
        }
    }

    /**
     * The window edges that hardware or system bars occupy, in window pixels.
     *
     * <p>The cutout always counts: the framework honours
     * LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES by handing the app the whole
     * panel and telling it where the hole is.  The system bars only count while
     * the window is laid out behind them, which is exactly when 沉浸模式 is on -
     * counting them the rest of the time would push the keycaps in twice, since
     * the framework has already shrunk the window for them.
     */
    static int[] insets(WindowInsets win, boolean immersive) {
        int l = 0, t = 0, r = 0, b = 0;
        if (win == null) {
            return new int[] { 0, 0, 0, 0 };
        }
        if (Build.VERSION.SDK_INT >= 28) {
            DisplayCutout cut = win.getDisplayCutout();
            if (cut != null) {
                l = cut.getSafeInsetLeft();
                t = cut.getSafeInsetTop();
                r = cut.getSafeInsetRight();
                b = cut.getSafeInsetBottom();
            }
        }
        if (immersive) {
            l = Math.max(l, win.getSystemWindowInsetLeft());
            t = Math.max(t, win.getSystemWindowInsetTop());
            r = Math.max(r, win.getSystemWindowInsetRight());
            b = Math.max(b, win.getSystemWindowInsetBottom());
        }
        return new int[] { l, t, r, b };
    }
}
