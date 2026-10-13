package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.List;

/**
 * The one screen where a game is set up: where the picture goes and where the
 * keys go, together.
 *
 * <p>They belong on one screen because they are one decision.  A 240x320 game
 * on a tall phone leaves black bands above and below the picture, the player
 * wants the controls in those bands rather than on top of the game, and the
 * only way to judge that is to see both at once - which is why the stage here
 * is the whole window, the picture is drawn at exactly the size and position
 * {@link PictureFit} says the port will paint it, and the keypad floats over it
 * in the coordinates it will have in the game.
 *
 * <p>Dragging empty space moves the picture, pinching empty space scales it;
 * dragging a key moves that key, and a long press still adds one.  Saving
 * writes the keys to the shared pad blob and the picture to this game's
 * sidecar, so the game picks both up when it resumes.
 */
public class PadEditActivity extends Activity implements PadView.EditListener, PadView.Sink {

    private PadConfig work;
    private PadView pad;
    private PictureFrame frame;
    private int screenW = JarStore.DEF_W;
    private int screenH = JarStore.DEF_H;

    /* The game being set up, or null when the editor was opened on its own
     * (the library's ⋮ menu): keys are global, a picture position is not. */
    private JarStore.Entry entry;
    private String jarName;

    private int viewMode = PictureFit.MODE_INT;
    private float viewZoom = 1f;
    private float viewPanX, viewPanY;
    private int detW, detH;
    /** Published by the frame on every draw, so a drag can be clamped. */
    private float maxPanX, maxPanY;

    private View toolbar;
    private TextView barPill;

    /**
     * The window as the game will paint it.
     *
     * <p>Black where the port would leave black, the MIDlet's rectangle where
     * the chosen fit puts it.  Deliberately not a mock-up: the numbers come
     * from the same arithmetic the presenter uses, so a key the player parks
     * in a bar is in that bar in the game too.
     */
    private class PictureFrame extends View {
        PictureFrame(android.content.Context c) {
            super(c);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int w = getWidth();
            int h = getHeight();
            Paint p = new Paint();
            p.setColor(Color.BLACK);
            canvas.drawRect(0, 0, w, h, p);
            if (w <= 0 || h <= 0) {
                return;
            }
            PictureFit fit = PictureFit.of(w, h, screenW, screenH, viewMode,
                    viewZoom, viewPanX, viewPanY, detW, detH);
            maxPanX = PictureFit.maxPan(w, fit.w);
            maxPanY = PictureFit.maxPan(h, fit.h);

            float l = fit.x, t = fit.y, r = fit.x + fit.w, b = fit.y + fit.h;
            p.setColor(Color.argb(255, 48, 96, 48));
            canvas.drawRect(l, t, r, b, p);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(2f);
            p.setColor(Color.argb(255, 235, 235, 235));
            canvas.drawRect(l, t, r, b, p);
            p.setStyle(Paint.Style.FILL);

            p.setColor(Color.WHITE);
            p.setTextAlign(Paint.Align.CENTER);
            p.setTextSize(Math.max(16f, Math.min(w, h) * 0.030f));
            canvas.drawText(screenW + "x" + screenH + " 游戏画面", w / 2f,
                    t + fit.h / 2f, p);
            p.setTextSize(Math.max(12f, Math.min(w, h) * 0.023f));
            p.setColor(Color.argb(230, 255, 220, 120));
            canvas.drawText(viewText(), w / 2f,
                    t + fit.h / 2f + p.getTextSize() * 1.8f, p);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Md3.init(this);
        jarName = getIntent().getStringExtra("jar");
        screenW = getIntent().getIntExtra("w", JarStore.DEF_W);
        screenH = getIntent().getIntExtra("h", JarStore.DEF_H);
        if (jarName != null) {
            entry = JarStore.byName(this, jarName);
        }
        if (entry != null) {
            screenW = entry.w;
            screenH = entry.h;
            viewMode = entry.viewMode;
            viewZoom = entry.zoom > 0.05f ? entry.zoom : 1f;
            viewPanX = entry.panX;
            viewPanY = entry.panY;
            detW = entry.detW;
            detH = entry.detH;
        }
        work = PadConfig.load(this).copy();
        Ui.edgeToEdge(getWindow());
        Ui.apply(this, Ui.immersive(this));

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Md3.surface);

