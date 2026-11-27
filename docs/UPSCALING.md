# 画面放大与超分辨率（upscaling）

游戏仍按设定的逻辑尺寸布局、接收触点和读写像素。桌面 SDL2、无头端和现代 Android 可以在光栅化阶段提高分辨率，也可以在呈现阶段选择独立滤镜。

## 内部渲染倍率、抗锯齿和 M3G FSR2

以 240×320 游戏为例，`--render-scale 2` 会按 480×640 绘制。再加 `--ssaa 2` 会在 960×1280 上采样，把每个 2×2 区域平均为一个输出像素，最终仍输出 480×640。两项均默认 1。建议先试 2×，再根据机型帧率决定是否加四采样。

```sh
# SDL2 或桌面启动器，画面按 2× 输出，四采样抗锯齿
build/nj2me --port sdl2 --render-scale 2 --ssaa 2 game.jar
# 像素画建议先试 3× 配「像素画清晰」
build/nj2me --port sdl2 --render-scale 3 --asset-filter pixel-art game.jar
# 240×320 游戏按 1920×2560 绘制与输出
build/nj2me --port sdl2 --render-scale 8 --asset-filter pixel-art game.jar
# 连续色调插画可选柔和平滑，旧选项 --smooth-assets 与这一行等效
build/nj2me --port sdl2 --render-scale 2 --asset-filter smooth game.jar
# 在启用 Vulkan FSR2 的构建上，为合适的 M3G 绘制启用时域重建
build-fsr2/nj2me --port sdl2 --render-scale 2 --fsr2 game.jar
```

| 画面内容 | 渲染行为 |
| --- | --- |
| LCDUI 图元 | 在内部采样网格重画线、矩形、圆角、弧线和三角形。线宽与裁剪按游戏坐标解释。 |
| 字体 | TTF 轮廓和现代 Android 系统、中文及自选字体按物理字号重绘，沿用逻辑基线和字距。位图字体在单个字形内处理覆盖率，清晰模式保留细笔画的实心部分。 |
| 图片与帧动画 | 原始像素按最近邻放大。柔和平滑在当前帧内按透明度做双线性采样。像素画清晰观察每个像素周围的 3×3 区域，沿连续斜边用原色修整台阶，保留平直边和孤立亮点。FSR1 锐利用 Catmull-Rom 立方核重建不透明区域并做限幅对比度锐化，结果钳在 4×4 邻域的逐通道范围内，透明边缘仍走精确的预乘合成。旋转或翻转后仍只取当前精灵帧。`drawRGB` 和逻辑像素读写按游戏像素执行。 |
| M3G 与 Mascot | 投影几何在物理缓冲区光栅化。M3G 可加 `--subdivide` 细分适用的不透明面片。柔和平滑对相邻纹素作线性插值。像素画清晰在纹理放大时将过渡收窄到相邻纹素中心间距的四分之一，缩小时沿用游戏的过滤与 mip 选择。透明边按可见颜色和覆盖率合成。 |
| 时域重建 | `--fsr2` 为兼容的 M3G 绑定启用 AMD FSR2 2.2.1。它在实际采样网格为逻辑图像的 2× 至 3× 时使用抖动颜色、深度和运动向量，在 `releaseTarget` 时写回高清图像。后画的 LCDUI/HUD 继续合成。 |

`--render-scale` 接受 1 至 8 的每一个整数，`--ssaa` 支持 1 或 2。240×320 游戏在 8× 下输出 1920×2560。开启四采样时，先在 3840×5120 上绘制，再合成为 1920×2560。每轴从 4× 提到 8×，需要绘制的像素数变为四倍。

高清颜色缓冲池上限为 96 MiB，单边上限为 16384 像素。运行时优先为屏幕预留空间。240×320 游戏的 8× 输出占 18.75 MiB，再加四采样共占 93.75 MiB。遇到尺寸、内存预算或分配限制时，先在所选倍率尝试关闭四采样，再逐级尝试较低倍率，直至可用。部分离屏图像也可能使用逻辑像素。以上仅计高清颜色缓冲，3D 深度、字体和游戏内存另计。

