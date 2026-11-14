# 可选 GLES2 二维后端

项目提供一个纯 C 的 GLES 2.0 后端，位于 [nj_gfx_gles2.c](../src/port/gles2/nj_gfx_gles2.c)。它实现 [二维加速契约](GRAPHICS_ACCELERATION.md) 的填充和图像复制回调，GL 头文件和链接依赖留在移植层。核心默认仍使用完整软件路径。

SDL2 和标准 Android 前端已有可选绑定：分别创建专用离屏 context，并继续使用原有窗口、输入及软件显示缓冲。构建和运行都需显式开启；默认仍为软件绘制。其他前端可以按下方 API 自行绑定。另外提供 [M3G / Mascot 的常量颜色片段路径](GLES2_3D.md)：CPU 保留原覆盖及深度量化，GPU 执行受支持的不透明颜色与深度合并，其余图元继续软件。Vulkan 后端尚未实现。

## 构建

```sh
cmake -S . -B build-gles2 \
  -DNJ_ENABLE_GFX_ACCEL=1 -DNJ_BUILD_GLES2_ACCEL=ON
cmake --build build-gles2 --target nj2me_gles2 test_gfx_gles2
ctest --test-dir build-gles2 -R '^test_gfx_gles2$' --output-on-failure
```

`NJ_BUILD_GLES2_ACCEL` 默认 `OFF`。只有明确开启时才查找 GLES2 开发头文件和库、构建独立静态库 `nj2me_gles2`。核心绘图、VM 和 MIDP 实现不调用 GL；启用后，`nj2me` archive 内的 SDL 移植对象会引用 GLES 绑定所需符号。开启后必须同时设置 `NJ_ENABLE_GFX_ACCEL=1`；核心、调用方与后端必须使用一致的结构布局。

适配库本身不依赖 EGL，可以使用 EGL 或平台原生方式管理 context。测试额外需要 EGL 开发文件；缺少它们时 CMake 明确报告不构建 EGL 测试。测试若无法初始化 EGL/context 则返回 77，CTest 标记为跳过，不算成功运行像素验证。

在使用 Mesa 的无显示环境中，可以用 `EGL_PLATFORM=surfaceless`。GLVND 同时安装多个厂商驱动时，`LIBGL_ALWAYS_SOFTWARE=1` 不能强制其他厂商使用 Mesa，需按机器实际路径设置 `__EGL_VENDOR_LIBRARY_FILENAMES` 为 Mesa 的 vendor JSON；以测试打印的 `GL_RENDERER` 为准。

## SDL2 与 Android 前端

SDL2 使用上面的 CMake 开关构建完整程序，然后在启动前设置运行开关：

```sh
cmake --build build-gles2 --target nj2me-cli
NJ_SDL2_GFX=gles2 build-gles2/nj2me --port sdl2 game.jar
```

未设置变量或值不等于 `gles2` 时，不创建加速 context。开启后，每个 port 创建自己的隐藏 SDL GL 窗口和 context；回调切换到该 context，完成后恢复调用方的窗口/context。它不接管 SDL 的显示 renderer。日志中的 `gfx: SDL2 GLES2 enabled` 给出实际 renderer，退出时打印成功 fill/blit 和拒绝计数。SDL 无 GL 支持、线程不匹配、context 丢失或重置时，本次运行继续使用软件；不会反复尝试创建失败的 context。各实例按自己持有的 SDL 子系统引用释放资源。

标准 Android 使用显式构建开关：

```sh
tools/build_android_apk.sh --gles2 --abi arm64-v8a
adb install -r build/android/nj2me.apk
```

安装后在游戏库「⋮ → 显示设置」，或游戏内面板选择「GLES2 实验加速」。选择保存在应用设置中，默认关闭，在**下一次启动游戏**时生效；修改设置不重启本局或切换本局的 context。未包含 GLES2 的安装包会禁用该选项并说明使用软件绘制。游戏面板另显示本局实际启动选择、就绪/软件/出错回退状态，以及已完成的 GPU 填充、图像、三维调用和交回软件的次数；就绪且成功计数为零时明确显示「等待支持的绘制」。这些次数不代表全部绘图调用，也不是速度或 GPU 时间。

