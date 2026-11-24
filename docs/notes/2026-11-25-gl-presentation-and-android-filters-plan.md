# GL 呈现层 + FSR shader 与 Android 滤镜 UI：实施计划（2026-11-25）

新目标 goal-7e37f74a 的计划。与已停止的 B2 无关：只替换/增强**呈现层**，不改 3D 事务
架构，软件呈现仍是默认。

## A. SDL2 GL 呈现 + FSR shader（分阶段，可独立验收）

### A1. 离屏 shader 管线（先做，无窗口依赖）
* 新文件 `src/port/posix/nj_gl_present.c`：复用 `nj_gl_api`/私有 GLES2 上下文
  （`nj_gles2` 已提供 context/GL 装载），实现：
  - 输入：`nj_surface` 帧（RGB565/ARGB8888）→ 上传 RGBA 纹理（每帧一次）。
  - 可插拔 GLSL：`nearest`、`sharp-bilinear`、`fsr`（FSR1 EASU 放大 + RCAS 锐化，
    MIT 算法；GLES2 用 4-tap 采样避免 gather）、`crt`/`lcd`（扫描线/网格叠加）。
  - 输出：FBO → `glReadPixels` PPM，供自动化对比。
* 触发：`NJ_GL_PRESENT_DUMP=<prefix>` + `--upscale fsr|sharp-bilinear|nearest|crt|lcd`，
  由 CLI 在 present 钩子调用；默认路径完全不变。
* 验收：LoopDemo/浴火银河 2 的 PPM 对比（nearest/sharp-bilinear 与软件整倍放大逐像素
  一致；fsr 记录 PSNR/边缘锐度而不是逐像素相等）、CPU/GPU 计时记录。

### A2. 窗口呈现
* SDL2 窗口改为可选 `SDL_WINDOW_OPENGL`（`NJ_SDL2_GL_PRESENT=1`），用 A1 的纹理+shader
  画游戏区域；键盘 UI 先用 GL 画（矩形/圆角/位图字体纹理，`nj_gl_ui.c`），逐步替换
  SDL_Renderer 路径；软件滤镜（scanline/LCD/CRT）继续作为无 GL 环境的回退。
* **不能在同一窗口混用 SDL_Renderer 与手写 GL**，所以 UI 必须一并进 GL；这是主要工作量。
* 回退策略：GL 初始化失败/不支持 → 自动回落到现有 SDL_Renderer 路径并记录日志。

## B. Android 滤镜 UI（独立，先做软件滤镜）
* 原生呈现：`android_display_present()`（`src/port/android/nj_port_android.c:1243`）在
  `ANativeWindow_lock` 后的拷贝循环里叠加扫描线/LCD 网格/CRT 暗角（按目标行/列，整数
  缩放后的设备坐标），默认关闭；不改变模拟器帧缓冲。
* JNI：`nativeSetFilter(int mode)`（0 nearest/1 linear/2 scanline/3 lcd/4 crt），
  由 `GameActivity` 新增“滤镜”设置行（与速度对话框同一模式）调用；选择存入
  SharedPreferences，启动时应用。
* 验收：模拟器安装 0.5.4x APK，截图对比 nearest vs scanline/CRT；游戏/键盘 UI 清晰度不受
  影响（滤镜只作用于游戏区域）。

## C. 持续完善
* 每轮保持回归证据：CTest 210/210、libretro 前端矩阵、真实 RetroArch、必要时的全语料
  探针；发布版本时更新 `docs/LIBRETRO.md`/`docs/RELEASE_*`。

## 顺序
1. Android 软件滤镜（B）——收益直接、无 GL 依赖。
2. A1 离屏 shader 管线与 FSR 验证。
3. A2 窗口呈现 + GL UI。
4. 把 Android 呈现也切到 GL 路径（ANativeWindow + EGL window），滤镜/FSR 共用 A1。