开启 `--fsr2` 时，软件桥接还要生成时域输入并与 GPU 交换帧数据，耗时取决于游戏和设备。标准透视、完整清除颜色和深度且可见片元不透明的绑定可使用 FSR2。倍率以抗锯齿合成前的实际宽高为准。例如，240×320 → 720×960 可使用 3× FSR2；2× 内部渲染加四采样得到 960×1280 采样网格，直接使用原生高清面片，再合成为 480×640。高于 3×、部分清除、精灵、透明混合或设备能力不足时保留高清空间渲染。可加速的面片交给已选后端，变更目标或从空间渲染返回时会重置时域历史。在确定性 Doom3 装置上与同帧原生渲染对照，2× 时域输出到 2× 真值的 MAE 为 0.73（空间立方核从 1× 放大为 3.25），3× 为 0.56；静止区域的闪变约为空间放大的一半（见 [FSR2 画质对照与回读耗时](notes/2026-11-27-fsr2-accuracy-and-readback.md)）。

在 Android 游戏内打开「≡」→「画面」→「高清渲染倍率」，可逐档选择 1× 到 8×。重新启动游戏后，画面面板会显示当前帧的「实际渲染倍率」。高倍率可以先关四采样，再按设备表现开启。FSR2 只与 2×、3× 高清倍率配对：打开 FSR2 时面板会自动把其它倍率改成 3×（在理想 8× 真值下 3× 时域比 2× 近 4–21%，代价约 +0.3 ms），把倍率选成 1× 或 4×–8× 时面板会自动关闭 FSR2 并提示原因，避免出现「已请求启用」但实际没有时域放大的组合（见 [Android Vulkan 与 FSR2 路径验证](notes/2026-11-27-android-vulkan-fsr2.md) 与 [4×–8× 时域放大链的取舍](notes/2026-11-27-temporal-chain-at-high-scales.md)）。「GL 放大」另设自动或 2× 到 8× 的窗口整数适配上限，受屏幕实际可用空间约束。「高清素材采样」支持原始像素、柔和平滑、像素画清晰和 FSR1 锐利，下次启动游戏时生效。桌面用 `--asset-filter nearest|smooth|pixel-art|fsr1`，默认 `nearest`。FSR2 构建需要 Vulkan 1.2 设备和可选 SDK。[Linux 与 Android 构建步骤](../src/port/fsr2/README.md)

像素画清晰适合有明确轮廓的人物、道具和地图。连续色调插画可选柔和平滑或更锐的 FSR1 锐利，想逐格观察原像素可选原始像素。《田园物语》在相同输入下以 3× 回放至第 1080 帧，人物区域的 276×266 个输出像素中，原始像素与清晰模式使用同样的 109 种颜色，清晰模式修整了其中 1,139 个像素，柔和平滑产生了 10,933 种颜色。

桌面单核微基准把同一组固定素材放大 4× 后连续绘制 40 次（37×29 精灵为 600 次），四种模式耗时（每次绘制，毫秒）：

| 素材 | 原始像素 | 柔和平滑 | 像素画清晰 | FSR1 锐利 |
| --- | --- | --- | --- | --- |
| 240×320 不透明斜条纹 | 0.20 | 1.89 | 1.12 | 7.84 |
| 240×320 全细节 | 0.20 | 2.82 | 1.10 | 11.12 |
| 37×29 半透明精灵旋转 90° | 0.007 | 0.081 | 0.026 | 0.152 |

FSR1 约是像素画清晰的 5–7 倍、柔和平滑的 2.7–3.1 倍（4× 整屏不透明素材每帧约 5.8–7.6 ms CPU），适合菜单、静态插画和地图这类不逐帧整屏重画的素材；纯色窗口退化为填充会更快。实际耗时取决于素材、倍率和设备，测法与画质对照见 [高清素材采样的 FSR1 模式](notes/2026-11-27-fsr1-asset-sampling.md)。

