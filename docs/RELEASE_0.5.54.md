# nano_j2me 0.5.54

这版收尾了 8× 高清 3D 的性能工作，并给「FSR1 锐利」素材采样再提速：

- 新增**实验性 GPU 常驻高清镜面**（Vulkan，默认关闭）：把 8× 高清镜像的颜色和深度留在显存里，M3G 的清屏、背景图与 3D 三角形直接在 GPU 上画进常驻镜像，2D 绘制与呈现前只做一次零拷贝导出。桌面 Doom3 8× 场内 400 帧从 45.5 s 降到 12.3 s（约 3.6×）；Android 端呈现上传仍是瓶颈，实测无明显收益，因此默认关闭、仅供尝鲜。
- **FSR1 锐利素材采样 CPU 成本累计降低约 40%**（半透明逐像素合成的 SSE4.1 向量化、段边界列复用与 ARGB8888 直取）。Doom3 8× 场内 FSR1 200 帧从 11.34–11.44 s 降到 6.78–7.14 s，输出逐位不变。
- 新增 `--deterministic` **确定性回放**工具（headless 虚拟时钟、固定随机种子与固定日历），并用它查清、修复了高清 3D 批处理的一个真实缺陷：journal 纹理池扩容后旧条目指针悬空。Vulkan 面片路径因此默认恢复为精确单包提交，保证确定性。
- 顺带回答 FSR2 倍率问题：FSR2 每轴硬上限 3×，4×–8× 时域链实测不划算，已在本文档与笔记中说明；打开 FSR2 时高清倍率自动配到 3×。

两套测试全部通过，arm64/ARMv7 的 libretro 核心与 Android 包都已恢复构建（修复了一个只影响非 x86 构建的 SSE4.1 内核编译守卫）。

## 下载与升级

| 设备 | 下载 | 要求 |
| --- | --- | --- |
| 常见 64 位 ARM 手机 | [Android arm64 标准版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-arm64-v8a.apk) | Android 5.0+，软件或 GLES2，默认解释器 |
| 64 位 ARM 手机，需要 JIT、Vulkan 或 AMD FSR2 | [Android arm64 增强版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-arm64-v8a-vulkan-fsr2.apk) | Android 9.0+，FSR2 需要支持 Vulkan 1.2 的设备和兼容的 M3G 场景 |
| 其他 Android 设备及模拟器 | [ARMv7](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-armeabi-v7a.apk) · [x86](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-x86.apk) · [x86_64](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-x86_64.apk) | Android 5.0+，选择对应架构 |
| 早期 ARM 设备 | [Android legacy](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-legacy.apk) | Android API 5+，ARMv5 |
| RetroArch / libretro | [八平台核心](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/nj2me-0.5.54-libretro-cores.zip) | Linux x86_64、AArch64、ARMv7，Android arm64、ARMv7、x86_64，Windows x86、x86_64 |

六个 APK 的 `versionCode` 均为 554，签名证书与 v0.5.51–v0.5.53 发行包一致，支持直接覆盖安装。下载全部发行包后，可在同一目录运行 `sha256sum -c SHA256SUMS-0.5.54.txt`。[SHA-256 校验清单](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.54/SHA256SUMS-0.5.54.txt)

## 实验性：GPU 常驻高清镜面（默认关闭）

8× 高清镜面是 1920×2560 的 ARGB 图像，加上深度共约 39 MB。默认路径下，每一批 3D 面片都要把颜色和深度各导入、导出一次，一帧约 1.5 批，等于每帧约 60 MB 的显存往返；此外 M3G 的背景图还在 CPU 上做 8× 缩放。

常驻模式把这套镜像留在显存里：M3G 清屏用 GPU 清屏、背景图用 GPU 纹理四边形、3D 三角形直接画进常驻镜像；只有 2D 绘制和呈现需要读像素时才同步一次，而且导出是零拷贝的（CPU 像素直接指向回读缓冲，不再多一次 19.66 MB 的拷贝）。

