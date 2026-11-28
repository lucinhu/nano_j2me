# 8× 高清路径：镜面/深度 GPU 驻留与 2D 合成上 GPU

状态：设计 + 测量（目标③第一步），尚未改默认渲染路径。

## 1. 为什么现在做

§13/§14 已经证明：

- 把多次 GPU 事务合成一次（journal 会话）没有收益：真实 8× 场内每帧仍是 619 个会话、
  每会话整屏颜色+深度往返约 39 MB，墙钟 40.9 s vs 逐包 40.1 s（400 帧）。
- 8× 每帧约 100 ms 墙钟里，user ≈ 64 ms/帧；其中主要两项是
  (a) 逐包 ROI 的 CPU↔GPU 导入/导出拷贝，(b) CPU 侧 8× 的 2D/图像缩放合成。
  1× 整场只有约 5 ms/帧，差出来的几乎全是「为 8× 服务的 CPU 工作」。

结论：只要 CPU 仍是权威像素、每次 GPU 事务都要往返，任何「事务层面」的优化天花板都很低。
必须把权威像素搬到 GPU：镜面与深度常驻，2D 与 3D 都在 GPU 上合成，CPU 只在观察点读回。

## 2. 测量

### 2.1 gprof（-pg 构建，8× 400 帧，journal 关，keys 200/205/215/216）

墙钟 58.2 s / user 39.2 s（-pg 开销使 user 高于正常的 25.8 s）。flat profile 主要项：

| 项 | self | 说明 |
| --- | --- | --- |
| `copy_words` | 7.7 s | `transfer_cpu` 的 ROI 行拷贝（逐包导入/导出） |
| `nj_gfx_triangles_validate` | 7.7 s | 逐包校验（采样可能与相邻热点混叠，量级正确） |
| `m3g_background_image` | 5.1 s | 1238 次（约 3 次/帧）全屏背景图 8× 缩放 |
| `nj_media_video_overlay` | 1.45 s | 视频 overlay 的 8× 缩放（6%） |
| `hd_art_span` | 1.24 s | 2D 图元写物理镜面的 span |
| `nj_region_draw` 子树 | ~1.3 s | drawImage/drawRegion 的 HD blit |
| `nj_run` | 1.4 s | Java 解释器 |

⇒ 约 2/3 用户时间可以归为「逐包 GPU 往返」+「2D/图像 8× 缩放」。

### 2.2 常驻模型的下限（`test_gfx_vulkan --bench-resident`）

新增 benchmark：1920×2560、颜色+深度全屏、单次事务（CPU 导入 39 MB → GPU 画 2 个
三角形 → 导出 39 MB → fence），30 次：

```
resident bench: 1920x2560 full colour+depth pass min 18.40 ms median 20.60 ms
```

一次全屏颜色+深度往返约 20.6 ms。常驻模型每帧只需要：

- 一次颜色导出（19.7 MB，约 5-7 ms，深度默认不读回）；
- 2D/3D 全部在 GPU 内完成（无导入）。

即帧预算有希望从 100 ms 降到 ~20-30 ms 量级（含 present 上传）；这与「先消除 CPU 2D 与
逐包往返」的判断一致。

## 3. 目标架构（方案 B）

1. **常驻目标**：每个高清倍率持有一组 `VkImage` 颜色（mirror 尺寸、RGBA8）与深度（D32），
   以及 staging 环形缓冲；CPU 不再持有权威像素。1× 仍走现有 CPU 路径。
2. **2D 管线**（按现状实现顺序）：
   - 纯色填充/清屏：一个全屏三角形 shader + scissor/rect；
   - 直线/多边形/曲线/圆角：CPU 只做几何细分（现有 `hd_*` 的几何部分），顶点上传 GPU 填充；
   - 图像/背景图/视频 overlay：纹理 quad（现有缩放/旋转/透明度作为 uniform；pixel-art/FSR1
     滤波改成采样器/自定义采样）；
   - 文字：glyph atlas 纹理 quad；
   - 保持与逻辑面一致的裁剪、翻转、blend/ROP 状态语义。
3. **3D 三角形**：现有 `gfx_hd_triangles` 的 ROI 改为直接渲染进常驻颜色/深度，不导入/导出；
   深度留在 GPU（`depth_retain` 语义变成常态）。CPU 需要读回的场景（createImage、
   software fallback、把镜面当 Java 数组访问）才导出。
4. **导出点**：present 前导出一次颜色（可选按脏矩形）；Android 目前用 GL 上传 CPU 像素，
   短期保持「导出 → GL 上传」，长期再评估 `VK_KHR_external_memory`/GL 共享或 Vulkan swapchain。
