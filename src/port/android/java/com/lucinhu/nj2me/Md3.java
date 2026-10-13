package com.lucinhu.nj2me;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
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
import android.widget.Toast;

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

    /* -------------------------------------------- the chrome over the game */

    /**
     * The control bar, the status card and the handle float ON the game rather
     * than in a window, so they keep the dark scheme's colours in both schemes:
     * a bright bar over a black picture is the one thing worse than the black
     * system bars this app used to have.  They are still Material 3 neutrals,
     * just read from the dark end.
     */
    static final int OVERLAY = 0xE62B2930;        /* surfaceContainerHigh at 90% */
    static final int OVERLAY_TEXT = 0xFFE6E0E9;   /* the dark scheme's onSurface */
    static final int OVERLAY_MUTED = 0xFFCAC4D0;  /* ... and onSurfaceVariant */
    static final int OVERLAY_ACCENT = 0xFFD0BCFF; /* ... and primary */

    /** A text button for something that sits on the picture. */
    static void overlayButton(TextView t, Context ctx) {
        t.setBackground(ripple(ctx, Color.TRANSPARENT, SHAPE_FULL, OVERLAY_ACCENT));
        type(t, 14f, OVERLAY_ACCENT, true);
        t.setAllCaps(false);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(ctx, 12), dp(ctx, 8), dp(ctx, 12), dp(ctx, 8));
    }

    /** The round handle that brings the bar back, floating over the picture. */
    static void overlayHandle(TextView t, Context ctx) {
        t.setBackground(rippleCircle(ctx, 0xB32B2930, OVERLAY_TEXT));
        type(t, 20f, OVERLAY_TEXT, false);
        t.setGravity(Gravity.CENTER);
        t.setIncludeFontPadding(false);
    }

    /**
     * A text button with tight padding, for a bar that has to hold half a dozen
     * of them and a slider next to each other.  {@link #textButton} is the
     * Material 3 size, which is the right one when there is room for it.
     */
    static void compactButton(TextView t, Context ctx) {
        t.setBackground(ripple(ctx, Color.TRANSPARENT, SHAPE_FULL, primary));
        type(t, 13f, primary, true);
        t.setAllCaps(false);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(ctx, 10), dp(ctx, 8), dp(ctx, 10), dp(ctx, 8));
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
        type(h, 24f, onSurface, false);
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


    /* --------------------------------------------- dialog anatomy, part 2 */

    /*
     * A Material 3 dialog is not a title, a wall of text and two buttons.  It
     * has a shape: a 24sp headline, supporting text at 14sp in a quieter
     * colour, content that is either prose or a list of 56dp rows with leading
     * icons, and its actions along the bottom as text buttons.  The pieces
     * below are that shape, so every dialog in the app is built from the same
     * four or five calls instead of from whatever the framework defaults to.
     */

    /** A vertical container for dialog content. The container pads, not this. */
    static LinearLayout body(Context ctx) {
        LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        return box;
    }

    /** The dialog headline: Material 3's headline-small. */
    static TextView title(Context ctx, String text) {
        TextView t = new TextView(ctx);
        t.setText(text);
        type(t, 24f, onSurface, false);
        t.setLineSpacing(dp(ctx, 4), 1f);
        return t;
    }

    /** A headline with a leading glyph, which is how the shell names a screen. */
    static LinearLayout titleWith(Context ctx, int kind, String text) {
        LinearLayout line = new LinearLayout(ctx);
        line.setOrientation(LinearLayout.HORIZONTAL);
        line.setGravity(Gravity.CENTER_VERTICAL);
        if (kind != 0) {
            line.addView(icon(ctx, kind, onSurfaceVariant));
            TextView t = title(ctx, text);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            lp.leftMargin = dp(ctx, 12);
            line.addView(t, lp);
        } else {
            line.addView(title(ctx, text), new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
        }
        return line;
    }

    /** Supporting text: 14sp onSurfaceVariant, with room to breathe. */
    static TextView supporting(Context ctx, String text) {
        TextView t = new TextView(ctx);
        t.setText(text);
        type(t, 14f, onSurfaceVariant, false);
        t.setLineSpacing(dp(ctx, 6), 1f);
        return t;
    }

    /**
     * One labelled block of prose: a 12sp label in the accent colour over a
     * 14sp body.  Labels are what turns a paragraph into instructions.
     */
    static LinearLayout section(Context ctx, String label, String text) {
        LinearLayout box = body(ctx);
        if (label != null && label.length() > 0) {
            TextView l = new TextView(ctx);
            l.setText(label);
            type(l, 12f, primary, true);
            l.setLetterSpacing(0.06f);
            box.addView(l);
        }
        TextView b = supporting(ctx, text);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        if (label != null && label.length() > 0) {
            lp.topMargin = dp(ctx, 2);
        }
        box.addView(b, lp);
        return box;
    }

    /**
     * A command the player is meant to type somewhere else.  It sits in its own
     * filled container - text a reader cannot select is text they cannot use -
     * and one tap copies it, because "adb push ..." on a phone is otherwise a
     * transcription exercise.
     */
    static TextView command(final Context ctx, final String text) {
        final TextView t = new TextView(ctx);
        t.setText(text);
        type(t, 13f, onSurface, false);
        t.setTypeface(Typeface.MONOSPACE);
        t.setBackground(round(ctx, surfaceContainerHighest, SHAPE_S));
        t.setPadding(dp(ctx, 12), dp(ctx, 10), dp(ctx, 12), dp(ctx, 10));
        t.setTextIsSelectable(true);
        t.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ClipboardManager cm = (ClipboardManager)
                        ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) {
                    cm.setPrimaryClip(ClipData.newPlainText("nano_j2me", text));
                    Toast.makeText(ctx, "已复制", Toast.LENGTH_SHORT).show();
                }
            }
        });
        return t;
    }

    /** A 1dp rule between rows of a list dialog. */
    static View divider(Context ctx) {
        View v = new View(ctx);
        v.setBackgroundColor(outlineVariant);
        v.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, dp(ctx, 0.5f))));
        return v;
    }

    /**
     * A list row: 56dp tall, leading icon in onSurfaceVariant, 16sp label in
     * onSurface, and - when this row is the chosen one - a check mark at the
     * trailing edge in the accent colour, which is how Material 3 marks a
     * single-choice list.  The state layer covers the whole row.
     */
    static LinearLayout row(Context ctx, int kind, String label, String hint) {
        return row(ctx, kind, label, hint, onSurface, false, null);
    }

    static LinearLayout row(Context ctx, int kind, String label, String hint,
                            boolean selected, View.OnClickListener click) {
        return row(ctx, kind, label, hint, onSurface, selected, click);
    }

    static LinearLayout row(Context ctx, int kind, String label, String hint,
                            int color, boolean selected, View.OnClickListener click) {
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(ctx, hint == null ? 56 : 64));
        row.setPadding(dp(ctx, 16), dp(ctx, 8), dp(ctx, 16), dp(ctx, 8));
        row.setBackground(ripple(ctx, Color.TRANSPARENT, SHAPE_S,
                color == error ? error : onSurfaceVariant));
        if (kind != 0) {
            LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            ip.rightMargin = dp(ctx, 16);
            row.addView(icon(ctx, kind, selected ? primary : (color == onSurface ? onSurfaceVariant : color)), ip);
        }
        LinearLayout text = body(ctx);
        TextView t = new TextView(ctx);
        t.setText(label);
        type(t, 16f, selected ? primary : color, false);
        text.addView(t);
        if (hint != null && hint.length() > 0) {
            TextView h = new TextView(ctx);
            h.setText(hint);
            type(h, 12f, onSurfaceVariant, false);
            LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            hp.topMargin = dp(ctx, 2);
            text.addView(h, hp);
        }
        row.addView(text, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        if (selected) {
            LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            cp.leftMargin = dp(ctx, 12);
            row.addView(icon(ctx, IC_CHECK, primary), cp);
        }
        if (click != null) {
            row.setOnClickListener(click);
        }
        return row;
    }

    static LinearLayout row(Context ctx, int kind, String label, boolean selected,
                            View.OnClickListener click) {
        return row(ctx, kind, label, null, onSurface, selected, click);
    }

    /** A row whose action cannot be undone: label, icon and state layer in error. */
    static LinearLayout dangerRow(Context ctx, int kind, String label,
                                  View.OnClickListener click) {
        return row(ctx, kind, label, null, error, false, click);
    }

    /** Layout parameters for a block with `topDp` above it. */
    static LinearLayout.LayoutParams gap(Context ctx, int topDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(ctx, topDp);
        return lp;
    }

    /** What a single-choice dialog does with the row the player picked. */
    interface Choice {
        void picked(int which, AlertDialog dialog);
    }

    /**
     * A single-choice list, which is most of this app's dialogs: a headline, a
     * supporting line, one 56dp row per option - the current one marked with a
     * trailing check and in the accent colour - and a 关闭 button.  The rows
     * close the dialog before they act, so an action that opens another dialog
     * does not stack two of them.
     */
    static AlertDialog chooser(final Context ctx, int iconKind, String headline,
                               String support, String[] labels, String[] hints,
                               int checked, final Choice choice) {
        LinearLayout box = body(ctx);
        box.addView(titleWith(ctx, iconKind, headline));
        if (support != null && support.length() > 0) {
            box.addView(supporting(ctx, support), gap(ctx, 6));
        }
        final AlertDialog[] holder = new AlertDialog[1];
        for (int i = 0; i < labels.length; i++) {
            final int which = i;
            String hint = (hints != null && i < hints.length) ? hints[i] : null;
            LinearLayout row = row(ctx, 0, labels[i], hint, i == checked, null);
            row.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    AlertDialog d = holder[0];
                    if (d != null) {
                        d.dismiss();
                    }
                    choice.picked(which, d);
                }
            });
            box.addView(row, gap(ctx, i == 0 ? 12 : 0));
        }
        holder[0] = show(new AlertDialog.Builder(ctx)
                .setView(box)
                .setNegativeButton("关闭", null), ctx);
        return holder[0];
    }

    /** The destructive action of a dialog: an error-coloured text button. */
    static void errorButton(AlertDialog d, int which) {
        TextView b = d.getButton(which);
        if (b != null) {
            b.setTextColor(error);
        }
    }

    /* --------------------------------------------------------------- icons */

    /*
     * Material 3 list rows lead with a glyph.  These are drawn rather than
     * fetched: the platform's own set is Material 2, and the icon fonts this
     * app renders for the MIDlets have no arrow glyphs at all.  Each path is
     * written in a 24x24 grid and scaled to the density, so one icon looks the
     * same at any size.
     */

    static final int IC_PLAY = 1;
    static final int IC_SCREEN = 2;
    static final int IC_MEMORY = 3;
    static final int IC_RENAME = 4;
    static final int IC_KEYS = 5;
    static final int IC_DELETE = 6;
    static final int IC_TUNE = 7;
    static final int IC_INFO = 8;
    static final int IC_POWER = 9;
    static final int IC_FULLSCREEN = 10;
    static final int IC_SORT = 11;
    static final int IC_IMPORT = 12;
    static final int IC_HELP = 13;
    static final int IC_CHECK = 14;

    static View icon(Context ctx, int kind, int color) {
        return new Icon(ctx, kind, color, dp(ctx, 24));
    }

    private static final class Icon extends View {
        private final int kind;
        private final int size;
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint glyph = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF r = new RectF();

        Icon(Context ctx, int kind, int color, int size) {
            super(ctx);
            this.kind = kind;
            this.size = size;
            line.setStyle(Paint.Style.STROKE);
            /* The canvas is scaled to the 24-unit grid below, so the stroke and
             * the glyph are given in grid units too - in device pixels they
             * would come out multiplied by that scale and the ring would fill
             * its own hole. */
            line.setStrokeWidth(1.7f);
            line.setStrokeCap(Paint.Cap.ROUND);
            line.setStrokeJoin(Paint.Join.ROUND);
            line.setColor(color);
            fill.setStyle(Paint.Style.FILL);
            fill.setColor(color);
            glyph.setColor(color);
            glyph.setTextAlign(Paint.Align.CENTER);
            glyph.setTextSize(13f);
            glyph.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        }

        @Override
        protected void onMeasure(int widthSpec, int heightSpec) {
            setMeasuredDimension(size, size);
        }

        @Override
        protected void onDraw(Canvas c) {
            c.save();
            c.scale(size / 24f, size / 24f);
            path.reset();
            switch (kind) {
                case IC_PLAY:
                    path.moveTo(8, 5);
                    path.lineTo(19, 12);
                    path.lineTo(8, 19);
                    path.close();
                    c.drawPath(path, fill);
                    break;
                case IC_SCREEN:
                    r.set(3, 5, 21, 17);
                    c.drawRoundRect(r, 2, 2, line);
                    c.drawLine(9, 20, 15, 20, line);
                    break;
                case IC_MEMORY:
                    r.set(6, 6, 18, 18);
                    c.drawRoundRect(r, 1.5f, 1.5f, line);
                    c.drawLine(6, 3.5f, 6, 6, line);
                    c.drawLine(10, 3.5f, 10, 6, line);
                    c.drawLine(14, 3.5f, 14, 6, line);
                    c.drawLine(18, 3.5f, 18, 6, line);
                    c.drawLine(6, 18, 6, 20.5f, line);
                    c.drawLine(10, 18, 10, 20.5f, line);
                    c.drawLine(14, 18, 14, 20.5f, line);
                    c.drawLine(18, 18, 18, 20.5f, line);
                    break;
                case IC_RENAME:
                    path.moveTo(4, 20);
                    path.lineTo(5.5f, 15);
                    path.lineTo(15, 5.5f);
                    path.lineTo(18.5f, 9);
                    path.lineTo(9, 18.5f);
                    path.close();
                    c.drawPath(path, fill);
                    break;
                case IC_KEYS:
                    r.set(3, 3, 11, 11);
                    c.drawRoundRect(r, 2, 2, fill);
                    r.set(13, 3, 21, 11);
                    c.drawRoundRect(r, 2, 2, fill);
                    r.set(3, 13, 11, 21);
                    c.drawRoundRect(r, 2, 2, fill);
                    r.set(13, 13, 21, 21);
                    c.drawRoundRect(r, 2, 2, fill);
                    break;
                case IC_DELETE:
                    c.drawLine(4, 7, 20, 7, line);
                    c.drawLine(9, 4.5f, 15, 4.5f, line);
                    r.set(6, 7, 18, 20.5f);
                    c.drawRoundRect(r, 2, 2, line);
                    c.drawLine(10, 10.5f, 10, 17, line);
                    c.drawLine(14, 10.5f, 14, 17, line);
                    break;
                case IC_TUNE:
                    c.drawLine(4, 7, 20, 7, line);
                    c.drawLine(4, 12, 20, 12, line);
                    c.drawLine(4, 17, 20, 17, line);
                    c.drawCircle(15, 7, 2.4f, fill);
                    c.drawCircle(9, 12, 2.4f, fill);
                    c.drawCircle(16, 17, 2.4f, fill);
                    break;
                case IC_INFO:
                    r.set(3.5f, 3.5f, 20.5f, 20.5f);
                    c.drawOval(r, line);
                    c.drawCircle(12, 7.5f, 1.4f, fill);
                    c.drawLine(12, 11, 12, 17, line);
                    break;
                case IC_POWER:
                    r.set(4.5f, 5, 19.5f, 20);
                    c.drawArc(r, -60, 300, false, line);
                    c.drawLine(12, 3, 12, 12, line);
                    break;
                case IC_FULLSCREEN:
                    c.drawLine(4, 9, 4, 4, line);
                    c.drawLine(4, 4, 9, 4, line);
                    c.drawLine(15, 4, 20, 4, line);
                    c.drawLine(20, 4, 20, 9, line);
                    c.drawLine(20, 15, 20, 20, line);
                    c.drawLine(20, 20, 15, 20, line);
                    c.drawLine(9, 20, 4, 20, line);
                    c.drawLine(4, 20, 4, 15, line);
                    break;
                case IC_SORT:
                    c.drawLine(4, 7, 20, 7, line);
                    c.drawLine(4, 12, 15, 12, line);
                    c.drawLine(4, 17, 10, 17, line);
                    break;
                case IC_IMPORT:
                    c.drawLine(12, 4, 12, 15, line);
                    c.drawLine(8, 11, 12, 15, line);
                    c.drawLine(16, 11, 12, 15, line);
                    c.drawLine(4, 15, 4, 20, line);
                    c.drawLine(4, 20, 20, 20, line);
                    c.drawLine(20, 20, 20, 15, line);
                    break;
                case IC_HELP:
                    r.set(3.5f, 3.5f, 20.5f, 20.5f);
                    c.drawOval(r, line);
                    c.drawText("?", 12, 16.8f, glyph);
                    break;
                case IC_CHECK:
                    c.drawLine(5, 12.5f, 10, 17.5f, line);
                    c.drawLine(10, 17.5f, 19, 6.5f, line);
                    break;
                default:
                    break;
            }
            c.restore();
        }
    }

    static int dp(Context ctx, float v) {
        return Math.round(v * ctx.getResources().getDisplayMetrics().density);
    }
}
