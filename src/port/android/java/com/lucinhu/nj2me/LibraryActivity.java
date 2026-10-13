package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
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
    private List<JarStore.Entry> all = new ArrayList<JarStore.Entry>();
    private EntryAdapter adapter;
    private EditText searchBox;
    private String query = "";
    /* The library remembers the order the player likes (see sortDialog()); the
     * default is the one a launcher would use, most recently played first. */
    private int sortMode;
    /* The dialog whose rows are live right now, so a row can close it. */
    private AlertDialog openBox;
    private static final String[] SORT_NAMES =
            { "最近运行", "名称", "文件大小", "最近导入" };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        /*
         * A flat list, one add button, one menu.  This is the J2ME-Loader
         * shape - icon, name, a line of detail - because a launcher is a thing
         * you look at for two seconds: everything a particular game needs
         * (screen size, heap, its name) lives behind a long press on its row,
         * and the keypad layout behind the menu.  Fewer things on screen, and
         * the one thing you came for is bigger.
         */
        /* Material 3 starts here: the palette is read once, before any view is
         * built, because every colour below comes out of it. */
        Md3.init(this);
        sortMode = getSharedPreferences("lib", MODE_PRIVATE).getInt("sort", 0);
        /* The theme's window is black, which a light scheme would flash on the
         * way in: the window wears the palette from the first frame. */
        getWindow().setBackgroundDrawable(new ColorDrawable(Md3.surface));

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Md3.surface);

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);

        /* A small top app bar: flat on the surface, a 22sp title over a 12sp
         * subtitle, and one icon button.  No divider, no elevation - Material 3
         * separates by colour, not by a line. */
        final LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(16), dp(8), dp(4), dp(8));
        Md3.appBar(header);

        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        TextView title = tv("Nano J2ME", 22f, Md3.onSurface, false);
        Md3.type(title, 22f, Md3.onSurface, true);
        headerCount = tv("", 12f, Md3.onSurfaceVariant, false);
        titles.addView(title);
        titles.addView(headerCount);
        header.addView(titles, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        header.addView(menuButton());
        column.addView(header);

        searchBox = new EditText(this);
        searchBox.setHint("搜索游戏");
        searchBox.setSingleLine(true);
        searchBox.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH);
        Md3.type(searchBox, 14f, Md3.onSurface, false);
        Md3.field(searchBox, this);
        searchBox.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void onTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                query = s.toString().trim().toLowerCase();
                applyFilter();
            }
        });
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        slp.setMargins(dp(16), 0, dp(16), dp(8));
        column.addView(searchBox, slp);

        list = new ListView(this);
        /* Material 3 lists have no dividers - the rows sit on the surface and
         * the wash of a press is the only thing that moves. */
        list.setDivider(null);
        list.setDividerHeight(0);
        list.setSelector(new ColorDrawable(Color.TRANSPARENT));
        list.setPadding(0, dp(8), 0, dp(96));      /* keep the last row clear of the add button */
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
        column.addView(list, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        /* The list shows the empty view by itself; the scroll view is only here
         * so that the six lines of it fit on a short screen. */
        ScrollView emptyBox = new ScrollView(this);
        empty = tv("", 14f, Md3.onSurfaceVariant, false);
        empty.setGravity(Gravity.CENTER);
        empty.setLineSpacing(dp(6), 1f);
        empty.setPadding(dp(24), dp(24), dp(24), dp(24));
        emptyBox.addView(empty, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        emptyBox.setVisibility(View.GONE);
        column.addView(emptyBox, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        list.setEmptyView(emptyBox);

        root.addView(column, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        final TextView fab = addButton();
        root.addView(fab, fabParams());

        setContentView(root);
        /*
         * The app bar colour runs under the status bar and the surface under
         * the navigation bar, so neither is the black stripe the theme would
         * otherwise leave there.  The bars stay visible; only the window
         * behind them becomes ours.
         */
        Ui.drawBehindBars(this, Md3.lightSurface());
        /*
         * A notch is a hole the framework cannot move out of the way, and the
         * status and navigation bars are now covered by this screen's own
         * colours: the title, the add button and the last row are padded past
         * all of them.  This screen keeps the bars themselves - it is a list,
         * and a list is not a game - so `true` here means "the window really
         * does reach under them", which is what drawBehindBars just set up.
         */
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                int[] e = Ui.insets(in, true);
                header.setPadding(dp(16) + e[0], dp(8) + e[1], dp(4) + e[2], dp(8));
                FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) fab.getLayoutParams();
                lp.setMargins(0, 0, dp(16) + e[2], dp(16) + e[3]);
                fab.setLayoutParams(lp);
                list.setPadding(0, dp(8), 0, dp(96) + e[3]);
                return in;
            }
        });
        root.requestApplyInsets();

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
        all = JarStore.list(this);
        applyFilter();
        if (all.isEmpty()) {
            empty.setText("还没有游戏\n\n"
                    + "点右下角的「+」从文件管理器选一个\n"
                    + "也可以直接在其他文件管理器里点开 .jar\n\n"
                    + "用电脑推送：\n"
                    + "adb push 游戏.jar " + JarStore.dir(this).getAbsolutePath() + "/");
        }
    }

    /*
     * Everything the list shows is decided here: the filter first, then the one
     * order the player picked.  Keeping the whole list in `all` means a search
     * never needs the disk, and clearing it is instant.
     */
    private void applyFilter() {
        List<JarStore.Entry> shown = new ArrayList<JarStore.Entry>();
        for (JarStore.Entry e : all) {
            if (query.length() == 0 || matches(e)) {
                shown.add(e);
            }
        }
        sort(shown);
        items = shown;
        adapter.notifyDataSetChanged();
        if (query.length() == 0) {
            headerCount.setText("J2ME 运行时 · " + all.size() + " 个游戏");
        } else {
            headerCount.setText("找到 " + shown.size() + " / " + all.size() + " 个游戏");
        }
    }

    private boolean matches(JarStore.Entry e) {
        String t = e.title == null ? "" : e.title.toLowerCase();
        String f = e.jar.getName().toLowerCase();
        return t.contains(query) || f.contains(query);
    }

    private void sort(List<JarStore.Entry> l) {
        final int mode = sortMode;
        Collections.sort(l, new Comparator<JarStore.Entry>() {
            @Override
            public int compare(JarStore.Entry a, JarStore.Entry b) {
                if (mode == 1) {
                    return a.title.compareToIgnoreCase(b.title);
                }
                if (mode == 2) {
                    /* biggest first: the ones that hog the storage are the ones
                     * a player is looking for when they sort by size */
                    return b.size > a.size ? 1 : (b.size < a.size ? -1 : 0);
                }
                if (mode == 3) {
                    long x = b.jar.lastModified();
                    long y = a.jar.lastModified();
                    return x > y ? 1 : (x < y ? -1 : 0);
                }
                long x = b.touched;
                long y = a.touched;
                return x > y ? 1 : (x < y ? -1 : 0);
            }
        });
    }

    private void sortDialog() {
        Md3.chooser(this, Md3.IC_SORT, "排序方式",
                "列表里 " + items.size() + " 个游戏的顺序"
                        + (all.size() != items.size() ? "（一共有 " + all.size() + " 个）" : ""),
                SORT_NAMES, null, sortMode, new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        sortMode = which;
                        getSharedPreferences("lib", MODE_PRIVATE).edit()
                                .putInt("sort", sortMode).apply();
                        applyFilter();
                    }
                });
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
        /* No container: a Material 3 list row is the surface itself, and the
         * press is a 12% wash of the content colour over it. */
        row.setBackground(Md3.row(this));
        row.setPadding(dp(16), dp(12), dp(16), dp(12));
        row.setMinimumHeight(dp(72));

        ImageView icon = new ImageView(this);
        row.addView(icon, new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        tp.leftMargin = dp(16);
        row.addView(text, tp);

        TextView name = tv("", 16f, Md3.onSurface, false);
        Md3.type(name, 16f, Md3.onSurface, true);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        TextView sub = tv("", 12f, Md3.onSurfaceVariant, false);
        sub.setSingleLine(true);
        sub.setEllipsize(TextUtils.TruncateAt.END);
        text.addView(name);
        text.addView(sub);

        row.setTag("card");
        /* bindRow() finds these two by position, so their order is the layout. */
        return row;
    }

    private void bindRow(ViewGroup row, JarStore.Entry e) {
        ((ImageView) row.getChildAt(0)).setImageBitmap(JarIcons.forEntry(e, dp(44)));

        LinearLayout text = (LinearLayout) row.getChildAt(1);
        /* The MIDlet's own name, or - for a jar that declares none - the file's.
         * JarStore.read() already decided this once and cached it. */
        ((TextView) text.getChildAt(0)).setText(e.title);

        /*
         * One line for everything else, in the order the player cares about:
         * the size the game will be asked to paint (or, once it has run, the
         * size it actually painted), the file name and how big the file is.
         */
        String det = e.detText();
        String size = (det != null) ? det + " · " : e.w + "x" + e.h + " · ";
        ((TextView) text.getChildAt(1)).setText(size + e.jar.getName() + " · " + e.sizeText());
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
        launch(e, false);
    }

    /**
     * @param restore true to hand the game its saved snapshot: the port loads it
     *                before the MIDlet starts, so the game resumes where the
     *                player left it instead of at the title screen.
     */
    private void launch(JarStore.Entry e, boolean restore) {
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
        if (restore) {
            it.putExtra("state",
                    JarStore.stateFile(this, e.jar.getName()).getAbsolutePath());
        }
        startActivity(it);
    }

    /** After importing, ask the two questions that decide whether it will run. */
    private void offload(final JarStore.Entry e) {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, "导入成功"));
        box.addView(Md3.supporting(this, e.title + "　" + e.jar.getName()
                + "\n它在手机上应该占多大一块屏幕？"), Md3.gap(this, 6));
        box.addView(rowAction(Md3.IC_PLAY, "用 240x320 开始", null, new Runnable() {
            @Override
            public void run() {
                e.w = 240;
                e.h = 320;
                JarStore.write(LibraryActivity.this, e);
                reload();
                launch(e);
            }
        }), Md3.gap(this, 12));
        box.addView(rowAction(Md3.IC_SCREEN, "挑一个分辨率",
                "2005 年前后的游戏多是 176x208 或 240x320", new Runnable() {
                    @Override
                    public void run() {
                        screenDialog(e);
                    }
                }));
        box.addView(rowAction(Md3.IC_TUNE, "先摆一摆按键",
                "把按键放到顺手的位置，再开始游戏", new Runnable() {
                    @Override
                    public void run() {
                        startEditor(e);
                    }
                }));
        showBox(box, "稍后");
    }


    /** Everything that can be done to one game, in one card. */
    private void options(final JarStore.Entry e) {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, e.title));
        box.addView(Md3.supporting(this, (e.detText() != null ? e.detText() : e.w + "x" + e.h)
                + " · " + e.sizeText() + "\n" + e.jar.getName()), Md3.gap(this, 6));
        box.addView(rowAction(Md3.IC_PLAY, "开始游戏", null, new Runnable() {
            @Override
            public void run() {
                launch(e);
            }
        }), Md3.gap(this, 12));
        if (JarStore.hasState(this, e.jar.getName())) {
            /* Only offered when there is something to go back to: a row that
             * starts the game at the title screen while calling itself "resume"
             * is worse than no row at all. */
            box.addView(rowAction(Md3.IC_PLAY, "恢复进度",
                    "接着上次保存的地方继续玩", new Runnable() {
                        @Override
                        public void run() {
                            launch(e, true);
                        }
                    }));
        }
        box.addView(Md3.divider(this));
        box.addView(rowAction(Md3.IC_SCREEN, "画布大小", e.w + "x" + e.h, new Runnable() {
            @Override
            public void run() {
                screenDialog(e);
            }
        }));
        box.addView(rowAction(Md3.IC_MEMORY, "Java 堆", e.heapMB + " MB", new Runnable() {
            @Override
            public void run() {
                heapDialog(e);
            }
        }));
        box.addView(rowAction(Md3.IC_RENAME, "显示名字",
                e.titleSet ? "你自己改的名字" : "jar 里写的游戏名", new Runnable() {
                    @Override
                    public void run() {
                        renameDialog(e);
                    }
                }));
        box.addView(rowAction(Md3.IC_TUNE, "调整画面和按键",
                "拖动画面、摆放按键，都在这一页", new Runnable() {
                    @Override
                    public void run() {
                        startEditor(e);
                    }
                }));
        box.addView(rowAction(Md3.IC_IMPORT, "联网",
                e.net != 0 ? "允许（默认）" : "已禁止", new Runnable() {
                    @Override
                    public void run() {
                        netDialog(e);
                    }
                }));
        box.addView(rowAction(Md3.IC_INFO, "传感器", sensorHint(e.sensor), new Runnable() {
                    @Override
                    public void run() {
                        sensorDialog(e);
                    }
                }));
        if (JarStore.hasState(this, e.jar.getName())) {
            box.addView(dangerAction(Md3.IC_DELETE, "删除存档", new Runnable() {
                @Override
                public void run() {
                    JarStore.deleteState(LibraryActivity.this, e.jar.getName());
                    toast("已删除存档");
                    options(e);
                }
            }));
        }
        box.addView(Md3.divider(this));
        box.addView(Md3.dangerRow(this, Md3.IC_DELETE, "删除", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (openBox != null) {
                    openBox.dismiss();
                }
                deleteDialog(e);
            }
        }));
        showBox(box, "关闭");
    }


    /** The sensor setting in one line: what the game will find. */
    private static String sensorHint(int mode) {
        switch (mode) {
            case 1:
                return "模拟：平放";
            case 2:
                return "模拟：左倾";
            case 3:
                return "模拟：右倾";
            case 4:
                return "关闭（游戏看不到传感器）";
            default:
                return "用手机的传感器";
        }
    }

    /**
     * The per-game network switch.
     *
     * <p>Some MIDlets open a connection in startApp() and wait for it; on a
     * phone with no data that is a splash screen that never ends.  A player who
     * knows the game is single-player can say so here, and every connection it
     * opens then fails at once, which is the answer the game is already written
     * to handle.
     */
    private void netDialog(final JarStore.Entry e) {
        String[] labels = { "允许联网（默认）", "禁止联网" };
        String[] hints = { "游戏可以像在手机上一样上网",
                           "连接直接失败，游戏走它自己的断网提示" };
        Md3.chooser(this, Md3.IC_IMPORT, "联网", e.title, labels, hints,
                e.net != 0 ? 0 : 1, new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        e.net = (which == 0) ? 1 : 0;
                        JarStore.write(LibraryActivity.this, e);
                        toast(e.net != 0 ? "已允许联网（下次启动生效）"
                                         : "已禁止联网（下次启动生效）");
                    }
                });
    }

    /**
     * The per-game sensor switch: the hardware, one simulated accelerometer, or
     * nothing at all.
     *
     * <p>JSR 256 games fall into two groups and neither is served by the plain
     * hardware answer: one needs an accelerometer that a phone without one does
     * not have, the other steers by tilt and needs one that holds still while
     * the player uses the keys.
     */
    private void sensorDialog(final JarStore.Entry e) {
        String[] labels = { "用手机的传感器（默认）", "模拟：平放", "模拟：左倾",
                            "模拟：右倾", "关闭传感器" };
        String[] hints = {
                "有就真的读，没有就没有",
                "提供一个加速度计，一直朝上",
                "提供一个加速度计，向左倒",
                "提供一个加速度计，向右倒",
                "游戏看不到任何传感器" };
        int checked = (e.sensor >= 0 && e.sensor <= 4) ? e.sensor : 0;
        Md3.chooser(this, Md3.IC_INFO, "传感器", e.title, labels, hints, checked,
                new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        e.sensor = which;
                        JarStore.write(LibraryActivity.this, e);
                        toast("传感器：" + sensorHint(which) + "（下次启动生效）");
                    }
                });
    }

    private void screenDialog(final JarStore.Entry e) {
        final int[] notes = { 0, 0, 1, 1, 2, 3, 3, 4, 4 };
        String[] labels = new String[JarStore.PRESETS.length + 1];
        String[] hints = new String[JarStore.PRESETS.length + 1];
        int checked = -1;
        for (int i = 0; i < JarStore.PRESETS.length; i++) {
            int w = JarStore.PRESETS[i][0];
            int h = JarStore.PRESETS[i][1];
            labels[i] = w + "x" + h;
            hints[i] = SCREEN_NOTES[notes[i]];
            if (e.w == w && e.h == h) {
                checked = i;
            }
        }
        labels[JarStore.PRESETS.length] = "自定义…";
        hints[JarStore.PRESETS.length] = "自己填宽和高（32 ~ 4096）";
        Md3.chooser(this, Md3.IC_SCREEN, "画布大小",
                "MIDlet 看到的就是这么大的一块画布（现在是 " + e.w + "x" + e.h + "）",
                labels, hints, checked, new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        if (which < JarStore.PRESETS.length) {
                            e.w = JarStore.PRESETS[which][0];
                            e.h = JarStore.PRESETS[which][1];
                            JarStore.write(LibraryActivity.this, e);
                            reload();
                        } else {
                            customScreenDialog(e);
                        }
                    }
                });
    }

    private static final String[] SCREEN_NOTES = {
        "很小的方块屏",
        "早期翻盖机",
        "S60 一代，很常见",
        "S60 二代，很常见",
        "方块屏（诺基亚 5500 一类）",
        "QVGA 竖屏，最常见",
        "QVGA 横屏",
        "后来的大屏竖屏",
        "更晚的大屏",
    };


    private void customScreenDialog(final JarStore.Entry e) {
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setText(e.w + "x" + e.h);
        Md3.field(input, this);
        LinearLayout box = Md3.body(this);
        box.addView(Md3.titleWith(this, Md3.IC_SCREEN, "自定义画布"));
        box.addView(Md3.supporting(this, "格式：宽x高，例如 176x208（32 ~ 4096）。"
                + "MIDlet 会按这个大小排版，游戏画错位时再调。"), Md3.gap(this, 6));
        box.addView(input, Md3.gap(this, 16));
        Md3.show(new AlertDialog.Builder(this)
            .setView(Md3.scroller(this, box))
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
            , this);
    }


    private void heapDialog(final JarStore.Entry e) {
        final int[] sizes = { 4, 8, 16, 24, 32, 64 };
        String[] labels = new String[sizes.length];
        String[] hints = new String[sizes.length];
        int checked = -1;
        for (int i = 0; i < sizes.length; i++) {
            labels[i] = sizes[i] + " MB";
            hints[i] = (i == 0) ? "只够最小的游戏"
                    : (i == sizes.length - 1 ? "除非游戏特别大，否则用不上" : null);
            if (e.heapMB == sizes[i]) {
                checked = i;
            }
        }
        Md3.chooser(this, Md3.IC_MEMORY, "Java 堆大小",
                "MIDlet 能用多少内存（现在是 " + e.heapMB + "MB）。"
                        + "游戏报 OutOfMemory 就调大一点。",
                labels, hints, checked, new Md3.Choice() {
                    @Override
                    public void picked(int which, AlertDialog d) {
                        e.heapMB = sizes[which];
                        JarStore.write(LibraryActivity.this, e);
                        reload();
                    }
                });
    }


    private void renameDialog(final JarStore.Entry e) {
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setText(e.title);
        Md3.field(input, this);
        LinearLayout box = Md3.body(this);
        box.addView(Md3.titleWith(this, Md3.IC_RENAME, "显示名字"));
        box.addView(Md3.supporting(this, "只改列表和工具栏上显示的名字，不动 jar 文件。"
                + "「用游戏名」可以改回 jar 里写的名字。"), Md3.gap(this, 6));
        box.addView(input, Md3.gap(this, 16));
        Md3.show(new AlertDialog.Builder(this)
            .setView(Md3.scroller(this, box))
            .setPositiveButton("保存", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    String t = input.getText().toString().trim();
                    if (t.length() > 0) {
                        e.title = t;
                        e.titleSet = true;
                        JarStore.write(LibraryActivity.this, e);
                        reload();
                    }
                }
            })
            /* The name in the jar is what the game calls itself; a player who
             * renamed a game can get back to it without re-importing. */
            .setNeutralButton("用游戏名", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    e.titleSet = false;
                    JarStore.Entry fresh = JarStore.read(LibraryActivity.this, e.jar);
                    e.title = fresh.title;
                    JarStore.write(LibraryActivity.this, e);
                    reload();
                    toast("已改回 " + e.title);
                }
            })
            .setNegativeButton("取消", null)
            , this);
    }


    private void deleteDialog(final JarStore.Entry e) {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, "删除 " + e.title + "？"));
        box.addView(Md3.supporting(this, "会删掉这个 jar，以及它的设置和存档目录名。"
                + "这一步不能撤销。"), Md3.gap(this, 6));
        AlertDialog d = Md3.show(new AlertDialog.Builder(this)
            .setView(Md3.scroller(this, box))
            .setPositiveButton("删除", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    JarStore.delete(e);
                    reload();
                }
            })
            .setNegativeButton("取消", null)
            , this);
        Md3.errorButton(d, DialogInterface.BUTTON_POSITIVE);
    }


    /** The overflow: keypad layout, add a jar, and how to get jars in. */
    private TextView menuButton() {
        TextView t = tv("⋮", 22f, Md3.onSurfaceVariant, false);
        Md3.iconButton(t, this);
        t.setLayoutParams(new LinearLayout.LayoutParams(dp(48), dp(48)));
        t.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                menu();
            }
        });
        return t;
    }

    /** The overflow: keypad layout, sorting, adding jars, and the help card. */
    private void menu() {
        final boolean immersive = Ui.immersive(this);
        LinearLayout box = Md3.body(this);
        box.addView(Md3.title(this, "Nano J2ME"));
        box.addView(Md3.supporting(this, "已导入 " + all.size() + " 个游戏"
                + (query.length() > 0 ? "，筛出 " + items.size() + " 个" : "")), Md3.gap(this, 6));
        box.addView(rowAction(Md3.IC_KEYS, "按键布局",
                "默认布局，所有游戏共用", new Runnable() {
                    @Override
                    public void run() {
                        startActivity(new Intent(LibraryActivity.this, PadEditActivity.class));
                    }
                }), Md3.gap(this, 12));
        box.addView(rowAction(Md3.IC_FULLSCREEN, immersive ? "沉浸模式：开" : "沉浸模式：关",
                immersive ? "游戏里隐藏状态栏和导航栏" : "游戏里显示状态栏", new Runnable() {
                    @Override
                    public void run() {
                        Ui.setImmersive(LibraryActivity.this, !immersive);
                        toast(!immersive
                                ? "沉浸模式已打开：游戏里隐藏状态栏和导航栏"
                                : "沉浸模式已关闭：游戏里显示状态栏");
                    }
                }));
        box.addView(rowAction(Md3.IC_SORT, "排序方式", SORT_NAMES[sortMode], new Runnable() {
            @Override
            public void run() {
                sortDialog();
            }
        }));
        box.addView(Md3.divider(this));
        box.addView(rowAction(Md3.IC_IMPORT, "添加 JAR", "从文件管理器里挑一个 .jar", new Runnable() {
            @Override
            public void run() {
                pickJar();
            }
        }));
        box.addView(rowAction(Md3.IC_HELP, "怎么放游戏进来", null, new Runnable() {
            @Override
            public void run() {
                help();
            }
        }));
        showBox(box, "关闭");
    }


    private void help() {
        LinearLayout box = Md3.body(this);
        box.addView(Md3.titleWith(this, Md3.IC_HELP, "怎么放游戏进来"));
        box.addView(Md3.supporting(this, "三种方式，挑一种就行。"), Md3.gap(this, 6));
        box.addView(Md3.section(this, "一 · 直接添加",
                "点右下角的「+」，在文件管理器里选一个 .jar。"), Md3.gap(this, 16));
        box.addView(Md3.section(this, "二 · 从文件管理器打开",
                "在任意文件管理器里点开 .jar，选「Nano J2ME」。"
                        + "之后这个后缀就默认用它打开。"), Md3.gap(this, 16));
        box.addView(Md3.section(this, "三 · 用电脑推送",
                "手机连上电脑，在电脑上执行（点一下可以复制）："), Md3.gap(this, 16));
        box.addView(Md3.command(this, "adb push 游戏.jar "
                + JarStore.dir(this).getAbsolutePath() + "/"), Md3.gap(this, 8));
        box.addView(Md3.section(this, "提示",
                "长按列表里的游戏可以改分辨率、内存、名字、按键；"
                        + "点右上角的 ⋮ 可以排序、切换沉浸模式。"), Md3.gap(this, 16));
        Md3.show(new AlertDialog.Builder(this)
                .setView(Md3.scroller(this, box))
                .setPositiveButton("知道了", null)
                , this);
    }


    /**
     * Shows a dialog whose body was built with the Md3 anatomy.  The rows of
     * such a body act through {@link #rowAction}, which closes this dialog
     * first, so an action that opens another dialog never stacks two of them.
     */
    private AlertDialog showBox(LinearLayout box, String negative) {
        openBox = Md3.show(new AlertDialog.Builder(this)
                .setView(Md3.scroller(this, box))
                .setNegativeButton(negative, null), this);
        return openBox;
    }

    /** A 56dp dialog row that closes the open dialog and then does its work. */
    private LinearLayout rowAction(int icon, String label, String hint, final Runnable action) {
        return Md3.row(this, icon, label, hint, false, new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (openBox != null) {
                    openBox.dismiss();
                }
                action.run();
            }
        });
    }

    /** Like rowAction(), in the error colour - for the things that destroy. */
    private LinearLayout dangerAction(int icon, String label, final Runnable action) {
        return Md3.dangerRow(this, icon, label, new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (openBox != null) {
                    openBox.dismiss();
                }
                action.run();
            }
        });
    }

    /** Opens the one screen where the picture and the keypad are placed. */
    private void startEditor(JarStore.Entry e) {
        Intent it = new Intent(this, PadEditActivity.class);
        it.putExtra("jar", e.jar.getName());
        it.putExtra("w", e.w);
        it.putExtra("h", e.h);
        startActivity(it);
    }


    /** The one action that always makes sense: bring a game in. */
    private TextView addButton() {
        TextView t = tv("+", 24f, Md3.onPrimaryContainer, false);
        Md3.fab(t, this, 56f, Md3.SHAPE_L);
        t.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickJar();
            }
        });
        return t;
    }

    private FrameLayout.LayoutParams fabParams() {
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp(56), dp(56));
        lp.gravity = Gravity.BOTTOM | Gravity.END;
        lp.setMargins(0, 0, dp(16), dp(16));
        return lp;
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

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                getResources().getDisplayMetrics()));
    }
}