5. **回退**：单包路径与现有 CPU 2D 完整保留；常驻模式用端口能力与运行时开关选择。

## 4. 里程碑

- **M0（完成）**：数据与设计；`--bench-resident` 基准。
- **M1（完成）**：Vulkan 常驻镜面对象与最小 API（`nj_vk_renderer_mirror_create/clear/fill/export`）
  + 单测 + 基准：320×240 全屏 clear 与两个矩形 fill 与 CPU 参考逐字节一致
  （`resident mirror: 0 mismatched pixels`）；1920×2560 每帧「clear + 8 个矩形 fill + 一次导出」
  `min 8.32 ms / median 16.46 ms`（30 次，机器有并发负载），对比逐包全屏三角形往返
  （20.6 ms）已明显更低；尚未接入 M3G/2D 调用方。
- **M2a（完成）**：常驻镜面加深常驻深度附件；新增 `nj_vk_renderer_mirror_texture`
  （任意源矩形 → 目标矩形的纹理 quad，nearest/bilinear、replace/alpha 混合，复用现有三角
  shader 与管线，源格式支持 RGB565/RGB888/ARGB8888）。单测：2×2 纹理最近邻放大到 32×32
  `0 nearest mismatches`；半透明黑覆盖背景 `0 blend outliers`。
  基准（1920×2560，load≈8.7）：clear+8 fills+全屏纹理 quad+export
  `min 4.70 / median 6.38 ms`；同机对比 逐包全屏颜色+深度往返 `min 21.39 / median 24.40 ms`、
  纯填充版本 `min 4.64 / median 5.30 ms`。
- **M2b（后端路径完成）**：常驻镜面三角形路径
  `nj_vk_renderer_mirror_triangles(mirror, list, import, export_colour, &submitted)`
  ——一次颜色（可选深度）导入、单个 render pass 内按包设置 viewport/scissor 画完所有包、
  一次颜色导出；纹理/顶点仍每批上传，深度只导入不导出。单测 `mirror_triangle_lists`
  （128×96、3 个包、每包 2 个三角形与独立纹理）：单次批量与「逐包各一次 import/export」
  像素逐字节一致（`0 mismatched pixels against sequential draws`）。
  基准（1920×2560、62 个 64×64 包、load≈8.4）：批量 `min 15.76 ms / mean 25.33 ms`，
  逐包 `min 921.58 ms / mean 1530.49 ms` ⇒ 同一批 62 包，常驻路径约为逐包 import/export 的
  1/58 耗时；当前 8× 场内每批约 62 包、每次事务约 40–49 ms，常驻路径把其中 GPU 往返部分
  压到 ~16–25 ms（仍含全屏 39 MB 一次导入+一次颜色导出）。**尚未接线**：MIDP 2D
  （背景图/overlay/drawImage/纯色/文字）与 M3G HD 三角形仍需切到该路径，
  8× 画质对照（截图/容差）与性能 A/B（目标 ≥20%）待做。
- **M2b 接线**：把 `nj_gfx` 的 2D 调用点（背景图/overlay/drawImage/纯色/文字）接到常驻镜面，
  M3G HD 三角形直接渲染进常驻镜面；8× 画质对照（截图/容差）与性能 A/B（目标 ≥20%）。
- **M3**：文字、曲线、视频 overlay；脏矩形导出。
- **M4**：present 集成与 Android 验证（present stats）、全量测试（236/222/check_android_java/
  test_gfx_vulkan/no_statics）、文档与提交。

## 5. 风险

- **逐位一致性**：GPU 光栅化与 CPU 2D 的像素细节（覆盖率、圆角、曲线、字体 hinting）不同，
  需要按「像素差容差 + 参考截图」验证，不能沿用逐位闸门；3D 与 2D 的合成顺序必须严格保持。
- **present 互操作**：Android present 走 GLES2 纹理上传；短期导出后上传可行，但 19.7 MB/帧的
  读回与上传仍是成本，长期需外存共享/Vulkan swapchain。
- **双后端**：GLES2 也有 HD 2D 路径，架构必须保持「后端可选」，不能把 Vulkan 细节漏进
  `nj_gfx` 公共层。
- **契约与测试**：常驻镜面需要新的端口能力描述与测试夹具；现有 session v2 可以复用一部分
  （保留附件、dirty 导入），但 2D 绘制不进 session 契约，需要独立接口。

## 6. 下一轮起点

