# Android GL 呈现路径设计（GL presentation for Android）

日期：2026-11-25  
状态：设计完成，待实现（默认关闭；失败自动回退软件呈现）

## 目标

把 SDL2 上已验证的「帧缓冲 → GLES2 纹理 → 可插拔后处理着色器 → 呈现」链路带到
Android，使 Android 也能使用 `--upscale` 同级的 nearest / sharp-bilinear / scanline /
LCD / CRT / FSR1 呈现效果。约束：

- 只有**呈现层**改变；不重开 frame-resident 渲染（B2），不改 3D 事务架构。
- 默认仍走现有 `ANativeWindow_lock` + 软件拷贝路径；GL 呈现是实验性开关。
- 任何步骤失败（EGL 配置、着色器编译、FBO 不完整、上下文丢失）立即回退软件路径，
  不能黑屏。
- 现有 M3G GLES2 加速（`nj_android_gles2.c` 的私有 pbuffer 上下文）保持工作。

## 现状（代码事实）

- `src/port/android/nj_port_android.c`：`android_state` 持有 `ANativeWindow *window`
  （约 line 299），`android_display_present()` 用 `ANativeWindow_lock` / blit /
  `ANativeWindow_unlockAndPost` 呈现 CPU 帧缓冲；surface 创建/变更/销毁由 JNI 回调
  `nativeSurfaceCreated/Changed/Destroyed`（约 line 6510/6538）维护。
- `src/port/android/nj_android_gles2.c`：自建 EGLDisplay + 私有 pbuffer EGLSurface +
  EGLContext（`EGL_PBUFFER_BIT` 配置），使 M3G 3D 可在独立上下文里执行；
  `nj_android_gles2_bind/start/stop/dispose` 管理生命周期，`info` 结构对外发布
  GL 版本/渲染器字符串。
- 桌面侧 A1/A2/FSR1 已完成：`src/port/gles2/nj_gfx_present.inc` 提供
  `nj_gles2_present(ud, src, mode, out_w, out_h, out_rgba)`（离屏 FBO + 回读），
  `nj_gfx_present` 六种模式；SDL2 端口把它接成窗口呈现。Android 可直接复用它
  渲染到**默认帧缓冲（FBO 0）**而无需回读。

## 方案

1. **扩展 `nj_gfx_present.inc`**：新增
   `int nj_gles2_present_target(nj_gles2 *s, const nj_surface *src, int mode, int out_w, int out_h, GLuint fbo);`
   - `fbo != 0` 时行为与 `nj_gles2_present` 相同（离屏 + 回读）；
   - `fbo == 0` 时绑定默认帧缓冲、按 `out_w×out_h` 设 viewport、上传源为纹理、
     执行同一套着色器像素处理，不调用 `ReadPixels`；
   - 现有 `nj_gles2_present()` 改为转调 `present_target(..., present_fbo)`，保持兼容。
2. **扩展 `nj_android_gles2.c`**：
   - 新增窗口路径：`int nj_android_gles2_set_window(nj_android_gles2 *s, ANativeWindow *win)`
     创建（或重建）`EGLSurface window_surface`（单独的 `EGL_WINDOW_BIT` 配置，
     与既有 pbuffer 配置共存；context 为现有 context，EGL 上下文不绑定 surface）；
     `void nj_android_gles2_clear_window(...)` 在 surfaceDestroyed 时销毁。
   - 新增 `int nj_android_gles2_present(nj_android_gles2 *s, const void *pixels, int width, int height, int stride, int format, int mode, int scale)`：
     - 加锁、确认 GL 已初始化；
     - `eglMakeCurrent(display, window_surface, window_surface, context)`；
     - 用 `nj_surface` 包住 CPU 像素，调用 `nj_gles2_present_target(s->backend, &surface, mode, width*scale, height*scale, 0)`；
     - `eglSwapBuffers`；
     - 恢复 `eglMakeCurrent(display, pbuffer, pbuffer, context)`（M3G 加速路径仍希望 pbuffer 为当前 surface）；
     - 任一失败返回 0，调用方回退软件呈现。
