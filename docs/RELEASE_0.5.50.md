# nano_j2me 0.5.50

这版为高清素材增加「像素画清晰」和「柔和平滑」两种采样，并优化软件绘制、Java 字节码执行以及 GLES2 和 Vulkan 的像素传输。像素画模式用素材原色修整连续斜边，柔和平滑则按透明度插值，适合渐变插画。Android 在游戏内打开「≡」→「画面」即可选择采样和高清倍率，重新启动游戏后生效。[画质设置与同帧对比](https://github.com/lucinhu/nano_j2me/blob/v0.5.50/docs/UPSCALING.md)

## 下载与升级

| 设备 | 下载 | 要求 |
| --- | --- | --- |
| 常见 64 位 ARM 手机 | [Android arm64 标准版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-arm64-v8a.apk) | Android 5.0+，软件或 GLES2，默认解释器 |
| 64 位 ARM 手机，需要 JIT、Vulkan 或 AMD FSR2 | [Android arm64 增强版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-arm64-v8a-vulkan-fsr2.apk) | Android 9.0+，FSR2 需要支持 Vulkan 1.2 的设备和兼容的 M3G 场景 |
| 其他 Android 设备及模拟器 | [ARMv7](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-armeabi-v7a.apk) · [x86](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-x86.apk) · [x86_64](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-x86_64.apk) | Android 5.0+，选择对应架构 |
| 早期 ARM 设备 | [Android legacy](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-legacy.apk) | Android API 5+，ARMv5 |
| RetroArch / libretro | [八平台核心](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/nj2me-0.5.50-libretro-cores.zip) | Linux x86_64、AArch64、ARMv7，Android arm64、ARMv7、x86_64，Windows x86、x86_64 |

六个 APK 的 `versionCode` 均为 550，签名证书与 v0.5.49 对应发行包一致，支持直接覆盖安装。下载全部发行包后，可在同一目录运行 `sha256sum -c SHA256SUMS-0.5.50.txt`。[SHA-256 校验清单](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.50/SHA256SUMS-0.5.50.txt)

## 性能

本次发布前的两项优化分别以开发版 `2b8c07a` 为基线单独测量。高清平滑绘制遇到相同且完全不透明的相邻源行时，一次插值可供多个输出行复用。解释器合并了时间片结束、取指越界和栈溢出的处理代码。GCC 16.2.1 的 x86-64 Release 默认构建中，`nj_interp_run` 从 82,782 字节减至 60,675 字节。

| 固定工作量 | 优化前 | 优化后 | 耗时降低 |
| --- | ---: | ---: | ---: |
| 《3D版田园物语》，3× 平滑，1,080 帧 | 4.113 秒 | 3.965 秒 | 3.6% |
| 《恋爱物语－我的机器女友》，3× 平滑，900 帧 | 2.444 秒 | 2.059 秒 | 15.7% |
| Minecraft J2ME 生成 256 个区块，默认解释器 | 9.154 秒 | 8.864 秒 | 3.2% |

两款 2D 游戏使用相同的 240×320 画布、固定游戏时钟和按键，固定同一 CPU 核心交替测量五对，取中位数。Minecraft 使用种子 `12345`，在另一固定核心上按模式交替测量三对。在编入 JIT 的构建中，解释模式耗时增加 0.56%，JIT 执行耗时降低 0.70%。18 次运行的 256 个区块摘要全部相同。上述耗时来自 Linux x86-64 桌面。

从 v0.5.49 升级还包括精灵透明混合、四采样抗锯齿合成、JIT 分支合流和编译缓存、GLES2 纹理上传与回读，以及 Vulkan 的 RGBA 纹理上传和 RGB565 转换优化。[精灵、JIT 缓存与 GPU 传输](https://github.com/lucinhu/nano_j2me/blob/v0.5.50/docs/notes/2026-11-26-render-jit-performance.md) · [高清平滑、JIT 分支与 GLES2](https://github.com/lucinhu/nano_j2me/blob/v0.5.50/docs/notes/2026-11-26-smooth-jit-upload.md) · [抗锯齿、字节码分派与 Vulkan](https://github.com/lucinhu/nano_j2me/blob/v0.5.50/docs/notes/2026-11-26-ssaa-dispatch-vulkan.md)

## 发行验证

- 纯软件构建通过 222/222 项测试，启用 JIT、GLES2、Vulkan 和 FSR2 的构建通过 236/236 项。两种构建各回放两款 2D 游戏，各有 22 张取样画面与基线逐字节一致。
- RTX 5080 Laptop 以 GLES2 和 Vulkan 分别回放《毁灭战士3》250 帧。每个后端的 31 张采样图、9,873 个绘图包与各自的基线一致，各提交 4,229,899 个三角形。
- 在 API 36 模拟器上，从公开的 v0.5.49 覆盖安装 x86_64 标准版，再安装通过 arm64 转译运行的增强版。两次都能从原游戏库启动同一个 MIDlet，素材取样区域与升级前逐字节一致。
- libretro 的 Linux 模拟前端通过 20/20 项，Windows x86_64 Wine 前端通过 4/4 项，八个平台的打包检查通过 32/32 项。
