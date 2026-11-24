# 画面放大与超分辨率（upscaling）

J2ME 画面的原生分辨率很小（常见 240×320、176×208、320×240）。nano_j2me 的
软件渲染器始终按游戏原生分辨率绘制，放大属于**呈现层**；这里汇总三种做法。

## 1. libretro / RetroArch：用前端 shader（现在就能用，无需改内核）

内核输出 240×320 的 RGB565 画面，RetroArch 的 slang shader 负责放大：

* 打开 `Settings > Video > Scaling`，把 `Integer Scale` 关掉（或按需开），
  `Aspect Ratio` 选 Core Provided。
* 打开 `Settings > Video > Shader`，载入 shader 预设。FSR 1.0（EASU + RCAS）
  在官方 shader 包里：`shaders_slang/fsr/fsr.slangp`（需要从
  `libretro/slang-shaders` 安装/更新 shader 包）。
* 像素风游戏更推荐整数倍 nearest 或 `sharp-bilinear`；3D 类（M3G）游戏用
  FSR 观感更平滑。

这条路不需要内核改动，也不影响任何兼容性/存档行为。

## 2. 独立 SDL2 版：软件呈现滤镜（已实现）

默认仍是 `nearest`，不改变现有观感。启动器「设置 → 缩放滤镜」一行现在有
五个模式，按小车/回车循环：

| 模式 | 效果 |
| --- | --- |
| 最近邻 | 保持像素（默认） |
| 线性 | 双线性平滑 |
| 扫描线 | 每两行设备像素压暗一行，模拟 CRT 扫描线 |
| LCD 网格 | 扫描线 + 每 3 像素的纵向 RGB 条纹间隔 |
| CRT | 更重的扫描线 + 罩栅阴影 |

实现要点：

* 滤镜只作用于**呈现层**：游戏帧缓冲（存档、截图、`--frame-dump`）完全不变。
* 扫描线/网格按窗口设备像素绘制，文字、按键等 UI 覆盖物保持清晰。
* 配置文件键 `filter_mode`（0=最近邻 1=线性 2=扫描线 3=LCD 4=CRT），旧键
  `filter_linear`（0/1）继续兼容读取；直接构造 SDL2 端口时也可用
  `NJ_SDL2_FILTER=nearest|linear|scanline|lcd|crt`（桌面客户端会用自己的
  设置覆盖该环境变量）。
* 验证证据：LoopDemo 与《浴火银河 2》窗口回读截图（960×1039）对比默认帧：
  扫描线模式 345,600/997,440 px 不同（34.7%），CRT 模式 537,600 px（53.9%），
  均为预期的隔行压暗，非渲染差异。

## 2b. GLES2 / FSR 后处理（计划，依赖 EGL 呈现）

FSR1（EASU+RCAS）与 sharp-bilinear 需要把原生帧作为纹理经 shader 画到窗口。
目前 SDL2/Android 的私有 GLES2 上下文只有离屏 pbuffer，没有窗口 surface（呈现
仍走 CPU framebuffer）。因此该 pass 排在目标 (1) 的 EGL 呈现（B3）之后：
呈现层改到 EGL window surface 后，再接入 `--upscale` 的 shader 管线，验收为
1080p 截图对比与 CPU 回归；软件渲染继续是默认路径。

## 3. 内部超采样（SSAA，不推荐默认）

M3G 以 2× 分辨率光栅化后缩小，画质提升但 CPU 约 4×，对 240p 手游收益有限，
仅作为未来实验选项记录。