3. **Android 端口接线**：`android_display_present()` 顶部检查
   `s->gl_present && s->gles2 && s->gles2_window`，满足则调用上一步的函数并
   `ANativeWindow_unlockAndPost` 不参与；失败则继续原软件路径。surface
   created/changed 时调用 `set_window`，destroyed 时 `clear_window`。
4. **开关与预览**：第一版用统一设置 `gl_present`（SharedPreferences 布尔，默认关）+
   `present_filter` 复用作模式选择（0 nearest、2 scanline、3 LCD、4 CRT、5 FSR1；
   1 视为 0），面板加一行「GL 呈现（实验）」并在开启时提示 FSR 对 3D 更明显。
   libretro 前端不需要此项（已有 RetroArch shader）。
5. **验证**：
   - 与软件呈现同帧 PPM 对比：nearest 模式 0 像素差（复用 SDL2 的对比脚本）；
   - scanline/CRT 的差异比例与 SDL2 记录一致（约 34.7% / 53.9%）；
   - 模拟器（`Medium_Phone_API_36.1`）截图 + logcat 日志；
   - 关闭开关时行为与 0.5.44 完全一致（默认路径零改动）；
   - 失败注入（强制不完整 FBO）确认回退不黑屏。

## 风险

- EGL 窗口 surface 与 pbuffer 配置不同：需要在启动时分别 `eglChooseConfig`，
  不接受「同一配置」假设。
- 私有 GL context 与窗口 surface 的线程约束：只在运行线程使用，并保持 gles2 锁。
- 部分模拟器/Mali 驱动对 `EGL_PBUFFER_BIT` + `EGL_WINDOW_BIT` 双配置支持差：
  失败即回退，不阻塞启动。
- FSR1 在手机 GPU 上的成本需实测（桌面 5080 上约 0.74 ms/帧）；首版只在
  `present_filter=5` 且用户显式开启时使用，并记录实测。

## 验收

- `docs/UPSCALING.md` 增加 Android GL 呈现章节，注明实验性与回退行为；
- 模拟器截图 + 像素差 + logcat 证据提交到 `docs/notes/`；
- 默认关；CTest 全量保持 211/211（改动只在 Android 端口，不影响其它平台）。

## Round 16 进展：Android 窗口 GL 呈现（实验，默认关闭）

- `src/port/android/nj_android_gles2.{h,c}`：同一 EGL context 增加 window surface（配置优先
  `EGL_PBUFFER_BIT|EGL_WINDOW_BIT`，不可用时回退 pbuffer 配置），`nj_android_gles2_set_window()` /
  `nj_android_gles2_present()` 在运行线程创建窗口 surface、`eglQuerySurface` 取得实际绘制尺寸、
  绘制后 `eglSwapBuffers`，随后把 pbuffer 恢复为 current（M3G GL 路径依赖它）；任何失败返回 0。
- `src/port/android/nj_port_android.c`：`android_gl_present_frame()` 用与软件呈现完全相同的几何
  （整数倍/AUTO 检测裁剪/玩家 pan 与钳制、`debug.nj2me.glpresent.scale` 仅作整数上限，0=跟随软件 fit），
  把逻辑帧缓冲子区域包装成 `nj_surface` 交给 `nj_gles2_present_onscreen()`（nearest/sharp/scanline/
  LCD/CRT）；FSR1（mode 5）仍需 readback 暂不在窗口路径。开关为系统属性
  `debug.nj2me.glpresent=1`（默认关闭，失败自动回退软件呈现）。
- 验证（Medium_Phone_API_36.1，SwiftShader）：logcat 见 `present: GL window path mode=N scale=S`、
  `gles2: enabled renderer=... (private pbuffer)`，游戏正常渲染无崩溃；同一次运行内 nearest 与 scanline
  截图对比 **920,563/2,592,000 像素（35.5%）** 变化，证明滤镜经 GL 窗口路径生效。
