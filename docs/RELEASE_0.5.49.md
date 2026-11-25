# nano_j2me 0.5.49

本版为 J2ME 游戏新增 1–4× 内部渲染、四采样抗锯齿，以及可选的 OpenGL ES 和 Vulkan 高清 M3G 面片渲染。游戏可以继续按逻辑尺寸布局、接收触摸和读回像素。纯软件构建可以独立使用高清渲染。

## 选择下载包

| 使用场景 | 发行文件 | 要求 |
| --- | --- | --- |
| Android 手机，使用软件或 GLES2 | `nj2me-0.5.49-arm64-v8a.apk` | Android 5.0+，64 位 ARM |
| Android 手机，需要 Vulkan M3G 或 AMD FSR2 | `nj2me-0.5.49-arm64-v8a-vulkan-fsr2.apk` | Android 9.0+，64 位 ARM。FSR2 还需要兼容的 Vulkan 1.2 设备 |
| 其他 Android 设备、模拟器 | `armeabi-v7a`、`x86` 或 `x86_64` APK | Android 5.0+，选择对应 ABI |
| 早期 ARM 设备 | `nj2me-0.5.49-legacy.apk` | Android API 5+，ARMv5 |
| RetroArch / libretro | `nj2me-0.5.49-libretro-cores.zip` | 解压后选择对应的 Linux、Android 或 Windows 核心，共 8 个目标 |

六个 APK 的 versionCode 均为 549，签名证书与 0.5.48 对应发行包一致。下载校验和清单后可运行 `sha256sum -c SHA256SUMS-0.5.49.txt` 核验全部七个二进制下载项。

## 画面与性能

- LCDUI 线条、弧线、面片、M3G、Mascot 和轮廓字体可以在物理分辨率上绘制。Java 像素访问沿用游戏设定的尺寸。现代 Android 支持按物理字号绘制系统中文字体。
- M3G 可选四子面片细分。GLES2 和 Vulkan 可以直接处理高清目标的颜色与深度，软件负责逻辑像素和硬件拒绝的绘制批次。Vulkan 复用缓冲区并合并连续的相同材质绘制。GLES2 窗口可直接上传紧密排列的小端 ARGB8888 高清帧。
- 增强版的 AMD FSR2 2.2.1 使用合适的 M3G 场景生成的深度、抖动和运动向量。含透明物体、部分清屏等不适合时域重建的绘制使用空间高清路径。
- 修正 `Graphics.copyArea` 在半透明图像和重叠区域上的复制。高清位图路径保留每个物理采样点的混合结果，并批量写入不透明纹素。

桌面可用 `--graphics software|gles2|vulkan` 选择绘制方式，用 `--render-scale 2 --ssaa 2` 取得 2× 输出与四采样。Android 可在游戏画面面板选择倍率和后端，下次启动时生效。[详细配置和示例](https://github.com/lucinhu/nano_j2me/blob/v0.5.49/docs/UPSCALING.md)

## 验证

| 项目 | 结果 |
| --- | --- |
| 软件构建 / GLES2、Vulkan、FSR2 构建 | CTest 222/222 和 232/232 通过 |
| Doom3D 启动及动态菜单，固定时钟 250 帧 | RTX 5080 Laptop 的 GLES2 与 Vulkan 各接收 9,873 批、4,229,899 个高清三角形。13 张 480×640 采样图共 3,993,600 像素，两种 GPU 路径仅 1 像素的单个通道相差 1 |
| 720p Vulkan 纹理场景，每批 1,920 个三角形 | 含颜色与深度往返，中位耗时 RTX 5080 Laptop 为 3.137 ms，Intel ARL 为 5.753 ms |
| GLES2 上传微基准，960×1280 源帧 | RTX 5080 Laptop 上中位耗时从 0.925 降至 0.632 ms，七种呈现滤镜结果与原实现逐字节一致 |
| Android 增强包 | API 36 模拟器请求 Vulkan 后回退至软件，完成 60 帧纹理场景。Android 真机上的 Vulkan 面片提交仍待实测 |
| libretro 发行文件 | Linux 模拟前端 20/20，Windows x86_64 Wine 4/4，八目标打包核验 32/32 |

GitHub Pages 工作流会在创建部署失败后等待 20 秒，使用同一份构建产物重试一次。部署成功后，Firefox 会实际打开已发布的演示并检查运行状态。[在线演示](https://lucinhu.github.io/nano_j2me/)