应用在创建本局时显式传入 0/1，因此应用设置优先于设备上遗留的 `debug.nj2me.gles2` property。移植层直接使用原 `nj_android_gles2_start`、未显式指定模式时，仍在启动时读取该 property 一次，供旧调用方兼容。正常 GLES 状态查询只复制受独立短锁保护的 CPU 快照，不读取 property、不调用 EGL，也不等待 GLES 绘图锁。

可用 `adb logcat -s nj2me` 检查 `gles2: enabled renderer=...` 和退出计数。Android 原生游戏线程创建 EGL 1×1 pbuffer/context、运行 VM 并在结束时销毁；UI 线程不操作 EGL。包装回调和 userdata 在 backend 关闭后仍有效，随后绘制会拒绝并走软件。context 初始化失败、丢失或重置后，本次运行永久回退，下一局重新初始化。极少见的 EGL 销毁失败会保留仍被资源引用的状态，避免释放后访问；音频仍会关闭。

旧机版 `tools/build_android_legacy.sh` 保持 Android 2.0 / ARMv5 软件实现，不引入 EGL/GLES 依赖。libretro 暂无本后端的自动绑定。

## 绑定与销毁

公开 API 见 [nj_gfx_gles2.h](../include/nj2me/nj_gfx_gles2.h)。平台负责创建专用 GLES2 context，并在 VM 的绘制线程上使它 current。`is_current` 必须验证**此实例所属的 context** 当前绑定且可用，不能仅检查“存在任意 GL context”。它不得调用 VM，必须在单次后端操作期间保持一致。

例如使用 EGL 的平台可以如此绑定：

```c
#include "nj2me/nj_port.h"
#include "nj2me/nj_gfx_gles2.h"
#include <EGL/egl.h>

typedef struct platform_graphics {
    EGLContext context;
    nj_gles2 *backend;
} platform_graphics;

static int graphics_current(void *ud)
{
    platform_graphics *p = (platform_graphics *)ud;
    return p->context != EGL_NO_CONTEXT &&
           eglGetCurrentContext() == p->context;
}

/* After the platform creates/makes current p->context on the render thread. */
static void graphics_attach(nj_port *port, platform_graphics *p)
{
    nj_gles2_config config = {graphics_current, p, 0};
    p->backend = nj_gles2_create(&config);
    port->gfx_accel = p->backend ? nj_gles2_backend() : NULL;
    port->gfx_accel_ud = p->backend;
}
```

独立 C 绘图可用 `nj_gfx_set_accel(g, nj_gles2_backend(), state)` 安装同一后端。每个实例保存自己的 GL 对象、缓冲及计数，无可写全局状态；不得从多个线程并发使用同一实例。

正常销毁顺序是停止绘制、解除全部 port / `nj_gfx` 引用、使原 context current，再调用 `nj_gles2_destroy`，最后销毁 context。返回 `0` 表示 context 不匹配，状态仍保留，不能丢弃指针；返回 `1` 才表示释放完成。`nj_gles2_destroy(NULL)` 是成功的空操作。

实际 context 丢失或销毁后，可用 `nj_gles2_abandon` 仅释放 CPU 状态；GL 资源必须由已销毁的 context 回收。不能把它当作正常 GL 清理。创建失败也可能因 context 中途失效而无法清理 GL 对象，平台应销毁失效 context，重新创建 context 和后端，再次绑定；创建失败不会泄漏后端的 CPU 状态。重建之前仍可继续软件绘制。

后端会改变 framebuffer、program、纹理、viewport、pixel-store 和其他 GL 状态，不恢复外部渲染器的状态，所以必须使用专用 context。平台负责线程归属、串行执行及 context 丢失处理。即使属于同一 share group，也不能用另一个 context 绘制或销毁此实例的 framebuffer。

## 当前支持范围