- 未完成/已知问题：GL 与软件呈现的“逐像素一致”验证不成立——LoopDemo 是动画，两次运行的动画相位不同，
  截图对比必然大量差异（实测 48%–68% 随相位波动）；需要静态画面 fixture 才能做 PPM 对比。下一轮：加
  `tests/staticdemo`（固定画面）+ 同相位截图对比，再决定是否增加用户可见开关；当前保持默认关闭。

## Round 18：静态 fixture 与一致性测试（GL 窗口路径已知差异）

- 新增 `tests/staticdemo/StaticDemo.java` + `tools/make_staticdemo_jar.sh`（固定网格 + 文字 + 角块，
  不 repaint、无 timer），并接入 `nj_fixtures`。用途：Android GL 呈现与软件呈现可以在**同一画面相位**
  下截图对比；动画 fixture（LoopDemo）两次运行相位不同，截图差异 48%–68% 全是相位噪声，不能作为验收。
- 模拟器（Medium_Phone_API_36.1，x86_64 APK）用 StaticDemo 对比 `debug.nj2me.glpresent=1`（nearest）
  与 `=0`（软件）截图：**2,086,304 / 2,592,000 像素（80.5%）不同**，GL 画面上下颠倒、位置与尺寸也
  与软件不一致（软件呈现画面从窗口顶部开始 4 倍整数放大、按键区在其下方；GL 画面出现在窗口下半部）。
- 结论：Android 窗口路径的 EGL surface 方向（V/U）与几何来源（ANSI window buffer vs EGL surface、
  SurfaceView/pad 区域）还没对齐；本轮试验的 4 种 V/U 翻转组合差异均在 80% 左右，说明主要误差来自
  几何/绘制区域而非单纯方向。默认仍为软件呈现；GL 窗口路径保持 `debug.nj2me.glpresent` 实验开关。
- 下一轮方案：在 Android 端复用已知正确的路径——先 `nj_gles2_present()` 离屏渲染 + 回读 RGBA（与
  SDL2 桌面 PPM 对比一致的那条路径），再用一次全屏纹理 blit 到窗口，方向由 `eglQuerySurface` 与
  `ANativeWindow_getWidth/Height` 的差值决定；以 StaticDemo nearest 0 像素差异为验收，再开放 scanline/
  LCD/CRT 与 FSR1。

## Round 19：方向与视口修正（差异 80.5% → 38.6%）

- 两个真实缺陷修复并提交：
  1. `src/port/android/nj_android_gles2.c`：`vp_y` 是窗口坐标（自上而下），`glViewport` 是自下而上；
     现按 EGL surface 高度换算（`gl_y = surface_h - (vp_y + vp_h)`），画面从窗口底部移回顶部；
  2. `src/port/gles2/nj_gfx_present.inc`：on-screen 路径的四边形 UV 需要 180° 旋转（EGL window
     surface 的呈现方向与离屏 FBO/回读路径相反）；离屏路径保持原 UV 不变（桌面 PPM 对比仍 0 差异）。
- StaticDemo 同相位对比：`debug.nj2me.glpresent=1`（nearest）vs 软件呈现 80.5% → **38.6%**
  （1,000,603 / 2,592,000 像素），且方向已正确（STATIC 正立、蓝块右上、绿块左下、红色中心，与软件一致），
  画面区域边界与软件完全一致（内容 bbox 相同，差异只在游戏画面矩形内）。
- 剩余差异性质：位置不再是平移问题（±40px 平移搜索最优 dy=0），采样色值在黑底/网格边缘处不同，
  怀疑整数倍采样边界（半像素/texel 中心）或 EGL surface 与 ANativeWindow buffer 尺寸不一致导致的
  轻微缩放。下一轮：在 StaticDemo 上逐像素比对网格线位置（用每 8 逻辑像素一条的网格作为标尺），
  确认是 1 像素偏移还是 scale 不同，再决定用 texel-center UV 采样或改 drawable 尺寸来源。

## Round 20：剩余 38.6% 差异的性质（未定论）

