# 高清面片的 Vulkan 列表事务与驻留深度设想

日期：2026-11-27
对象：`src/port/vulkan/tri_draw.inc`、`include/nj2me/nj_port.h`、`src/midp/nj_lib_m3g.c`
关联：[高清面片的颜色/深度往返](2026-11-27-hd-triangle-transfer.md)、[高清面片批处理的取舍](2026-11-27-hd-journal-batching.md)

> **状态（2026-11-27 晚）**：本文的列表事务实验已被后续的确定性回放证伪——多包事务在输入逐位
> 相同的情况下输出不可复现（读到未初始化宿主内存）、有可见伪影且比逐包更慢，实现已回退，Vulkan
> 端口不再注册列表回调。完整证据与结论见 [高清面片的颜色/深度往返](2026-11-27-hd-triangle-transfer.md) §十二。

## 一、目的

前一轮用 `-DNJ_M3G_PROFILE=1` 测得 8× 原生高清的 M3G 记录开销集中在每次事务的
颜色（19.6 MB）+ 深度（19.6 MB）导入/导出，因而提出两条改进：把一帧的多次
M3G 通道合并成一个 Vulkan 列表事务，或在多包之间把深度留在 GPU 上。这一轮先把
列表事务实现出来，用确定性回放验证像素与耗时，再决定是否提交。

## 二、实现（已回退，仅作记录）

`nj_vk_renderer_triangles_list` 按 `nj_gfx_triangles.h` 的列表契约实现：

- `nj_gfx_triangles_list_validate` 校验版本、表面、深度、≤256 包、≤131072 三角形与不重叠；
- 逐包 `analyze()`，把各自的纹理视图偏移抬到共享纹理池的位置（`view.offset += texel_base`），
  顶点按包 ROI 归一化后顺序追加，记录每包的顶点基址；
- 取所有包 ROI 的并集，`target()` 一次；整批只做一次颜色/深度导入、一次渲染通道
  （`renderArea` = 并集，逐包设置 viewport/scissor 与 `firstVertex`）、一次导出、一次
  submit + fence；失败路径 `goto decline`，一个像素都不写；
- 单包列表直接转调原来的单包事务，保持小场景零额外开销。

`nj_gfx_triangles_list` 的等价性最初由 `tests/test_gfx_vulkan.c` 的 `ordered_lists()` 覆盖
（现在叫 `batch_equivalence()`，已扩展到 5 个包、各带不同纹理/深度/雾/blend）：列表结果与
逐包调用颜色逐字节一致、深度 keys 差 ≤1；带非法混合的包使整批 DECLINE 且不改动任何像素；
单包列表与单包事务一致。

M3G 侧另外加了 `gfx_hd_journal` 端口能力位与 `jr_hd_flush_queued()`，在 FSR2 采样与
软件高清回退前提交队列，并在高清镜像 collect/dispose 前提交，保证 CPU 读到的镜像
已落盘。

## 三、测量

装置：`tests/games/毁灭战士3_10598.jar`，`--fps 1000 --render-scale 8 --ssaa 1
--asset-filter pixel-art --run-frames 240 --dump-every 240`，
`--keys '200:down:down,205:down:up,215:fire:down,216:fire:up'`（进入 NEW GAME），
桌面 RTX 5080 + NVIDIA Vulkan。逐包结果可复现：240 帧时 00240.ppm 的 sha256
`0da0690e0f699f8e6ab7bc258d7808b15168e3bbb7d58b0ca408b4f3770c7de0`。

| 观察 | 结果 |
| --- | --- |
| 高清包是否都被加速路径接受 | `[m3ghd] calls=8675 ok=8675 decline=0 software=0 temporal=0`（240 帧，约 36.1 包/帧） |
| 开启高清 journal 后的批大小 | 每批 52–105 包，并集 1920×2560 |
| 批处理像素一致性 | 00240.ppm 与逐包结果不同：MAE 13.6/255，96.7% 像素变化 |
| 批处理耗时 | 240 帧 27.5 s（8.6 fps）；逐包 27.2–27.9 s（8.6–8.9 fps）→ 无收益 |
| 逻辑分辨率注册列表回调后 | 1× 运行的第 120/180/240 帧与逐包结果不同，而逐包基线与自身两次运行完全一致，排除游戏计时抖动 |

## 四、结论

列表事务在后端层是逐字节正确的（单元测试覆盖），但把它接到 M3G 引擎上并不等价：
一帧内的 M3G 通道之间夹着 CPU 直接写同一张高清镜像的操作（LCDUI/HUD 与游戏的
软件背景），而列表只在提交时导入一次并集，中途的 CPU 内容不会被重新导入，因此
像素会变。要让批处理成立，需要「队列打开期间没有任何 CPU 写镜像」的保证，而该
保证在现有 M3G 流程里不存在。

更重要的是**即使不计正确性，批处理也没有更快**（27.5 s 对 27.2–27.9 s）。这与上一轮
「每次事务的颜色/深度往返是 8× 主要开销」的推断不符：8× 的额外时间分散在 36 包/帧
的所有 GPU 绘制与逐包固定开销上，单次大事务并不会减少 GPU 的实际填充量。上一轮
`-DNJ_M3G_PROFILE=1` 的 pump 数值来自不同的开关组合，不能直接外推。

因此本轮的代码改动全部回退，仓库保持在 `5f4fa584`。可继续的方向是**深度驻留**而不是
批处理：深度键只有软件回退/清屏才会由 CPU 写，本装置在 8× 下软件与 FSR2 回退均为 0，
所以让 D32 附件在连续加速包之间保持为权威副本、只在 CPU 真的要读深度前导出，可省掉
每包近一半的往返；颜色不能同样处理，因为 2D 绘制会直接写颜色。落地前需要给软件深度
写入（软件高清走位、清屏）加一条失效回调，确保下一次加速包会重新导入深度。

## 五、复现

```sh
# 基线（逐包）
cmake -S . -B /tmp/nj-base -DCMAKE_BUILD_TYPE=Release -DNJ_BUILD_CLI=ON \
  -DNJ_BUILD_SDL2=ON -DNJ_ENABLE_GFX_ACCEL=1 -DNJ_ENABLE_VULKAN=ON \
  -DNJ_ENABLE_FSR2=ON -DNJ_PIXFMT=NJ_PIXFMT_RGB565 \
  -DNJ_FSR2_SDK_ROOT=$PWD/.local-tools/debug-runs/fsr2-sdk/install
cmake --build /tmp/nj-base -j 12 --target nj2me-cli

VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json __NV_PRIME_RENDER_OFFLOAD=1 \
/tmp/nj-base/nj2me --graphics vulkan --render-scale 8 --ssaa 1 \
  --asset-filter pixel-art --dump-every 240 --port headless --size 240x320 \
  --heap 16 --no-heap-grow --fps 1000 --run-frames 240 --stats \
  --keys '200:down:down,205:down:up,215:fire:down,216:fire:up' \
  --fs-dir /tmp/hd/fs/ --rms-dir /tmp/hd/rms/ --dump-frames /tmp/hd/frames/ \
  tests/games/毁灭战士3_10598.jar
```

单元测试：`test_gfx_vulkan`（`ordered_lists`）、`test_gfx_triangles`、
`test_gfx_triangles_gles2`、`test_m3g_triangles`、`test_m3g_hd`。
