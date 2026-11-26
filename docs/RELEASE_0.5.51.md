# nano_j2me 0.5.51

这版修正 FSR 在不同显示尺寸下的画质，并降低高清画面的窗口呈现耗时。FSR1 现在按游戏画面的实际显示区域工作：原尺寸直接保留像素，放大时按对齐的源像素重建边缘，缩小时按每个目标像素覆盖的区域求平均，不再对已经是高分辨率的画面再放大一次。Android 在游戏内切换「GL 呈现」滤镜时会重建正确的窗口，切回软件呈现后局部刷新的背景也不再残留黑块。[画质设置与同帧对比](https://github.com/lucinhu/nano_j2me/blob/v0.5.51/docs/UPSCALING.md)

## 下载与升级

| 设备 | 下载 | 要求 |
| --- | --- | --- |
| 常见 64 位 ARM 手机 | [Android arm64 标准版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-arm64-v8a.apk) | Android 5.0+，软件或 GLES2，默认解释器 |
| 64 位 ARM 手机，需要 JIT、Vulkan 或 AMD FSR2 | [Android arm64 增强版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-arm64-v8a-vulkan-fsr2.apk) | Android 9.0+，FSR2 需要支持 Vulkan 1.2 的设备和兼容的 M3G 场景 |
| 其他 Android 设备及模拟器 | [ARMv7](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-armeabi-v7a.apk) · [x86](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-x86.apk) · [x86_64](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-x86_64.apk) | Android 5.0+，选择对应架构 |
| 早期 ARM 设备 | [Android legacy](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-legacy.apk) | Android API 5+，ARMv5 |
| RetroArch / libretro | [八平台核心](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/nj2me-0.5.51-libretro-cores.zip) | Linux x86_64、AArch64、ARMv7，Android arm64、ARMv7、x86_64，Windows x86、x86_64 |

六个 APK 的 `versionCode` 均为 551，签名证书与 v0.5.50 对应发行包一致，支持直接覆盖安装。下载全部发行包后，可在同一目录运行 `sha256sum -c SHA256SUMS-0.5.51.txt`。[SHA-256 校验清单](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.51/SHA256SUMS-0.5.51.txt)

## 画质

同一帧《田园物语》第 1080 帧分别用修改前和修改后的构建回放。对话文字区域按原尺寸呈现时，原先有 2,226 个像素偏离游戏原图，现在全部一致。整帧中除同帧变化的 38×19 速度角标外，76,078 个像素原先有 66,920 个不一致，现在为 0。

| 游戏画面 → 显示区域 | 修改前 RGB MAE | 修改后 RGB MAE |
| --- | ---: | ---: |
| 240×320 → 240×320 | 17.710 | 0 |
| 240×320 → 143×191 | 23.802 | 0.024 |
| 240×320 → 241×323 | 24.759 | 7.910 |
| 240×320 → 371×495 | 24.400 | 6.038 |
| 240×320 → 781×1045 | 24.142 | 4.338 |
| 1920×2560 → 443×591 | 15.808 | 0.005 |
| 1920×2560 → 240×320 | 1.042 | 0.001 |

误差以对齐的源像素插值结果为参照，单位为 0–255。强放大时 FSR1 每轴最多用 EASU 重建 2×，其余倍率由锐化采样补齐，因此上表仍有数点差异，但文字边缘已不再被抹平。

## 流畅度

RTX 5080 Laptop 上以真实 SDL 窗口回放同一游戏帧，每组 8 帧预热后取 3×36 帧的中位数，修改前后交替测量。

| 游戏画面 → 显示区域 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 240×320 → 240×320 | 0.517 ms | 0.143 ms |
| 240×320 → 480×640 | 0.914 ms | 0.786 ms |
| 960×1280 → 480×640 | 7.590 ms | 1.465 ms |
| 960×1280 → 960×1280 | 9.528 ms | 2.271 ms |
| 1920×2560 → 480×640 | 3.775 ms | 3.827 ms |
| 240×320 → 720×960 | 1.698 ms | 2.003 ms |

高清画面缩回窗口是本次收益最大的场景，原尺寸呈现不再经过一次多余的全屏采样。8× 输入缩小时耗时基本持平；原始画面直接放大 3× 时中位耗时增加 0.30 ms，P95 从 2.77 ms 降至 2.53 ms。

FSR2 在具备深度和运动数据的 M3G 场景中继续生效。实际采样倍率超过每轴 3× 时保留原生高清面片，《毁灭战士 3D》2× 下执行 1,719 次 FSR2，4× 下抽查的 21 帧全部与原生高清对照逐像素一致。

## 发行验证

- 纯软件构建通过 222/222 项测试，启用 JIT、GLES2、Vulkan 和 FSR2 的构建通过 236/236 项。
- SDL 局部刷新回归覆盖高清纹理切回覆盖绘制后更新区域外的底色；Android 模拟器验证同一局内切换滤镜、改变窗口尺寸和从后台恢复。
- 六个 APK 的包名、ABI、最低系统版本、`versionCode` 和签名在打包时逐一核对。两个 M3G 时域与空间采样报告见 [分辨率修复记录](https://github.com/lucinhu/nano_j2me/blob/v0.5.51/docs/notes/2026-11-26-fsr-resolution.md)。