- 用 StaticDemo 的 8 逻辑像素网格做标尺，逐行比较软件/GL 截图：软件网格线位于 x=67/71、87/91、119/123…（间距 32）
  且各行完全一致；GL 的网格线间距同为 32，但**相位随行变化**（y=1100 时整体 -5 px、y=1150 时 +1 px、y=1200 时 -6 px），
  ±4 px 平移搜索也无法把差异降到 82% 以下。说明不是简单的 1 像素平移，而是**逐帧/逐行的轻微缩放或位移**，
  最可能的来源是 AUTO 视图的 pacer 平移（`view_dy` 每帧变化）或 EGL surface 与 ANativeWindow buffer 尺寸不一致。
- 验证状态：方向、位置、矩形 bbox 已与软件一致（`91eaec6`）；nearest 的 38.6% 静态差异仍未解决，
  Android GL 呈现保持默认关闭的实验特性，自动回退软件呈现。下一轮计划：在测试日志里打印 EGL surface 尺寸
  （`eglQuerySurface`）与 ANativeWindow buffer 尺寸并固定 AUTO 平移（测试时锁定 view 模式）再比对，确认缩放来源后修正。

## Round 21：几何参数已核实一致；此前对比数据来自旧 APK

- 关键发现：测试脚本里 `adb install -r -t a.apk | tail -1 || adb install -r -t b.apk | tail -1`
  的 `||` 实际绑定到 `tail`（其退出码恒为 0），所以**安装失败时不会执行第二遍安装**，
  设备上一直是旧 APK。`dumpsys package` 的 `lastUpdateTime` 核对证实了这一点。
  这解释了前几轮"方向/几何改完截图不变"的现象；后续测试必须先单次 `adb install` 并用
  `lastUpdateTime` 确认版本。
- 正确安装本轮 APK 后，新增的一次性日志给出确定数据：
  `present: GL first frame buf=1080x2400 surface=1080x2400 vp=60,88 960x1280 view=0 zoom=1.000`
  —— EGL surface 与 ANativeWindow buffer 尺寸一致、视口与软件路径的整数倍布局一致，
  因此"surface/buffer 尺寸不一致导致缩放"的假设被排除。
- 现状：nearest 下截图仍与软件呈现不同（画面内容相位/区域不一致，非平移/翻转可对齐），
  问题范围收敛到 on-screen 四边形采样/方向细节（离屏路径 PPM 对比保持 0 差异，说明 shader 与
  上传本身正确）。Android GL 呈现继续默认关闭、自动回退软件。
- 下一轮：用正确的安装流程（并在启动后校验 `lastUpdateTime`）重做 StaticDemo 基准对比；
  若修不动，则把 Android GL 呈现明确封为实验特性并在文档/发布说明中标注，转入 F 项收尾与 0.5.45。

## Round 22：像素一致性仍不成立，Android GL 呈现定为实验特性

- 新增测试钩子 `debug.nj2me.glview=0/1/2`（把逻辑视图钉死为 AUTO/INT/FILL，避开 AUTO 平移干扰），
  并保留一次性的 `present: GL first frame buf=.. surface=.. vp=.. view=..` 几何日志。
- 用正确安装流程（`lastUpdateTime` 已确认）重做 StaticDemo 对比：日志证实
  `buf=1080x2400 surface=1080x2400 vp=60,88 960x1280`，但 GL 与软件截图的差异仍为
  整屏 38.6% / 画面区 81.4%，在 ±40 像素内穷举平移最优仍为 78%+，格线逐行相位不一致。
  说明差异不是平移/翻转/尺寸，而是 on-screen 四边形采样本身与软件像素网格不对应
  （离屏回读路径与 SDL2 窗口路径均为 0 差异，shader 与上传逻辑正确）。
- 结论：Android GL 呈现（`debug.nj2me.glpresent=1`）保持**默认关闭的实验特性**，
  自动回退软件呈现，不进入正式 UI/发布说明；后续若继续，需要对比 GL 在 window surface 上
  逐像素输出（或改用 FBO→readback 混合验证）来定位根因。其余目标项不受影响。
