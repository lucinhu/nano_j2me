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

## 7 本轮 JIT 实测（2026-11-28）

构建两份 CLI（软件光栅、1×、300 帧、`--deterministic`，同一按键序列）：

- 解释器：`/tmp/nj-new2-build/nj2me`（默认构建）
- JIT：`/tmp/nj-jit-build/nj2me`（`-DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1`，
  Release、无 Vulkan）

取 2 次运行的 USER CPU 最小值（同帧输出哈希除标注外一致）：

| 标题 | 解释器 | JIT | 变化 |
|---|---:|---:|---:|
| 傲世三国志－群雄争霸 | 5.48 s | 6.59 s | +20% 更慢 |
| 真幻想三国志完整版 | 3.96 s | 5.06 s | +28% 更慢 |
| 三国赤壁小乔秘史 | 3.71 s | 3.26 s | −12% 更快 |
| 暗黑启示录2－黑暗王朝 | 3.26 s | 2.59 s | −21% 更快 |
| CCTV 贺岁游戏-三国演义 | 2.21 s | 2.50 s | +13% 更慢 |
| 3D 激爆拳击 | 1.79 s | 2.61 s | +46% 更慢，且**像素不同**（`a58a7b30…` vs `1e8be96b…`） |
| 毁灭战士3（900 帧） | 1.304 s | 1.283 s | −1.6%，输出一致 |

结论：

1. 现有 baseline JIT 在 CPU 密集标题上仍是一半更快、一半更慢，和
   `docs/JIT_GAME_CACHE.md` 的历史结论一致；**没有普遍优势**。
2. 至少一款真实游戏（3D 激爆拳击）在 JIT 下输出像素不同，说明 JIT 仍有
   正确性缺陷；在修复之前不能作为发行默认。
3. 因此 AOT/持久代码缓存暂不立项：它会继承同一套块编译质量与正确性
   问题，收益上限也只是 CPU 密集标题的 VM 部分（当前 3D 工作负载里
   `nj_run` 约 4%）。先继续做图形管线（L1/L2/L3）。

## 8. L1 Android swapchain present（Round 2 进展，opt-in）

目标：Android 上把常驻镜面直接 present 到 Vulkan swapchain，省掉每帧 ENTIRE 帧回读 +
`SDL_UpdateTexture`/GL 纹理上传 + CPU 缩放（桌面 resident 8× 的 export ≈7.8 ms/帧，
Android 上的上传同量级）。

实现（`src/port/vulkan/`，`include/nj2me/nj_port.h` 新增 `hd_present_*` 钩子）：

- renderer 创建时枚举并启用 `VK_KHR_surface` + `VK_KHR_android_surface`（instance）与
  `VK_KHR_swapchain`（device）；`nj_vk_renderer_present_attach` 用 `ANativeWindow` 建
  surface/swapchain（镜像同格式优先，否则 RGBA8，要求 BLIT_DST；MAILBOX 否则 FIFO），
  `present_frame` 每帧 acquire → aspect-fit 居中 → `vkCmdBlitImage`（常驻镜面 →
  swapchain 图像）→ `vkQueuePresentKHR`。
- Android 端口在窗口就绪时 attach，在 `android_present_frame` 的 HD 帧上先走该钩子，
  成功则跳过 GL/CPU present。
- 本轮调试中修掉两个真实缺陷：
  1. **扩展名指针生命周期**：instance/device 的扩展名字符串指针声明在查找块内、
     却在块结束后才传给 `vkCreateInstance`/`vkCreateDevice`，栈槽复用导致 guest
     libvulkan `strcmp` 读到 `0x000000020000000f` 段错误。修复=把数组提升到函数作用域。
  2. swapchain 格式只接受与镜面完全相同格式（BGRA8），而 AVD surface 只提供 RGBA8。
     改为接受两种 8 位格式（blit 按分量映射）。

AVD（Medium_Phone_API_36.1，-gpu host，8× HD，Doom3 标题画面同阶段）：

| present 路径 | 帧率窗口（fps） |
|---|---|
| 旧 GL 上传 | 6.9 / 7.2 / 6.8 / 10.5 |
| Vulkan swapchain | 7.8 / 9.1 / 8.5 / 13.8 |

