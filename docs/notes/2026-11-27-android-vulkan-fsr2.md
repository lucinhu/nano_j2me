# Android Vulkan 3D 面片渲染与 FSR2 时域放大的实机路径验证

此前 `arm64` 增强包虽然包含 Vulkan 3D 面片渲染和 FSR2 时域放大，但没有验证过
Android 上的实际路径：常规 x86_64 模拟器没有对应的 FSR2 SDK，而增强包只按
`arm64-v8a` 构建。本轮补齐了 x86_64 的 FSR2 交叉编译，在 API 36 模拟器（gfxstream
Vulkan HAL 直通宿主机 RTX 5080 Laptop）上跑通，并修掉两个让这条路径长期不可用的
真实问题。

## 一、先让 x86_64 能构建 Vulkan + FSR2

- 用 NDK 30.0.16248370 的 `clang++` 按
  `--target=x86_64-linux-android28 -std=c++17 -fPIC -DNDEBUG -DFFX_GCC
  -fno-exceptions -stdlib=libc++` 编译 `ffx_assert.cpp`、`ffx_fsr2.cpp`、
  `vk/ffx_fsr2_vk.cpp`、`vk/shaders/ffx_fsr2_shaders_vk.cpp`。
- 直接复用 `arm64` 产物里生成的 SPIR-V 排列头（`generated/*_permutations.h`、
  `compat.hpp`）——着色器字节码与 CPU 架构无关，不需要在 x86_64 上重跑
  `glslangValidator`/`SPIRV-Tools`。
- 产物：`.local-tools/debug-runs/fsr2/x86_64/libffx_fsr2_vk.a`（2,674,030 B，4 个
  ELF x86-64 目标文件）以及同样布局的 `install/` 前缀（`include/`、`lib/`、
  `share/licenses/`）。
- `tools/build_android_apk.sh` 和 `tools/build_android.sh` 不再把 `--fsr2-sdk`
  限制在 `arm64-v8a`：前缀只提供一个架构的静态库，由调用方保证 ABI 配对，API 下限
  仍是 28。`tools/build_fsr2_sdk.py` 本身就能交叉编译：把 `CXX`/`AR` 指向
  `x86_64-linux-android28-clang++`/`llvm-ar` 即可得到同样大小（2,674,030 B）的前缀，
  步骤已写进 `src/port/fsr2/README.md`。
- 构建结果：`build/android-vulkan-x86_64/nj2me.apk` 4,323,720 B；`libnj2me.so`
  仍保持 `.data + .bss = 0`（Android 构建脚本的硬性检查），Vulkan 与 FSR2 都编进去。

## 二、问题 1：Vulkan 渲染器在 Android 上永远初始化失败

**现象**：在画面面板把「Vulkan 3D 面片渲染」设为请求启用，重启游戏后 logcat 只有

```
vulkan: physical device setup failed
W nj2me: Vulkan unavailable; continuing with previous renderer
```

**定位**：先在渲染器里加诊断日志，应用内得到 `vulkan: enumerate result=0 count=0`
——`vkEnumeratePhysicalDevices` 成功返回 0 个物理设备。再用 NDK 单独编译探头程序
放到 `/data/local/tmp` 对比：

| 探头构造 | 结果 |
| --- | --- |
| 显式 `VkApplicationInfo`，`apiVersion = VK_API_VERSION_1_0` | `enum count=1`，`Goldfish GFXStream (NVIDIA GeForce RTX 5080 Laptop GPU) type=2 api=1.3` |
| 先创建 EGL/GLES2 pbuffer 上下文再枚举 | 仍是 `count=1`，排除 GLES 共存问题 |
| `VkInstanceCreateInfo` 归零、`pApplicationInfo = NULL` | `enum count=0` |

**根因**：`src/port/vulkan/nj_gfx_vulkan.c` 用 `VK_INIT(INSTANCE_CREATE_INFO)` 清零
结构体后直接 `vkCreateInstance`，没有填 `pApplicationInfo`，`apiVersion` 为 0。桌面
驱动通常容忍这种写法，模拟器的 gfxstream/ranchu HAL 会因此枚举不到任何设备。

**修复**：在实例创建前准备栈上的 `VkApplicationInfo`：

```c
VkApplicationInfo app_info = VK_INIT(APPLICATION_INFO);
app_info.apiVersion = VK_API_VERSION_1_0;
app_info.pApplicationName = "nano_j2me";
app_info.applicationVersion = 1;
app_info.pEngineName = "nano_j2me";
app_info.engineVersion = 1;
instance.pApplicationInfo = &app_info;
```

不改动任何静态可写状态，`libnj2me.so` 的 `.data/.bss` 仍为 0。

**验证**：logcat

```
vulkan: enumerate result=0 count=1
vulkan: device 0 Goldfish GFXStream (NVIDIA GeForce RTX 5080 Laptop GPU) type=2 api=1.3
vulkan: renderer ready on Goldfish GFXStream (NVIDIA GeForce RTX 5080 Laptop GPU)
3D triangles: Vulkan ready
```

