# 可选 M3G / Mascot GLES2 路径

M3G 与 Mascot 现在可以通过纯 C 的同步 fragment 接口，把受支持的三角形交给移植层 GLES2 后端。接口见 [nj_gfx_raster.h](../include/nj2me/nj_gfx_raster.h)，CPU 参考实现见 [nj_gfx_raster.c](../src/gfx/nj_gfx_raster.c)。SDL2 和标准 Android 复用 [GLES2 绑定](GLES2_BACKEND.md) 的专用 context、运行开关与生命周期。默认关闭；旧 Android 2.0 版仍只使用软件。

这是保留原有像素规则的混合实现：CPU 继续执行几何、裁剪、覆盖和深度量化，GPU shader 执行常量颜色输出及 M3G 的深度比较/写入。GPU 读取的是候选覆盖/深度 key，颜色与深度合并在 shader 中执行；保留 CPU 覆盖循环以保持原有像素一致性。完整 GPU 几何、三维纹理采样、透明混合和 Vulkan 尚未实现。同步上传/读回可能比纯软件更慢，当前不承诺性能收益。

## 开启

```sh
cmake -S . -B build-gles2 -DNJ_ENABLE_GFX_ACCEL=1 -DNJ_BUILD_GLES2_ACCEL=ON
cmake --build build-gles2
NJ_SDL2_GFX=gles2 build-gles2/nj2me --port sdl2 game.jar
```

