package com.example.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.util.List;

/**
 * The launcher: pick a .jar, set it up, run it.
 *
 * <p>There is no storage permission and no media scanner here on purpose.  A jar
 * gets in either through the document picker (copied into app storage, so it
 * survives the source file moving) or by being pushed into that same directory
 * over adb, and both end up as rows in the same list.  That is the smallest
 * thing that works on every Android from 8 to 15 without asking for anything.
 */
public class LibraryActivity extends Activity {

    private static final int REQ_PICK = 1;

    private ListView list;
    private TextView empty;
    private TextView headerCount;
    private List<JarStore.Entry> items;
    private EntryAdapter adapter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(rgb(14, 14, 18));          /* #0E0E12 */

        /* Header: name and game count on the left, the two actions on the right. */
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(16), dp(14), dp(16), dp(14));
        header.setBackground(rect(new int[] { 0xFF16203A, 0xFF0E0E12 },
                GradientDrawable.Orientation.LEFT_RIGHT, 0, 0));

        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        TextView title = tv("Nano J2ME", 22f, 0xFFFFFFFF, true);
        headerCount = tv("", 12f, 0xFF9AA0A6, false);
        titles.addView(title);
        titles.addView(headerCount);
        header.addView(titles, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        header.addView(button("按键布局", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startActivity(new Intent(LibraryActivity.this, PadEditActivity.class));
            }
        }));
        header.addView(button("添加 JAR", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickJar();
            }
        }));
        root.addView(header);

        list = new ListView(this);
        list.setDivider(null);
        list.setDividerHeight(dp(6));
        list.setPadding(dp(10), dp(10), dp(10), dp(10));
        list.setClipToPadding(false);
        list.setVerticalScrollBarEnabled(false);
        list.setOverScrollMode(View.OVER_SCROLL_NEVER);
        adapter = new EntryAdapter();
        list.setAdapter(adapter);
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int pos, long id) {
                launch(items.get(pos));
            }
        });
        list.setOnItemLongClickListener(new AdapterView.OnItemLongClickListener() {
            @Override
            public boolean onItemLongClick(AdapterView<?> p, View v, int pos, long id) {
                options(items.get(pos));
                return true;
            }
        });
        root.addView(list, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        /* The list shows the empty view by itself; the scroll view is only here
         * so that the six lines of it fit on a short screen. */
        ScrollView emptyBox = new ScrollView(this);
        empty = tv("", 13f, 0xFF6A6F78, false);
        empty.setGravity(Gravity.CENTER);
        empty.setLineSpacing(dp(4), 1f);
        empty.setPadding(dp(22), dp(22), dp(22), dp(22));
        emptyBox.addView(empty, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        emptyBox.setVisibility(View.GONE);
        root.addView(emptyBox, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        list.setEmptyView(emptyBox);

        setContentView(root);

        /* First draw needs the count before the first resume lands. */
        reload();

        /*
         * A file manager can hand us a .jar directly, which is the shortest path
         * a user has: tap the jar, pick nano_j2me, it runs.
         *
         * With one exception: a RECREATION.  The intent that started an
         * activity stays attached to it for its whole life, so when the system
         * rebuilds this one - it was reclaimed while the game held the
         * foreground, or the configuration changed - onCreate sees the very
         * same VIEW URI again.  Importing it there is why leaving a game put
         * another copy of the jar in the list every single time.  A genuinely
         * new delivery still arrives through onNewIntent(), which is where a
         * second import belongs.
         */
        if (savedInstanceState != null) {
            setIntent(new Intent());
        }
        handleViewIntent(getIntent());
    }

    @Override
    protected void onResume() {
        super.onResume();
        reload();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleViewIntent(intent);
    }

    private void handleViewIntent(Intent it) {
        if (it == null) {
            return;
        }
        Uri uri = null;
        if (Intent.ACTION_VIEW.equals(it.getAction())) {
            uri = it.getData();
        } else if (Intent.ACTION_SEND.equals(it.getAction())) {
            /* "Share -> nano_j2me" from a file manager or a chat app. */
            uri = intentStream(it);
        }
        if (uri == null) {
            return;
        }
        /*
         * Consume it.  One delivery gets one import, whatever runs next: the
         * URI stops being the activity's intent here, so no later resume or
         * recreation can pick it up again.  (JarStore.importUri() also refuses
         * to make a second copy of a jar it already has, which is the second
         * half of the same guarantee - a repeated import is then not just
         * ignored but harmless.)
         */
        setIntent(new Intent());
        try {
            JarStore.Entry e = JarStore.importUri(this, uri);
            offload(e);
        } catch (IOException ioe) {
            toast("无法读取这个文件：" + ioe.getMessage());
        } catch (RuntimeException re) {
            /* A sender can hand over a URI this app is not allowed to open - a
             * grant that was never made, a provider that refuses the path.  The
             * framework raises SecurityException, not IOException, so without
             * this the launcher dies on someone else's bad intent. */
            toast("没有权限读取这个文件：" + re.getMessage());
        }
    }

    @SuppressWarnings("deprecation")
    private static Uri intentStream(Intent it) {
        android.os.Parcelable p = it.getParcelableExtra(Intent.EXTRA_STREAM);
        return (p instanceof Uri) ? (Uri) p : null;
    }

    /* ------------------------------------------------------------------ list */

    private void reload() {
        items = JarStore.list(this);
        adapter.notifyDataSetChanged();
        if (items.isEmpty()) {
            empty.setText("还没有游戏\n\n"
                    + "点右上角「添加 JAR」从文件管理器选一个\n"
                    + "或者用电脑执行：\n"
                    + "adb push 游戏.jar " + JarStore.dir(this).getAbsolutePath() + "/");
        }
        headerCount.setText("J2ME 运行时 · " + items.size() + " 个游戏");
    }

    private class EntryAdapter extends BaseAdapter {
        @Override
        public int getCount() {
            return items == null ? 0 : items.size();
        }

        @Override
        public Object getItem(int i) {
            return items.get(i);
        }

        @Override
        public long getItemId(int i) {
            return i;
        }

        @Override
        public View getView(int i, View reuse, ViewGroup parent) {
            ViewGroup row = (reuse != null && "card".equals(reuse.getTag()))
                    ? (ViewGroup) reuse : rowCard();
            bindRow(row, items.get(i));
            return row;
        }
    }

    /* ------------------------------------------------------------------ rows */

    private ViewGroup rowCard() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setBackground(rect(null, null, rgb(25, 25, 32), dp(14)));   /* #191920 */
        row.setPadding(dp(12), dp(12), dp(12), dp(12));
        row.setMinimumHeight(dp(64));

        ImageView icon = new ImageView(this);
        row.addView(icon, new LinearLayout.LayoutParams(dp(48), dp(48)));

        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        tp.leftMargin = dp(12);
        tp.rightMargin = dp(8);
        row.addView(text, tp);

        TextView name = tv("", 15f, 0xFFF2F2F5, true);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        TextView sub = tv("", 11.5f, 0xFF8A9099, false);
        text.addView(name);
        text.addView(sub);

        LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.VERTICAL);
        chips.setGravity(Gravity.END);
        row.addView(chips, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView screen = tv("", 11f, 0xFFA8C7FF, false);
        screen.setBackground(rect(null, null, rgb(30, 42, 68), dp(9)));  /* #1E2A44 */
        screen.setPadding(dp(6), dp(2), dp(6), dp(2));
        chips.addView(screen);

        TextView det = tv("", 10f, 0xFF6FCF97, false);
        det.setPadding(0, dp(3), dp(2), 0);
        det.setVisibility(View.GONE);
        chips.addView(det);

        row.setTag("card");
        /* bindRow() finds these three by position, so their order is the layout. */
        return row;
    }

    private void bindRow(ViewGroup row, JarStore.Entry e) {
        ((ImageView) row.getChildAt(0)).setImageBitmap(JarIcons.forEntry(e, dp(48)));

        LinearLayout text = (LinearLayout) row.getChildAt(1);
        /* JarIcons keeps the parsed manifest of the last few dozen jars, so this
         * is a map lookup on every bind but the first one. */
        String[] info = JarIcons.info(e.jar);
        ((TextView) text.getChildAt(0)).setText(info != null ? info[0] : e.title);
        ((TextView) text.getChildAt(1)).setText(e.jar.getName() + " · " + e.sizeText());

        LinearLayout chips = (LinearLayout) row.getChildAt(2);
        String det = e.detText();
        /* Before the game has ever run, nothing was measured, so the chip shows
         * what it will be asked to paint; afterwards the truth wins. */
        ((TextView) chips.getChildAt(0)).setText(det != null ? det : e.w + "x" + e.h);
        TextView detView = (TextView) chips.getChildAt(1);
        if (det == null) {
            detView.setVisibility(View.GONE);
        } else {
            detView.setVisibility(View.VISIBLE);
            detView.setText("设置 " + e.w + "x" + e.h);
        }
    }

    /* --------------------------------------------------------------- actions */

    private void pickJar() {
        Intent it = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        it.addCategory(Intent.CATEGORY_OPENABLE);
        it.setType("*/*");
        /* A jar is a zip: some pickers call it java-archive, some call it
         * octet-stream, and none of them agree.  Ask for all three. */
        it.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/java-archive", "application/octet-stream", "*/*" });
        try {
            startActivityForResult(it, REQ_PICK);
        } catch (android.content.ActivityNotFoundException e) {
            toast("这台设备没有文件选择器");
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK || res != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        try {
            JarStore.Entry e = JarStore.importUri(this, data.getData());
            reload();
            offload(e);
        } catch (IOException ioe) {
            toast("导入失败：" + ioe.getMessage());
        } catch (RuntimeException re) {
            toast("导入失败：" + re.getMessage());
        }
    }

    private void launch(JarStore.Entry e) {
        JarStore.touch(this, e);
        Intent it = new Intent(this, GameActivity.class);
        it.putExtra("jar", e.jar.getName());
        it.putExtra("jarpath", e.jar.getAbsolutePath());
        it.putExtra("title", e.title);
        it.putExtra("w", e.w);
        it.putExtra("h", e.h);
        it.putExtra("heap", e.heapMB);
        it.putExtra("view", e.viewMode);
        it.putExtra("zoom", e.zoom);
        it.putExtra("panx", e.panX);
        it.putExtra("pany", e.panY);
        startActivity(it);
    }

    /** After importing, ask the two questions that decide whether it will run. */
    private void offload(final JarStore.Entry e) {
        new AlertDialog.Builder(this)
            .setTitle(e.title)
            .setMessage("导入成功。这个游戏的屏幕分辨率是多少？")
            .setPositiveButton("用 240x320", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    e.w = 240;
                    e.h = 320;
                    JarStore.write(LibraryActivity.this, e);
                    reload();
                    launch(e);
                }
            })
            .setNeutralButton("改分辨率", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    screenDialog(e);
                }
            })
            .setNegativeButton("稍后", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    reload();
                }
            })
            .show();
    }

    private void options(final JarStore.Entry e) {
        final String[] actions = { "运行", "修改分辨率", "修改内存", "改名", "按键布局", "删除" };
        new AlertDialog.Builder(this)
            .setTitle(e.title)
            .setItems(actions, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    switch (which) {
                        case 0: launch(e); break;
                        case 1: screenDialog(e); break;
                        case 2: heapDialog(e); break;
                        case 3: renameDialog(e); break;
                        case 4:
                            Intent it = new Intent(LibraryActivity.this, PadEditActivity.class);
                            it.putExtra("w", e.w);
                            it.putExtra("h", e.h);
                            startActivity(it);
                            break;
                        default: deleteDialog(e); break;
                    }
                }
            })
            .show();
    }

    private void screenDialog(final JarStore.Entry e) {
        final String[] labels = new String[JarStore.PRESETS.length + 1];
        for (int i = 0; i < JarStore.PRESETS.length; i++) {
            labels[i] = JarStore.PRESETS[i][0] + "x" + JarStore.PRESETS[i][1];
        }
        labels[JarStore.PRESETS.length] = "自定义…";
        new AlertDialog.Builder(this)
            .setTitle("画布大小（决定 MIDlet 看到的画面）")
            .setItems(labels, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    if (which < JarStore.PRESETS.length) {
                        e.w = JarStore.PRESETS[which][0];
                        e.h = JarStore.PRESETS[which][1];
                        JarStore.write(LibraryActivity.this, e);
                        reload();
                    } else {
                        customScreenDialog(e);
                    }
                }
            })
            .show();
    }

    private void customScreenDialog(final JarStore.Entry e) {
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setText(e.w + "x" + e.h);
        new AlertDialog.Builder(this)
            .setTitle("宽x高")
            .setView(input)
            .setPositiveButton("确定", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    String[] parts = input.getText().toString().trim().split("[xX*]");
                    try {
                        int nw = Integer.parseInt(parts[0].trim());
                        int nh = Integer.parseInt(parts[1].trim());
                        if (nw >= 32 && nh >= 32 && nw <= 4096 && nh <= 4096) {
                            e.w = nw;
                            e.h = nh;
                            JarStore.write(LibraryActivity.this, e);
                            reload();
                            return;
                        }
                    } catch (RuntimeException ignored) {
                        /* fall through to the toast */
                    }
                    toast("格式：宽x高，例如 176x208");
                }
            })
            .setNegativeButton("取消", null)
            .show();
    }

    private void heapDialog(final JarStore.Entry e) {
        final int[] sizes = { 4, 8, 16, 24, 32, 64 };
        String[] labels = new String[sizes.length];
        for (int i = 0; i < sizes.length; i++) {
            labels[i] = sizes[i] + " MB";
        }
        new AlertDialog.Builder(this)
            .setTitle("Java 堆大小（当前 " + e.heapMB + "MB）")
            .setItems(labels, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int which) {
                    e.heapMB = sizes[which];
                    JarStore.write(LibraryActivity.this, e);
                    reload();
                }
            })
            .show();
    }

    private void renameDialog(final JarStore.Entry e) {
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setText(e.title);
        new AlertDialog.Builder(this)
            .setTitle("显示名字")
            .setView(input)
            .setPositiveButton("确定", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    String t = input.getText().toString().trim();
                    if (t.length() > 0) {
                        e.title = t;
                        JarStore.write(LibraryActivity.this, e);
                        reload();
                    }
                }
            })
            .setNegativeButton("取消", null)
            .show();
    }

    private void deleteDialog(final JarStore.Entry e) {
        new AlertDialog.Builder(this)
            .setTitle("删除 " + e.title + "？")
            .setMessage("会一起删掉它的设置和存档目录名。")
            .setPositiveButton("删除", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    JarStore.delete(e);
                    reload();
                }
            })
            .setNegativeButton("取消", null)
            .show();
    }

    private TextView button(String label, View.OnClickListener click) {
        TextView t = tv(label, 13f, 0xFFFFFFFF, false);
        t.setPadding(dp(12), dp(8), dp(12), dp(8));
        StateListDrawable bg = new StateListDrawable();
        bg.addState(new int[] { android.R.attr.state_pressed },
                rect(null, null, rgb(23, 83, 200), dp(18)));
        bg.addState(new int[0], rect(null, null, rgb(47, 111, 237), dp(18)));  /* #2F6FED */
        t.setBackground(bg);
        t.setOnClickListener(click);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(8), 0, 0, 0);
        t.setLayoutParams(lp);
        return t;
    }

    /* ---------------------------------------------------------------- chrome */

    /** One line of text; every styled piece of this screen is built from it. */
    private TextView tv(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        if (bold) {
            t.setTypeface(t.getTypeface(), android.graphics.Typeface.BOLD);
        }
        return t;
    }

    /**
     * A rounded rectangle, or a rounded gradient when {@code colors} has two
     * entries.  {@code size} is the corner radius in pixels and {@code 0} means
     * square, which is what an unpadded row background wants.
     */
    private Drawable rect(int[] colors, GradientDrawable.Orientation dir, int color, int size) {
        GradientDrawable g = new GradientDrawable();
        if (colors != null && colors.length > 1) {
            g.setOrientation(dir);
            g.setColors(colors);
        } else {
            g.setColor(color);
        }
        g.setCornerRadius(size);
        return g;
    }

    private static int rgb(int r, int g, int b) {
        return Color.rgb(r, g, b);
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                getResources().getDisplayMetrics()));
    }
}
