# 帧率与高清画质路线图（2026-11-28）

起因：用户要求「继续优化速度、把帧率做到最高、素材更清晰」，并问
「现在是解释执行吗、能否用硬件指令加速、能否提前 AOT 编译」。

这份笔记只记录**本轮之前已经量化过的事实**，以及据此排出的下一阶段工作。
所有数字都来自本仓库文档或本会话的实测，来源列在每节末尾。

## 1 执行引擎：现在是解释器，JIT 是可选构建

- 唯一执行入口是 `nj_exec()`（`src/core/nj_engine.c`），默认转发到可移植
  解释器 `nj_interp_run()`（`src/core/nj_interp.c:466`）。发行版 APK 与桌面
  CLI 都是这个路径。
- 可选 baseline JIT：`CMakeLists.txt:185` 的 `NJ_ENABLE_JIT=CACHE STRING`
  默认 **0**，且 `tools/build_android.sh:212` 明确写着 "Optional baseline JIT
  (default off)"，需要 `-DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1` 才启用。
- JIT 覆盖面：小端 RV32（ILP32）、AArch64 LP64、System V x86-64 LP64(+SSE2)；
  Windows x64 仍走解释器；没有运行时 CPU 探测、没有持久代码缓存、没有方法级
  优化器或寄存器分配，只有 per-VM 有界热块缓存
  （`src/core/nj_jit.c`、`nj_jit_x86_64.c`、`nj_jit_arm64.c`、`nj_jit_rv32.c`）。
- 实测结论：JIT 不是「没做」，而是**对游戏普遍没有优势**。
  - 早期固定 CPU 负载：端到端 9.856 s → 15.119 s（+53.4% 更慢），
    原生覆盖仅 18.9%（`docs/JIT_PERFORMANCE.md`）。
  - field-cache 修复后，同一负载 fresh-VM 两轮 13.28 s → 2.92 s（4.55×，
    解释器 8.68 s），原生字节码覆盖 98.92%（`docs/JIT_CACHE_LOOKUP.md`）。
  - 但两款 2D 游戏仍不如解释器：Bounce Tales 2.0.3 JIT 16.31 fps vs 解释器
    17.33/17.46 fps；ACH1945 Lite JIT 12.86 vs 13.42/13.87 fps
    （`docs/JIT_GAME_CACHE.md`，该文档明确写 "does not establish a general
    advantage over the interpreter, or a 3D speedup"）。
- 在当前 3D 工作负载里，VM 只占很小一块：pixel-art 常驻 8× Doom3 的 gprof
  中 `nj_run` 约 4.4%（`hd_art_span` 占 54%）。

结论：把「解释器换成 AOT/JIT」放到优先级后面。值得做的是：（a）用 CLI
在 CPU 密集型语料上重新测一轮 JIT（有覆盖率高时的 4.55× 潜力）；（b）如果
确实有受益标题，再考虑 Android 侧 opt-in + 自适应；（c）持久代码缓存/AOT
是独立大工程，收益只在 VM 占比高的标题上体现，先有测量再决定。

## 2 帧时间都花在哪（已有数据）

桌面 CLI（Vulkan、8×、`--resident`、Doom3 场内 400 帧，`--deterministic`）：

- 墙钟约 12.3 s / 400 帧 ≈ **30.8 ms/帧**（虚拟时钟 13.2 s，说明没有真实
  sleep，时间都花在执行上）。
- `[vkprof]`：619 次导出合计 3.1 s ≈ **7.8 ms/帧**（GPU→staging 回读，
  zero-copy 下 CPU 侧拷贝为 0）；三角提交 1.0 s、清屏 0.34 s、纹理 quad 0.33 s。
- gprof CPU 合计约 9.4 ms/帧：`hd_art_span` 54%（像素画 2D 放大）、
  `nj_region_draw` 8.3%、`sw_span` 6.7%、`jr_hd_target` 4.4%、
  `nj_src_pixel` 2.8%、`nj_run` 2.2%（240 帧样本）。
- 桌面窗口呈现另算：`docs/notes/2026-11-27-desktop-present-stats.md` 实测
  mode 5 下 draw 4.3 ms / swap 1.1 ms，成本是全幅 `SDL_UpdateTexture` 的上传。

Android 模拟器（JBenchmark3D、8×、present stats 温暖窗口）：

- draw 3.3–3.8 ms、swap 5.8–7.5 ms、frame **24–29 ms** ⇒ 约 15–20 ms 在
  CPU 游戏+2D 合成+整帧回读/上传；`--resident` 开/关在同一区间
  （`.local-tools/debug-runs/avd-resident-20261128/`）。

结论：两个平台的大头都是「每帧把 19.66 MB 的 8× 画面搬来搬去 + CPU 2D 放大」，
而不是 VM。

## 3 下一阶段杠杆（按收益排序）

### L1 Vulkan 直接呈现（M4 的替代路线，不需要 AHB）