Android 使用 `tools/build_android_apk.sh --gles2` 构建，在游戏库「⋮ → 显示设置」或游戏内面板打开「GLES2 实验加速」，下一次启动游戏生效。默认关闭，选择持久保存且优先于遗留的设备 property；普通软件包会禁用开关。游戏内面板显示本局实际状态与成功调用次数，修改下次启动选择不改变本局。详见 [Android 设置与状态](GLES2_BACKEND.md#sdl2-与-android-前端)。不带构建开关、关闭应用开关、初始化失败或 context 失效时保留软件路径。退出日志的 `rasters` 表示后端接受的 packet 数，包括完全被遮挡、`drawn=0` 的 packet；它不表示全部三维图元使用 GPU，也不是性能计数器。

其他移植层可设置 `port.gfx_raster = nj_gles2_raster`、`port.gfx_raster_ud = backend`。和二维接口一样，平台负责专用 context、正确的 owning thread、串行调用和有效生命周期；VM 与核心不依赖 GL 类型。

## 当前支持范围

- M3G：无纹理、无雾、非 sprite、最终三个顶点颜色相同且不透明、允许 alpha 写入，混合模式为原实现的 `ALPHA` 或 `REPLACE`。支持原有 viewport/clip、剔除、双面颜色选择、反向深度范围、bias、等深通过，以及深度测试/写入的独立开关。不符合条件时保留完整软件路径。
- Mascot：无纹理/环境图/平滑插值、无有效 toon 的常量颜色三角形，flags 为 `0` 或 `SHADED`、blend 为 `REPLACE`。颜色按原始 vertex 0 的 light 和原 `mr_flat_color` 规则计算。点、线、纹理、透明/加减混合等继续软件绘制，保持 flush 排序后的原始顺序。`NJ_USE_LIBC=0` 时使用原软件实现。
- 目标支持 RGB565、RGB888、ARGB8888，允许正字节行距和 padding。超出预算、分配失败、后端拒绝或 GL 错误都回到同一次图元的原软件路径。

受支持图元的候选覆盖沿原软件循环生成，不以 GPU 的像素中心或边缘规则推测。例如 Mascot 的固定点 DDA 与 GL 三角形边缘覆盖存在差异；M3G 的逐行浮点累积及 `double pz += dxz` 也会影响深度量化。保留这些循环是本版逐像素兼容的前提。

## Packet 与深度

`nj_gfx_raster` 描述一个已裁剪的 device 坐标矩形：

- `keys` 从该矩形左上角开始，stride 以 `int32` 元素计数。`0` 表示没有覆盖，`1..16777216` 是候选深度 key。没有深度的 Mascot 用 `1` 标记覆盖。
- `depth` 从整个目标 surface 的 `(0,0)` 开始，stride 同样以元素计数。key 越大越近；`candidate >= old` 通过，等深也通过。清除值为 `0`。深度属于原 target binding，不能按显示帧擅自清空。
- `color` 是不透明 `0xFFRRGGBB`。测试和写深度可以分别关闭；二者都关闭时 depth 可以为 NULL。
- 所有输入只借用到同步返回，尺寸和对齐由调用方保证。像素、keys、depth、packet、surface 描述和 `drawn` 不能重叠。后端不保存 VM 堆指针或依赖地址不变的纹理缓存。

M3G 的最大 key 是 `16777216`，需要 25 位有效表示。后端把 key 拆成 RGBA8 四个字节，通过 shader 的逐字节字典序比较，避免 GLES2 浮点精度或常见深度附件位数丢失相邻 key。颜色和深度两个 pass 都读取**同一份旧深度**。GPU 输出深度随后读回原 Java `int[]`，所以之后的软件图元可以使用最新颜色和深度。

## 原子提交与回退

成功返回 `NJ_GFX_ACCEL_DONE` 前，颜色和所需深度都已从私有 FBO 读回、完成 GL 错误及结果检查，然后一起提交 CPU 缓冲，`*drawn` 写为 `0` 或 `1`。其他返回值不得修改调用方的颜色、深度或 `*drawn`。即使第二次读回失败，也不能留下第一次颜色写入。

包装层在提交成功后发现恢复 context 失败，仍保留 `DONE`，随后永久回退到软件，避免同一图元被重复绘制。初始化和资源销毁规则与二维后端相同，不能在错误线程销毁仍被 GL 引用的状态。

每个图元最多生成 1,048,576 个候选 key，即 4 MiB 临时覆盖缓冲。后端默认同样限制为 1,048,576 像素；CPU RGBA scratch 最多三份，另有三个私有 GL RGBA 纹理。三维资源第一次调用时才创建，不要求二维初始化同时创建三维 shader。

## 验证程序

- [test_m3g_accel.c](../tests/test_m3g_accel.c)：真实 M3G API，原软件与 CPU packet reference 比较完整颜色、Java 深度、guards、计数，以及分配失败、拒绝、并行路径和实例隔离。
- [test_mascot_accel.c](../tests/test_mascot_accel.c)：DDA 覆盖/颜色矩阵与真实 Mascot API 排序后混合绘制。该测试自身使用 CPU callback，不作为 GPU 证据。
- [test_gfx_raster_gles2.c](../tests/test_gfx_raster_gles2.c)：真实 EGL/GLES，独立颜色/深度预期，GPU/软件交错、多 context、分配和 GL 故障注入，并经过生产 M3G/Mascot 入口调用 GPU。

可用 `ctest --test-dir build-gles2 -R 'test_(m3g_accel|mascot_accel|gfx_raster_gles2)' --output-on-failure` 运行；缺少实际 EGL/context 时 GPU 测试返回 77，明确记为跳过。

2026-11-12 验证记录：

- NVIDIA RTX 5080 与 Mesa llvmpipe 均通过 1,464 个独立 case、12,736 项 packet 检查、37 次原子拒绝。经过生产入口实际接受 M3G GPU 调用 48 次、Mascot 2 次；M3G fixture 另完成 316,194 项检查。包含两个读回点的晚期 GL 错误、分配失败、损坏读回、shader 初始化失败与失败期间 context gate 改变后的资源释放。
- M3G 的 CPU protocol reference 完成 530,249 项检查；Mascot 完成 15,552 个矩阵 case、708,544 项检查。它们验证引擎覆盖和回退，GPU 证据来自上一条。
- 启用配置通过 20 项相关 CTest，默认软件配置通过 17 项。默认配置的 M3G 加速专用测试会自行跳过 hook；原软件 M3G/Mascot 测试正常执行。启用构建期间遇到工作区另一个进行中的 AMR 解码修改，图形实验单独设置 `NJ_ENABLE_AMR=0`，没有修改该解码器；这里不声称验证了音频。
- SDL 包装层在 NVIDIA、Mesa 和强制 GLES 2.0 下通过 2,543 项检查，接受 18 次 raster；测试覆盖复制回调、双实例、foreign/null context 恢复和 CPU/GPU 交错。Android 包装层的 host 真实 EGL 故障测试通过 19 次 GPU DONE、12 次原子 DECLINE，提交后 reset 保留正确 DONE；这不是安卓真机测试。EGL 销毁失败时只保证安全保留，测试通过复位私有状态重试清理，不代表公开自动重试。
- 新测试/backend/SDL/gfx/core-raster 对象在强制 Mesa GLES 2.0 下 ASan、UBSan 和泄漏检查通过；其余 production core archive 未插桩。
- 原项目 `m3gdemo.jar` 在 SDL 软件和 GLES2 下均报告 `M3G checks=148 failures=0`，正常退出 0。GLES2 运行实际接受 16 个 raster packet。标准 Android API 21 / SwiftShader 上，测试 APK 的相同 Java fixture 也通过 148 项检查、16 次 raster 接受、正常退出 0。这是完整 APK / Java fixture 的 Android 模拟器结果；追加的真机离屏原生验证见下文。
- 《浴火银河》独立 RMS、300 帧并回放后续按键的场景正常退出；统计 `rasters=0`，该场景仍使用纹理等软件分支，不能据此宣称商业游戏的三维 GPU 路径已覆盖。

2026-11-13 Android 15 / Qualcomm Adreno 725 真机追加验证：

- 在用户继续使用手机的约束下，通过 ADB shell 运行 ARM64/API21 原生程序，仅使用私有 EGL pbuffer。没有安装 APK、启动 Activity、切换前台或修改手机系统图形属性；测试以 `nice -n 10` 运行并设置 60 秒超时，上传文件和独立临时目录已清理。
- 未修改的 `test_gfx_raster_gles2.c` 在实际 Qualcomm GLES 3.2 驱动上通过 1,464 个独立 case、12,736 项 packet 检查、37 次原子拒绝；生产 M3G 入口接受 48 次 GPU 调用、Mascot 2 次，M3G fixture 另通过 316,194 项检查。含 ADB 启动与输出收集的单次执行约 1.28 秒；不作为性能基准。
- 当前 Android 包装层连接真实 EGL/GLES 后，通过保留的二维生命周期检查和三维独立颜色/深度 oracle：GPU DONE 19、原子 DECLINE 12、foreign-thread 1，提交后 reset 保留正确 DONE 和 `*drawn`。本地测试副本的 property/log 函数使用隐藏的私有名称，不干扰厂商驱动的系统函数；故障/reset 为测试注入，不表示观察到自然设备故障。该次执行约 0.27 秒。
- 本轮只验证离屏原生后端、生产 M3G/Mascot API 调用及包装层，不代表真机完整 APK/Java 游戏界面、音频、长时间运行或性能已验证。测试构建保留 `NJ_ENABLE_AMR=0`、`NJ_ENABLE_SYSTEM_AMR=0`；没有因此修改生产解码器。

这些结果证明受支持子集的输出与回退契约，不代表全部商业三维游戏已经加速，也没有建立性能提升结论。

## 2026-11-13/14：三角形批量路径（可选同步事务）

二维 raster 之外，本轮加入了一个**独立的**可选三角形批量契约，用于把一次
`Graphics3D.render(VertexBuffer, IndexBuffer, Appearance, Transform)` 的
全部三角形在一次回调里交给后端，而不是按屏幕条带多次回调：

- 公开头文件 [nj_gfx_triangles.h](../include/nj2me/nj_gfx_triangles.h)，校验器
  [nj_gfx_triangles.c](../src/gfx/nj_gfx_triangles.c)。接口是同步事务：
  `nj_gfx_triangles_fn(ud, packet, &submitted)` 返回 `DONE` 表示 packet 覆盖的
  全部 CPU 颜色、深度和 `*submitted` 都已提交；返回其它值表示未写调用方任何
  字节，调用方必须重跑原软件路径（`jr_draw` 的 `jr_draw_walk`）。后端没有
  新增 GL 类型进入核心，也没有跨回调保留借用指针。
- 深度契约沿用二维 raster 的 24 位键：`q = round(clamp(z)*16777215)`、
  `key = 16777216 - q`，fragment 深度为 `q/M`。原生驱动的 half-tie 可能造成
  1 个键的差异，测试按此放行。
- 后端 `src/port/gles2/nj_gfx_triangles_gles2.inc` 要求 `GL_EXT_frag_depth`、
  `GL_OES_depth24` 和 ≥23 位 fragment highp；不满足时整个路径 SKIP，保留软件。
- `tests/test_gfx_triangles_gles2.c` 在 4 个真实 context 上通过 1,443 个独立
  case、2,732,649 项检查、26 次原子拒绝（NVIDIA ES3.2 与桌面 GL4.6、Mesa
  强制 ES2 与 GL2.1/GLSL 120）；覆盖颜色误差 ≤1、D24 边界、纹理组合、
  雾/混合/alpha/深度写、别名、读回失败、context 丢失和双实例。Mesa ES2 下
  ASan/UBSan/泄漏检查通过。
- Windows 构建（MinGW x64）在 Wine + NVIDIA GL 4.6 下以 `--graphics gles2`
  运行 `maze.jar`：`INFO: gfx: SDL2 GLES2 enabled; renderer=NVIDIA GeForce
  RTX 5080 Laptop GPU/PCIe/SSE2; version=4.6.0 NVIDIA 615.78.08`，
  `fills=2964 blits=150 declines=0`，exit 0（日志
  `.local-tools/debug-runs/gfx-full-20261113/windows-audit/win-gles2-maze.log`）。
  三角形路径未在 Windows 上复验。
- SDL 包装层通过 2,797 项检查（软件/GPU 交错、复制回调、双实例、
  foreign/null context 恢复）；Android 包装层 host 真实 EGL 通过 121 个
  快照/22 次保留 GL 查询/8 种选择，提交后 reset 保留正确 DONE。

真实游戏证据（libretro 硬件 GL，当前源码构建，core SHA
`9099103538d18f8c4b051acdd20d3443104ed18fabee72eecb38d2a0d1003533`）：

- Minecraft（`tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar`，2400 帧硬件）：
  `triangles=1189`、`declines=226374`、2D `fills=619 blits=1821`，存档/读档
  1/1，上下文替换后 180 帧正常；画面为真实石地地形与 HUD。
- Bounce（2D）：`fills=6882 blits=347`，球/花/树画面正常，存档/读档 1/1。

性能结论（RTX 5080 Laptop，48 个真实 MC packet/6,432 三角形）：软件
1.173842 ms，GPU ROI 7.694929 ms（6.55×），GPU 全表面 32.089298 ms；仅
272 三角形的天空 packet GPU 更快（0.239307 vs 0.334504 ms）。二维 GPU 在
同一主机上明显慢于软件（1×1 fill 11.03 µs vs 0.20 µs；240×320 fill
200.75 µs vs 2.89 µs）。因此默认仍是软件渲染，**不宣称加速**；GPU 路径的
价值在于可选的硬件呈现能力和已验证的回退契约。

跨回调帧级批处理（journal）已实现（2026-11-14）：JSR-184 允许在
releaseTarget 前排队绘制，核心因此在 `src/midp/nj_lib_m3g.c` 持有有界
journal（最多 256 个 packet / 131,072 个三角形 / 16 MiB 纹理快照），把
bindTarget→releaseTarget 之间的多次 `Graphics3D.render` 合并成一次有序
list 事务。新契约 `nj_gfx_triangles_list`（`include/nj2me/nj_gfx_triangles.h`）
与单 packet 语义等价，但 CPU 颜色/深度只在最后一个 packet 结束时权威；
DONE 原子提交全部 packet，任何 DECLINE 不写调用方缓冲、调用方可按原顺序
软件重放。GLES2 后端一次导入并集 ROI 颜色/深度键、一次 seed、一次纹理上传、
一次颜色+深度回读、一次提交；每个 packet 仍按自己的 viewport 裁剪绘制。
核心在 bindTarget/releaseTarget/setViewport/clear、`Graphics` 2D 加载、
`present`、`Image.getRGB`/`getPixels` 前刷新，保证软件路径与 Java 观察顺序
一致；纹理在入队时快照，避免借用指针失效。

测量（SDL2 桌面，RTX 5080 Laptop，同一 MC 存档 600 帧，不锁帧）：软件
8.67 s；单 packet GPU 13.89 s；journal GPU 9.85 s（比单 packet 快 1.57×，
事务数 8,640→~400，约 24 packet/事务、1,900 三角形/事务），但仍比软件慢
约 14%。结论仍是“GPU 可选/实验性”，不宣称对 MC 加速。视觉对照
`shot-off.png`/`final-on.png` 与后端 list-vs-single 像素+深度全等测试一致。

libretro 决定：不接入 list 钩子。libretro 的硬件门只在 `retro_run` 内打开
约 2 ms（`LR_HW_BUDGET_MS`），VM 线程在门关闭时提交会被立即 DECLINE；
400 帧实测 93,000 次 list 提交中 78,222 次因门关闭被拒，仅 2 次被接受。
真正的修复需要前端节奏化提交（超出本端口能力），故 libretro 保持单 packet
路径并在 `lr_port_init` 注明原因；SDL/Android 为同线程同步 GL，启用 journal。
日志：`.local-tools/debug-runs/gfx-full-20261113/sdl-mc-journal/`。

测试：`tests/test_m3g_triangles.c` 新增 journal 用例（两次 render 合并为一个
list、barrier 拆分、DONE 计数与不重放、DECLINE 按序回退软件），全套
584,740 项检查通过；`tests/test_gfx_triangles_gles2.c` 增加 list 与逐 packet
的像素+深度全等（1,458 用例/2,759,081 检查）。

Android 说明：API 21 模拟器的 SwiftShader/host 翻译层不提供
`GL_EXT_frag_depth`，APK 内三角形路径 SKIP（`SKIP native triangles:
frag_depth=0 depth24=1 highp=23`）。journal 在该环境按契约 DECLINE 后逐
packet 回退软件重放，MC 真实地形/水/云/HUD 画面正确（截图
`android/final-build/shot-mc-journal4.png`），Maze 二维 `fills=2964
blits=940 declines=0`。要在真机启用三维批量需要 ES3 核心 `gl_FragDepth`
的 ESSL 300 变体，本轮未实现。

## 为什么 GPU 路径反而更慢（2026-11-14 实测拆解）

用户问"为什么用 GPU 加速反而更慢"，为此在
`src/port/gles2/nj_gfx_triangles_gles2.inc` 加了默认关闭的开发用 profiler
（`-DNJ_GLES2_PROFILE=1` 打开，默认编译为空），在 SDL2 + RTX 5080 上用同一
MC 存档复放 600 帧，统计每个事务各阶段耗时（800 个事务的累计值）：

| 阶段 | 每事务 | 占比 | 说明 |
|---|---:|---:|---|
| 颜色回读 `glReadPixels` | 210 µs | 23% | 同步等待 GPU 完成 |
| 存储/FBO 准备 | 138 µs | 15% | renderbuffer/FBO 挂接、完整性检查、`glGetError` |
| seed 全屏 quad | 89 µs | 10% | 把 CPU 颜色+深度灌进 FBO |
| CPU 提交（`nj_surface_set` 逐像素） | 87 µs | 9% | 把并集 ROI 写回 Java 像素数组 |
| 深度键回读 | 79 µs | 9% | 第二次同步 `glReadPixels` |
| 深度打包+上传 | 73 µs | 8% | 每像素打包成大端 RGBA |
| 纹理/FBO 重建 | 78 µs | 8% | ROI 尺寸变化时的 `TexImage2D`/`RenderbufferStorage` |
| 颜色导入+上传 | 57 µs | 6% | 逐像素 `nj_surface_get` |
| **实际绘制三角形** | **48 µs** | **5%** | 两个 pass（颜色+深度） |
| 纹理上传/校验等 | 32 µs | 4% | |
| **合计** | **917 µs** | 100% | 平均 3.1 万像素、1,548 个三角形 |

结论：**一次事务 95% 的时间不是画三角形，而是 CPU↔GPU 的往返与逐像素
转换**。原因是当前契约要求 DONE 返回后 CPU 颜色和深度立即权威（Java 随时
可能读/改目标像素），所以每次 flush 都必须：上传整块 ROI 颜色 → 打包上传
整块 ROI 深度 → seed 全屏 → 绘制 → 两次同步 `glReadPixels` → 把整块 ROI
逐像素写回。`glReadPixels` 是管线同步点，CPU 必须等 GPU 完成，无法重叠。
ROI 是保守包围盒：平均每事务 3.1 万像素被完整遍历 4 次，而软件光栅器只碰
三角形覆盖到的像素。

批量合并已经把事务数从 8,640 降到 ~1,200（约 7×），这是 GPU 路径从
13.89 s 改善到 9.85 s 的原因；但每帧仍有约 2 次事务，固定成本约
1.8 ms/帧，大于这些小块三角形在软件里光栅化的成本（软件整帧约 7 ms），
所以仍比纯软件慢约 20%。只有覆盖率高的大批次才划算：实测 MC 的 272 三角形
天空 packet GPU 0.239 ms vs 软件 0.335 ms，而 2 三角形/12 像素的小批次
仍要 ~72 µs。

另有一个独立的负收益项：可选的 2D fill/blit 后端同样是同步事务
（上传+绘制+回读），实测对 1×1 填充 11 µs vs 软件 0.20 µs、整屏 240×320
填充 201 µs vs 2.9 µs、整屏 RGB888→RGB565 blit 320 µs vs 67 µs，即**任何
尺寸都更慢**。Maze 300 帧实测 GPU-2D 1.52 s vs 软件 1.33 s。因此 SDL2 和
Android 端口不再默认安装 2D 加速回调（`out->gfx_accel`），需要实验时用
`NJ_SDL2_GLES2_2D=1` 环境变量显式打开；三维三角形 list 路径不受影响。
关掉 2D 后 Maze 的 `--graphics gles2` 与软件同速（1.34 s vs 1.36 s）。

要把 GPU 路径真正做到比软件快，需要改契约或后端架构，而不是继续调参数：

1. 持久 GPU 会话：FBO/深度/目标纹理在两次屏障之间常驻 GPU，屏障（2D 绘制、
   `Image.getRGB`、存档、present）才回读+提交，消除每次事务的导入/seed/回读；
2. 异步回读：PBO + fence，允许回读与下一次 CPU 工作重叠；
3. 只提交被三角形覆盖的像素（逐三角形包围盒），而不是整块并集 ROI；
4. 成本模型：只对覆盖率高/三角形多的事务走 GPU，其余保持软件。

以上都未在本轮实现；当前默认仍是纯软件渲染，GPU 为可选实验特性。
profiler 代码默认关闭（`NJ_GLES2_PROFILE=0`），只用于后续性能分析。