- 填充：RGB565、RGB888、ARGB8888 目标；`NJ_BLEND_NONE` 直接覆盖，或不透明颜色的 `NJ_BLEND_SRC_OVER`。
- 图像复制：三种源/目标格式的全部组合，全部八种旋转/镜像，平移、裁剪、源区域和正字节行距。
- `NJ_ROP_SET`：保留源值，global alpha 不参与混合；`NJ_ROP_OPAQUE`：global alpha 必须为 255，强制源 alpha 为 255；`NJ_ROP_PAINT`：global alpha 为 255 且所选源区域所有像素都不透明。
- 遮罩、XOR、部分透明混合、源目标存储重叠、超出预算/GL 限制、context 不可用，以及 GL 错误，均拒绝并交回软件路径。对 PAINT 的全区域不透明检查是保守选择，裁剪外存在透明像素也会拒绝。

创建需要 fragment `highp`，不足或 shader 编译/链接失败时返回 NULL。后端会检查纹理、viewport 和 framebuffer 限制，失败不算成功绘制。

`max_pixels=0` 默认每个源区域及可见目标区域上限为 1,048,576 像素。两个 CPU RGBA scratch buffer 各自不超过该上限，另有 GL 源纹理与目标纹理。缓冲可复用但不保存源内容有效性的假设；每次 blit 都重新读取当前 CPU 源，成功后才保留更新的 CPU 目标。

## 同步与回退

填充通过 GL clear，复制通过纹理采样和 quad 绘制，输出到实例私有 RGBA framebuffer。`glReadPixels` 完成且检查没有 GL 错误之后，才把结果转换回调用者目标格式；目标 stride 的 padding 和裁剪外像素保持不变。操作中途失败，私有 GL 存储可能已变化，但调用方 CPU 目标没有部分写入，软件路径可以直接执行。

GL 对象和 scratch 可以跨回调保留；传入的 `nj_gfx`、source、mask 和目标像素地址不会保留。软件绘制修改源或目标、图像移动或存档恢复不会导致按地址缓存的陈旧纹理。

`nj_gles2_get_stats` 提供成功 `fills`、`blits` 以及 `declines`，用于确认实际调度。它不是 GPU 时间或性能计数器。同步上传、逐操作读回及格式转换可能使该实现比软件更慢，不能由 GL 成功计数推导性能收益。

## 验证

[test_gfx_gles2.c](../tests/test_gfx_gles2.c) 使用真实 EGL pbuffer/context 和 GLES 调用，逐字节比较软件结果并检查成功/拒绝计数。测试覆盖格式、变换、奇数行距、裁剪、ROP、CPU/GL 交错、拒绝前不写入、源与 padding 保护、多实例和错误 context 生命周期。

2026-11-12 验证结果：

- NVIDIA GeForce RTX 5080 的 GLES 3.2，以及显式选择 Mesa 26.2.6 llvmpipe 的 GLES 3.2，均完成同一整套测试；Mesa 限制为 GLES 2.0 context 的整套测试也通过。
- 每次完整运行包含 2,571 次结果比较、43,411,331 项检查、41 次成功 fill、1,830 次成功 blit、506 次拒绝。Release 中的检查也保持启用。奇数行距的 SET 还与独立的正向变换/字节打包预期比较。
- Mesa 下整套测试通过 ASan / UBSan 和 leak 检测。EGL 无法初始化时的 exit 77 跳过行为已验证。
- 独立后端阶段的 CMake 配置通过 6 项图形相关 CTest；默认关闭后端的配置通过 5 项对应 CTest。该阶段核心 archive 不包含 GL / EGL / Vulkan 未解析符号，后端通过无可写全局状态检查。接入 SDL 后，开启配置的 SDL 移植对象会引用 GL；核心绘图、VM 和 MIDP 仍不调用 GL。
- 测试发现并修正软件参考 `nj_src_pixel` 对 ARGB 字节偏移除以 4 后向下取整的问题，使奇数字节行距按实际字节读取。当前公开 ARGB Image 路径仍使用 packed 行距。

前端接入后的追加验证：