| Doom3 8× 场内 400 帧（确定性回放，桌面 RTX 5080） | 墙钟 |
| --- | ---: |
| 默认（journal 关，逐批往返） | 45.1–45.6 s |
| `--resident` | **12.0–12.7 s** |

常驻开启后的分解：619 次导出共 3.1 s、三角形提交 1.05 s、GPU 清屏 0.34 s、背景图四边形 0.33 s。

打开方式（默认关闭，适合尝鲜）：

- 桌面：`--graphics vulkan --render-scale 8 --resident`
- Android：`adb shell setprop debug.nj2me.vk_resident 1`（下次启动生效，可在 logcat 看到 `vulkan: resident HD mirror enabled`）

限制与已知差异：

- 与默认路径相比画面不是逐位相同：GPU 光栅化与最近邻采样和 CPU 参考有固有差异，快照帧最多约 3% 像素、最大通道差 10；同一版本的多次运行逐位确定。
- 1× 路径完全不受影响；FSR2 路径自动不启用常驻（FSR2 的时域重建在 CPU 上写高清目标）。
- Android 模拟器上 `present stats` 与默认路径无差异（`draw`/`swap`/`frame` 同一区间）——Android 的瓶颈在 GLES2 呈现上传，需后续把呈现也搬到 GPU 图像上。

## FSR1 锐利素材采样：CPU 成本再降约 40%

v0.5.53 的 FSR1 锐利在 8× 下对每帧重复放大的半透明叠加层逐像素做 Q16 预乘混合与 source-over，是整套素材采样里最贵的路径。本版做了两处优化：

1. **半透明逐像素合成 SSE4.1 向量化**：4 像素一组，用 `_mm_mullo_epi32` 完成两阶段 Q16 混合，`/255` 用 `(y*0x8081)>>23` 精确复现（已对 0–65535 全值验证）；遇到透明目标、alpha 0 或整像素捷径时整组回退原标量实现。
2. **段边界列复用与 ARGB8888 直取**：相邻半透明段共享边界列，改为复用；ARGB8888 源跳过格式分发直接取像素字。

| Doom3 8× 场内、FSR1 锐利、200 帧（同一二进制交错 A/B） | 墙钟 |
| --- | ---: |
| v0.5.53 | 11.34–11.44 s |
| 本轮优化后 | **6.78–7.14 s（累计约 −40%）** |

