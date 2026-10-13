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
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.List;

/**
 * Visual editor for the on-screen keypad.
 *
 * <p>The dark band around the preview is the whole point: a MIDlet's own pixels
 * are precious and a key laid over them hides them, so the editor draws where
 * the game will actually be ({@link GameFrame}) and the player puts controls in
 * the bars around it.  That is a layout decision the runtime cannot make for
 * them, which is why it is a screen and not a setting.
 */
public class PadEditActivity extends Activity implements PadView.EditListener {

    private PadConfig work;
    private PadView pad;
    private int screenW = JarStore.DEF_W;
    private int screenH = JarStore.DEF_H;

    /** The letterboxed game area the keys must not cover. */
    private class GameFrame extends View {
        GameFrame(android.content.Context c) {
            super(c);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int w = getWidth();
            int h = getHeight();
            Paint p = new Paint();
            p.setColor(Color.argb(255, 18, 18, 20));
            canvas.drawRect(0, 0, w, h, p);

            float scale = Math.min(w / (float) screenW, h / (float) screenH);
            float gw = screenW * scale;
            float gh = screenH * scale;
            float left = (w - gw) / 2f;
            float top = (h - gh) / 2f;

            p.setColor(Color.argb(255, 48, 96, 48));
            canvas.drawRect(left, top, left + gw, top + gh, p);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(2f);
            p.setColor(Color.argb(255, 220, 220, 220));
            canvas.drawRect(left, top, left + gw, top + gh, p);
            p.setStyle(Paint.Style.FILL);

            p.setColor(Color.WHITE);
            p.setTextSize(Math.max(16f, Math.min(w, h) * 0.035f));
            p.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(screenW + "x" + screenH + " 游戏画面", w / 2f, top + gh / 2f, p);
            p.setTextSize(Math.max(12f, Math.min(w, h) * 0.026f));
            p.setColor(Color.argb(220, 255, 220, 120));
            canvas.drawText("按键放在上下黑边里", w / 2f, top + gh / 2f + p.getTextSize() * 1.6f, p);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        screenW = getIntent().getIntExtra("w", JarStore.DEF_W);
        screenH = getIntent().getIntExtra("h", JarStore.DEF_H);
        work = PadConfig.load(this).copy();
        Ui.edgeToEdge(getWindow());
        Ui.apply(this, Ui.immersive(this));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        final View toolbar = buildToolbar();
        root.addView(toolbar);

        FrameLayout stage = new FrameLayout(this);
        stage.addView(new GameFrame(this), new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pad = new PadView(this, work);
        pad.setEditing(true);
        pad.setEditListener(this);
        stage.addView(pad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(stage, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        final TextView hint = new TextView(this);
        hint.setText("布局=整套按键 · 拖动=移动 · 双指=缩放 · 长按按键=改键/删除 · 长按空白=添加 · 滑杆=游戏中显示的透明度 · 淡黄框=避开刘海/挖孔的可用区");
        hint.setTextColor(Color.argb(220, 200, 200, 200));
        hint.setTextSize(11f);
        hint.setPadding(dp(8), dp(6), dp(8), dp(8));
        root.addView(hint);

        setContentView(root);
        /* The editor has to show the same safe area the game will use. */
        final boolean immersive = Ui.immersive(this);
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                int[] e = Ui.insets(in, immersive);
                pad.setSafeAreaPx(e[0], e[1], e[2], e[3]);
                toolbar.setPadding(dp(6) + e[0], dp(4) + e[1], dp(6) + e[2], dp(4));
                hint.setPadding(dp(8) + e[0], dp(6), dp(8) + e[2], dp(8) + e[3]);
                return in;
            }
        });
        root.requestApplyInsets();
    }

    @Override
    public void onBackPressed() {
        confirmDiscard();
    }

    private View buildToolbar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.argb(255, 24, 24, 26));
        bar.setPadding(dp(6), dp(4), dp(6), dp(4));

        bar.addView(button("布局", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                layoutDialog();
            }
        }));
        bar.addView(button("添加按键", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickKey(null, 0.32f, 0.86f);
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
                work.save(PadEditActivity.this);
                finish();
            }
        }));
        bar.addView(button("取消", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                confirmDiscard();
            }
        }));

        SeekBar bar2 = new SeekBar(this);
        bar2.setMax(85);
        bar2.setProgress(Math.round(work.opacity * 100f) - 5);
        bar2.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
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
        bar.addView(bar2, slp);
        return bar;
    }

    private TextView button(String label, View.OnClickListener click) {
        TextView t = new TextView(this);
        t.setText(label);
        t.setTextColor(Color.WHITE);
        t.setTextSize(13f);
        t.setPadding(dp(9), dp(7), dp(9), dp(7));
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

    /* ----------------------------------------------------- pad edit callbacks */

    @Override
    public void onKeyLongPress(final PadConfig.Key key) {
        final String[] actions = { "改键", "复制", "删除" };
        new AlertDialog.Builder(this)
            .setTitle(KeyCatalog.labelOf(key.code))
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
            })
            .show();
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
        new AlertDialog.Builder(this)
            .setTitle("悬浮按键布局 · 当前：" + PadConfig.presetName(work.preset))
            .setItems(names, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    work.applyPreset(PadConfig.byId(ids[which]));
                    pad.setConfig(work);
                    pad.invalidate();
                }
            })
            .show();
    }

    /** Add a key here, or re-point an existing one. */
    private void pickKey(final PadConfig.Key existing, final float nx, final float ny) {
        final List<KeyCatalog.Item> items = KeyCatalog.items();
        String[] labels = new String[items.size()];
        for (int i = 0; i < items.size(); i++) {
            labels[i] = items.get(i).label + "  (code " + items.get(i).code + ")";
        }
        new AlertDialog.Builder(this)
            .setTitle(existing == null ? "添加按键" : "改成哪个键")
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
            })
            .show();
    }

    private static float clamp01(float v) {
        return v < 0f ? 0f : (v > 0.9f ? 0.9f : v);
    }

    private void confirmDiscard() {
        new AlertDialog.Builder(this)
            .setTitle("放弃这次修改？")
            .setPositiveButton("放弃", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    finish();
                }
            })
            .setNegativeButton("继续编辑", null)
            .show();
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