验证中，StaticDemo 的 128×144 画布以 3× 输出为 384×432，四采样使 2,044 个输出像素发生变化。《毁灭战士 3》195 个主机帧中记录到 99 次 240×320 → 480×640 的 AMD dispatch，31 次沿用历史、68 次重置历史，另有 118 次 M3G 目标释放走高清空间路径。计数按目标释放，一个主机帧可包含多次释放。无头 `--dump-frames` 可以导出实际输出尺寸供对比。

下图上排取自桌面 StaticDemo，下排取自 API 36 Android 模拟器运行同一个中文 MIDlet。三列分别使用原始分辨率、3× 内部渲染、3× 内部渲染加四采样。在同一窗口位置截取像素，中文测试中的 5×5 逻辑定位块在三档画质下覆盖同样的 625 个屏幕像素。

![桌面线条、轮廓字体和 Android 中文系统字体三档画质对照](images/internal-render-comparison.png)

### 选择软件、OpenGL ES 或 Vulkan

`--graphics software` 使用 CPU 绘制，可单独使用高清倍率和四采样。`--graphics gles2` 使用 SDL2 的 OpenGL ES 后端。`--graphics vulkan` 将可加速的 M3G 三角形交给 Vulkan，支持 SDL2 和无头运行。使用对应构建后，可以用同一个 JAR 比较三条路径。

```sh
cmake -S . -B build-gpu -DNJ_ENABLE_GFX_ACCEL=1 -DNJ_BUILD_GLES2_ACCEL=ON -DNJ_ENABLE_VULKAN=ON
cmake --build build-gpu --target nj2me-cli
build-gpu/nj2me --port sdl2 --graphics gles2 --render-scale 2 game.jar
build-gpu/nj2me --port sdl2 --graphics vulkan --render-scale 2 game.jar
build-gpu/nj2me --port sdl2 --graphics software --render-scale 2 game.jar
```

在高清 M3G 模式下，GPU 接收物理分辨率的顶点、颜色与深度缓冲区，只交换绘制涉及的矩形。Java 的逻辑像素和深度仍由软件生成，供游戏同步读取。驱动拒绝某批面片时，软件按原顺序重绘该批。FSR2 是独立可选项，按上节的条件收集时域输入。GLES2 窗口在小端上可直接上传紧密排列的 ARGB8888 高清帧，像素预算为 4096²，具体尺寸还受设备纹理与视口上限约束。

GLES2 默认允许每批面片的绘制矩形最多覆盖 1,048,576 个像素。超过该值的批次由软件按当前内部倍率绘制。《毁灭战士 3》在 240×320、8× 的前四帧均输出 1920×2560。桌面 RTX 5080 Laptop 的 Vulkan 后端接收了其中两批、共 836 个三角形，GLES2 的这两批走软件。对于较小的绘制区域，两个后端都通过了 8× 加四采样的 M3G 测试，128×128 采样图的全部像素与软件参考相同。

Android 的常规包提供软件和 GLES2。arm64 增强包在画面面板增加「Vulkan 3D 面片渲染」，下次启动游戏时尝试启用；初始化失败时游戏沿用已选的绘制方式，并在 logcat 说明原因。API 36 模拟器（gfxstream Vulkan、直通宿主机 NVIDIA GPU）上已完成 Vulkan 面片提交与 FSR2 时域放大的实际验证，见 [Android Vulkan 与 FSR2 路径验证](notes/2026-11-27-android-vulkan-fsr2.md)。Vulkan 面片提交此前已在桌面 NVIDIA 和 Intel 设备上验证。

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

* 窗口滤镜作用于呈现阶段。无头 `--dump-frames` 记录滤镜之前的画面，大小由内部渲染倍率决定。
* 扫描线/网格按窗口设备像素绘制，文字、按键等 UI 覆盖物保持清晰。
* 配置文件键 `filter_mode`（0=最近邻 1=线性 2=扫描线 3=LCD 4=CRT），旧键
  `filter_linear`（0/1）继续兼容读取；直接构造 SDL2 端口时也可用
  `NJ_SDL2_FILTER=nearest|linear|scanline|lcd|crt`（桌面客户端会用自己的
  设置覆盖该环境变量）。
