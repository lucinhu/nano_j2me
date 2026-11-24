# 画面放大与超分辨率（upscaling）

J2ME 画面的原生分辨率很小（常见 240×320、176×208、320×240）。nano_j2me 的
软件渲染器始终按游戏原生分辨率绘制，放大属于**呈现层**；这里汇总三种做法。

## 1. libretro / RetroArch：用前端 shader（现在就能用，无需改内核）

内核输出 240×320 的 RGB565 画面，RetroArch 的 slang shader 负责放大：

* 打开 `Settings > Video > Scaling`，把 `Integer Scale` 关掉（或按需开），
  `Aspect Ratio` 选 Core Provided。
* 打开 `Settings > Video > Shader`，载入 shader 预设。FSR 1.0（EASU + RCAS）
  在官方 shader 包里：`shaders_slang/fsr/fsr.slangp`（需要从
  `libretro/slang-shaders` 安装/更新 shader 包）。像素风游戏更推荐整数倍
  nearest 或 `sharp-bilinear`；3D 类（M3G）游戏用 FSR 观感更平滑。

### 推荐预设（按游戏类型选一个，不需要改内核）

| 游戏类型 | 推荐预设（`shaders_slang/` 下） | 说明 |
| --- | --- | --- |
| 2D 像素风 | `sharp-bilinear-simple.slangp` | 轻度平滑但保留像素块，性能开销最小 |
| 2D 像素风 + 整数倍 | `nearest/nearest.slangp`（开 Integer Scale） | 完全无插值，适合 240×320 的整数倍放大 |
| 2D + CRT 观感 | `crt/crt-geom.slangp` 或 `crt/shaders/crt-royale.slangp` | 扫描线/荫罩；royale 性能要求高 |
| LCD 掌机观感 | `handheld/lcd-grid-v2.slangp` | 模拟 LCD 网格 |
| M3G 3D / 全 3D 标题 | `fsr/fsr.slangp`（EASU+RCAS） | 3D 边缘平滑、字体不糊；性能可接受 |
| 混合 2D/3D | `fsr/fsr.slangp` 或 `sharpen/sharpen-bilinear.slangp` | 兼顾文字与 3D 边缘 |

使用要点：

* FSR / sharp-bilinear 建议关闭 Integer Scale 并让窗口/全屏自由缩放；
  nearest 建议开启 Integer Scale，避免非整数倍采样的不均匀像素。
* `Aspect Ratio` 用 **Core Provided**（内核按游戏原生的 240×320 等比例输出，
  不做拉伸）；窗口比例不合适时由 shader 的缩放阶段处理。
* 画质与性能只取决于前端 shader；内核的存档、截图、帧转储仍然是原始
  分辨率，不受影响。没有 shader 支持的前端（或用命令行 CLI 时）不启用
  这些预设，行为与现在完全一致。
* RetroArch 中可用 `Settings > Video > Shader > Apply Shader Preset` 保存为
  每个内核/游戏的自定义预设；切换游戏时自动生效。

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

### Android 版：软件呈现滤镜（已实现，0.5.43+）

设置 →「画面滤镜」提供：关闭（默认）/ 扫描线 / LCD 网格 / CRT。滤镜在原生
`android_display_present()` 锁定窗口缓冲后应用，只改变窗口显示；模拟器帧缓冲、
存档、截图、帧转储与 libretro 核心均不受影响。选择保存在应用私有
SharedPreferences（`present_filter`），通过 `nativeSetPresentFilter(mode)` 传给原生层，
下一帧生效；旧版「画面反锯齿」开关保持不变。GL/FSR 呈现路径就绪后，同一设置行会
增加 FSR 选项。

**模拟器验证（2026-11-25，API 36 AVD，x86_64 APK）**：LoopDemo 游戏画面在
关闭滤镜时为基准；切换「扫描线」后画面区域 614,784 / 2,592,000 像素变化
（23.7%，隔行压暗）；「CRT」后 819,712 像素变化（31.6%，扫描线+栅格）；
按键/状态栏 UI 不在滤镜区域。`am force-stop` 后重新启动并进入游戏，设置面板仍显示
「画面滤镜：CRT」，SharedPreferences 持久化验证通过。
