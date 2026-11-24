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