同阶段约 +13~31%；截图形状/颜色正确（居中 letterbox + 系统浮层）。

已知缺口：present 依赖常驻镜面；若首批 HD 帧在镜面创建前到达（JBenchmark3D 启动
阶段），钩子返回 0 后 Vulkan 已占用 ANativeWindow，旧 GL present 不可用，CPU 回退
约 2–3 s/帧（0.3–1 fps）。下一步需加「无 resident 时 CPU 帧上传 + blit」路径，或让
屏幕 HD 目标首帧即建立镜面；两条路径都验证后再考虑默认开启。当前保持 opt-in：
Android `debug.nj2me.vk_resident=1` + `debug.nj2me.vk_present=1` 才会启用，
默认走旧 present 路径。

补充（Round 2 后续）：为覆盖「常驻镜面尚未建立」的首批 HD 帧，`present_frame` 现在会用
`resident_ensure` 零拷贝接管 CPU 帧（像素直接落进 staging 缓冲），再用
`nj_vk_renderer_mirror_import_rect` 整帧导入镜面后 blit 呈现；只有非常规格式
（非 ARGB8888/4bpp/小于 16×16）才回退旧 present。该路径仍需在 AVD 上复测
JBenchmark3D 启动阶段帧率。

### 8.1 AVD 复核（Round 8）：adopt 路径可见，逐帧 CPU 导入会黑屏

用带 `present` 的 x86_64 APK 在 AVD（8× HD，`debug.nj2me.vk_resident=1`、
`debug.nj2me.vk_present=1`）上对比：

| 构建 | Doom3 截图（5s/10s/15s） | 结果 |
|---|---|---|
| `0ae35285`（仅 adopt 路径） | 133474 / 133474 / 307013 字节，内容不同 | **可见且逐帧变化** |
| 临时补丁：present 前若 `resident_cpu_touched` 则整帧 import | 三次均 77913 字节且哈希相同 | **黑屏（仅 UI 键盘）** |

结论：present 之前把 CPU 帧整帧 import 回镜面会破坏可见输出（原因未查明：布局/同步或
与 acquire 的顺序问题），该补丁已撤销。`0ae35285` 的 adopt 路径本身在 Doom3 上截图正常，
JBenchmark3D 启动阶段此前测到约 58 fps。

因此「Android 默认开启 resident+present」仍不能放开：纯 2D / HUD 帧需要在 GPU 侧合成
（L2/M3），或用脏矩形导入替代整帧导入后再验证。

## 8.6 CPU 帧按显示尺寸上传（present small）

### 问题

直呈路径下，2D 标题的 HD 帧（8× = 1920×2560，19.66 MB）每帧都要整帧上传到 GPU，
present stats 的 import 段实测 7–20 ms，是 2D 帧率（20–29 fps）的主要开销之一。
8.5 节的脏 tile 方案在该标题上不成立（逐帧全屏重绘），但现成的 `vkCmdBlitImage`
本来就是 **NEAREST 采样**把 1920×2560 缩到显示尺寸（1080×1440）：

- 原先：CPU 上传 19.66 MB → GPU NEAREST 缩到 1080×1440；
- 现在：CPU 先按同一采样格 NEAREST 缩到 1080×1440 → 上传 6.2 MB → GPU 1:1 blit。

因为采样格完全一致，画质不降；上传字节数降到约 1/3（按显示区域计）。

### 实现

- `src/port/vulkan/nj_gfx_vulkan.c`：renderer 新增 `present_small`（一个按显示矩形
  懒创建的 mirror）、`present_scratch` 与开关 `present_small_enabled`
  （`NJ_VK_PRESENT_SMALL=0` / `debug.nj2me.vk_present_small=0` 可关闭整帧上传）。
  `present_upload_small()` 做 NEAREST 收缩并 `nj_vk_renderer_mirror_import_rect()`；
  `nj_vk_renderer_present_frame()` 现在先算显示矩形，CPU 帧（无 resident 或
  `resident_cpu_touched`）优先走 small 路径，3D GPU 帧仍直接 blit 常驻镜面。
  关闭 small 或创建失败时回退到原来的整帧上传/首帧 adopt 路径。