《毁灭战士 3》标题画面的 3D 背景正常渲染，`present stats` 记录 4× 物理输出
（960×1280）下 draw 中位数 0.40–1.03 ms、swap 中位数 13.1–14.3 ms。也就是说在
模拟器上，每帧成本主要落在窗口提交，而不是图层过滤。

第 9 轮给这里补了按呈现间隔统计的第三行 `present stats: frame ms`：`frame − draw − swap`
就是游戏自身的生产耗时。8× 高清下实测 draw 4.19 / swap 8.36 / frame 28.59 ms，
即约 16.0 ms/帧（约 35 fps）花在游戏与 2D 镜像生产，而不是 GL 上传——这把
"8× 为什么掉帧"从猜测变成了可归因的数字。桌面版有同名行与同一套读法，见
[桌面 present stats](2026-11-27-desktop-present-stats.md)。

## 三、问题 2：FSR2 时域历史被同一帧的第二次绑定重置

**现象**：打开 FSR2 后，逐帧日志显示每次 dispatch 都是 `reset=1`，时域历史每帧
丢弃，等于退化成空间放大。

**定位**：给 `m3g_temporal_bind` 加绑定日志后看到同一帧编号会绑定两次：

```
FSR2 bind target_changed=0 serial=295 prev_serial=294 active=0 prev_faces=1022 reset=1
FSR2 bind target_changed=0 serial=295 prev_serial=295 active=0 prev_faces=1022 reset=1
```

旧判定 `serial != s->serial + 1` 把同一帧的第二次绑定当成新流，因而再次重置。

**修复**：同一帧（`serial == s->serial`）的再次绑定继续复用历史：

```c
s->reset = s->active || !s->prev.count || target != s->target ||
           (serial != s->serial && serial != s->serial + 1) ||
           s->gc != vm->heap.gc_count;
```

目标变化、GC 换代、面片列表逐项比对失败时仍然重置。修复后所有 dispatch 都是
`reset=0`，连续两帧标题画面截图平均差 0.252/255（最大 54），与修复前构建相比平均
差 0.433/255，画面稳定、无鬼影。关掉 Vulkan 只留 GLES2 3D + FSR2 也能复现同一
双重绑定现象，说明这是 Android 端原有问题，不是 Vulkan 后端引入的。

## 四、诊断开关

- **Vulkan**：初始化失败时 logcat 会打印枚举结果、设备数量、每个候选设备的跳过原因
  （CPU/无 BGRA8 混合/格式或用法不支持/无图形队列）以及失败阶段与 `VkResult`。
- **FSR2**：Android 上

  ```
  adb shell setprop debug.nj2me.fsr2_trace 1
  ```

  之后会输出 `FSR2 bind ...`、`FSR2 context`、`FSR2 dispatched 240x320 -> 480x640
  triangles=1022 ...` 等行；桌面构建仍用环境变量 `NJ_FSR2_TRACE=1`。

## 五、验证环境与记录

- 模拟器：`Medium_Phone_API_36.1`，`-gpu host`，vulkan 驱动 `ranchu`（Vulkan 1.3，
  gfxstream 直通宿主机 RTX 5080 Laptop）。
- 游戏：《毁灭战士 3》，240×320 逻辑分辨率，HD 渲染倍率 2×，GL 呈现 FSR1，
  Vulkan 3D 面片渲染与 FSR2 时域放大均请求启用。
- 截图与日志在 `.local-tools/debug-runs/fsr-present-cost/android/`
  （`vk-settings.png`、`vx2-doom.png`、`fsr2on-title.png`、`r2-smoke-doom3-steady.png`
  等）；本轮用官方构建器产出的 x86_64 SDK 前缀重建 APK 后复测同样的日志。
- 测试：增强构建 236/236、软件构建 222/222（模拟器同时运行时 libretro 存档用例会因
  4 秒超时偶发失败，关掉模拟器重跑即全部通过）。
- 本轮修改的代码：`src/port/vulkan/nj_gfx_vulkan.c`（实例创建与诊断）、
  `src/midp/nj_m3g_temporal.inc`（平台无关的 FSR2 跟踪与同帧绑定判定）、
  `tools/build_android_apk.sh`、`tools/build_android.sh`（FSR2 SDK 的 ABI 放开）。

## 六、游戏内 3D 验证与 FSR2 / 高清倍率的配对

前两轮只在《毁灭战士 3》标题画面上拿到 FSR2 证据：那一段是持续动画的 3D 背景，但
游戏没有可用的输入回放，进不了正式关卡；《田园物语》是 2D，整局都不会绑定 FSR2。
本轮换用 `tests/games/JBenchmark3D.jar`：把它推到
`/sdcard/Android/data/com.lucinhu.nj2me/files/jars/jb3d.jar`（旁挂同名
`jb3d.jar.cfg`，`title=JBenchmark3D`、240×320），进库点开，欢迎页按软左键
（`adb shell input keyevent 82`，即 MENU）即开始，之后是自动播放的旋转 3D 场景，
无需任何导航。这是可重复的游戏内 3D 素材，日志、截图见
`.local-tools/debug-runs/fsr-round3/`。