- [test_sdl2_gles2.c](../tests/test_sdl2_gles2.c) 在 NVIDIA RTX 5080 与 Mesa llvmpipe 上各通过 2,220 项检查，观察到成功 fill 27、blit 36、拒绝 27。覆盖真实 VM Image/Graphics 的 GPU/软件交错、提前复制的 port、三轮双实例创建/退出、关闭一个实例后另一个继续绘制，以及外部 context、纹理、viewport、clear color 和无 context 绑定的恢复。视频驱动确实不可用时返回 77。
- SDL 实际运行 Maze，以同一 snapshot 回放相同按键，软件与 GLES2 的 45 帧显示 backbuffer 逐像素一致。1942 的菜单按键回放实际使用成功绘制与软件拒绝两种路径；未验证游戏完整通关。
- API 21 Android 模拟器的 SwiftShader 上安装了最终 ARMv7 / x86_64 测试 APK，确认 `enabled` 和非零成功计数。Maze 同一 snapshot 在 property 0 与 1 下恢复后的截图完全一致，恢复及后台返回后按键仍可移动角色，最终正常退出为 0。此为安卓模拟器验证，不等同于安卓真机 GPU 验证。
- 启用配置的 18 项图形、LCDUI、GameCanvas、存档、生命周期、M3G/Mascot 回归全部通过；默认软件配置的对应 16 项全部通过。SDL/Android 的初始化失败、错误线程、context/reset 失效与后续软件回退也经过单独验证；Android 真实 EGL 生命周期测试另通过 Mesa ASan/UBSan。
- 本次审查修复了 SDL 的全局退出影响其他实例、完整前端销毁丢失外部 current context，以及继承 no-error context 导致无法可靠检查 GL 错误的问题。专用 context 强制开启错误报告，并恢复调用方的创建属性。

补充 Android 15 真机原生测试：Qualcomm Adreno 725、GLES 3.2 上运行未修改的 `test_gfx_gles2.c`，通过同一完整矩阵：2,571 次比较、43,411,331 项检查，成功 fill 41、blit 1,830、拒绝 506。包含多 context、销毁与 CPU-only abandon、拒绝后软件回退及 CPU 像素一致性。这是独立原生后端验证；应用 UI、游戏和后台恢复的安卓证据仍来自上述 API 21 模拟器。该次设备随后断开，包装层故障注入未能运行。2026-11-13 重新连接同一真机后，独立原生包装层测试已通过保留的二维生命周期检查及新增三维检查（GPU DONE 19、原子 DECLINE 12、foreign-thread 1），也验证了提交后 reset 保留 DONE 和 `*drawn`。故障由测试注入，property/log 使用测试私有函数，不修改手机系统属性；仍不等同于完整 APK 的 UI/后台恢复真机验证。

2026-11-13 应用内设置与状态追加验证：

- 新增 [test_android_gles2.c](../tests/test_android_gles2.c)，在 Linux 的 GLES/EGL 可选构建中注册为 CTest。NVIDIA 与强制 GLES 2.0 的 Mesa 均通过 107 次快照检查、20 次持有 GL 锁时的跨线程查询及 8 组模式选择；确认 getter 无 GL/EGL/property 调用，显式模式覆盖 property、原 start 只读取一次 property、就绪零计数和普通拒绝不误报失败、停止后保留计数、提交后 reset 保留 DONE。原二维/三维生命周期 harness 也全部通过。
- 现代 GLES、现代纯软件、API5 legacy 的 JNI 对象均编译通过；后两者无 GL/EGL/加速后端依赖。现代 Java 编译与 D8 minAPI21 转换通过。
- API21 独立模拟器的真实应用 UI 验证普通包灰置；GLES 包默认关闭、选择重启后保留、显式选择优先于相反的 property；当前软件局中打开选择仍保持软件，当前 GPU 局中关闭选择仍保持 GPU，下一局才改变模式。Maze 实际移动与 HOME/返回后继续操作通过；M3G Java fixture 再次通过 148 项检查，记录 16 次 GPU raster。
- 本轮没有更新或操作用户正在使用的真机应用。模拟器使用只读临时覆盖，验证完毕关闭；没有以本轮模拟器结果追加真机 UI 或性能结论。

以上是二维阶段及应用设置的正确性验证，未包含安卓真机应用完整流程、商业游戏完整流程或性能基准。同步逐操作读回可能慢于软件实现，默认不开启。新增三维混合路径的支持范围、颜色/深度原子回退和独立验证见 [GLES2_3D.md](GLES2_3D.md)；完整 GPU 几何/纹理流水线和 Vulkan 尚未实现。
