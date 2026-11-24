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

## A1 进展：离屏 GLSL 呈现管线（2026-11-25，round 4）

新增 `src/port/gles2/nj_gfx_present.inc`（由 `nj_gfx_gles2.c` 末尾包含，使用私有
结构体与静态辅助函数），公开接口 `nj_gles2_present(ud, src, mode, out_w, out_h,
out_rgba)`（`include/nj2me/nj_gfx_gles2.h`，模式 0..5）。流程：任意 `nj_surface`
格式→RGBA8888 上传→离屏 FBO→可插拔片元着色器→`glReadPixels`（输出行序从上到下）。
默认软件呈现不变，只有显式设置环境变量才运行。

环境变量（SDL2 端口写入 PPM 供帧对比）：
- `NJ_GL_PRESENT=<0..5>`：0 nearest / 1 sharp-bilinear / 2 扫描线 / 3 LCD / 4 CRT /
  5 FSR-lite（sharp-bilinear + RCAS 风格锐化；FSR1 EASU 尚未实现，着色器注释已注明）
- `NJ_GL_PRESENT_DUMP=<prefix>`：输出 `<prefix>00000.ppm`，需配合 `--graphics gles2`
- `NJ_GL_PRESENT_SCALE=<1..8>`（默认 2）、`NJ_GL_PRESENT_COUNT=<n>`（默认 1）

验证（LoopDemo 240x320，2x 输出 480x640，RTX 5080）：与软件帧缓冲（RGB565 按同样的
位扩展还原）对比——
- nearest：**0 / 307,200 像素差异（逐像素一致）**
- sharp-bilinear：0（整数倍时退化为最近邻，符合算法）
- 扫描线：153,600（恰为隔行一半像素）、LCD：170,880、CRT：297,554
- FSR-lite：44（仅边缘锐化）
- 单次 480x640 呈现 + 读回约 2 ms（首次 45 ms 含着色器编译）
- 默认构建（headless CLI）与软件呈现不受影响；`--upscale` 命令行开关随 A2 窗口呈现
  一起接入，A1 阶段用环境变量。

待办：A2 窗口 GL 呈现（私有上下文→SDL 窗口交换，失败自动回落软件）、FSR1 EASU
精确实现、Android GL 呈现接入同一管线。