**问题 3：高清倍率超过 3× 时 FSR2 静默失效。** HD 4× 下 logcat 只有一行
`FSR2 context 240x320 -> 960x1280: Unsupported FSR2 input/output dimensions`，
之后没有任何 `FSR2 dispatched`。原因是 AMD FSR2 2.2 的 Ultra Performance 档最多
每个轴 3× 重建（`nj_fsr2_create` 也按 `ow <= 3 * rw` 收口），而高清倍率 4× 会让
240×320 的时域输入对应 960×1280 的输出，比例 4 直接被拒；`m3g_temporal_bind`
随后返回，3D 悄悄退回纯空间渲染。更糟的是画面面板仍显示「FSR2 时域放大：请求
启用」，两种状态不一致。

**修复（画面面板 + 原生报错）。** `GameActivity.java` 的 `addHdChoice` 现在把两个
设置当成一对维护：打开 FSR2 时，若高清倍率不是 2× 也不是 3×，自动改成 **3×**；
反过来把倍率选成 1× 或 4×–8× 时自动关闭 FSR2，并提示「FSR2 时域放大需要 2× 或 3×
高清渲染，已关闭 FSR2」。选 3× 而不是 2× 的依据见
[4×–8× 时域放大链的取舍](2026-11-27-temporal-chain-at-high-scales.md)：在理想 8×
真值下 3× 时域比 2× 近 4–21%，代价约 +0.3 ms。`src/port/fsr2/nj_fsr2_vk.cpp` 的失败
信息带上实际尺寸和「at most 3x per axis」的限制，logcat 里能直接看出是配对问题
而不是驱动缺失。

**实测矩阵**（AVD `Medium_Phone_API_36.1`、`-gpu host`、窗口 1080×2400、GL 呈现
FSR1、JBenchmark3D 连续 3D、Vulkan 面片渲染有效）：

| 高清倍率 | FSR2 | 时域 dispatch | 呈现绘制中位 | 交换中位 | PSS |
| --- | --- | --- | --- | --- | --- |
| 2× | 请求启用（有效） | `240x320 -> 480x640`，1443 次，仅 2 次 reset | 0.39 ms | 15.4 ms | 181 MB |
| 3× | 请求启用（有效） | `240x320 -> 720x960`，1383 次，仅 2 次 reset | 0.66–0.85 ms | 14.5–14.8 ms | 181 MB |
| 4× | 自动关闭 | 无 | 0.91–1.17 ms | 13.4–13.8 ms | 187 MB |
| 8× | 自动关闭 | 无 | 3.09–3.36 ms | 5.5–5.7 ms | 278 MB |

2× 与 3× 的 reset 次数都只有启动时的 2 次，之后连续 dispatch 全部 `reset=0`、phase
逐个递增，说明时域历史在游戏内连续 3D 下稳定工作。8× 的交换耗时反而更低，是因为
1920×2560 的高清镜像让帧率掉到约 52 fps（120 帧约 2.3 秒），而 2×/3× 是满 60 fps
（120 帧约 2.0 秒）；8× 期间没有 FATAL/OutOfMemory，Vulkan 面片照常提交，只是没有
时域放大。面板配对也逐项点验过：8× + FSR2 打开时把倍率改成 4×，FSR2 立刻变「关闭」；
从 8× 直接打开 FSR2，倍率会自动回到 3×；2×/3× 可以保留 FSR2 打开。

## 七、遗留

- 游戏在启动和场景切换时会在同一帧编号内绑定/释放同一目标两次，因此可能出现一帧两次
  FSR2 dispatch；修复后这类重复绑定不再清空历史。稳态每帧只有一次 dispatch（本次
  记录中 312 次连续 dispatch 的 phase 逐个递增，且全部 `reset=0`）。后续若要压掉切换
  时的重复 dispatch，可按 pass 分别保存历史，或只在每帧最后一次 `releaseTarget` 上
  dispatch。
- 没有 arm64 真机可用，`arm64` 的 Vulkan + FSR2 只做了构建与桌面链路验证，本轮新增
  的实例参数修复对真实驱动同样成立，但仍需真机复验。
- 4×–8× 的高清倍率只能纯空间放大。第 7 轮把"FSR2 重建到 3× 再放大进 4×–8×
  镜像"的链做了同帧真值量化：链的 3D 像素等同于直接用 3× + FSR2（比原生 3× 好
  0–21%），但仍不如原生 8×（8× 到窗口 MAE 1.72/SSIM 0.9992），却要多付 8× 镜像的
  上传（桌面约 +2 ms/帧，Android 8× 已掉到约 52 fps）。结论是不实现，见
  [4×–8× 时域放大链的取舍](2026-11-27-temporal-chain-at-high-scales.md)。
- `JBenchmark3D` 现在是游戏内 3D 的可重复素材：欢迎页用 MENU 键启动，场景自动播放，
  不需要点击或输入回放；后续验证 Vulkan 面片与 FSR2 时优先用它，而不是《毁灭战士 3》
  的标题画面。