M1：给 `nj_vk_renderer` 加常驻镜面对象与最小 2D 操作（clear/fill/纹理 quad/export），
在 `test_gfx_vulkan` 里与 CPU 参考对比并基准；先不接 M3G，保证独立可测。

## 7. M2b 接线（2026-11-28）

MIDP 2D 与 M3G 高清路径已接到常驻镜面（实现细节与数据见
`docs/notes/2026-11-27-hd-triangle-transfer.md` §十八）：

- 端口新增 `hd_mirror_sync/clear/image/drop` 四个能力；`nj_render_mirror.sync` 让 CPU 2D
  在写物理目标前先导出/同步 GPU 状态。
- `m3g_clear` 用 GPU 矩形清屏（视口外保留、局部清用 `vkCmdClearAttachments`）；
  `m3g_background_image` 的最近邻缩放变成 GPU 纹理 quad（REPEAT 保留 CPU 路径）；
  三角形经 journal 列表一次提交进常驻镜面，深度常驻 GPU 不回读。
- 开关：`NJ_VK_RESIDENT=1` / Android `debug.nj2me.vk_resident`，默认关闭。

结果（Doom3 8× 400 帧，同机 interleaved，load≈4.2）：

- 稳定段 journal 关 23–26 s/100 帧 vs 常驻 10–11 s/100 帧 ⇒ **2.0–2.5×**；整轮粗测
  56.9/88.2 s vs 41.7/47.0 s。
- 8× 确定性回放两次逐位相同（帧 60/120/180 = `846057676cfed67a` / `b2f32d1c682185d3` /
  `695e0c5a49938a7f`）；与 journal 关基线相比：标题帧 60 完全一致；场内帧 300 差
  13,633 px（0.28%、最大 22，只在几何边缘细线）；星空背景单像素最大差 39。
- 1× 常驻模式也确定，但因 journal 改走 list 提交而与逐包基线不同（opt-in 专用；默认路径
  未变）。

AVD present stats（x86_64 --gles2 --vulkan，8× HD，JBenchmark3D）：温暖后 resident on 与 off
无可测差异（draw 3.35/3.30 vs 3.18/3.13，swap 5.86/5.18 vs 5.80/5.30，frame 24.0/29.8 vs
23.35/28.73 ms），说明 AVD 的帧时被 GLES2 present/上传主导，CPU 合成已不是瓶颈；桌面
2.0–2.5× 的收益要等 M4（present 直接消费 GPU 图像）才能在 Android 上兑现。

下一步：M3 脏矩形导出（把 present 前的全屏颜色导出也缩到 HUD/变化区域）、M4 present 接
GPU 图像与更多 title 回归；通过后把这些能力做成默认开启（但保留精确单包回退）。

## 8. 校验开销剔除（2026-11-28）

常驻路径稳定后，profile 显示 73% CPU 在重复的契约校验上：journal 入队、journal commit、
后端列表入口各校验一遍，且包含包 ROI 的深度 keys 扫描。新增端口 `gfx_triangles_list_trusted`
（journal 提交时信任自身已校验的包）与 `nj_gfx_triangles_validate_fast`（跳过 keys 范围扫描，
只用于内部 journal 包）后：8× 场内 400 帧 24.9 s → 14.3 s（−42%），profile 采样 CPU
13.08 s → 3.48 s（−73%），确定性哈希逐位不变。

剩余热点：`hd_art_span`（CPU 2D 高清图像 blit，~5 次全屏/帧，1.13 s）与 `jr_draw_walk`
（逻辑面软件走查，1.22 s）。前者属于本方案的 M2b 扩展（图像也上 GPU），后者可先验证
常驻模式下逻辑面是否仍被读取。

### 8.1 FSR2 与批量冒烟（2026-11-28）

常驻镜面与 FSR2 temporal 路径互斥：FSR2 时 `nj_midp_hd_target` 不安装 `mirror.sync`，
M3G 清屏/背景的 GPU 钩子也加 `!render_quality.fsr2` 守卫（Doom3 2×+FSR2 常驻开关输出
逐位相同）。12 个 3D title 的 8× 冒烟：全部 exit 0、两种模式各自确定；11 个在 120 帧内
未渲染可见 3D（标题/加载），Doom3 的关/开差异 3.21%（最大 10）。默认开启仍待更多
「进入 3D 场景」的回归素材。

### 8.2 常驻路径成本拆分与 1x 隔离（2026-11-28 晚）

