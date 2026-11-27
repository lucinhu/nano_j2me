# nano_j2me 0.5.53

这版给「高清素材采样」加上第四种方式 **FSR1 锐利**：用 Catmull-Rom 三次卷积放大 2D 图片、图块和文字，再做一次带区间钳制的对比度锐化（RCAS 思路），比柔和平滑更锐、比像素画清晰更适合连续色调素材，同时不会在局部颜色区间之外产生过冲。两套测试全部通过，模拟器上四种方式都能在面板里选择并在重启后保留。[画质设置](https://github.com/lucinhu/nano_j2me/blob/v0.5.53/docs/UPSCALING.md)

同时补上呈现耗时的归因：Android 与桌面 SDL 两个移植的 `present stats` 现在除绘制（`draw`）与交换（`swap`）外，还会输出显示间隔（`frame`），因此「frame − draw − swap」就是游戏自己产生这一帧的成本，可以直接定位卡在游戏逻辑、3D 绘制还是呈现上传。

## 下载与升级

| 设备 | 下载 | 要求 |
| --- | --- | --- |
| 常见 64 位 ARM 手机 | [Android arm64 标准版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-arm64-v8a.apk) | Android 5.0+，软件或 GLES2，默认解释器 |
| 64 位 ARM 手机，需要 JIT、Vulkan 或 AMD FSR2 | [Android arm64 增强版](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-arm64-v8a-vulkan-fsr2.apk) | Android 9.0+，FSR2 需要支持 Vulkan 1.2 的设备和兼容的 M3G 场景 |
| 其他 Android 设备及模拟器 | [ARMv7](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-armeabi-v7a.apk) · [x86](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-x86.apk) · [x86_64](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-x86_64.apk) | Android 5.0+，选择对应架构 |
| 早期 ARM 设备 | [Android legacy](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-legacy.apk) | Android API 5+，ARMv5 |
| RetroArch / libretro | [八平台核心](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/nj2me-0.5.53-libretro-cores.zip) | Linux x86_64、AArch64、ARMv7，Android arm64、ARMv7、x86_64，Windows x86、x86_64 |

六个 APK 的 `versionCode` 均为 553，签名证书与 v0.5.51、v0.5.52 发行包一致，支持直接覆盖安装。下载全部发行包后，可在同一目录运行 `sha256sum -c SHA256SUMS-0.5.53.txt`。[SHA-256 校验清单](https://github.com/lucinhu/nano_j2me/releases/download/v0.5.53/SHA256SUMS-0.5.53.txt)

## 高清素材采样：新增 FSR1 锐利

「高清素材采样」现在仍是四种：原始像素、柔和平滑、像素画清晰，以及新增的 **FSR1 锐利**。前两种和像素画清晰的语义不变，旧设置原样保留。

| 240×320 图放大 4× 后的平均梯度 | 原图（最近邻） | 柔和平滑 | 像素画清晰 | FSR1 锐利 |
| --- | ---: | ---: | ---: | ---: |
| 连续色调（田园物语对话画面） | 5.041 | 4.670 | 5.041 | **5.417** |
| 像素画（田园物语像素风画面） | 6.395 | 5.783 | 6.403 | **6.987** |

FSR1 锐利的实现分两步：先用 4×4 的三次卷积核重采样（对平坦颜色能精确还原原值），再按 RCAS 的思路把结果往远离双线性基准的方向推 0.375，并钳制在 4×4 邻域的最小/最大值之间，所以不会出现亮边或暗边。半透明像素、遮罩和不透明叠加仍然走原来的精确合成路径，字节级不变；文字也使用更锐的覆盖率曲线。锐度参数做过 0.125/0.25/0.375/0.5 四档扫描，梯度单调上升且都无过冲，默认取 0.375。

开销（桌面 RTX 5080，每次绘制中位耗时 / 每源像素）：

| 测试图（放大到 4× 逐次绘制） | 原始像素 | 柔和平滑 | 像素画清晰 | FSR1 锐利 |
| --- | ---: | ---: | ---: | ---: |
| 条纹 240×320 | 0.20 ms | 1.89 ms | 1.12 ms | 7.84 ms |
| 照片色调 240×320 | 0.20 ms | 2.82 ms | 1.10 ms | 11.12 ms |
| 37×29 半透明精灵（旋转 90°） | 0.007 ms | 0.081 ms | 0.026 ms | 0.152 ms |

FSR1 锐利是四种方式里最贵的，适合追求锐度的静态画面或文字较多的高清界面；大面积动画或低端设备仍建议柔和平滑或像素画清晰。命令行为 `--asset-filter fsr1`，Android 设置面板对应「高清素材采样（下次启动）：FSR1 锐利」。

模拟器实机核对（4× 高清、田园物语标题与对话）：四种选项都能选中，强制重启后仍保留；同一画面裁切区域的平均梯度为柔和平滑 4.343、FSR1 锐利 4.751、像素画清晰 4.634，FSR1 与柔和平滑的 RGB 平均差 1.39（最大 69），没有 EGL 或运行时错误。

## 呈现耗时：新增 frame 行

`present stats` 现在输出三行，Android（`debug.nj2me.present_stats=1`）与桌面（`NJ_PRESENT_STATS=1`）一致：

```
present stats: mode=5 1080x2400 frames=120 draw ms min=… median=… p95=…
present stats: swap ms min=… median=… p95=…
present stats: frame ms min=… median=… p95=…
```

`frame` 是相邻两次呈现的间隔，因此 `frame − draw − swap` 是游戏自己产生这一帧的成本。模拟器 8× 原生面片跑 Doom3 时首帧窗口为 `draw` 4.189 ms / `swap` 8.355 ms / `frame` 28.591 ms，也就是约 16.0 ms 花在游戏帧本身（约为 35 fps），瓶颈不在呈现上传。

## 已量化但不采用：高清面片批处理

这一版还验证了「把一帧内多个高清 3D 批次合并成一个驻留会话」的想法：4× 时能省 8.3% 帧时间，但 8× 时反而慢 15.4%（会话并集接近整屏，颜色与深度整块上下载比逐批次 ROI 更贵，还不算会话拒绝后的回放）。因此不启用，高清 8× 三维要提速必须让镜像或深度常驻 GPU，属于后续架构改动。数据与复现命令见 [高清面片批处理](https://github.com/lucinhu/nano_j2me/blob/v0.5.53/docs/notes/2026-11-27-hd-journal-batching.md)。

## 发行验证

- 纯软件构建通过 222/222 项测试，启用 JIT、GLES2、Vulkan 和 FSR2 的构建通过 236/236 项。
- `tools/check_android_java.sh` 全部检查通过；六个 APK 的包名、ABI、最低系统版本、`versionCode` 和签名在打包时逐一核对。
- libretro 八平台核心通过 32 项提交检查与 20 项前端接口测试（含存档状态）。
- FSR1 素材采样的实现、精度测试与开销数据见 [FSR1 高清素材采样](https://github.com/lucinhu/nano_j2me/blob/v0.5.53/docs/notes/2026-11-27-fsr1-asset-sampling.md)；呈现耗时的完整方法见 [桌面 present stats](https://github.com/lucinhu/nano_j2me/blob/v0.5.53/docs/notes/2026-11-27-desktop-present-stats.md)。