现状：Vulkan 渲染器只做离屏渲染，结果回读到 CPU，再由 GL/SDL 上传呈现。
每帧两趟 19.66 MB（GPU→CPU 回读、CPU→GPU 上传）。Android 上 AHB 互操作
在 gfxstream 模拟器里会崩（`docs/notes/2026-11-28-gpu-2d-composition-plan.md`
§8.6），但 **VkSwapchainKHR 在模拟器上可用**——因此不依赖 AHB 也能去掉这两趟。

- Android：`vkCreateAndroidSurfaceKHR` + swapchain；渲染器直接用原生窗口呈现
  resident mirror（可带 FSR1/nearest 放大与色调映射）。
- 桌面 SDL2：`SDL_Vulkan_CreateSurface` + swapchain，替代 `SDL_UpdateTexture`。
- 校验：AVD `present stats` 的 swap/frame 应明显下降；桌面 `--stats` 与
  确定性闸门不变；回退路径保留（window 变化/创建失败仍走原 GL 呈现）。

### L2 2D 合成上 GPU（M3.2 起）

现状：`hd_art_span` 是 pixel-art 8× 放大的 CPU 热点（桌面约 5 ms/帧，
移动端占比更高）。M3.1 已提供 region import/export（0.18 ms 的小矩形导出），
下一步是把 drawImage/drawRegion/fillRect/glyph 变成 resident mirror 上的
quad，并按帧批处理（≤1 次颜色 import + ≤1 次 export）。

### L3 画质：更清晰的放大与抗锯齿

- 现状选项：pixel-art（最近邻＋像素块重建）、filter（平滑）、fsr1（锐利，
  CPU Catmull-Rom+RCAS 风格）；`--ssaa 2` 已存在但 8× 下代价大。
- 候选（按代价排序）：
  1. GPU 后处理：在 resident mirror 导出/呈现前加 FXAA/SMAA 或 RCAS 锐化
     pass（一次全屏 fragment shader，成本 <1 ms）——改善 3D 边缘的「糊/锯齿」。
  2. 新的 2D 放大算法选项：MMPX/xBR-lite 之类边缘保持像素画放大，比
     FSR1 的三次核更「锐」；作为第四个 asset filter 选项，保留旧项作对照。
  3. GPU 版 FSR1 素材放大：把 CPU 的三次核+clamp 移植到 fragment shader，
     速度换画质（可外加可调锐度）。
- 需要用户确认「模糊」指哪一类：2D 素材、3D 贴图、还是文字/UI——三者
  对应的优化不同。

### L4 引擎（JIT/AOT）

先做测量再投入：用 CLI 在 CPU 密集型语料上跑解释器 vs JIT 两套构建
（`-DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1`），确认是否有标题能达到
`docs/JIT_CACHE_LOOKUP.md` 的 4.55× 量级；若有，再讨论 Android opt-in、
启动预编译（提前编译热方法）与持久代码缓存（伪 AOT）。

## 4 里程碑与闸门

- L1a：Android Vulkan swapchain 呈现（opt-in），AVD present stats 记录前后
  对比；闸门：In-Game 截图无回归、`--deterministic` 稀疏 dump 一致。
- L1b：桌面 SDL2 Vulkan 呈现（opt-in），`--stats` 计时对比。
- L2：GPU 2D quad（图像绘制首期），CPU 热点 `hd_art_span` 占比下降；
  每步跑 8× 逐位闸门与截图对照。
- L3a：resident 后处理（FXAA 或 RCAS），截图/差值对照 + 成本 <1 ms。
- L3b：新 2D 放大选项（MMPX/xBR-lite 或 GPU FSR1），与 pixel-art/fsr1 并排对照。
- L4：JIT 语料测量报告（是否值得后续 AOT）。

每步全量测试：236/236 + 222/222 + `check_no_statics` + `check_android_java`
+ `test_gfx_vulkan`；每步提交前跑确定性闸门（8× plain
`846057676cfed67a`/`c65baebe4730fcbe`/`dc5cb60109dd03cb`，resident
`846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`）。

## 5 风险

- Vulkan swapchain 需要设备扩展与窗口生命周期管理（旋转、后台、窗口重建），
  模拟器与真机差异要在 AVD 上先验证。
- GPU 2D 合成会改变像素路径，必须守住 8× 闸门与逐位一致性；ROP 语义
  （mask/xor/setPixels）优先保留 CPU 路径、逐步迁移。
- 画质改动必须同时给「旧选项保留」与并排对比截图，避免用户观感倒退。
- AOT/持久缓存涉及代码内存管理与安全（校验 jar/class 哈希），先做测量。

## 6 参考

- `docs/JIT.md`、`JIT_PERFORMANCE.md`、`JIT_CACHE_LOOKUP.md`、`JIT_GAME_CACHE.md`
- `docs/notes/2026-11-26-render-jit-performance.md`
- `docs/notes/2026-11-27-desktop-present-stats.md`
- `docs/notes/2026-11-28-gpu-2d-composition-plan.md`（M1–M3.1 已完成）
- `docs/notes/2026-11-28-pixel-art-scaling-profile.md`（CPU 2D 热点与负结果）
- `docs/notes/2026-11-27-fsr1-asset-sampling.md`（FSR1 及其向量化）
- `docs/notes/2026-11-27-continue-plan.md`（上一目标收尾）
