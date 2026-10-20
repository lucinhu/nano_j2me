# Android 中文输入法端到端验证（2026-10-21）

目标里一直挂着「Android 输入法、中文输入」，此前只验证过 ASCII：桌面 SDL2 的真实
fcitx5 中文提交已经跑通（见 `2026-10-17-xy-ime.md`），Android 侧只到硬件键盘输入
`test5678`。本轮在模拟器上把中文输入法这条链路走完了。

## 环境

- AVD `OpenHV_API26_googleapis`（x86_64、Android 8.0 / API 26、320x640、Google APIs
  镜像），从头启动（`-no-window -no-audio -no-boot-anim -no-snapshot-load
  -gpu swiftshader_indirect`）。
- 系统自带 Google 拼音输入法：`com.google.android.inputmethod.pinyin/.PinyinIME`，
  不需要额外安装第三方输入法：
  `adb shell ime enable com.google.android.inputmethod.pinyin/.PinyinIME`、
  `adb shell ime set com.google.android.inputmethod.pinyin/.PinyinIME`。
- 被测 APK 就是默认经典手机布局那一版 `build/android-classic/nj2me.apk`
  （x86_64 + arm64-v8a，SHA-256 `314ca862091f5adac9de4699…`）。
- 原版 JAR `侠义OL-八龙聚首_诺基亚 N97系列(360×640)-871204.jar`
  （SHA-256 `bfa87c2046aa0d2b3847521ffdd34baf27a3de252b07031830d3b966eb2e66d2`，
  未修改）推进到 `/sdcard/Android/data/com.lucinhu.nj2me/files/jars/`。

## 过程

1. 库 → 侠义 OL → 悬浮键盘 ● → 注册协议「接受」→ 游戏注册页（游戏账号/游戏密码/
   手机号码）。
2. 点游戏账号输入框，`GameActivity` 的 `ImeEditor`（原生 EditText）出现并弹出
   Google 拼音键盘（[01-editor-pinyin-ime.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-ime-20261021/01-editor-pinyin-ime.png)）。
3. 在拼音键盘上点出 `nihao`，候选栏出现「你好」等词
   （[02-pinyin-candidates.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-ime-20261021/02-pinyin-candidates.png)）。
4. 选中「你好」提交，编辑器里出现 你好
   （[03-committed-zh-editor.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-ime-20261021/03-committed-zh-editor.png)）。
5. 用悬浮键盘的确定软键离开编辑，回到游戏注册页：游戏账号字段显示「你好」
   （[04-form-field-zh.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-ime-20261021/04-form-field-zh.png)）。中文经 IME →
   `ImeEditor`（`InputConnectionWrapper.finishComposingText/commitText`）→ JNI
   `updateNativeText` → MIDP `TextField`，属于真实提交而不是截图拼字。

## 环境坑

- 该 API 26 镜像在无头 + swiftshader 下，全屏 `GameActivity` 启动会触发 SystemUI
  崩溃：`NullPointerException ... NavigationBarFragment.onKeyguardOccludedChanged`
  （确认对话框卡住输入）。规避：`settings put global hide_error_dialogs 1`，必要时
  `am force-stop com.android.systemui` 并点掉错误框；`wm size reset` / `wm density
  reset` 把上一个会话留下的 640x1280/420 覆盖值还原。这些都只影响模拟器，不是模拟器
  或应用的正常行为。
- 备用：`fcitx5-android` 0.1.3 的 x86_64 APK（minSdk 23，自带
  `lib/x86_64/libIMEPinyin.so`、`libpinyin.so`）已下载到
  `.local-tools/debug-runs/android-ime-20261021/fcitx5-android-x86_64.apk`，本轮没用
  上（系统自带拼音已够），真机没有中文输入法时可以装它。

## 边界

- 只验证了「输入并确认到 MIDP 文本字段」，没有提交注册、登录或进入游戏；没有验证
  Android 网络、存档、多行文本或密码框。
- 模拟器无头截图由 `adb exec-out screencap` 取得；桌面 fcitx5 与 Android 拼音是两条
  不同链路，各自独立验证。
- 未在 arm64 真机安装（手机仍离线）。