`NJ_VK_PROFILE` 下 8× 场内 400 帧：每帧约 12.8 ms 的 `mirror_export`（19.66 MB 回读）、
3.1 ms 三角提交、1.4 ms 清屏、0.9 ms 纹理 quad，合计约 18 ms；余下约 19 ms 是 CPU 2D、
逻辑走查、VM 与 present。导出是下一阶段最大单项，只有 present 直接消费 GPU 图像（M4）才
能去掉。另加：全屏重绘后跳过颜色导入（确定性哈希不变）、`--resident` CLI 开关、以及
`gfx_triangles_hd_only`（常驻的 journal 列表只对 HD 生效，1x 输出逐位不变）。

### 8.3 导出拆分与 readback 带宽上限（2026-11-28 深夜）

8× 每帧导出 ~12.8 ms 中约 4.2 ms 是 CPU 行拷贝、8.5 ms 是 submit+fence+GPU→host 传输；
2×/4×/8× 的每次导出的传输时间分别约 0.24/1.21/8.4 ms（1.23/4.9/19.66 MB），8× 已接近
PCIe 回读带宽（2.4–3.4 GB/s）。强制 write-combined（HOST_COHERENT）staging 会让 CPU 拷贝
慢 30 倍，不可用。结论：导出无法再靠调参优化，必须走 M4（present 消费 GPU 图像）或把 2D
合成全部留在 GPU；2D 图像 blit 目前仍是 CPU 侧（`hd_art_span`）。

### 8.4 导出零拷贝（2026-11-29 凌晨）

把 HD target 的 CPU 像素直接指向 mirror 的 readback staging（原始分配保存、释放时恢复），
导出跳过 host copy、导入跳过颜色打包。Doom3 8× 场内 400 帧：14.9 s → 12.0–12.7 s
（相对 journal 关 45.1–45.6 s 为 3.6–3.8×），export 5.0–5.2 s → 3.1 s，像素哈希不变。
剩余 export 3.1 s（≈7.8 ms/帧）几乎全是 submit+fence+GPU→host 传输，要再降只能让 present
直接消费 GPU 图像（M4）。

### 8.5 Android 外部内存能力与零拷贝冒烟（2026-11-29 上午）

在 renderer 创建后加了一个只读探针（Android `VK_LOG`）：枚举设备扩展并检查
`VK_ANDROID_external_memory_android_hardware_buffer` 与 `VK_KHR_external_memory`。
模拟器（Medium_Phone_API_36.1，`-gpu host`，Goldfish GFXStream / RTX 5080，Vulkan 1.3）
上读数：`vulkan: device extensions=88 ahb=1 extmem=1` ⇒ **AHB 外部内存可用**，M4
（present 直接消费 GPU 图像）在 Vulkan 侧没有阻挡。

同时对 Round 11 的导出零拷贝做设备冒烟（JBenchmark3D 8×，resident 开/关各一轮）：
`adb logcat` 无 FATAL/异常，彩色 3D 场景正常渲染（`/tmp/burst-on-*` 多帧均有内容），
present stats（温暖窗）on：draw 3.794 / swap 7.496 / frame 28.261 ms，off：3.797 / 7.298 /
27.562 ms —— 与 Round 7 相同，即别名在 Android 上不改变可见结果，也仍无可测收益
（帧时由 GLES2 present 上传主导）。证据在
`.local-tools/debug-runs/avd-alias-20261128/`。

M4 的两个真正难点已明确：(1) AHB 的 32 位格式只有 RGBA_8888/RGBX_8888（字节序 R,G,B,A），
而镜面是 `VK_FORMAT_B8G8R8A8_UNORM` 且 CPU 2D 代码按 0xAARRGGBB 解释，直接共享需要新增
RGBA 像素格式或 GPU 侧转置；(2) CPU 2D HUD 需要可写共享内存，若 HUD 改成独立 overlay 纹理
在 GL present shader 里合成，则依赖目标内容的 2D ROP（混合/掩码/文字）无法正确工作。
⇒ 可行顺序是先把 2D 合成整体搬上 GPU（M3，含依赖目标的 ROP 在 GPU blend 中实现），
再让 present 直接消费常驻图像；否则只能在 AHB 与 CPU 缓冲之间做一次 GPU blit，
收益仅相当于去掉回读的一半。

### 8.6 AHB 导入探针：模拟器不可验证（2026-11-29）

新增独立探针 `src/port/vulkan/nj_vk_ahb_probe.c`（Android Vulkan 构建专用，
`debug.nj2me.ahb_probe=1` / `NJ_AHB_PROBE=1` 触发，自带 instance/device，不碰渲染器）：
分配 64×64 RGBA_8888 AHardwareBuffer → `vkGetAndroidHardwareBufferPropertiesANDROID`
→ 导入 VkImage → 分配/绑定导入内存 → 清屏并回读像素。

