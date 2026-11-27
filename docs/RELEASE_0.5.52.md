# nano_j2me 0.5.52

这版修好 Android 上的 Vulkan 3D 面片渲染，并把 AMD FSR2 时域放大的回读开销降到原来的百分之一以下，使其在手机上的开销可以忽略。FSR2 现在与 3× 高清倍率配对，时域历史在游戏一帧内的多次绘制之间保持连续，不再反复重置；同一帧的 8× 原生渲染对照显示，2× FSR2 的空间误差接近原生 2× 渲染，而比纯空间放大低 4 倍以上，时域闪烁也低于空间放大的一半。高清 4×–8× 的「时域放大后再空间放大」链路经量化对比更差，因此不引入，4×–8× 继续使用原生高清面片加空间放大。[画质设置与同帧对比](https://github.com/lucinhu/nano_j2me/blob/v0.5.52/docs/UPSCALING.md)

## 下载与升级

| 设备 | 下载 | 要求 |
| --- | --- | --- |
| 常见 64 位 ARM 手机 | [Android arm64 标准版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-arm64-v8a.apk) | Android 5.0+，软件或 GLES2，默认解释器 |
| 64 位 ARM 手机，需要 JIT、Vulkan 或 AMD FSR2 | [Android arm64 增强版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-arm64-v8a-vulkan-fsr2.apk) | Android 9.0+，FSR2 需要支持 Vulkan 1.2 的设备和兼容的 M3G 场景 |
| 其他 Android 设备及模拟器 | [ARMv7](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-armeabi-v7a.apk) · [x86](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-x86.apk) · [x86_64](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-x86_64.apk) | Android 5.0+，选择对应架构 |
| 早期 ARM 设备 | [Android legacy](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-legacy.apk) | Android API 5+，ARMv5 |
| RetroArch / libretro | [八平台核心](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/nj2me-0.5.52-libretro-cores.zip) | Linux x86_64、AArch64、ARMv7，Android arm64、ARMv7、x86_64，Windows x86、x86_64 |

六个 APK 的 `versionCode` 均为 552，签名证书与 v0.5.51 对应发行包一致，支持直接覆盖安装。下载全部发行包后，可在同一目录运行 `sha256sum -c SHA256SUMS-0.5.52.txt`。[SHA-256 校验清单](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.52/SHA256SUMS-0.5.52.txt)

## 画质

时域放大的误差以同一帧游戏的 1920×2560 原生 8× 渲染为真值，按精确面积权重缩小到目标尺寸后比较；同一次游戏回放的帧、绘制与三角形计数完全一致，因此各构建可逐像素对照。

| 呈现在 960×1280 | RGB MAE | SSIM |
| --- | ---: | ---: |
| 原生 8× 渲染（理想值） | 1.719 | 0.9992 |
| FSR2 3×（当前默认配对） | 2.257 | 0.9790 |
| 原生 3× 面片渲染 | 2.280 | 0.9784 |
| FSR2 2× | 2.362 | 0.9698 |
| 1× 画面 + 纯空间放大 | 4.067 | 0.9412 |

| 呈现在 551×751 | RGB MAE | SSIM |
| --- | ---: | ---: |
| 原生 8× 渲染（理想值） | 1.734 | 0.9991 |
| FSR2 3×（当前默认配对） | 1.990 | 0.9925 |
| 原生 3× 面片渲染 | 2.018 | 0.9921 |
| FSR2 2× | 2.512 | 0.9678 |
| 1× 画面 + 纯空间放大 | 4.081 | 0.9496 |

静态区域（占画面 83.9% 的不动像素）上的逐帧闪烁量：原生 2× 渲染 0.1132，FSR2 2× 0.1482，纯空间放大 0.2904。也就是说 FSR2 在空间误差上接近原生渲染，在时域稳定性上约为纯空间放大的两倍。

因为每轴 3× 已经是 FSR2 的质量上限，开启 FSR2 时高清倍率现在自动配到 3×：相比 2×，960×1280 窗口误差再降 4.4%，551×751 降 21%，1080×1440 降 4.0%，代价只有呈现耗时增加 0.03–0.11 ms。4×–8× 的链路（FSR2 先放大到 2×/3× 再空间放大到 8×）经同帧对照，其 3D 像素与 FSR2 3× 相同且仍不如原生 8×，却要多付一次 8× 全屏上传，因此不采用。

## 流畅度

FSR2 每次派发原先从只标记为 `HOST_VISIBLE` 的显存读回结果，在 NVIDIA 上落到写合并（非缓存）内存，逐字节读取约 226 ns/像素。现在优先分配 `HOST_CACHED` 内存并按字重排，输出逐位不变：

| FSR2 派发（RTX 5080 Laptop） | 修改前 | 修改后 |
| --- | ---: | ---: |
| 120×160 → 240×320 | 17.554 ms | 0.143 ms |
| 240×320 → 480×640 | 69.356 ms | 0.555 ms |
| 240×320 → 720×960 | 156.846 ms | 0.830 ms |

同一回放从 28.599 s / 8.4 fps 降到 14.512 s / 16.5 fps，导出的帧逐字节一致。Android 模拟器上该改动无性能差异（gfxstream 的宿主内存本就可缓存），但也没有回退。

真实窗口回放（SDL，RTX 5080 Laptop，`present stats` 中位绘制耗时）：8× 画面缩到 960×1280 窗口 4.22–4.41 ms，8× 缩到 551×751 为 3.23–3.29 ms，4× 缩到 960×1280 为 0.86–0.88 ms。呈现耗时由上传带宽决定，GPU 滤镜只占 0.2–0.4 ms；从 4× 换到 8× 多出的约 2 ms 就是多上传的 15 MB。

Android 模拟器（`-gpu host`，RTX 5080）实机数据：高清 2× + FSR2 在 12 秒内派发 1,443 次、3× 为 1,383 次，除启动帧外全部连续复用历史（`reset=0`）；3D 基准 8× 原生面片约 56 fps（绘制中位 3.29–3.42 ms、交换中位 5.6–6.2 ms），2D《田园物语》8× 维持 60 fps（绘制中位 2.46 ms）。

## 兼容性与实机验证

- Android 模拟器（API 36，gfxstream Vulkan）上 Vulkan 3D 面片渲染现在真正工作：此前实例创建未提供 `VkApplicationInfo`，`apiVersion` 为 0，导致枚举不到任何物理设备而静默回退；修复后日志输出 `vulkan: enumerate result=0 count=1`、`vulkan: renderer ready`、`3D triangles: Vulkan ready`。
- 游戏一帧内多次绘制共用同一帧计数重新绑定 FSR2 目标时，历史不再被误判为新序列而重置；启动后的稳定阶段每次派发都是连续历史。
- 「FSR2 时域放大」与「高清渲染倍率」在设置面板中自动配对：从 8× 打开 FSR2 会回到 3×，把倍率改成 1× 或 4×–8× 会关闭 FSR2 并提示；旧版本留下的不匹配偏好也会在启动时自动修正。
- libretro 状态请求的等待从固定 4 秒墙钟超时改为「虚拟机仍在推进就继续等待」，高负载下不再偶发 `retro_serialize` 返回 0。

## 发行验证

- 纯软件构建通过 222/222 项测试，启用 JIT、GLES2、Vulkan 和 FSR2 的构建通过 236/236 项，两套测试并发运行时同样全部通过。
- `tools/check_android_java.sh` 全部检查通过；六个 APK 的包名、ABI、最低系统版本、`versionCode` 和签名在打包时逐一核对。
- libretro 八平台核心通过 32 项提交检查与 20 项前端接口测试（含存档状态）。
- 画质、时域与回读耗时的完整方法、数据和复现命令见 [FSR2 画质与回读](https://github.com/lucinhu/nano_j2me/blob/v0.5.52/docs/notes/2026-11-27-fsr2-accuracy-and-readback.md)、[强放大时域链路](https://github.com/lucinhu/nano_j2me/blob/v0.5.52/docs/notes/2026-11-27-temporal-chain-at-high-scales.md)、[FSR1 锐度与呈现耗时](https://github.com/lucinhu/nano_j2me/blob/v0.5.52/docs/notes/2026-11-27-fsr1-sharpness-and-present-cost.md) 与 [Android Vulkan/FSR2](https://github.com/lucinhu/nano_j2me/blob/v0.5.52/docs/notes/2026-11-27-android-vulkan-fsr2.md)。
