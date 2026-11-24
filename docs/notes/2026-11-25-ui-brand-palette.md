# UI 统一：品牌配色落地（2026-11-25，round 13）

## 背景

Android 启动器/游戏面板原本使用 Material 3 基线配色（浅色为淡紫，深色为紫调），
Android 12+ 还会取壁纸动态色；网页版用旧的绿色强调色（#3DDC97），SDL2 桌面启动器
用另一套蓝灰高亮。三个平台和 App 图标（深藏青底 + 白 D-pad + 蓝按钮 #2F6FED）互不
一致。

## 改动

- **Android**（`src/port/android/java/com/lucinhu/nj2me/Md3.java`）
  - 浅色与深色两套基线角色改为品牌蓝派生：
    浅色 `primaryContainer #2F5FA8`（FAB）、`surface #F7F9FF`、
    `surfaceContainer #EBEFF8`；深色 `primary #AFC6FF`、
    `primaryContainer #1B4B96`、`surface #10131A`、`surfaceContainer #1B1E27`。
  - 关闭 Material You 动态取色（`dynamic = false`，保留代码供将来 opt-in）：
    壁纸色会让每台设备外观不同，与网页/SDL2/图标无法统一。
- **网页**（`web/css/app.css`）：`--accent #6F9BFF`、`--accent-strong #4C7BFF`、
  `--accent-software rgba(...)`，深色表面改为与 Android 深色一致的藏青层次
  （`--surface #11141B` … `--border #242A38`）。
- **SDL2 启动器**（`src/client/nj_client_ui.c`）：背景/分隔线/选中行改为同一藏青层次，
  `UI_COL_SEL #2F5FA8`、`UI_COL_BG #0F1219`、`UI_COL_FOLDER #4C7BFF`。

## 验证

Android 模拟器（`Medium_Phone_API_36.1`，x86_64 APK）截图逐像素取样：

| 位置 | 改动前 | 改动后 |
|---|---|---|
| 浅色 FAB | `(218, 226, 255)` 淡紫 | `(47, 95, 168)` 品牌蓝 |
| 浅色标题区背景 | `(254, 251, 255)` | `(247, 249, 255)` 蓝调白 |
| 深色 FAB | `(58, 70, 104)` 灰蓝（动态色） | `(27, 75, 150)` 品牌蓝 |
| 深色背景 | `(26, 27, 32)` | `(16, 19, 26)` 藏青 |

截图保存在 `/tmp/r13-shots/`：`before-library.png`、`after-light2.png`、
`before-dark.png`、`after-dark2.png`、`before-panel.png`、`after-panel.png`。

构建：`tools/build_android_apk.sh --abi x86_64 --gles2` rc=0；
SDL2 桌面端 `cmake --build /tmp/r5-gles2 -j10` rc=0；网页仅 CSS 变量改动，无 JS/资源变化。

## PWA 主题色与网页验证（round 13 收尾）

- `web/index.html` 的 `theme-color` 与 `web/manifest.json` 的 `theme_color` 从旧的
  `#101318` 对齐到品牌深色表面 `#11141B`（与 Android 暗色 `surface`、web
  `--surface` 同一值），`background_color` 保持 `#0B0E13`（与 `--bg` 一致）。
- 布局审计结论（未改代码）：Android 启动器已使用 16dp 水平边距、72dp 行高、
  `Md3.SHAPE_*` 形状刻度；web 使用 `--radius` 16/10/13 三档；SDL2 启动器
  48dp 行高与 16dp 边距。三端间距/圆角刻度一致，本次只统一颜色，避免无谓改动。
- 网页 shell 回归：`tools/check_web.sh --browser` → **PASS**（62 s，
  Playwright Firefox，42 项页面检查），证据
  `.local-tools/debug-runs/web-verify-20261125-0318`。
