package com.lucinhu.nj2me;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Environment;
import android.net.Uri;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Locale;
import java.lang.ref.WeakReference;

/** API5 launcher with its own file browser; no installed file manager is required. */
public final class LibraryActivity extends Activity {
    private final ArrayList<File> jars = new ArrayList<File>();
    private ArrayAdapter<String> adapter;
    private TextView info;
    private boolean launching, resumed, destroyed;
    private static final int PICK_JAR = 1;
    private AlertDialog dialog;
    private ProgressDialog progress;
    private File browserDirectory;
    private ImportTask importing;

    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        setTitle(R.string.app_name);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        info = new TextView(this);
        info.setPadding(8, 8, 8, 8);
        root.addView(info);
        LinearLayout actions = new LinearLayout(this);
        Button refresh = new Button(this);
        refresh.setText("刷新");
        refresh.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) { refresh(); }
        });
        actions.addView(refresh, new LinearLayout.LayoutParams(0, -2, 1));
        Button add = new Button(this);
        add.setText("导入 JAR");
        add.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) { showImportOptions(); }
        });
        actions.addView(add, new LinearLayout.LayoutParams(0, -2, 1));
        Button settings = new Button(this);
        settings.setText("设置");
        settings.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) { showSettings(); }
        });
        actions.addView(settings, new LinearLayout.LayoutParams(0, -2, 1));
        root.addView(actions);
        ListView list = new ListView(this);
        adapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1);
        list.setAdapter(adapter);
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                choose(jars.get(position));
            }
        });
        root.addView(list, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        Object retained = getLastNonConfigurationInstance();
        if (retained instanceof ImportTask) {
            importing = (ImportTask)retained;
            importing.attach(this);
        }
        if (saved != null) {
            String path = saved.getString("browserDirectory");
            if (path != null) { browserDirectory = new File(path); }
            if (importing == null && saved.getBoolean("importPending")) {
                toast("上次导入被系统中断，请重新选择文件；已完成的文件会保留在库中。");
            }
        } else { consumeIntent(getIntent()); }
    }

    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        consumeIntent(intent);
    }

    protected void onResume() {
        super.onResume();
        resumed = true;
        launching = false;
        refresh();
        if (importing != null) {
            showProgress();
            deliverImport(importing);
        }
    }

    protected void onPause() {
        resumed = false;
        super.onPause();
    }

    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("importPending", importing != null);
        if (browserDirectory != null) { state.putString("browserDirectory", browserDirectory.getAbsolutePath()); }
    }

    public Object onRetainNonConfigurationInstance() { return importing; }

    protected void onDestroy() {
        destroyed = true;
        if (importing != null) { importing.detach(this); }
        if (dialog != null) { dialog.dismiss(); dialog = null; }
        hideProgress();
        super.onDestroy();
    }

    private void refresh() {
        jars.clear();
        adapter.clear();
        try {
            jars.addAll(JarFiles.list(this));
            for (int i = 0; i < jars.size(); i++) {
                File jar = jars.get(i);
                adapter.add(jar.getName() + "\n" + jar.getParent());
            }
            info.setText("将 .jar 放入以下任一目录：\n"
                    + JarFiles.externalJars().getAbsolutePath() + "\n"
                    + JarFiles.privateJars(this).getAbsolutePath()
                    + "\n也可点“导入 JAR”浏览文件或输入路径，导入后启动。"
                    + "\n存档和 RMS 保存在应用私有目录。"
                    + (jars.isEmpty() ? "\n尚未找到可读的 JAR。" : ""));
        } catch (IOException e) {
            info.setText(e.getMessage());
        }
        adapter.notifyDataSetChanged();
    }

    private void choose(final File jar) {
        if (launching || importing != null) { return; }
        if (GameActivity.hasRunningGame()) {
            toast("文件已在游戏库中。请先退出当前游戏，再点选启动。");
            return;
        }
        try {
            if (!JarFiles.state(this, jar).isFile()) { launch(jar, false); return; }
            if (dialog != null) { dialog.dismiss(); }
            dialog = new AlertDialog.Builder(this).setTitle(jar.getName())
                .setItems(new String[]{"继续即时存档", "重新启动"},
                    new DialogInterface.OnClickListener() {
                        public void onClick(DialogInterface dialog, int which) {
                            launch(jar, which == 0);
                        }
                    }).setNegativeButton("取消", null).create();
            dialog.show();
        } catch (IOException e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void launch(File jar, boolean restore) {
        if (launching || importing != null) { return; }
        if (GameActivity.hasRunningGame()) { toast("请先退出当前游戏，再点选启动。"); return; }
        launching = true;
        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("jarpath", jar.getAbsolutePath());
        intent.putExtra("restore", restore);
        startActivity(intent);
    }

    private void toast(String message) {
        Toast.makeText(this, message == null ? "无法读取文件。" : message, Toast.LENGTH_LONG).show();
    }

    private void consumeIntent(Intent intent) {
        if (intent == null) { return; }
        String action = intent.getAction();
        if (!Intent.ACTION_VIEW.equals(action) && !Intent.ACTION_SEND.equals(action)) { return; }
        // Do not keep the URI in our launch intent or replay it after recreation.
        setIntent(new Intent(this, LibraryActivity.class).setAction(Intent.ACTION_MAIN));
        Uri source = null;
        try {
            if (Intent.ACTION_VIEW.equals(action)) { source = intent.getData(); }
            else {
                Object stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (stream instanceof Uri) { source = (Uri)stream; }
            }
        } catch (RuntimeException e) { toast("外部应用提供了无效的文件地址。"); return; }
        if (source == null) { toast("外部应用未提供文件地址。"); return; }
        startImport(source);
    }

    private void showImportOptions() {
        if (importing != null) { showProgress(); return; }
        if (dialog != null) { dialog.dismiss(); }
        dialog = new AlertDialog.Builder(this).setTitle("导入后启动 JAR")
            .setItems(new String[]{"浏览文件与目录", "输入文件或目录路径", "使用系统文件选择器"},
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface selected, int which) {
                        if (which == 0) { showBrowser(initialDirectory()); }
                        else if (which == 1) { showPathEntry(); }
                        else { systemPicker(); }
                    }
                }).setNegativeButton("取消", null).create();
        dialog.show();
    }

    private File initialDirectory() {
        if (browserDirectory != null && browserDirectory.isDirectory()) { return browserDirectory; }
        File directory = JarFiles.externalJars();
        if (!directory.isDirectory()) { directory = Environment.getExternalStorageDirectory(); }
        return directory.isDirectory() ? directory : new File("/");
    }

    private void showBrowser(File directory) {
        try { directory = directory.getCanonicalFile(); }
        catch (IOException ignored) { directory = directory.getAbsoluteFile(); }
        browserDirectory = directory;
        final ArrayList<File> entries = new ArrayList<File>();
        File[] children = directory.listFiles();
        if (children != null) {
            for (File file : children) {
                if (file.isDirectory() || file.isFile() && file.canRead()
                        && file.getName().toLowerCase(Locale.US).endsWith(".jar")) { entries.add(file); }
            }
        }
        Collections.sort(entries, new Comparator<File>() {
            public int compare(File left, File right) {
                if (left.isDirectory() != right.isDirectory()) { return left.isDirectory() ? -1 : 1; }
                return left.getName().compareToIgnoreCase(right.getName());
            }
        });
        File parent = directory.getParentFile();
        if (parent != null) { entries.add(0, parent); }
        final boolean hasParent = parent != null;
        String[] names = new String[entries.size()];
        for (int i = 0; i < names.length; i++) {
            File file = entries.get(i);
            names[i] = hasParent && i == 0 ? "[.. 上级目录]"
                    : file.isDirectory() ? "[" + file.getName() + "]" : file.getName();
        }
        if (children == null) { toast("无法读取此目录；可返回上级或输入其他路径。"); }
        if (dialog != null) { dialog.dismiss(); }
        dialog = new AlertDialog.Builder(this).setTitle(directory.getAbsolutePath())
            .setItems(names, new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface selected, int which) {
                    File file = entries.get(which);
                    if (file.isDirectory()) { showBrowser(file); }
                    else { startImport(Uri.fromFile(file)); }
                }
            }).setNeutralButton("输入路径", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface selected, int which) { showPathEntry(); }
            }).setNegativeButton("取消", null).create();
        dialog.show();
    }

    private void showPathEntry() {
        final EditText path = new EditText(this);
        path.setSingleLine(true);
        path.setText(initialDirectory().getAbsolutePath());
        path.setSelectAllOnFocus(true);
        if (dialog != null) { dialog.dismiss(); }
        dialog = new AlertDialog.Builder(this).setTitle("输入 JAR 或目录的完整路径")
            .setView(path).setPositiveButton("打开", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface selected, int which) {
                    String text = path.getText().toString().trim();
                    if (text.length() == 0) { toast("请输入路径。"); return; }
                    if (text.startsWith("content://") || text.startsWith("file://")) {
                        startImport(Uri.parse(text));
                        return;
                    }
                    File file = new File(text);
                    if (file.isDirectory()) { showBrowser(file); }
                    else { startImport(Uri.fromFile(file)); }
                }
            }).setNegativeButton("取消", null).create();
        dialog.show();
    }

    private void systemPicker() {
        Intent pick = new Intent(Intent.ACTION_GET_CONTENT);
        pick.setType("*/*");
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        try { startActivityForResult(pick, PICK_JAR); }
        catch (ActivityNotFoundException e) {
            toast("未安装文件选择器，使用内置浏览器。");
            showBrowser(initialDirectory());
        } catch (SecurityException e) {
            toast("无法使用系统文件选择器，使用内置浏览器。");
            showBrowser(initialDirectory());
        }
    }

    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_JAR && result == RESULT_OK) {
            if (data == null || data.getData() == null) { toast("文件选择器未返回文件地址。"); }
            else { startImport(data.getData()); }
        }
    }

    private void startImport(Uri source) {
        if (importing != null) { toast("正在导入文件，请完成后重新打开下一个文件。"); return; }
        if (dialog != null) { dialog.dismiss(); dialog = null; }
        importing = new ImportTask(getApplicationContext(), source);
        importing.attach(this);
        showProgress();
        new Thread(importing, "nj2me-import").start();
    }

    private void showProgress() {
        if (progress != null || destroyed || isFinishing()) { return; }
        progress = new ProgressDialog(this);
        progress.setTitle("导入 JAR");
        progress.setMessage("正在复制并校验，请稍候…");
        progress.setIndeterminate(true);
        progress.setCancelable(false);
        progress.show();
    }

    private void hideProgress() {
        if (progress != null) { progress.dismiss(); progress = null; }
    }

    private void deliverImport(ImportTask task) {
        if (destroyed || isFinishing() || !resumed || task != importing || !task.done) { return; }
        importing = null;
        task.detach(this);
        hideProgress();
        refresh();
        if (task.error != null) { toast("导入失败：" + task.error); }
        else { toast("JAR 已就绪：" + task.result.getName()); choose(task.result); }
    }

    /** Retained across configuration changes; it holds only application context and a weak UI reference. */
    private static final class ImportTask implements Runnable {
        private final Context context;
        private final Uri source;
        private volatile WeakReference<LibraryActivity> owner = new WeakReference<LibraryActivity>(null);
        volatile boolean done;
        File result;
        String error;

        ImportTask(Context app, Uri uri) { context = app; source = uri; }
        void attach(LibraryActivity activity) { owner = new WeakReference<LibraryActivity>(activity); }
        void detach(LibraryActivity activity) {
            if (owner.get() == activity) { owner = new WeakReference<LibraryActivity>(null); }
        }

        public void run() {
            try { result = JarFiles.importJar(context, source); }
            catch (IOException e) { error = e.getMessage() == null ? e.toString() : e.getMessage(); }
            catch (RuntimeException e) { error = e.toString(); }
            catch (OutOfMemoryError e) { error = "内存不足，无法校验该 JAR。"; }
            done = true;
            final LibraryActivity activity = owner.get();
            final ImportTask task = this;
            if (activity != null) {
                activity.runOnUiThread(new Runnable() {
                    public void run() { activity.deliverImport(task); }
                });
            }
        }
    }

    private void label(LinearLayout parent, String text) {
        TextView label = new TextView(this);
        label.setText(text);
        parent.addView(label);
    }

    private void showSettings() {
        final SharedPreferences prefs = JarFiles.settings(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(12, 8, 12, 8);
        label(content, "游戏屏幕（下次启动生效）");
        final Spinner size = new Spinner(this);
        String[] names = new String[JarFiles.SIZES.length];
        for (int i = 0; i < names.length; i++) {
            names[i] = JarFiles.SIZES[i][0] + " × " + JarFiles.SIZES[i][1];
        }
        ArrayAdapter<String> sizeAdapter = new ArrayAdapter<String>(this,
                android.R.layout.simple_spinner_item, names);
        sizeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        size.setAdapter(sizeAdapter);
        size.setSelection(JarFiles.sizeIndex(prefs));
        content.addView(size);
        label(content, "J2ME 堆内存（低内存手机建议 8 / 16 MB）");
        final Spinner heap = new Spinner(this);
        ArrayAdapter<String> heapAdapter = new ArrayAdapter<String>(this,
                android.R.layout.simple_spinner_item, new String[]{"4 MB", "8 MB", "16 MB", "32 MB"});
        heapAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        heap.setAdapter(heapAdapter);
        heap.setSelection(JarFiles.heapIndex(prefs));
        content.addView(heap);
        final CheckBox keypad = new CheckBox(this);
        keypad.setText("显示触摸键盘");
        keypad.setChecked(prefs.getBoolean("keypad", true));
        content.addView(keypad);
        final CheckBox sound = new CheckBox(this);
        sound.setText("声音（PCM16 / 44100Hz）");
        sound.setChecked(prefs.getBoolean("sound", true));
        content.addView(sound);
        label(content, "实体方向键/数字键可用；Back 为游戏右软键，Menu 打开控制菜单。\n"
                + "此精简版不提供平台 MP3/AMR/AAC、网络、蓝牙及传感器桥接；"
                + "音频依赖核心自带解码/合成。即时存档失败时请使用游戏内 RMS 存档。\n"
                + "旧版与现代版使用同一包名，安装时互相替换。");
        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        if (dialog != null) { dialog.dismiss(); }
        dialog = new AlertDialog.Builder(this).setTitle("设置").setView(scroll)
            .setPositiveButton("保存", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int which) {
                    prefs.edit().putInt("size", size.getSelectedItemPosition())
                        .putInt("heap", heap.getSelectedItemPosition())
                        .putBoolean("keypad", keypad.isChecked())
                        .putBoolean("sound", sound.isChecked()).commit();
                }
            }).setNegativeButton("取消", null).create();
        dialog.show();
    }
}
