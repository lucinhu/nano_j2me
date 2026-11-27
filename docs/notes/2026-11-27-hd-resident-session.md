# 高清面片的驻留会话：一次完整尝试与结论

本篇记录一次**未落地**的实验：为 Vulkan 三维面片后端实现「驻留会话」（begin/end），
让同一帧内多个 M3G 批次共享一次颜色/深度导入与导出，从而消除第 4 轮剖面中
`vkWaitForFences` 占据的每包 0.61 ms 等待。结论是：**会话机制本身可用（1× 逐字节一致），
但物理镜像上不可落地**——因为 CPU 会在批次之间写同一块镜像，而每次 flush 的并集矩形
并不比逐包 ROI 更小，8× 反而更慢。代码已完整回退。

## 一、动机

第 4 轮的相位剖面（`2026-11-27-hd-triangle-transfer.md` §七）在确定性 8× 游戏内回放上给到：

| 相位 | 2× | 8× |
| --- | --- | --- |
| accel 路径合计 | 0.90 s（6.5%） | 10.3 s（35%） |
| 其中 `vkWaitForFences` | 0.71 s | **5.59 s（exec 的 97%）** |
| 每包等待 | 0.03 ms | **0.61 ms** |

8× 每帧约 38 个批次，每个批次独占一次「整块 ROI 颜色 + 深度」导入、绘制、导出。
会话的思路是：把这一帧的多个批次并到一个驻留会话里，只导入/导出一次。

## 二、设计（已回退）

- `include/nj2me/nj_gfx_triangles.h` 早已定义了会话契约（begin 播种、包进入会话、
  end 原子提交、abort 丢弃、decline 由调用方回放），GLES2 端口已有实现；
- Vulkan `tri_draw.inc` 增加 `nj_vk_renderer_triangles_{begin,end,abort,list}`：
  begin 播种并集矩形（`transfer_rect`）、打开一个记录中的命令缓冲与 render pass；
  包只上传顶点/纹理并在驻留附件上记录绘制（`record_draws`，含每包 viewport/scissor 偏移）；
  end 一次导出并提交；
- `nj_port` 增加 `gfx_hd_journal` 开关，Vulkan 置 1；`nj_lib_m3g.c` 把日志门禁放宽到
  物理路径，并在读取 CPU 深度键（FSR2 捕获）、写镜像的软件回退、`nj_midp_hd_collect/dispose`
  之前强制提交会话；`nj_midp_hd_collect/dispose` 同理。

## 三、测量

用确定性 Doom3 8× 游戏内回放（`--keys '200:down:down,205:down:up,215:fire:down,216:fire:up'`，
240 帧，逐字节可复现的基线帧 00240 sha256 `0da0690e…`）：

| 构建 | 帧 00240 sha256 | wall | fps | 与基线 MAE | 变化像素 |
| --- | --- | --- | --- | --- | --- |
| 基线 5f4fa584 | `0da0690e…` | 27.2 s | 8.8 | — | — |
| 会话 | `f27aab0b…` | 31.2 s | 7.7 | **27.2/255** | **98.3%** |

画面整体偏亮（均值 59/49/41 对 27/28/28）。也就是说：会话不仅没有变快，**内容还是错的**。

1× 对照（同一 Vulkan 日志门禁，120 帧，未进入 NEW GAME 的确定性段）逐字节一致：
基线与会话帧 00120 均为 `308eddc6…`，耗时 12.04 s 对 12.03 s。这说明会话的
命令缓冲/附件/回读管线本身没有问题，问题只出现在「物理镜像 + 帧内多个 CPU 写者」的组合上。

## 四、为什么不可行

1. **CPU 会写同一块物理镜像。** HD 模式下 Java 的 2D 绘制（HUD/文字）和 M3G 的背景、
   清屏等路径直接在镜像上写字；日志会话把镜像像素按指针借给 GPU 批次，任何一次写都必须
   先提交会话。虽然给已知路径加了提交点，但仍有路径（例如帧中途的合成/背景替换）没有
   经过这些提交点，于是驻留内容覆盖了 CPU 刚写的像素——这解释了 98% 像素不同、整体偏亮。
2. **并集矩形不省传输。** 日志的一次 flush 通常只含 1–2 个批次，物理镜像上每个批次的 ROI
   已经是三角形包围盒（第 3 轮的包级剖面：1459×1793、1063×1063、869×869…），并集不会比
   逐包 ROI 更小；反而因为会话把整块并集导入/导出一次，8× 下比逐包还多传了内容。
   这与第 1–2 轮 list 交易「并集导入无效」的结论一致。
3. 因此真正的出路不是会话，而是**让整帧的镜像（颜色与深度）都常驻 GPU**，把 2D/HD 合成也
   变成 GPU 命令，只在呈现时做一次缩小采样。那是跨 GL/Vulkan 与 MIDP 合成层的架构改动，
   不属于单点优化。

## 五、复现

```sh
# 树干回退后，本次实验没有留下代码；如需重试，按第 2 节在 tri_draw.inc 中加回
# begin/end/abort 并把 nj_port.gfx_hd_journal 置 1，然后：
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json __NV_PRIME_RENDER_OFFLOAD=1 \
  build/nj2me --graphics vulkan --render-scale 8 --ssaa 1 --asset-filter pixel-art \
  --dump-every 240 --port headless --size 240x320 --heap 16 --no-heap-grow --fps 1000 \
  --run-frames 240 --stats --keys '200:down:down,205:down:up,215:fire:down,216:fire:up' \
  --fs-dir ... --rms-dir ... --dump-frames /tmp/out/ tests/games/毁灭战士3_10598.jar
sha256sum /tmp/out/00240.ppm   # 基线应为 0da0690e0f699f8e6ab7bc258d7808b15168e3bbb7d58b0ca408b4f3770c7de0
```
