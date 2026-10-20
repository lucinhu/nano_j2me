# Android 默认悬浮按键改为经典手机布局（2026-10-21）

## 需求

Android 版新导入的游戏原先以「方向盘」布局开场，玩家要求把默认布局改成
「经典手机」（`m15835`：安卓默认布局请选择经典手机布局）。这批 JAR 是按功能机
写的：12 个数字键同时承担 2/4/6/8 方向与 5 跳跃，右侧八向集群上的对角键还要
同时按下两个方向，`Canvas.getKeyStates()` 才看得到斜向行走。

## 改动

- `src/port/android/java/com/lucinhu/nj2me/PadConfig.java`
  - `defaults(winW, winH)` 直接返回 `classic(winW, winH)`（经典手机）。新安装、
    没有存档布局的游戏、以及编辑器的「重置」都从这里取布局。
  - 方向盘布局保留为独立工厂 `dpad()`；`byId(PRESET_DPAD)` 与未知 id 的回退都
    走 `dpad()`，所以预设选择器里仍有「方向盘」这一项，只是不再是默认。
  - 字段初值 `preset` 同步改为 `PRESET_CLASSIC`。
- 只换默认值：玩家已经保存的布局（`pad` 偏好里的 `keys`、按 jar 保存的布局、
  或「设为默认布局」）仍然优先，不会因为这次改动被覆盖。
- `tests/android/com/lucinhu/nj2me/PadConfigTest.java` 改为断言默认预设是
  `PRESET_CLASSIC`，并逐字节比较 `defaults().blob()` 与
  `byId(PRESET_CLASSIC).blob()`，防止以后只改名字不改布局。
- `docs/PORTING.md` 的布局章节补上一句：经典手机是 `PadConfig.defaults()` 的
  默认，保存过的布局优先。

## 验证

离线 Java 检查（`tools/check_android_java.sh`，JDK 即可，无需设备）：
`PadConfigTest` 119 项检查全过，整套 Android Java 检查 0 failed。

x86_64 + arm64-v8a 新 APK 由 `tools/build_android_apk.sh` 构建，安装到
API 21 x86_64 模拟器（320x640、全新安装）后截图：

- [默认布局编辑器](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-classic-layout-20261021/02-default-pad-editor.png)：
  左侧 1/2/3、4/5/6、7/8/9、* 0 #，右侧 L/R 软键加八向集群、● 居中，正是经典手机。
- [预设选择器](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-classic-layout-20261021/03-preset-checked.png)：
  标题「悬浮按键布局」，副标题「整套换掉现在的按键（当前：经典手机）」，「经典手机」一行打勾。
- [游戏内悬浮键盘](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-classic-layout-20261021/04-ingame-classic-pad.png)：
  侠义 OL 标题画面上的默认按键就是数字块 + 八向集群。
- [按键送达](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-classic-layout-20261021/05-after-fire.png)：
  点 ● 后菜单从「游客注册」变为「登陆游戏」，说明悬浮按键确实送进了 MIDlet，不只是画出来。
- [安装后的库](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-classic-layout-20261021/01-library.png)。

构建产物：`build/android-classic/nj2me.apk`（1,276,128 字节，
SHA-256 `314ca862091f5adac9de4699…`，versionName 0.1.0 / versionCode 10，
minSdk 21 / targetSdk 34，含 x86_64 与 arm64-v8a）。该 APK 是调试签名，未提交。

## 边界

- 只改新游戏的默认布局；模拟器上已验证的旧 APK 数据先卸载，没有旧偏好残留。
- 这个 AVD 的 `/sdcard` 未挂载（`Vold: Failed to find mounted volume`），JAR 只能
  推进应用私有目录 `/data/data/com.lucinhu.nj2me/files/jars` 后启动；这是验证环境
  限制，不影响默认布局结论。
- 顺带发现：City Bloxx 在这个 API 21 模拟器上启动后，游戏自身
  `getResourceAsStream` 失败并 `System.exit()`，回到库界面。该问题与默认布局无关，
  本轮未诊断（桌面版本可进入游戏）。
- 未测中文/输入法、网络与存档；未在 arm64 真机安装（手机仍离线）。