* 验证证据：LoopDemo 与《浴火银河 2》窗口回读截图（960×1039）对比默认帧：
  扫描线模式 345,600/997,440 px 不同（34.7%），CRT 模式 537,600 px（53.9%），
  均为预期的隔行压暗，非渲染差异。

## 2b. GLES2 / FSR 后处理（已实现，SDL2 桌面版）

`--upscale fsr` 对最终游戏画面使用共享的 GLES2 空间滤镜。它按已渲染帧和游戏视口的**实际像素尺寸**选择处理方式。

| 从输入到视口 | 呈现方式 |
| --- | --- |
| 尺寸相同 | 逐像素复制。SDL2 可直接提交原帧。 |
| 放大（任意倍率） | 用立方核（Keys 曲线，A=0.10）直接从原始纹素的包围盒重建到视口尺寸，再按所选锐度用 RCAS 在重建后的像素上锐化。 |

锐度（RCAS `sharp`，0.05–1.0，桌面 `NJ_GL_PRESENT_SHARP`/`--upscale-sharp`，Android「FSR1 锐度」）默认取 AMD 参考值 `exp2(-0.2)=0.87055056`。实测误差对锐度是单调的（参考图取面积平均，本身偏软），且收益随倍率衰减：2× 时降到 0.05 只改善 MAE 5.6%，8× 时 0.5%。因此不按倍率区分锐度，保留这个偏锐的默认值，具体见[锐度灵敏度](notes/2026-11-27-fsr1-sharpness-and-present-cost.md)。
| 放大，选 bicubic | 同一个立方核重建，但跳过 RCAS 锐化，画面更柔和。 |
| 任一轴缩小 | 按目标像素覆盖的源区域加权平均。超过 8 倍的缩小分阶段处理，保留细线和交错色块的平均亮度。 |

立方核的 A 参数由原生高分真值扫得：A=0.10 在 2D 文字、斜线和 3D 场景上同时比 Catmull-Rom（A=-0.5）的过冲和旧版 FSR1 的 2× 重建网格更接近原生画面。整数倍率时采样相位均匀（每 s 个输出像素有一个正落在纹素中心），边缘粗细一致，不会出现非整数倍放大的相位抖动；只有 1× 是逐像素复制，2× 以上仍是立方核插值。

桌面默认适配窗口内游戏区域，`--upscale-scale 2` 等显式倍率仍可指定中间输出尺寸。Android 直接绘制到 EGL 窗口。`--fsr2` 则用于提供深度和运动数据的 M3G 绘制，见上面的内部渲染设置。[跨分辨率实测](notes/2026-11-26-fsr-resolution.md)、[立方核重建与原生高分对照](notes/2026-11-27-fsr-cubic-reconstruction.md)、[缩放矩阵、整数倍率与缩小路径](notes/2026-11-27-fsr-scale-matrix.md)、[FSR2 画质对照与回读耗时](notes/2026-11-27-fsr2-accuracy-and-readback.md)、[Android Vulkan 与 FSR2 路径验证](notes/2026-11-27-android-vulkan-fsr2.md)、[FSR1 锐度灵敏度与 4×/8× 呈现耗时](notes/2026-11-27-fsr1-sharpness-and-present-cost.md)、[桌面 present stats](notes/2026-11-27-desktop-present-stats.md)、[高清素材采样的 FSR1 模式](notes/2026-11-27-fsr1-asset-sampling.md)、[高清面片批处理的取舍](notes/2026-11-27-hd-journal-batching.md)、[高清面片的颜色/深度往返](notes/2026-11-27-hd-triangle-transfer.md)

### Android 版：软件呈现滤镜（已实现，0.5.43+）

设置 →「画面滤镜」提供：关闭（默认）/ 扫描线 / LCD 网格 / CRT。滤镜在原生
`android_display_present()` 锁定窗口缓冲后应用，只改变窗口显示；模拟器帧缓冲、
存档、截图、帧转储与 libretro 核心均不受影响。选择保存在应用私有
SharedPreferences（`present_filter`），通过 `nativeSetPresentFilter(mode)` 传给原生层，
下一帧生效。「GL 呈现」可单独选择 FSR1 和 bicubic，各渲染倍率开关见上面的内部渲染设置。