- 仅 CPU 帧走 small；`resident` 常驻镜面、3D 三角、确定性闸门均不受影响。

### AVD 实测（Medium_Phone_API_36.1，-gpu host，8×，resident+present opt-in）

| 场景 | import | 窗口 fps |
|---|---|---|
| 2D《傲世三国志》（小标题） | 6.1–7.3 ms（原 1.9–11.3） | 27.8–30.0（原 26.8–29.4） |
| 2D 同上（复杂画面窗口） | 9.1–10.7 ms（原 11.3–20.0） | 25.4–27.1（原 19.4–19.6） |
| 3D Doom3 | 0.37 ms，small=2/60 | 不受影响（GPU 直画，无回传） |

截图（`/tmp/r14-sango.png`）确认 2D 标题文字与立绘清晰度与之前一致；Doom3 无回退。


## 9. 素材放大：新增 `fsr1-strong`（Round 9）

用户反馈「素材比较模糊」。现有 2D 高清素材采样选项为：原始像素 / 柔和平滑 / 像素画清晰 /
FSR1 锐利。FSR1 锐利的 RCAS 对比度强度写死在 Q8=96（0.375）；新增同一重建算法的
「FSR1 超锐」档，强度 Q8=160（0.625）：

- C：`HD_FSR1_SHARP_STRONG`、`hd_fsr1_bitmap(..., sharp)` 参数、`NJ_ASSET_FSR1_STRONG = 4`。
- CLI：`--asset-filter fsr1-strong`。
- Android 面板：「高清素材采样」新增「FSR1 超锐」（下标 4，下次启动生效）。
- 默认仍是「原始像素」，不改变任何现有用户的输出。

Doom3 8× resident 帧 120（1920×2560）对照：fsr1 vs fsr1-strong 平均通道差 0.225、
最大差 10、差>4 的像素 25,629（约 0.5%），高光边缘更硬，属局部锐化变化而非整体偏移；
pixel-art 基准帧 60 仍是 `846057676cfed67a`，未受影响。

### 8.2 CPU 帧导入的根因修复（Round 10）

Round 8 记录的「present 前整帧 import 变成黑屏」根因已找到：`mirror_import_rect()` 在
「整帧、不打包深度」时跳过 `transfer_rect_at()`，假设调用者的像素已经在这个渲染器的
staging buffer 里——但常驻镜面的零拷贝别名指向的是**镜面自己的** staging buffer，两者
不是同一块内存。于是整帧纯颜色导入把上一帧残留的 staging 内容上传成图像，2D 游戏因此
黑屏。修正为：只有调用者像素确实别名到本 staging buffer 时才跳过拷贝，否则一律拷入。

同时 present 在 `resident_cpu_touched` 时把 CPU 已绘制的帧上传一次再 blit：2D 游戏
每帧在 CPU 侧绘制，若不回传，swapchain 会一直显示首帧 adopt 的旧图。

AVD 实测（Medium_Phone_API_36.1，-gpu host，8×，resident+present opt-in）：
- 2D 游戏《傲世三国志-群雄争霸》：修复前黑屏（仅键盘），修复后标题画面/对话框正常显示，
  `present ok` 连续、帧率 20.8–29.7 fps（旧 GL/CPU 回退路径为 0.3–1 fps）。
- Doom3 8× 标题：`present ok` 连续，8.1–9.0 fps（与 Vulkan 直呈基线一致，无回归）。
- 帧率低于 GL 路径 ~32 fps 的原因是 2D 帧每帧 19.6 MB 的 CPU→GPU 上传；后续 L2
  （脏矩形/2D 上 GPU）可消除。

### 8.3 CPU 帧从镜面 staging 直接上传（Round 11）

2D 游戏 present 时 CPU 帧已通过零拷贝别名留在镜面自己的 staging buffer 里，而
`mirror_import_rect()` 仍先把整帧 memcpy 进渲染器的 staging 再上传。新增快速路径：
整帧、不打包深度、调用者像素正好是镜面 staging 时不拷贝，直接 `sync_buffer(m->stage)`
后用该 buffer 做 `vkCopyBufferToImage`（深度打包路径仍走原拷贝）。

