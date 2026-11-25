# UI 重做：人性化、自释其意（2026-11-25 起）

目标（用户指令）：UI 要更好看、更人性化，让界面自己说明用途，不要多余的解释性文字。

## 原则

1. 控件自释其意：图标 + 短标题 + 当前值；当前值就是说明，不写第二行解释。
2. 副标题只保留两类：可变当前值（如「1 秒」「32 MB」「960x1280」）或不可见状态（如「已启用 · cheats.txt」）。
3. 删除操作指引型文案（"回到卡片里…"、"点开可以…"、"用电脑推送：adb push …"）。
4. 文案随功能更新：能用一个词说清就不用一句；中英文保持同等简洁。
5. 功能、默认行为、JNI 设置链路与持久化不变。

## 本轮（round 1）改动

Android 游戏内面板（`GameActivity.java`）：
- 作弊码：说明句 → `已启用 · <文件名>`（未启用时无副标题）。
- 搜索作弊数值：删除三句式解释，仅留标题+图标。
- 帧率上限：标题去掉冗余的「：值」，当前值下移为副标题（原来同时出现在标题与解释句中）。
- 保存状态：删除"选一个存档位保存这一局；回到游戏卡片里「读取存档」继续"，由存档位选择器自释。

Android 启动器（`LibraryActivity.java`）：
- 空状态：五行说明（含 adb 命令）→「还没有游戏 / 点右下角的 + 添加 .jar」。
- 标题计数：「J2ME 运行时 · N 个游戏」→「N 个游戏」；搜索时「N / M 个游戏」。
- 游戏卡片「读取存档」：删除"选择快速存档或某个存档位继续"。
- 「显示名字」：副标题改为当前显示名（值即说明）。

网页（`web/js/i18n.js`）：
- 拖放提示：「也可以点「导入 JAR」选择文件，支持一次导入多个。」→「也可以点「导入 JAR」，支持多选。」（英文同步）。

## 验证

- `bash tools/build_android_apk.sh --abi x86_64 --gles2` → `ok`（Java 编译通过），APK 1,079,449 B。
- 无测试/工具断言这些文案原文（已 grep `tools/ tests/ web/js`）。
- 截图与模拟器验收在后续轮次补齐（启动器空状态、游戏卡片、游戏内面板）。

## 后续

- SDL2 桌面启动器同原则精简（`src/client/nj_client_ui.c` 46 处 draw_text）。
- 网页版布局与提示进一步自释（导入区、错误提示）。
- 面板分组：把设置按「画面 / 存档 / 输入 / 高级」分段，段内只留短标签与值。

## 后续轮次（进度记录）

- round 2：修复 GL/FSR 下大画布黑屏（自定义视图未被 GL 呈现接受，`798d118`）；新增 `--upscale-scale auto`（输出=显示区像素，nearest 与软件呈现 0/997,440 差异，`7f2ef06`）。
- round 3：画布大小选择器删除错误/冗长的分辨率释义，只留数值；自定义项副标题改为“32 ~ 4096”；对话框说明改为“现在是 WxH”（`56e8a53`）。
- round 4：网页导入/存储提示精简为单句约束（`0c63b9e`）。
- 待办：SDL2 启动器文案精简；Android 面板“放大：自动/2×/3×/4×”选项；面板分组（画面/存档/输入/高级）。

## Round 10: Android「放大」setting for the GL present

- The experimental GL present row keeps its short label; the picture magnifier is now a separate row
  「放大：自动 / 2× / 3× / 4×」 (self-explanatory, no paragraph hint).
- Native: `nativeSetGlPresentScale(handle, scale)` stores `android_state.gl_present_scale`
  (0 = auto, 2/3/4 = integer cap), logged as `present: GL picture scale=N`.
- Java: preference `gl_present_scale` (default 0), applied at both native init sites and from the chooser.
- Build: `tools/build_android_apk.sh --abi x86_64 --gles2` ok.

### Round 10 verification (emulator)

- AVD `Medium_Phone_API_36.1`, fresh x86_64 APK; StaticDemo 360x640 panel shows the new row
  `放大：自动` directly under `GL 呈现（实验）：FSR1（GL）`.
- Tapping it opens a four-item chooser (自动（填满画面）/ 2× / 3× / 4×); picking `2×` closes the
  dialog and the row immediately reads `放大：2×` (preference persisted).

## Round 11: SDL2 launcher copy trim

- Settings footer is now contextual and short: `下次启动生效` on the auto-grow row and
  `↑↓ 选择 · ←→ 调整 · Enter 确认 · Esc 返回` elsewhere (was a two-clause sentence).
- F1 help sheet: `F2 收藏 · 最近玩过` replaces the long favorite blurb; the trailing
  "窗口大小会自动记住" line is gone and the config path is a single labelled line.
- The launcher already draws every option as `label … value` with the value right-aligned,
  so no extra explanatory subtitles were needed.
- Verified with `cmake --build /tmp/r5-gles2 -j10` (rc=0).