        frame = new PictureFrame(this);
        root.addView(frame, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        pad = new PadView(this, work);
        pad.setEditing(true);
        pad.setEditListener(this);
        pad.setSink(this);
        root.addView(pad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        /* The bars float over the stage instead of shrinking it: the stage has
         * to be the window the game gets, or the preview would be a lie.  They
         * only take the touches that land on a button - the rest falls through
         * to the keypad underneath, so a key under the toolbar is still
         * draggable. */
        toolbar = buildToolbar();
        FrameLayout.LayoutParams tlp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tlp.gravity = Gravity.TOP;
        root.addView(toolbar, tlp);

        /* What the toolbar leaves behind when it is put away: a key the player
         * wants at the very top of the screen would otherwise sit under it. */
        barPill = new TextView(this);
        barPill.setText("▾ 工具条");
        Md3.overlayHandle(barPill, this);
        Md3.type(barPill, 12f, Md3.OVERLAY_TEXT, false);
        barPill.setPadding(dp(12), dp(6), dp(12), dp(6));
        barPill.setVisibility(View.GONE);
        FrameLayout.LayoutParams plp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        plp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        plp.topMargin = dp(6);
        root.addView(barPill, plp);
        barPill.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                setToolbarVisible(true);
            }
        });

        setContentView(root);
        /* The editor has to show the same safe area the game will use. */
        final boolean immersive = Ui.immersive(this);
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                int[] e = Ui.insets(in, immersive);
                pad.setSafeAreaPx(e[0], e[1], e[2], e[3]);
                toolbar.setPadding(dp(6) + e[0], dp(4) + e[1], dp(6) + e[2], dp(4));
                barPill.setPadding(dp(12) + e[0], dp(6), dp(12) + e[2], dp(6));
                return in;
            }
        });
        root.requestApplyInsets();
        showTip();
    }

    /**
     * The gestures, told once, instead of a strip of small print along the
     * bottom that was in the way of the keys the player was placing.
     */
    private void showTip() {
        if (getSharedPreferences("pad", MODE_PRIVATE).getBoolean("tipOff", false)) {
            return;
        }
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), 0, dp(24), 0);

        TextView lines = new TextView(this);
        lines.setText("拖动空白：移动游戏画面\n"
                + "双指：缩放画面\n"
                + "长按空白：添加按键\n"
                + "拖动按键：摆位置 · 双指按按键：改大小\n"
                + "长按按键：改键 / 删除\n"
                + "滑杆：按键在游戏里的透明度\n"
                + "淡黄框：避开刘海 / 挖孔的可用区\n"
                + "工具条的 ▴ 把它收起来，点顶部「▾ 工具条」再打开");
        Md3.type(lines, 13f, Md3.onSurfaceVariant, false);
        lines.setLineSpacing(dp(4), 1f);
        box.addView(lines);

        final CheckBox never = new CheckBox(this);
        never.setText("不再提示");
        never.setTextColor(Md3.onSurface);
        never.setButtonTintList(android.content.res.ColorStateList.valueOf(Md3.primary));
        never.setPadding(0, dp(8), 0, 0);
        box.addView(never);

        Md3.show(new AlertDialog.Builder(this)
                .setCustomTitle(Md3.headline(this, "调整画面和按键", "怎么操作"))
                .setView(box)
                .setPositiveButton("知道了", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface d, int which) {
                        if (never.isChecked()) {
                            getSharedPreferences("pad", MODE_PRIVATE).edit()
                                    .putBoolean("tipOff", true).apply();
                        }
                    }
                }), this);
    }

    /** Puts the toolbar away so the top of the screen can hold a key. */
    private void setToolbarVisible(boolean visible) {
        if (toolbar != null) {
            toolbar.setVisibility(visible ? View.VISIBLE : View.GONE);
        }
        if (barPill != null) {
            barPill.setVisibility(visible ? View.GONE : View.VISIBLE);
        }
    }

    @Override
    public void onBackPressed() {
        confirmDiscard();
    }

    /* ------------------------------------------------------------- the bar */

    private View buildToolbar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Md3.surfaceContainer);
        bar.setPadding(dp(6), dp(4), dp(6), dp(4));

        bar.addView(button("布局", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                layoutDialog();
            }
        }));
        bar.addView(button("画面", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                viewDialog();
            }
        }));
        bar.addView(button("重置", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                work.replaceWith(PadConfig.defaults());
                refresh();
            }
        }));
        bar.addView(button("保存", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                save();
            }
        }));
        bar.addView(button("取消", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                confirmDiscard();
            }
        }));

        SeekBar slider = new SeekBar(this);
        /* the platform slider is teal in the dark theme and would disappear on
         * a light palette; both halves wear the accent instead */
        slider.getProgressDrawable().setColorFilter(Md3.primary,
                android.graphics.PorterDuff.Mode.SRC_IN);
        slider.getThumb().setColorFilter(Md3.primary,
                android.graphics.PorterDuff.Mode.SRC_IN);
        slider.setMax(85);
        slider.setProgress(Math.round(work.opacity * 100f) - 5);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                work.opacity = (progress + 5) / 100f;
                pad.setConfig(work);
            }

            @Override
            public void onStartTrackingTouch(SeekBar sb) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar sb) {
            }
        });
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        slp.setMargins(dp(8), 0, 0, 0);
        bar.addView(slider, slp);

        /* The strip can get out of the way: a player who wants a key along the
         * very top of the screen should not have to place it under the tools
         * they are using to place it. */
        bar.addView(button("\u25b4", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                setToolbarVisible(false);
            }
        }));
        return bar;
    }

    private TextView button(String label, View.OnClickListener click) {
        TextView t = new TextView(this);
        t.setText(label);
        /* five call-to-action labels and a slider share one row, so this is the
         * compact Material 3 text button rather than the full-size one */
        Md3.compactButton(t, this);
        t.setOnClickListener(click);
        return t;
    }

    /**
     * Redraw, and stop claiming the layout is still the preset it started as:
     * every caller of this is a change the player made by hand (the preset
     * picker redraws through pad.invalidate() instead).
     */
    private void refresh() {
        work.preset = PadConfig.PRESET_CUSTOM;
        pad.setConfig(work);
        pad.requestLayout();
        pad.invalidate();
    }

    /* -------------------------------------------------------- the picture */

    @Override
    public void onViewGesture(float zoomFactor, float dx, float dy) {
        viewZoom *= zoomFactor;
        if (viewZoom < 0.25f) {
            viewZoom = 0.25f;
        } else if (viewZoom > 8f) {
            viewZoom = 8f;
        }
        viewPanX += dx;
        viewPanY += dy;
        clampPan();
        frame.invalidate();
    }

    private void clampPan() {
        viewPanX = PictureFit.clampPan(viewPanX, maxPanX);
        viewPanY = PictureFit.clampPan(viewPanY, maxPanY);
    }

    private String viewText() {
        return viewName(viewMode) + " " + zoomText() + " · 偏移 " + panText()
                + " · 可挪 " + Math.round(maxPanX) + "/" + Math.round(maxPanY);
    }

    private String panText() {
        return Math.round(viewPanX) + "," + Math.round(viewPanY);
    }

    private String zoomText() {
        return (Math.round(viewZoom * 100f) / 100f) + "x";
    }

    private static String viewName(int mode) {
        if (mode == PictureFit.MODE_FILL) {
            return "铺满屏幕";
        }
        if (mode == PictureFit.MODE_AUTO) {
            return "适配画面";
        }
        return "整数倍缩放";
    }

    /**
     * How the picture is fitted, and the two ways to place it.
     *
     * <p>Picking a fit keeps the player's zoom and offset, because that is what
     * the port does: the fit decides the base scale, it does not take the
     * picture away from the finger that moved it.
     */
    private void viewDialog() {
        final int[] modes = { PictureFit.MODE_INT, PictureFit.MODE_FILL, PictureFit.MODE_AUTO };
        final int shown = viewMode == PictureFit.MODE_CUSTOM ? PictureFit.MODE_INT : viewMode;
        /* three fits, then 居中, then - when the port has measured a canvas -
         * the offer to adopt it as this game's resolution */
        final boolean adopt = detW > 0;
        String[] items = new String[modes.length + (adopt ? 2 : 1)];
        String[] names = { "整数倍缩放（默认，像素最清晰）",
                "铺满屏幕（放到最大，不裁画面）",
                "适配画面（只画 MIDlet 真正画到的那块）" };
        for (int i = 0; i < names.length; i++) {
            items[i] = (modes[i] == shown ? "\u2713 " : "      ") + names[i];
        }
        items[3] = "居中（偏移归零，缩放回到 1.0）";
        if (adopt) {
            items[4] = "用 " + detW + "x" + detH + " 作为分辨率（端口测到的）";
        }
        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this, "游戏画面 · 当前：" + viewName(shown),
                    viewName(shown) + " " + zoomText() + " · 偏移 " + panText()
                    + " · 可挪 " + Math.round(maxPanX) + "/" + Math.round(maxPanY)
                    + "\n拖动画面空白处也能挪，双指缩放"))
            .setItems(items, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    if (which < modes.length) {
                        viewMode = modes[which];
                    } else if (which == 3) {
                        viewZoom = 1f;
                        viewPanX = 0f;
                        viewPanY = 0f;
                    } else {
                        adoptDetected();
                    }
                    frame.invalidate();
                }
            })
            .setPositiveButton("微调位置", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    nudgeDialog();
                }
            })
            .setNegativeButton("关闭", null), this);
    }

    /** Arrow buttons, for placing the picture when a drag is too coarse. */
    private void nudgeDialog() {
        final int step = Math.max(4,
                Math.round(Math.min(pad.getWidth(), pad.getHeight()) * 0.02f));
        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.VERTICAL);
        grid.setPadding(dp(24), dp(4), dp(24), dp(12));

        final TextView info = new TextView(this);
        info.setPadding(dp(24), dp(8), dp(24), dp(8));
        grid.addView(info);

        grid.addView(nudgeRow(new String[] { null, "\u2191", null }, step, info));
        grid.addView(nudgeRow(new String[] { "\u2190", "居中", "\u2192" }, step, info));
        grid.addView(nudgeRow(new String[] { null, "\u2193", null }, step, info));
        updateNudge(info, step);

        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this, "微调位置", "每按一次移动 " + step + " 像素"))
            .setView(grid)
            .setNegativeButton("关闭", null), this);
    }

    /**
     * One row of the nudge cross: an arrow, 居中, or the gap where the cross
     * has no arm.  The arrow's direction is its own label, so one builder can
     * make all five buttons.
     */
    private View nudgeRow(String[] cells, final int step, final TextView info) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        for (int i = 0; i < 3; i++) {
            final String label = cells[i];
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(52), 1f);
            lp.setMargins(dp(2), dp(2), dp(2), dp(2));
            if (label == null) {
                row.addView(new View(this), lp);
                continue;
            }
            final boolean centre = "居中".equals(label);
            final float ax = "\u2190".equals(label) ? -step : ("\u2192".equals(label) ? step : 0f);
            final float ay = "\u2191".equals(label) ? -step : ("\u2193".equals(label) ? step : 0f);
            TextView t = new TextView(this);
            t.setText(label);
            t.setGravity(Gravity.CENTER);
            Md3.tonalButton(t, this);
            t.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (centre) {
                        viewZoom = 1f;
                        viewPanX = 0f;
                        viewPanY = 0f;
                    } else {
                        viewPanX += ax;
                        viewPanY += ay;
                    }
                    clampPan();
                    frame.invalidate();
                    updateNudge(info, step);
                }
            });
            row.addView(t, lp);
        }
        return row;
    }

    private void updateNudge(TextView info, int step) {
        info.setText("每步 " + step + " px · 可挪 ±" + Math.round(maxPanX)
                + " / ±" + Math.round(maxPanY) + " px\n当前偏移 " + panText()
                + " · " + viewName(viewMode) + " " + zoomText());
    }

    /** Take the port's measurement as this game's canvas size, next launch. */
    private void adoptDetected() {
        if (entry == null) {
            return;
        }
        entry.w = detW;
        entry.h = detH;
        JarStore.write(this, entry);
        screenW = detW;
        screenH = detH;
        toast("已改成 " + detW + "x" + detH + "，下次启动生效");
    }

    private void save() {
        work.save(this);
        if (entry != null) {
            entry.viewMode = viewMode;
            entry.zoom = viewZoom;
            entry.panX = viewPanX;
            entry.panY = viewPanY;
            JarStore.write(this, entry);
            toast("已保存：按键和画面位置");
        } else {
            toast("已保存按键；画面位置要从小游戏的「调整」进来才会记住");
        }
        finish();
    }

    /* ----------------------------------------------------- pad edit callbacks */

    @Override
    public void onKeyLongPress(final PadConfig.Key key) {
        final String[] actions = { "改键", "复制", "删除" };
        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this, KeyCatalog.labelOf(key.code),
                    "这个按键要做什么"))
            .setItems(actions, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    if (which == 0) {
                        pickKey(key, 0, 0);
                    } else if (which == 1) {
                        work.keys.add(new PadConfig.Key(key.code,
                                clamp01(key.x + 0.04f), clamp01(key.y + 0.04f),
                                key.w, key.h));
                        refresh();
                    } else {
                        work.keys.remove(key);
                        refresh();
                    }
                }
            }), this, true);
    }

    @Override
    public void onEmptyLongPress(float nx, float ny) {
        pickKey(null, nx, ny);
    }

    /**
     * Swap in one of the built-in layouts.
     *
     * <p>This replaces every key rather than merging: the point of "数字键盘" is
     * that a game using digits gets twelve keys where a D-pad was, and keeping
     * both would leave the old layout sitting under the new one.
     */
    private void layoutDialog() {
        final int[] ids = PadConfig.PRESET_IDS;
        String[] names = new String[ids.length];
        for (int i = 0; i < ids.length; i++) {
            names[i] = (ids[i] == work.preset ? "\u2713 " : "      ")
                    + PadConfig.presetName(ids[i]) + " · " + PadConfig.presetHint(ids[i]);
        }
        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this,
                    "悬浮按键布局", "当前：" + PadConfig.presetName(work.preset)))
            .setItems(names, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    work.applyPreset(PadConfig.byId(ids[which]));
                    pad.setConfig(work);
                    pad.invalidate();
                }
            }), this, true);
    }

    /** Add a key here, or re-point an existing one. */
    private void pickKey(final PadConfig.Key existing, final float nx, final float ny) {
        final List<KeyCatalog.Item> items = KeyCatalog.items();
        String[] labels = new String[items.size()];
        for (int i = 0; i < items.size(); i++) {
            labels[i] = items.get(i).label + "  (code " + items.get(i).code + ")";
        }
        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this,
                    existing == null ? "添加按键" : "改成哪个键", null))
            .setItems(labels, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    int code = items.get(which).code;
                    if (existing != null) {
                        existing.code = code;
                    } else {
                        work.keys.add(new PadConfig.Key(code, clamp01(nx), clamp01(ny),
                                0.14f, 0.06f));
                    }
                    refresh();
                }
            }), this, true);
    }

    private static float clamp01(float v) {
        return v < 0f ? 0f : (v > 0.9f ? 0.9f : v);
    }

    private void confirmDiscard() {
        Md3.show(new AlertDialog.Builder(this)
            .setCustomTitle(Md3.headline(this, "放弃这次修改？", "改动还没有保存"))
            .setPositiveButton("放弃", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    finish();
                }
            })
            .setNegativeButton("继续编辑", null), this);
    }

    /* ------------------------------------------- the sink the pad talks to */

    /*
     * The editor is not playing anything: the pad is in edit mode, where a
     * touch moves a key instead of pressing one, so the two press callbacks
     * cannot fire.  Only the picture gesture has something to do here.
     */

    @Override
    public void onPadKey(int androidKeyCode, boolean down) {
    }

    @Override
    public void onGameTouch(int pointerId, int phase, float x, float y) {
    }

    private void toast(String s) {
        android.widget.Toast.makeText(this, s, android.widget.Toast.LENGTH_SHORT).show();
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