AVD 实测（8×，resident+present）：2D《傲世三国志》稳定 21.8–22.3 fps（两次测量窗口），
标题阶段 33 fps；Doom3 8× 标题 8.2–17.2 fps（随场景变化）。相对未优化版本（20.8–29.7 fps）
处于同一量级，说明 22 fps 附近的天花板不在主机拷贝，而在「单帧 fence 等待 + 整帧上传 +
blit」的串行开销。下一步 L2：脏矩形导入（仅上传变化区域）/ 2D 图元上 GPU。

## 8.4 Present 阶段计时与 2D 上传瓶颈（Round 12）

给 Android Vulkan 直呈加了分阶段计时（`debug.nj2me.vk_present_stats=1` 或 env `NJ_VK_PRESENT_STATS=1`，每 60 帧打印一次 `vulkan: present stats`）。测量环境：AVD Medium_Phone_API_36.1（-gpu host，gfxstream→RTX 5080 Laptop），x86_64 APK，8× HD，resident+present opt-in。

| 场景 | import ms | blit ms | acquire ms | queue ms |
|---|---:|---:|---:|---:|
| 2D《傲世三国志》第 1 窗 | 1.88 | 0.49 | 0.66 | 0.18 |
| 同上，标题动画 | 1.92 | 0.71 | 0.76 | 0.16 |
| 同上，场景载入后 | 4.24 | 1.01 | 1.23 | 0.16 |
| 同上，复杂画面 | 11.34 | 1.81 | 2.53 | 0.17 |
| 同上，稳定大画面 | 14.18 | 1.93 | 2.89 | 0.17 |
| 3D Doom3 8× 标题 | 0.32 | 1.03 | 1.40 | 0.25 |

- 3D：present 总开销约 3 ms/帧，直呈本身不是瓶颈。
- 2D：import（每帧整帧 1920×2560×4 = 19.66 MB 的 staging→image 拷贝及其 fence 等待）从 1.9 ms 涨到 14 ms，是 2D 直呈从 ~60 fps 掉到 ~22 fps 的主因；gfxstream 的 buffer→image 路径比原生 GPU 明显更慢。
- 桌面（headless 无 present）对照同一 2D 游戏 300 帧 USER CPU：scale 1/2/4/8 = 5.47/5.82/6.28/8.17 s ⇒ 8× 高清 2D 光栅化约 9 ms/帧，其余是游戏逻辑；桌面不是瓶颈，Android 的整帧上传才是。
- 结论：下一步 L2 应优先做**脏矩形导入**（只上传变化区域）。2D SLG 的地图/对话框大多静态，理论上可把每帧上传从 19.6 MB 降到数百 KB，2D 直呈有望明显超过 30 fps；3D 的 HUD 帧同样受益（当前 HUD 一画就整帧回传）。

### 8.5 脏区域导入尝试（Round 13，未采纳）

在 present 上传 CPU 帧前，用 CPU 影子副本逐行比较出「变化的 64 行 tile」，只上传变化 tile（单次 submit、多段 buffer→image 拷贝；超过 3/4 tile 变化时退回整帧上传）。实测（AVD，8×，resident+present）：

- 2D《傲世三国志》：几乎每帧所有 tile 都变化（全屏滚动地图/动画），脏路径从不命中，退回整帧上传；逐行扫描额外消耗约 2–3 ms/帧，fps 反而略降（19.5–29.7 fps，与原 20–29.7 同区间但低端更差）。
- 3D Doom3：3D 画面由 GPU 直画常驻镜面，present 本身不上传（60 帧仅 2 次 upload，来自首帧 adopt）；脏路径无用武之地。

结论：脏区域导入对「逐帧全屏重绘」的 2D 标题无收益、且扫描有成本，已回退（未合并）。要真正降低 Android 端 2D 帧成本，只能走结构性方案：GPU 侧 2D 合成（L2），或对 2D 帧采用「低分辨率上传 + GPU 放大」的直呈路径（质量/性能权衡，可选开关）。
