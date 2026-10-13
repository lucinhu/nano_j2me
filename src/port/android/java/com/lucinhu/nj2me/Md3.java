package com.lucinhu.nj2me;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Material 3, drawn with the framework alone.
 *
 * <p>The real thing lives in com.google.android.material, which drags in
 * AppCompat, RecyclerView, the AndroidX core and about eleven megabytes of
 * resources behind it.  This file is the part of it this app actually uses -
 * the dark colour roles, the shape scale, the state layer, the type scale - and
 * nothing else, which is why the whole launcher can look like Material 3 for a
 * few kilobytes instead of a few megabytes.
 *
 * <p>Two things are worth knowing before changing anything here:
 *
 * <ul>
 *   <li>On Android 12 and later the palette is taken from the wallpaper with
 *       the system's own tonal palettes.  Those are looked up <em>by name</em>,
 *       not by {@code android.R.color.system_accent1_500}: a resource id is
 *       inlined into the dex at compile time, so a build made against one
 *       platform version would ask an older device for a resource it does not
 *       have.  A name lookup costs microseconds, happens once per activity, and
 *       simply keeps the baseline palette on a device that has no such colour.
 *   <li>A state layer is not a colour change: Material 3 puts a 12% wash of the
 *       content colour over the container while a finger is down, which is
 *       exactly what a {@link RippleDrawable} is.  Every tappable thing here is
 *       built from {@link #ripple}.
 * </ul>
 *
 * <p>Nothing in here may reference {@code R.*}: this project links its
 * resources with aapt2 but never generates a usable R class for javac.
 */
final class Md3 {

    /* ------------------------------------------------------------- colours */

    /* The baseline Material 3 dark palette.  It is what a device without
     * dynamic colour gets, and the fallback for any role the system palette
     * does not provide.  The app stays on the dark side on purpose: its theme
     * is dark, a J2ME canvas is black, and the framework's own dialog text
     * comes out light - a light scheme would need a second theme plus a
     * hand-painted dialog body to stay readable. */
    static int primary = 0xFFD0BCFF;
    static int onPrimary = 0xFF381E72;
    static int primaryContainer = 0xFF4F378B;
    static int onPrimaryContainer = 0xFFEADDFF;
    static int secondary = 0xFFCCC2DC;
    static int secondaryContainer = 0xFF4A4458;
    static int onSecondaryContainer = 0xFFE8DEF8;
    static int tertiary = 0xFFEFB8C8;
    static int tertiaryContainer = 0xFF633B48;
    static int surface = 0xFF141218;
    static int surfaceContainerLow = 0xFF1D1B20;
    static int surfaceContainer = 0xFF1D1B20;
    static int surfaceContainerHigh = 0xFF2B2930;
    static int surfaceContainerHighest = 0xFF36343B;
    static int onSurface = 0xFFE6E0E9;
    static int onSurfaceVariant = 0xFFCAC4D0;
    static int outline = 0xFF938F99;
    static int outlineVariant = 0xFF49454F;
    static int error = 0xFFF2B8B5;
    static int onError = 0xFF601410;

    /* The steps of one system palette, in the order the framework stores them.
     * Note the direction: system_neutral1_0 is WHITE and system_neutral1_1000
     * is black, the opposite of Material's tone numbers, where tone 0 is black.
     * Reading them by tone number is how a dark screen comes out white. */
    private static final int N0 = 0, N10 = 1, N50 = 2, N100 = 3, N200 = 4, N300 = 5;
    private static final int N400 = 6, N500 = 7, N600 = 8, N700 = 9, N800 = 10;
    private static final int N900 = 11, N1000 = 12;
    private static final String[] STEPS = {
        "0", "10", "50", "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000"
    };

    /** The state layer: content colour at 12%, the Material 3 pressed wash. */
    private static final int STATE = 0x1F;

    private static boolean ready;
    private static boolean dynamic;

    /**
     * The scheme in use: light when the system is not in night mode, dark when
     * it is.  A Material 3 app follows the device, and this one has to do it
     * with the framework's own resources, so the two schemes are built from the
     * same tonal palettes read in opposite directions.
     */
    private static boolean light;

    /** The system's own answer, remembered so a second call can return early. */
    private static boolean night;

    /** True when the wallpaper palette was used, for the status line. */
    static boolean isDynamic() {
        return dynamic;
    }

    static boolean isLight() {
        return light;
    }

    /**
     * Reads the palette.  Call it first thing in an activity's onCreate: after
     * the first call it only costs a configuration read, which is what it has
     * to do - the player can flip the system's night mode while this process
     * stays alive.
     */
    static void init(Context ctx) {
        boolean dark = (ctx.getResources().getConfiguration().uiMode
                        & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        if (ready && dark == night) {
            return;
        }
        ready = true;
        night = dark;
        /*
         * A dark icon on a light bar needs SYSTEM_UI_FLAG_LIGHT_STATUS_BAR,
         * which arrived in API 23.  Below that a light scheme would draw a white
         * clock on a white bar, so those devices keep the dark one.
         */
        light = !dark && Build.VERSION.SDK_INT >= 23;
        baseline();
        dynamic = false;
        if (Build.VERSION.SDK_INT >= 31) {
            dynamic = dynamicPalette(ctx);
        }
    }

    /** The Material 3 baseline palette for the scheme in use. */
    private static void baseline() {
        if (!light) {
            return;
        }
        primary = 0xFF6750A4;
        onPrimary = 0xFFFFFFFF;
        primaryContainer = 0xFFEADDFF;
        onPrimaryContainer = 0xFF21005D;
        secondary = 0xFF625B71;
        secondaryContainer = 0xFFE8DEF8;
        onSecondaryContainer = 0xFF1D192B;
        tertiary = 0xFF7D5260;
        tertiaryContainer = 0xFFFFD8E4;
        surface = 0xFFFEF7FF;
        surfaceContainerLow = 0xFFF7F2FA;
        surfaceContainer = 0xFFF3EDF7;
        surfaceContainerHigh = 0xFFECE6F0;
        surfaceContainerHighest = 0xFFE6E0E9;
        onSurface = 0xFF1D1B20;
        onSurfaceVariant = 0xFF49454F;
        outline = 0xFF79747E;
        outlineVariant = 0xFFCAC4D0;
        error = 0xFFB3261E;
        onError = 0xFFFFFFFF;
    }

    /**
     * Material You: the system exposes its five tonal palettes, thirteen steps
     * each, as framework resources.  A missing step (an OEM that dropped one, a
     * preview build) simply leaves the baseline value in place.
     */
    private static boolean dynamicPalette(Context ctx) {
        int[] a1 = palette(ctx, "system_accent1_");
        int[] a2 = palette(ctx, "system_accent2_");
        int[] a3 = palette(ctx, "system_accent3_");
        int[] n1 = palette(ctx, "system_neutral1_");
        int[] n2 = palette(ctx, "system_neutral2_");
        if (a1 == null || n1 == null) {
            return false;
        }
        if (light) {
            /* Tone 40 is the accent in a light scheme, tone 90 its container,
             * tone 10 what sits on it - the same palette, read from the other
             * end than the dark scheme below. */
            primary = tone(a1, N600, primary);
            onPrimary = tone(a1, N0, onPrimary);
            primaryContainer = tone(a1, N100, primaryContainer);
            onPrimaryContainer = tone(a1, N900, onPrimaryContainer);
            if (a2 != null) {
                secondary = tone(a2, N600, secondary);
                secondaryContainer = tone(a2, N100, secondaryContainer);
                onSecondaryContainer = tone(a2, N900, onSecondaryContainer);
            }
            if (a3 != null) {
                tertiary = tone(a3, N600, tertiary);
                tertiaryContainer = tone(a3, N100, tertiaryContainer);
            }
            surface = tone(n1, N10, surface);
            surfaceContainerLow = tone(n1, N50, surfaceContainerLow);
            surfaceContainer = tone(n1, N50, surfaceContainer);
            surfaceContainerHigh = tone(n1, N100, surfaceContainerHigh);
            surfaceContainerHighest = tone(n1, N100, surfaceContainerHighest);
            onSurface = tone(n1, N900, onSurface);
            if (n2 != null) {
                onSurfaceVariant = tone(n2, N700, onSurfaceVariant);
                outline = tone(n2, N500, outline);
                outlineVariant = tone(n2, N200, outlineVariant);
            }
            return true;
        }
        primary = tone(a1, N200, primary);
        onPrimary = tone(a1, N900, onPrimary);
        primaryContainer = tone(a1, N700, primaryContainer);
        onPrimaryContainer = tone(a1, N100, onPrimaryContainer);
        if (a2 != null) {
            secondary = tone(a2, N200, secondary);
            secondaryContainer = tone(a2, N700, secondaryContainer);
            onSecondaryContainer = tone(a2, N100, onSecondaryContainer);
        }
        if (a3 != null) {
            tertiary = tone(a3, N200, tertiary);
            tertiaryContainer = tone(a3, N700, tertiaryContainer);
        }
        surface = tone(n1, N900, surface);
        surfaceContainerLow = tone(n1, N900, surfaceContainerLow);
        surfaceContainer = tone(n1, N800, surfaceContainer);
        surfaceContainerHigh = tone(n1, N800, surfaceContainerHigh);
        surfaceContainerHighest = tone(n1, N700, surfaceContainerHighest);
        onSurface = tone(n1, N100, onSurface);
        if (n2 != null) {
            onSurfaceVariant = tone(n2, N200, onSurfaceVariant);
            outline = tone(n2, N400, outline);
            outlineVariant = tone(n2, N700, outlineVariant);
        }
        return true;
    }

    /** True when the current surface is light and the system bars need dark icons. */
    static boolean lightSurface() {
        return luminance(surface) > 0.5f;
    }

    private static float luminance(int c) {
        float r = channel(Color.red(c));
        float g = channel(Color.green(c));
        float b = channel(Color.blue(c));
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    private static float channel(int v) {
        float c = v / 255f;
        return c <= 0.03928f ? c / 12.92f : (float) Math.pow((c + 0.055f) / 1.055f, 2.4f);
    }

    private static int[] palette(Context ctx, String prefix) {
        Resources res = ctx.getResources();
        int[] out = new int[STEPS.length];
        int found = 0;
        for (int i = 0; i < STEPS.length; i++) {
            int id = res.getIdentifier(prefix + STEPS[i], "color", "android");
            if (id == 0) {
                continue;
            }
            try {
                out[i] = res.getColor(id);
                found++;
            } catch (Resources.NotFoundException ignored) {
                /* an id that resolves but has no value: leave it at zero */
            }
        }
        /* Without most of the steps this is not a palette, it is a mistake. */
        return found >= 10 ? out : null;
    }

    private static int tone(int[] p, int i, int fallback) {
        return (i < p.length && p[i] != 0) ? p[i] : fallback;
    }

    static int alpha(int color, int a) {
        return (color & 0x00FFFFFF) | ((a & 0xFF) << 24);
    }

    /* -------------------------------------------------------------- shapes */

    /** Material 3's shape scale, in dp. */
    static final float SHAPE_XS = 4f, SHAPE_S = 8f, SHAPE_M = 12f, SHAPE_L = 16f;
    static final float SHAPE_XL = 28f, SHAPE_FULL = 1000f;

    static GradientDrawable round(Context ctx, int color, float radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(ctx, radiusDp));
        return g;
    }

    static GradientDrawable roundOutlined(Context ctx, int color, int stroke, float radiusDp) {
        GradientDrawable g = round(ctx, color, radiusDp);
        g.setStroke(Math.max(1, Math.round(dp(ctx, 1))), stroke);
        return g;
    }

    static GradientDrawable circle(int color) {
        GradientDrawable g = new GradientDrawable();
        g.setShape(GradientDrawable.OVAL);
        g.setColor(color);
        return g;
    }

    /** A container plus its state layer: the Material 3 way to be tappable. */
    static RippleDrawable ripple(Context ctx, int color, float radiusDp, int layer) {
        return new RippleDrawable(ColorStateList.valueOf(alpha(layer, STATE)),
                round(ctx, color, radiusDp), null);
    }

    static RippleDrawable rippleCircle(Context ctx, int color, int layer) {
        return new RippleDrawable(ColorStateList.valueOf(alpha(layer, STATE)),
                circle(color), null);
    }

    /** A list row: no container of its own, just the wash over the surface. */
    static RippleDrawable row(Context ctx) {
        return new RippleDrawable(ColorStateList.valueOf(alpha(onSurface, 0x14)),
                round(ctx, Color.TRANSPARENT, 0f), null);
    }

    /* ---------------------------------------------------------- typography */

    static Typeface medium() {
        return Typeface.create("sans-serif-medium", Typeface.NORMAL);
    }

    static void type(TextView t, float sizeSp, int color, boolean medium) {
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp);
        if (medium) {
            t.setTypeface(medium());
        }
    }

    /* ---------------------------------------------------------- components */

    /** A small top app bar: surface, flat, no divider. */
    static void appBar(View v) {
        v.setBackgroundColor(surface);
        v.setElevation(0f);
    }

    /** A text button: primary label, full radius, wash on press. */
    static void textButton(TextView t, Context ctx) {
        t.setBackground(ripple(ctx, Color.TRANSPARENT, SHAPE_FULL, primary));
        type(t, 14f, primary, true);
        t.setAllCaps(false);
        t.setLetterSpacing(0.01f);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(ctx, 16), dp(ctx, 10), dp(ctx, 16), dp(ctx, 10));
        t.setMinWidth(dp(ctx, 64));
    }

    /** A filled tonal button, the Material 3 default for a dialog's action. */
    static void tonalButton(TextView t, Context ctx) {
        t.setBackground(ripple(ctx, secondaryContainer, SHAPE_FULL, onSecondaryContainer));
        type(t, 14f, onSecondaryContainer, true);
        t.setAllCaps(false);
        t.setLetterSpacing(0.01f);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(ctx, 20), dp(ctx, 10), dp(ctx, 20), dp(ctx, 10));
        t.setMinWidth(dp(ctx, 64));
    }

    /**
     * The floating action button: 56dp, 16dp corners, primary container.  Not a
     * circle - that was Material 2 - and its icon wears the matching "on"
     * colour rather than white.
     */
    static void fab(TextView t, Context ctx, float sizeDp, float radiusDp) {
        t.setBackground(ripple(ctx, primaryContainer, radiusDp, onPrimaryContainer));
        type(t, sizeDp <= 40 ? 20f : 24f, onPrimaryContainer, false);
        t.setGravity(Gravity.CENTER);
        t.setIncludeFontPadding(false);
        t.setElevation(dp(ctx, 6));
    }

    /** A 48dp round icon button, for a bar that has icons in it. */
    static void iconButton(TextView t, Context ctx) {
        t.setBackground(rippleCircle(ctx, Color.TRANSPARENT, onSurfaceVariant));
        type(t, 22f, onSurfaceVariant, false);
        t.setGravity(Gravity.CENTER);
        t.setIncludeFontPadding(false);
    }

    /**
     * A dialog headline and its supporting text, for
     * {@link AlertDialog.Builder#setCustomTitle}.  The framework's own title is
     * Material 2 sized and cannot be recoloured without reaching into private
     * ids, so every titled dialog in this app hands over its own.
     */
    static LinearLayout headline(Context ctx, String title, String body) {
        LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView h = new TextView(ctx);
        h.setText(title);
        type(h, 22f, onSurface, false);
        box.addView(h);
        if (body != null && body.length() > 0) {
            TextView b = new TextView(ctx);
            b.setText(body);
            type(b, 14f, onSurfaceVariant, false);
            b.setLineSpacing(dp(ctx, 4), 1f);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            lp.topMargin = dp(ctx, 12);
            box.addView(b, lp);
        }
        return box;
    }

    /**
     * Show a dialog with the Material 3 container: 28dp corners on
     * surfaceContainerHigh, text buttons, no capital letters.  The framework
     * background carries the dialog's own padding, so replacing it means
     * putting that padding back on the decor view.
     *
     * @param listy true for a dialog whose body is a list of items, which
     *              Material 3 runs to the edges of the container
     */
    static AlertDialog show(AlertDialog.Builder builder, Context ctx, boolean listy) {
        AlertDialog d = builder.create();
        d.show();
        Window w = d.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(round(ctx, surfaceContainerHigh, SHAPE_XL));
            View decor = w.getDecorView();
            int side = dp(ctx, listy ? 8 : 24);
            decor.setPadding(side, dp(ctx, listy ? 8 : 24), side, dp(ctx, 8));
        }
        button(d, AlertDialog.BUTTON_POSITIVE, ctx);
        button(d, AlertDialog.BUTTON_NEGATIVE, ctx);
        button(d, AlertDialog.BUTTON_NEUTRAL, ctx);
        /*
         * The framework paints setMessage() with the theme's text colour, and
         * this app's theme is dark while the palette may well be light: a
         * message nobody can read is worse than no message.
         */
        TextView msg = (TextView) d.findViewById(android.R.id.message);
        if (msg != null) {
            msg.setTextColor(onSurfaceVariant);
            msg.setLineSpacing(dp(ctx, 4), 1f);
        }
        if (listy && d.getListView() != null) {
            d.getListView().setDivider(null);
            d.getListView().setDividerHeight(0);
            /*
             * The rows come from the theme's own item layout, which is light
             * text on a dark dialog - invisible on a light palette.  Only the
             * colour is touched: their metrics are the framework's, and padding
             * them here would grow them after the dialog has been measured.
             */
            d.getListView().setOnHierarchyChangeListener(
                new ViewGroup.OnHierarchyChangeListener() {
                    @Override
                    public void onChildViewAdded(View parent, View child) {
                        if (child instanceof TextView) {
                            ((TextView) child).setTextColor(onSurface);
                        }
                    }

                    @Override
                    public void onChildViewRemoved(View parent, View child) {
                    }
                });
        }
        return d;
    }

    static AlertDialog show(AlertDialog.Builder builder, Context ctx) {
        return show(builder, ctx, false);
    }

    private static void button(AlertDialog d, int which, Context ctx) {
        TextView b = d.getButton(which);
        if (b == null) {
            return;
        }
        type(b, 14f, primary, true);
        b.setAllCaps(false);
        b.setLetterSpacing(0.01f);
    }

    /** An edit field the Material 3 way: filled container, 4dp top corners. */
    static void field(TextView t, Context ctx) {
        t.setBackground(round(ctx, surfaceContainerHighest, SHAPE_XS));
        t.setTextColor(onSurface);
        t.setHintTextColor(onSurfaceVariant);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        t.setPadding(dp(ctx, 16), dp(ctx, 14), dp(ctx, 16), dp(ctx, 14));
    }

    static int dp(Context ctx, float v) {
        return Math.round(v * ctx.getResources().getDisplayMetrics().density);
    }
}
