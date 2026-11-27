# 高清镜面/深度 GPU 常驻（会话化）方案

日期：2026-11-27 晚（下一轮工作起点）
关联：[高清面片的颜色/深度往返](2026-11-27-hd-triangle-transfer.md)（§1、§4、§8、§12）、
[高清面片批处理的取舍](2026-11-27-hd-journal-batching.md)、[继续计划](2026-11-27-continue-plan.md)

## 一、目标

目标②：让高清镜面（颜色）和深度缓冲在 GPU 上常驻，跨多个 M3G 提交只付一次
导入/导出，而不是每个提交都做一次全 ROI 往返。这是上一轮 §4/§5/§8 指向的方向，
也是 §12 之后的唯一剩余杠杆（多包合并已经证明不可用）。

## 二、实测依据（上一轮的数据）

- 8× 下物理镜面 1920×2560：颜色 19.66 MB、深度 keys 19.66 MB；一次事务的颜色+深度
  导入导出合计约 78.6 MB 的 CPU↔GPU 流量。按实测约 2 GB/s 的往返带宽，固定开销
  约 40 ms/事务（§1）。
- 场内 400 帧：38162 个包、619 次事务（约 61.7 包/事务，1.55 事务/帧），事务数由
  每帧 M3G `render()` 调用之间必须让 CPU 观察目标（bind/release/clear/PRESENT）决定。
- 标题画面 200 帧：218 个包全部 1 包/事务（每次 append 都被下一次 pump 立即提交），
  会话化没有收益；收益只在场内。
- 多包合并（一次事务装多包）已被证伪：输入逐位相同时输出不可复现、依赖 ASLR/
  堆扰动、有可见伪影且更慢（§12.3），Vulkan 端口已回到单包路径。
- 驻留会话的旧尝试（时序驻留整帧）失败在「CPU 帧内写同一物理镜像 + 并集矩形不减
  传输」（hd-resident-session.md），但那次是每包回放层面；本次要用**已有契约的
  正式 session API**，把导入/导出从每事务降到每会话。

## 三、已有钩子（不需要新契约）

`include/nj2me/nj_gfx_triangles.h:128-172` 已经定义了持久会话契约，`nj_port.h`
也已有字段（:866-880）：

- `gfx_triangles_begin(ud, nj_gfx_triangles_session*)`：从当前 CPU 颜色（和声明的深度）
  播种会话矩形，DONE 后会话内目标由 GPU 持有，CPU 缓冲 stale。
- 会话期间的提交用单包或列表回调；其 DONE 只表示「已排队进会话」，CPU 不可观察。
- `gfx_triangles_end(ud, &submitted)`：读回会话矩形并原子提交 CPU 颜色（声明了深度则
  含深度）；`abort(ud)` 丢弃且不碰 CPU 像素。会话期间某个提交 decline 即会话失效，
  调用方必须 abort 并软件重放全部已提交包。
- 约束：`begin` 的 `nj_gfx_triangles_session` 带 surface/depth/depth_stride 与矩形；
  会话内提交必须落在矩形内。

Vulkan 后端的物理附件本来就是持久对象（`nj_gfx_vulkan.c` 的 `target()` 只增不减，
`r->color`/`r->depth` 跨事务复用），所以会话化的核心只是：把 import/export 挪到
begin/end，中间只做 render pass + submit。

## 四、方案 A（推荐，先做）

1. **Vulkan 会话实现**（src/port/vulkan/tri_draw.inc）：
   - begin：记录会话矩形、surface/depth 指针与 stride、`session_open=1`；`target()` 到
     矩形尺寸；一次 `transfer_rect` 导入颜色 + 深度 keys（和当前单包 execute 的 seed
     相同）；不做提交（惰性）。
   - 会话内单包/list 提交：沿用现有着色/管线路径，但只 `pass_begin/pass_packet`，
     写入同一组附件；不 export、不 wait（或在事务上限处安全 submit 但保持附件 resident）。
   - end：最后一次 submit + fence，然后一次 export 颜色（和深度）；清 session 标志。
   - abort：丢弃已排队命令，清 session 标志，不碰 CPU。
   - 失败路径：任何提交 DECLINE 时按契约让会话失效（调用方 abort + 软件重放）。
2. **M3G HD 路径接线**（src/midp/nj_lib_m3g.c）：当 `hd_scale>1` 且端口提供会话钩子
   且 `gfx_triangles_session` 允许时，在第一次成功的 HD GPU 绘制打开会话；所有已有的
   «必须让 CPU 观察目标» 点（nj_midp_gfx_load 的 FLUSH_GFX、present 的 FLUSH_PRESENT、
   bind/release/clear、FSR2 采样、createImage/readback）先 `end` 会话再 flush，保证 CPU
   看到的是最新像素；会话结束后的下一次 HD 绘制重新 begin。
3. **验证与收益**：
   - 单元测试：`tests/test_gfx_vulkan.c` 加 session 用例（begin→多包→end 与逐包参考
     逐位一致；abort 不改像素；decline 使会话失效）。
   - 确定性回放：8× 帧 60/120/180 必须与当前单包路径一致（`846057676cfed67a`/
     `c65baebe4730fcbe`/`dc5cb60109dd03cb`）；1× 400 帧 `79fb8934…`/`82272d8e…`/
     `c911305c…` 不变。允许的差异只有契约明确允许的深度 keys ±1，且要用 1×/2× 与
     软件参考验证。
   - 耗时：8× 场内 400 帧、机器空闲时 A/B（外部 wall + M3GPROF pump 总和），目标
     ≥20% 提升；否则不默认开启。
4. **落地方式**：先做成可选钩子（默认关闭，env/构建开关），跑通确定性闸门后再决定
   是否默认。

## 五、方案 B（后续，工作量大）

2D 图元（矩形/线/弧/多边形/图像 blend/ROP/文字）也上 GPU，颜色+深度全程常驻 GPU，
present 直接消费 GPU 图像（`display_hd` 需要能接 GPU 帧，headless 用一次 readback），
彻底去掉每帧的 CPU 往返。需要新的 2D GPU 光栅器/合成器，风险和范围都比 A 大一个
量级，放到 A 验证收益之后再评估。

## 六、风险与回退

- 会话期间 CPU 缓冲 stale，任何漏掉的 flush 点都会让 Java 读到旧像素（HUD/getRGB/
  createImage/FSR2 采样）；必须逐一核对 M3G、LCDUI、media、game 的所有入口。
- 与 journal/列表路径的交互：会话内可以继续用 journal 排队，但 commit 不再 export；
  decline 时要 abort 并软件重放，容易重复绘制，先禁用 journal 打开会话。
- 无收益/回归时：钩子默认 NULL 即完全回到当前单包路径，代码可独立回退。

## 七、下一步（M1）

实现 Vulkan session 钩子 + 单元测试，先不接 M3G；用 1×/2× 的 test_m3g_hd 与
test_gfx_vulkan 验证像素等价，再接 M3G HD 路径跑确定性闸门与耗时。