模拟器（Medium_Phone_API_36.1，-gpu host）结果：
- AHardwareBuffer 分配成功；设备同时有 `VK_KHR_external_memory` 与
  `VK_ANDROID_external_memory_android_hardware_buffer`；探针自己的 instance/device 创建成功。
- 在设备创建后的第一个驱动调用（属性查询/导入）时，**模拟器宿主进程直接崩溃**
  （crashpad dump，adb 掉线）。⇒ gfxstream 只是声明了扩展，实际使用会杀死模拟器，
  因此 M4（present 直接消费 GPU 图像）**无法在本机 AVD 上验证**，只能留给真实 Android
  设备；探针保留为真机诊断工具，并在文件头注明不要在模拟器上启用。

结论：AHB 方向的正确性验证依赖真机，本轮不再继续该分支；后续轮次优先做仍可本地验证的
④ FSR1/呈现优化与已有常驻路径的收尾（默认开启决策需要更多进入 3D 场景的素材）。

### 8.7 M3 实施切口：把 2D 缩放/合成搬上 GPU（2026-11-29 下午）

重新采样默认 pixel-art 常驻路径（8× 240 帧，`--resident`，Doom3）：`hd_art_span`
（CPU 最近邻放大写像素）占 **54.4%**（1.23 s / 115.5 M 次 span），其后
`nj_region_draw` 4.9%、`nj_run` 4.4%、`sw_span` 4.4%、`sw_tri_band` 3.5%、
`jr_hd_target` 3.5%、`m3g_background_image` 2.7%、`nj_surface_format_color` 2.2%、
`nj_src_pixel` 1.8%。

三项内层 SIMD 尝试全部否决（详见 `docs/notes/2026-11-28-pixel-art-scaling-profile.md`）：
SSE 宽存储填充 +45% 回退、u64 成对存储中性、半透明合成 4 像素 SSE4.1 有 6 像素差异且
未定位原因。⇒ `hd_art_span` 的标量内层已接近极限，要继续降 CPU 必须把整段搬上 GPU，
而不是在同一循环里换指令。

M3 切口（按顺序，每步独立可验证）：

1. **region import/export**：当前 `mirror_import`/`mirror_export` 只做整面（19.66 MB）。
   改为按矩形（`transfer_rect_at` 已有矩形参数）并记录 `gpu_dirty`/`cpu_dirty` 区域；
   单测：整面与 region 结果逐位一致、多次 dirty 合并正确、导出后清脏。
2. **2D 图像 quad**：`nj_render_mirror` 增加 `image` 钩子（源 surface+矩形、目标矩形、
   filter、wrap），resident 后端在 import 后的同一 render pass 里画 quad；`hd_blit`
   在 resident 模式且支持该路径时改用钩子（首期只支持 ARGB8888/RGB565 源、PAINT、
   nearest/filter 两种 filter；mask/xor/其它 ROP 保持 CPU）。
3. **矩形与文字 glyph quad**：`hd_block` 的纯色填充/描边与 `nj_text.c` 的 glyph blit
   走 GPU quad；曲线（`hd_curve`）先用 CPU 生成 mask 或折线 quad，最后再考虑 GPU 曲线。
4. **同帧批处理与一次往返**：把所有 2D quad 收集成一个列表，在需要 CPU 观察
   （present/readback/依赖目标的 CPU 操作）之前一次 import →（3D 与 2D 同 pass 或分 pass）
   →一次 export；目标每帧 ≤1 次颜色 import + ≤1 次 export（4× 以上 export 已贴带宽，
   主要省 import 与 CPU 像素工作）。
5. **验证闸门**：每步都要求 8×/1× 确定性闸门保持（8× resident baseline
   `846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`，8× plain baseline
   `846057676cfed67a`/`c65baebe4730fcbe`/`dc5cb60109dd03cb`）；2D 图元与 CPU 参考
   逐像素对比（在 `tests/test_gfx_vulkan.c` 扩 mirror 2D 用例）。

风险与前置：2D ROP 语义（blend/mask/xor）依赖目标像素，GPU 侧需要保留目标或把 ROP
转成 blend/pipeline 状态；FSR1 素材过滤上 GPU 需把 Catmull-Rom+RCAS 折进 shader
（可选后续）；CPU/GPU 交错写入的生命周期（dirty 代次）是最大正确性风险，先写单测再接线。