FSR1 相对像素画清晰的额外开销从约 34 ms/帧降到约 10 ms/帧。1×/4×/8× 的转储帧与优化前逐字节相同，`test_gfx_hd` 的 FSR1 精度断言（半透明样本与遮罩不变）通过。实现细节见 [FSR1 高清素材采样](https://github.com/lucinhu/nano_j2me/blob/v0.5.54/docs/notes/2026-11-27-fsr1-asset-sampling.md) 的附五、附六。

## 正确性：高清 3D 批处理缺陷与确定性回放

- 新增 `--deterministic`（headless 端口虚拟时钟 + 固定 `java.util.Random` 种子 + 固定日历 2000-01-01）：同一 JAR、同一按键序列在不同机器负载下逐位一致，是这一轮所有 A/B 与闸门的基础。
- 用它查清：一帧内多包合进一次 GPU 事务时，相同输入下输出不可复现（依赖 ASLR 与堆扰动）。根因是 journal 的纹理池扩容后，已排队条目里的纹理指针仍指向已释放的旧池；修复后逐位等价。该缺陷解释了此前多次「批处理不稳定」的观察。
- 即使修复后，多包事务在这台 Vulkan 驱动上仍无法做到运行间完全一致，因此发布版 Vulkan 端口默认保持**精确单包提交**（确定性优先），列表/驻留会话只作为内部 opt-in（`NJ_VK_SESSION=1`）保留。
- 附带优化：journal 提交路径去掉重复的参数校验（8× 场内 400 帧 24.9 s → 14.3 s，约 −42%），并修正非 x86 构建上 FSR1 行内核的编译守卫（arm64/ARMv7 重新可编译）。

## 为什么 FSR2 只能 3×：4×–8× 时域链的取舍

FSR2 每轴最多重建 3×，这是算法本身的上限，不是设置项。本版没有实现「FSR2 3× → FSR1 放大到 4×–8×」的时域链，因为实测不划算：链的 3D 像素最终就是 FSR2 3× 的结果，不可能优于原生 8× 渲染，唯一收益是 2D/HUD 拿到 8× 镜像，代价是多一次空间放大和一次 8× 呈现上传——桌面约 +2 ms/帧，Android 8× 从 60 fps 掉到约 52 fps。

与理想 8× 画面的距离（RGB MAE，越小越好）：

| 候选（渲染 → 窗口） | 960×1280 | 551×751 | 1080×1440 |
| --- | ---: | ---: | ---: |
| 原生 8×（无时域） | **1.72** | **1.73** | **1.72** |
| FSR2 3× | 2.26 | 1.99 | 2.24 |
| FSR2 2× | 2.36 | 2.51 | 2.33 |

因此当前策略是：打开 FSR2 时高清倍率自动配对到 **3×**（比 2× 好 4–21%，代价约 +0.3 ms），选 1× 或 4×–8× 时自动关闭 FSR2。想要 FSR2 的画质请用 3× + FSR2；想要最高画质请用 8× 原生（可再开本版的实验性常驻镜面冲帧率）。完整数据见 [4×–8× 时域放大链的取舍](https://github.com/lucinhu/nano_j2me/blob/v0.5.54/docs/notes/2026-11-27-temporal-chain-at-high-scales.md)。

## 已量化但不采用

- **多包批处理 / 驻留会话默认开启**：即使修复悬垂指针，多包事务在 Vulkan 驱动上仍不可完全复现，且 8× 场内合并后传输字节不减（并集接近整屏），默认仍走单包；`--resident` 依赖常驻镜像，属于另一条路径。
- **Android AHB 直接共享呈现**：设备扩展齐全，但 gfxstream 模拟器在导入 AHardwareBuffer 时会让模拟器宿主进程崩溃，只能在真机上验证，本轮不进入发布路径。

## 发行验证

- 纯软件构建通过 222/222 项测试；启用 JIT、GLES2、Vulkan 和 FSR2 的构建通过 236/236 项。
- `tools/check_no_statics.sh` 通过（92 个目标文件）；`tools/check_android_java.sh` 全部检查通过。
- 六个 APK 的包名、ABI、最低系统版本、`versionCode`（554）和签名在打包时逐一核对；libretro 八平台核心全部构建并通过架构与导出检查。
- 确定性闸门（8× 200 帧，帧 60/120/180）：默认路径 `846057676cfed67a`/`c65baebe4730fcbe`/`dc5cb60109dd03cb`；`--resident` `846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`，各自多次运行逐位一致。

## 发布包实测

- 安装本页的 `nj2me-0.5.54-x86_64.apk`（Android 14 / API 36 模拟器，`-gpu host`，宿主 RTX 5080）：`versionCode=554`、`versionName=0.5.54`，覆盖安装成功；启动毁灭战士3（8× 高清）后 `present stats` 三行正常输出，`draw` 中位 5.599 ms、`swap` 6.019 ms、`frame` 133.9 ms（标题画面约 7.5 fps，与既有实测处于同一量级），无 EGL 或 FATAL 错误。
- 六个 APK 逐一核对：包名 `com.lucinhu.nj2me`、`versionCode=554`、ABI 正确（arm64-v8a 两份、armeabi-v7a、x86、x86_64、legacy 为 armeabi），minSdk 分别为 21/28/5，签名证书 SHA-256 `17c9c8ec…502b` 与 v0.5.51–v0.5.53 发行包一致，可覆盖安装。
- libretro 八平台核心均内嵌版本 0.5.54，`.info` 的 `display_version = "0.5.54"`；`SHA256SUMS-0.5.54.txt` 共 7 项（6 个 APK + 核心 zip），zip 内按 `android-arm64/`、`linux-x86_64/` 等八个目录组织。
