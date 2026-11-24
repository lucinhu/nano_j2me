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

## 2. 独立 SDL2 / Android 版：GLES2 后处理 pass（计划实现）

现有私有 GLES2 上下文可以复用：把原生分辨率帧上传为纹理，经一个后处理
着色器画到窗口：

* `nearest`（默认，保持现状）；
* `sharp-bilinear`（低成本、清晰）；
* `fsr`（FidelityFX FSR 1.0 EASU + RCAS，MIT 许可；GLES2 移植需避开
  gather 优化）。

计划以 `--upscale <mode>` / 环境变量控制，默认不变；验收为 1080p 截图对比
与 CPU 回归（软件渲染仍是默认路径）。

## 3. 内部超采样（SSAA，不推荐默认）

M3G 以 2× 分辨率光栅化后缩小，画质提升但 CPU 约 4×，对 240p 手游收益有限，
仅作为未来实验选项记录。