要量 Android 端每帧的呈现开销，执行 `adb shell setprop debug.nj2me.present_stats 1`：属性
每秒采样一次，每 120 帧把绘制（make-current、上传、滤镜）与交换（`eglSwapBuffers`）的时间
按 min/median/p95 写进 logcat，清掉属性即停。模拟器实测数字见
[缩放矩阵、整数倍率与缩小路径](notes/2026-11-27-fsr-scale-matrix.md)。

桌面端有同一套开关：`NJ_PRESENT_STATS=1`，每 120 帧向 stderr 打印
`present stats: mode=… WxH frames=120 draw ms min/median/p95`、对应的 `swap`，以及按呈现间隔算出的 `frame ms`（`frame − draw − swap` 就是游戏自身的每帧生产耗时）
一行，字段含义与 Android 相同，可直接比较同一帧在不同窗口尺寸下的呈现成本。
8× 高清镜像（20 MB）放大到 960×1280 的实测 draw 中位数约 4.2 ms，几乎全部是
纹理上传，说明呈现成本以字节数为准；详见
[桌面 present stats](notes/2026-11-27-desktop-present-stats.md)。

**模拟器验证（2026-11-25，API 36 AVD，x86_64 APK）**：LoopDemo 游戏画面在
关闭滤镜时为基准；切换「扫描线」后画面区域 614,784 / 2,592,000 像素变化
（23.7%，隔行压暗）；「CRT」后 819,712 像素变化（31.6%，扫描线+栅格）；
按键/状态栏 UI 不在滤镜区域。`am force-stop` 后重新启动并进入游戏，设置面板仍显示
「画面滤镜：CRT」，SharedPreferences 持久化验证通过。

## 真实游戏验证（2026-11-25，SDL2/GLES2 桌面）

窗口截图取自 SDL2 GLES2 路径（`--graphics gles2 --upscale ... --upscale-scale 2`，
Galaxy on Fire 2 启动画面，960x1039 窗口，含按键层）：

| 对照 | 差异（>6/255 像素） | 说明 |
|---|---|---|
| nearest 2x vs FSR 2x | 631,524 / 997,440（**63.31%**） | 文字与 UI 边缘经 EASU 重建 + RCAS 锐化，块状锯齿明显减少 |
| nearest 1x vs 软件呈现 | 0 / 997,440 | 呈现层替换本身无几何/采样偏差（A2 证据） |

截图：[`docs/images/upscale-nearest-galaxy.png`](images/upscale-nearest-galaxy.png)、
[`docs/images/upscale-fsr-galaxy.png`](images/upscale-fsr-galaxy.png)。

CPU 记录（同机 RTX 5080，Linux）：

| 场景 | 软件呈现 | GL 呈现 + FSR 2x | 差值 |
|---|---|---|---|
| Galaxy on Fire 2，560 帧 @30 fps（3D GL） | user 4.663 s / sys 0.313 s | user 5.872 s / sys 0.268 s | +1.21 s user（≈+26%） |
| LoopDemo，3000 帧 fps 0（纯 2D，演示后处理本身开销） | user 5.884 s / sys 0.390 s | user 8.116 s / sys 0.416 s | +2.23 s（≈0.74 ms/帧） |

结论：FSR 2x 的后处理成本约 0.7–2 ms/帧（取决于分辨率与场景），换来明显更好的
放大质量；默认仍为软件渲染，`--upscale` 为显式选项。

## 独立 Android 版：GL 呈现（实验）

在游戏内打开「画面」→「GL 呈现」，可选择最近邻、扫描线、LCD 网格、CRT、FSR1 或双三次。默认使用软件呈现。软件与 GL 互切时仅替换游戏画面的 SurfaceView，当前游戏继续运行。不同 GL 滤镜共用同一个窗口。

Android 使用与桌面相同的缩放算法，直接绘制到实际大小的 EGL 窗口。若驱动拒绝该窗口，软件路径接手呈现，取得新窗口后可以重新尝试 GL。
