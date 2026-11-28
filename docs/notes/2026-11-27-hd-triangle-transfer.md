# 高清面片的颜色/深度往返与 `--stats` 三角形计数

高清（HD）倍率下 3D 面片比 1× 慢得多，但此前只有总耗时，没有把耗时归到具体阶段。
这份笔记用确定性桌面回放把 8× 的额外耗时定位到「颜色 + 深度」的 CPU↔GPU 往返，
并记录了顺带发现并修复的一个三角形计数缺陷。

## 一、测量装置

确定性回放（与 FSR 画质对照同一装置）：

```bash
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
__NV_PRIME_RENDER_OFFLOAD=1 ./nj2me --graphics vulkan --render-scale 8 \
  --ssaa 1 --asset-filter pixel-art --port headless --size 240x320 \
  --heap 16 --no-heap-grow --fps 1000 --run-frames 1000 --stats \
  --fs-dir DIR/fs/ --rms-dir DIR/rms/ tests/games/毁灭战士3_10598.jar
```

阶段归属用 `-DNJ_M3G_PROFILE=1` 构建（`src/midp/nj_lib_m3g.c` 内已有的开发期计时，
每 600 次 append 打印一行 `M3GPROF`）：

| 倍率 | 帧数 | 总耗时 | walk | append | pump | 每次 pump |
| --- | --- | --- | --- | --- | --- | --- |
| 2× | 1000 | 21.8 s | 105 ms | 62 ms | 3286 ms / 1199 次 | 2.7 ms |
| 8× | 1000 | 132.8 s | 112 ms | 69 ms | 48333 ms / 1199 次 | 40 ms |

walk（软件收集/raster）与 append（把命令拷进 journal）几乎不随倍率变化；
**pump 从 2.7 ms 涨到 40 ms，且与高清镜面像素数成正比**。
pump 即一次面片事务：把颜色（1920×2560×4 = 19.6 MB）和深度（同尺寸的 CPU key，19.6 MB）
导入 GPU，画完再把两者导出回 CPU，合计约 78 MB，实测有效带宽约 2 GB/s。

因此 8× 相对 1× 多出的约 50 ms/帧，几乎全部是这一次全分辨率颜色+深度往返。
（`gprof` 在这里不可用：渲染在 worker 线程上，主线程采样只覆盖约 8% 的时间。）

## 二、批处理事务的结论

为验证「把一帧内的多个高清面片包合并成一次导入/导出」是否值得，实现了
Vulkan 侧的列表事务（一次并集矩形导入、一次渲染 pass、一次导出），并临时允许
高清路径进入 journal。结果：

- 渲染结果逐位一致：8× 第 60 帧 base 与修改版 SHA256 相同；1× 前 215 帧完全一致
  （第 216 帧起是按键进入新场景，游戏本身对时序敏感，不能作为等同性比较）。
- 桌面确定性标题画面 240 帧：base 24.067 s / journal 24.195 s / 关闭 journal 24.089 s，
  即中性。标题画面每帧只有 1 个高清包，没有可合并空间。
- 模拟器（Doom3 标题，8×，Vulkan，present stats）：正式包 `draw 6.700 / swap 10.066 /
  frame 138.562 ms`，journal 包 `9.027 / 14.707 / 151.072 ms`，反而慢约 5%。
- 场内一帧有多个包时，并集矩形导入/导出本身仍是一次全分辨率往返，且设备不支持
  常驻会话时会退化为「拒绝 + 重放」，抵消收益。

结论：只做「一次事务里多包合并」不能解决问题，列表事务与高清 journal 开关已回退。
需要的是减少往返本身（脏标记跳过冗余导入、深度驻留 GPU 设备），见第四节。

## 三、`--stats` 三角形计数缺陷（已修复）

`jr_hd_mesh` 在渲染器**副本**上调用高清后端，副本里的 `drawn` 计数被丢弃，
于是 `--stats` 的 `tris=` 只统计逻辑光栅化，高清加速实际光栅化的三角形没有计入：

- 修复前 8× 240 帧 `tris=83381`；修复后 `190010`（同一场景 1× 为 836，不受影响）；
- 2× 同样受影响：修复前 `82123`，修复后 `187292`，与物理光栅化的实际工作量一致。

修复方式：在 `jr_hd_mesh` 的每个返回分支把副本的增量折回
`r->drawn += hd.drawn - base;`（`base` 是副本创建时的计数）。
像素不受影响，修复后 8× 第 60 帧与基线逐位相同。

## 四、后续方向

1. **脏标记跳过冗余导入**：上一次导出后若 CPU 镜面没有被 2D/HUD 写过，
   则下一次事务的颜色导入可以省掉（GPU 附件内容与 CPU 副本一致）。
2. **深度设备驻留**：深度只在 GPU 上累积，CPU 侧仅在软件回退、Java 观察者或
   深度清除时才同步，可再省约 39 MB/帧。
3. 两者合计预计能把 8× 的 40 ms/次往返降到 10 ms 量级；
   实现需要镜面和深度的同步/脏标记设计，单独一轮推进。

## 五、修正：往返「流量」不是 8× 的瓶颈

第二节的批处理实验之后又做了逐包统计（开发期 `-DNJ_VK_PACKET_DBG=1`，每包打印
`pkt 宽x高 at x,y seed=… write=… tris=…`，退出时汇总）：

| 项目 | 数值（240 帧，8×，in-game 回放） |
| --- | --- |
| 面片包总数 | ≈7000（≈29 包/帧） |
| 颜色+深度传输像素 | ≈897 Mpx（`MB_px=6842` 换算） |
| 每帧传输像素 | ≈3.7 Mpx |
| 全程平均带宽 | ≈0.5 GB/s |

也就是说，颜色/深度的往返流量只用了总线带宽的几十分之一，**不是** 40 ms/pump 的来源。
pump 里占主导的是每次事务的固定开销：重置命令池、`vkQueueSubmit`、`vkWaitForFences`
的驱动/调度往返，以及 CPU 侧把颜色搬进/搬出 staging 的 memcpy；它按「事务次数」
计费而不是按字节计费。这解释了为什么按包合并（把 29 次事务压成 49 次/240 帧）
没有提速：并集矩形把传输量放大，抵消了固定开销的减少，同时还要额外的 journal
拷贝和镜像一致性维护。

## 六、深度 key ↔ float 转换的向量化

每包还要做一次 CPU 侧的深度换算（软件光栅的 24 位整数 key ↔ `D32_SFLOAT`）：

| 方向 | 标量 | SSE2 / NEON | 加速 |
| --- | --- | --- | --- |
| key → float（导入） | 0.96 ns/px（2.51 ms / 261 万像素） | 0.37 ns/px（0.98 ms） | 2.6× |
| float → key（导出） | 0.35 ns/px（0.91 ms） | 0.32 ns/px（0.84 ms） | 1.1× |

实现是 `src/port/vulkan/tri_draw.inc` 里的 `depth_import`/`depth_export`：
SSE2（x86_64 恒可用）+ NEON（`__ARM_NEON`）+ 标量尾部，舍入规则与原来的标量表达式
逐位一致（往返 261 万像素 0 处不同，桌面 8× 回放帧 sha256 与改前完全相同）。

端到端影响：桌面 8× in-game 回放 27.12/26.60 s → 27.12/26.72 s（噪声内）；
AVD 8× 三维 `frame` 中位数 73–112 ms（改前）对 95–118 ms（改后），同样落在
模拟器的测量噪声里。换算只占 8× 总耗时约 3%，所以量不出来是预期结果——保留它
是因为它是零风险的逐位等价优化，在 CPU 相对更慢的 ARM 手机上收益方向一致。

## 七、逐阶段计时：GPU 侧的拷贝/填充等待才是面片路径的大头

给 `nj_vk_renderer_triangles` 的每个阶段加了临时计时（`NJ_VK_PROFILE=1`，退出前每 600 包
打印一行；测完已从代码中移除），同一确定性 in-game 回放 240 帧：

| 阶段 | 2×（85 包/帧） | 8×（152 包/帧） |
| --- | --- | --- |
| prep（analyze/target/reserve/几何上传） | 56 ms | 1479 ms |
| seed（颜色+深度拷入 staging） | 57 ms | 1478 ms |
| exec（记录 + 提交 + 等待 + 回读同步） | 723 ms | 5757 ms |
| └ 其中 `vkQueueSubmit` | — | 53 ms |
| └ 其中 `vkWaitForFences` | — | **5589 ms（占 exec 97%）** |
| export（从 staging 拷回镜面） | 64 ms | 1551 ms |
| 面片路径合计 / 总耗时 | 0.90 s / 13.9 s（6.5%） | 10.3 s / 29.4 s（**35%**） |

结论：

- 提交本身只有 53 ms，**等待 GPU 完成**才是 exec 的全部成本：平均每个包约 0.15 ms，
  由整包 ROI 的颜色+深度「拷入 → 绘制 → 拷出」GPU 工作量决定，与像素数成正比。
- 因此优化方向不是批处理（事务次数）也不是传输带宽（0.5 GB/s），而是**减少每个包的
  GPU 拷贝次数**：同一 ROI 的连续包（M3G 每帧多 pass 的典型形态，8× 实测 152 包/帧）
  如果颜色/深度能驻留 GPU，只在该串包的首尾各拷一次，就能省掉大部分等待。
- 这需要一个「驻留会话」：会话期间 CPU 侧镜面失效，CPU 写入前必须先提交/结束会话。
  GLES2 后端已有 begin/end 会话原语，Vulkan 后端还没有；下一轮按此实现并用同一
  回放做逐位对照与耗时对照。

## 八、驻留会话尝试（否）与每包 CPU 侧小优化（是）

**驻留会话**：按七的推论实现过一版「同一表面连续包共用一个 GPU 驻留会话」（Vulkan begin/end
+ 高清单包开启 journal），逐位对照失败：8× in-game 帧 00240 的 sha256 从
`0da0690e…` 变成 `f27aab0b…`，MAE 27.2，98.3% 像素改变（画面整体偏亮）。原因有两条：
① M3G 的多个 pass 之间 CPU 还会往同一块高清镜面写入（Java 2D/HUD、M3G 背景/clear 路径），
其中并非每条写入都经过 journal 的 flush 点，驻留内容覆盖了新写入的 CPU 像素；
② journal flush 的并集矩形并不比逐包 ROI 小（包 ROI 已经是三角形包围盒），8× 下传输量反而更大。
1× 对照（120 帧）逐位一致、12.040 s vs 12.031 s，说明会话机制本身可用，但高清镜面不是
「一整帧都可驻留」的对象。结论：真正的杠杆是把整帧镜面（颜色+深度）与 2D 合成都搬到 GPU
命令里，属于跨 GL/Vulkan 与 MIDP 合成层的架构改动。代码已全部回退，详见
[高清驻留会话](2026-11-27-hd-resident-session.md)。

**每包 CPU 侧小优化**（保留）：颜色回读/回写从 `memcpy` 换成 `_mm_stream_si128` 非临时存储
（大目标下这些行拷贝的容量远超缓存，普通存储会先按行取回再覆盖），以及纹理描述符缓存
（画出纹理池 realloc 次数远小于包数，原来每个包都 `vkUpdateDescriptorSets`）。

用临时 `NJ_VK_XFER_PROF=1` 只计 `transfer_cpu` 时间（8× 标题回放 240 帧，各 2 次）：

| 版本 | 调用数 | transfer CPU 时间 |
| --- | --- | --- |
| memcpy + 每包更新描述符 | 247 | 1.746 s / 1.684 s |
| 非临时存储 + 描述符缓存 | 246 | **1.637 s / 1.518 s** |

即这一项 CPU 时间降 6–10%（每 240 帧省 0.1–0.17 s）。整轮墙钟仍在噪声内（24.7 s vs
24.5–24.8 s），因为颜色/深度 CPU 拷贝只占整轮的约 7%；收益主要体现在 CPU 偏弱的设备上。
逐位一致性：确定性 8× in-game 帧 00240 sha256 仍为
`0da0690e0f699f8e6ab7bc258d7808b15168e3bbb7d58b0ca408b4f3770c7de0`。

另有一条否定的尝试：把两张附件的布局全程固定在 `VK_IMAGE_LAYOUT_GENERAL`（避免每包
UNDEFINED→TRANSFER_DST 与 render pass 的布局转换），逐位一致但耗时没有变化（标题回放
23.5/26.9 s vs 24.5/23.8 s）；GENERAL 在移动端 tiled GPU 上通常更慢，因此没有保留。

## 九、现状复盘：第二轮「批处理无用」的结论可能被一个列表后端 bug 污染

把 6–9 轮的改动合起来看，8× 面片路径的账本（同一确定性 in-game 回放 240 帧）大致是：

| 项 | 2× | 8× | 说明 |
| --- | --- | --- | --- |
| 总耗时 | 13.9 s | 29.4 s | 8× 每帧多约 38 个包 |
| 面片路径（prep+seed+exec+export） | 0.90 s（6.5 %） | 10.3 s（35 %） | 其中 `vkWaitForFences` 5.59 s |
| 其余（游戏逻辑 / 软件走查 / 2D / 呈现） | 13.0 s | 19.1 s | 与 8× 无直接关系 |
| 逐包 CPU 颜色拷贝 | — | 1.52–1.64 s（非临时存储后） | 见第八节 |

第二轮试过的「列表事务 + 高清 journal 批处理」当时判为「不省时」，但那次测到的像素并不一致：
帧 00240 与逐包渲染差 MAE 13.6、96.7 % 像素不同，而且**注册 list 回调后连 1× 的第 120/180/240 帧
都与可复现基线不同**。1× 的 M3G journal 本来就使用 list（GLES2 后端一直如此），所以这更像是
**Vulkan 列表实现自身有等价性 bug**（单元测试里只覆盖了两个简单包），而不是「批处理没有收益」。
也就是说，那次否定结论不可靠：批处理本应把「每包一次整 ROI 颜色+深度往返」变成「每帧一次」，
在 38 包/帧的 8× 形态下上限是省掉约 5.6 s 的 GPU 等待。

下一轮应先把 Vulkan 列表做实（重点怀疑点：多包纹理池偏移/material 里的纹理偏移没有按包基址
修正、包间共享顶点缓冲的 firstVertex 基址、以及 seed/write depth 在并集矩形上的语义），用
逐包 vs 列表的逐位对照验证到 1× 也完全一致，再重测高清 journal 批处理；只有逐位一致后的耗时
数据才算数。

## 十、Vulkan 列表回调的参考实现与逐位对照

给 `src/port/vulkan/tri_draw.inc` 补上了 `nj_vk_renderer_triangles_list`：它不做任何批处理，
按顺序对每个包调用单包参考实现，任何一个包 decline 就让整个列表 decline（调用方随后软件重放），
`*submitted` 为各包之和。这样 Vulkan 后端就能进入 M3G journal 的列表事务路径，同时保持
「先正确、后优化」的顺序。注册在 `nj_vk_renderer_attach`（`gfx_triangles_list` +
`gfx_triangles_list_ud`），高清驻留/会话回调仍为 NULL。
（2026-11-27 晚更正：端口现已不再注册这两个回调，列表入口只保留给直接调用者与测试；
原因是多包事务输出不可复现，详见 §十二。）

逐位对照（确定性 1× 回放，`--render-scale 1`，转场前的帧）：

| 帧 | 逐包基线 | 列表参考实现 |
| --- | --- | --- |
| 00100 | `7bfa322b77576619…` | `7bfa322b77576619…` |
| 00200 | `8169fa658281e1de…` | `8169fa658281e1de…` |

（转场之后的帧不可用作对照：Doom3 在 NEW GAME 后对时序敏感，同一二进制两次运行也会分叉。）

结论：列表契约在 Vulkan 后端的实现本身是逐位精确的，因此第九节里 8× 批处理的像素差异
只可能来自「并集矩形 + 驻留」的批处理层，而不是列表接线错误；下一轮可以在这个参考实现
之上做真正的并把逐位对照作为闸门。

> **2026-11-27 晚更正**：上面的逐位对照只在「每个事务恰好 1 包」时成立；多包事务在输入完全
> 相同的情况下输出不可复现，见 §十二。


## 十一、列表批处理：单命令缓冲、单次提交（逐位等价）

在参考实现之上把列表事务改成一次执行：整张列表只做一次并集矩形的颜色/深度导入、一次渲染过程（每个包用动态 viewport/scissor 放到自己的偏移）、一次并集导出、一次 `vkQueueSubmit` + `vkWaitForFences`。列表调用内部不可能有宿主写镜面，因此把各包背靠背画在同一附件上，与逐包“导出→再导入”的参考语义等价。

> **2026-11-27 晚更正**：本节「逐位等价」与 −1.2% 的测量都只覆盖每次 1 包的场景。多包事务后来被
> 证明会读到未初始化的宿主内存，像素不可复现、比逐包更慢且有可见伪影，批处理实现已回退，见 §十二。

- 逐位等价：确定性 Doom3 1× 回放（240 帧）第 100/200 帧 sha256 分别 `7bfa322b77576619b1aff958e04dc690814144d6f64af923253a9a82693bf9bb`、`8169fa658281e1de7a78ef12622c7077f66a757bff62b78c20597c416c6b2d94`，与逐包参考实现完全一致。
- 耗时（同一二进制，`NJ_VK_NO_LIST=1` 对照，各 3 次，240 帧 1× 回放）：关闭 12.954/12.845/12.843 s，开启 12.590/12.695/12.693 s ⇒ 中位数 12.845 → 12.693 s（约 −1.2%）。1× 每帧只有约 4 个包，收益有限；8× 的 ~38 包/帧才是批处理的目标，但要等高清路径接入 journal（`gfx_hd_journal`）后才会经过列表。
- 下一步：把高清面片路径接入列表（需恢复 `jr_hd_flush_queued` 等刷新点），用同一逐位闸门（转场前 1× 帧 + 8× 确定性回放）验证后测量 8× 收益。

### 高清路径接批处理的首次尝试（未启用）

把高清面片路径接入 journal（`gfx_hd_journal` + 刷新点）后，用同一 8× 确定性回放对比同一二进制的开关两种模式：第 60 帧一致，第 120/180 帧不一致（`e6f4ca71…`/`ca71d0b8…` vs `62525976…`/`4c0afc53…`），说明列表语义等价的前提在这次接入里不成立——某个宿主写镜面的路径没有经过刷新点。为了不把不确定的开关交付，默认关闭，仅保留 `NJ_VK_HD_JOURNAL=1` 调试入口；下一步需要先定位所有镜面写入点（M3G 背景/清屏、2D、FSR2 回填）再重开。

> **2026-11-27 晚更正**：当时把 8× 的帧 120/180 差异归因于「未经过刷新点的镜面写入」，这个
> 结论是错的：同一二进制连跑三次墙钟回放，帧 120 差 764~2253 px（最大 1-2）、帧 180 差最多
> 768K px（最大 46-77），游戏状态本身已分叉；当时保留的开关两份 dump 之间帧 120 只差 478 px、
> 最大 1 LSB，属回放噪声。真正的问题是对时序敏感的回放缺少虚拟时钟，以及多包事务的未初始化读取
> （§十二）。


## 十二、确定性回放与批处理实验的收尾（决定性结论）

### 12.1 确定性回放装置（已落地）

- CLI 新增 `--deterministic`（`nj_run_opts.deterministic`）：通过 `nj_vm_set_deterministic()` 固定
  `java.util.Random` 的播种，并把 headless 端口放进虚拟时钟。
- headless 端口的虚拟时钟模型：`now_ms` 从 2000-01-01T00:00:00Z 起按虚拟毫秒走；每呈现一帧推进
  33 ms（逻辑 30 fps，真实墙钟可以远慢于它）；`Thread.sleep` 只推进虚拟时间、不真正睡眠；
  `Date`/`Calendar` 读同一虚拟日历（时区偏移 0）。日历钩子仅在启用虚拟时钟时挂载，且只认自己的
  状态指针，因此「调用方替换 `port.ud`」的用法不会再崩（`tests/test_jit` 曾因此段错误，已修）。
- 8× 回放（Doom3，200 帧）现在逐位可复现：帧 60/120/180 = `846057676cfed67a` /
  `c65baebe4730fcbe` / `dc5cb60109dd03cb`（journal 开关无关，因为 Vulkan 不再注册 list 回调）。
- 1× 回放（400 帧，转场前的帧）同样可复现：帧 100/200/300/400 = `79fb89347746ef64` /
  `82272d8e4f649f38` / `c911305c5e3a0210` / `c911305c5e3a0210`。
- 旧闸门值（`1481221e…`/`62525976…`/`4c0afc53…`）来自墙钟回放，只是当时那一次运行的样本，
  **不能再当逐位闸门**。

### 12.2 高清 journal 的收益实测

- 标题画面（Doom3，8×，200 帧）：每次 render 只有 1 个包，append 后立即被 pump 提交 ⇒ 每个事务
  1 包，批处理没有合并对象。开启 journal：13.58 s → 14.31 s（+5.4%），另外还快照了 55.8 MB 纹理。
- 场内（Doom3，8×，400 帧，200/205/215/216 按键进入游戏）：journal **确实在批处理**——38162 个包
  进入 619 次 list 事务（≈61.7 包/事务），append 拷贝总计 166.9 ms，无失败、无 CPU 重放。但墙钟
  75-92 s 对关闭时的约 63 s（同期机器有其他负载，趋势仍明显），pump 的 GPU 等待合计 47.1 s。
  并集矩形的 import/export 比逐包 ROI 更贵，批处理在真正需要它的场内反而更慢。
- 结论：高清 journal 没有收益，且下面要说的多包事务还有正确性缺陷，因此不再保留。

### 12.3 多包事务的决定性缺陷（本次实验的核心发现）

> **2026-11-27 深夜更正**：本节把「多包事务输出不可复现」归因于 Vulkan host 侧的未初始化读取，
> 这个结论是错的。真正原因在 M3G journal 内部：纹理池扩容后没有把已排队三角形里的纹理指针
> 一起搬走，导致后续读取释放后的旧池。修复后多包事务（含 8× HD 会话）完全确定，详见 §十三。
> 本节其余测量（耗时、图像差、ASLR/MALLOC_PERTURB_ 现象）仍然有效，只是解释换了。


1× 回放（`--render-scale 1` 也会进 journal）复现最快，3-4 秒一次：

- `NJ_M3G_JOURNAL_MAX=1`（每事务 1 包）连跑三次，帧 300/400 全部为
  `c911305c5e3a0210`/`c911305c5e3a0210`，且与完全不注册 list 回调（`NJ_VK_NO_LIST=1`，仅调试用）
  逐位一致 ⇒ **单包事务是精确的，逐包路径和 journal 数据路径本身没有问题**。
- `NJ_M3G_JOURNAL_MAX=8`（每事务 8 包）：每次运行的帧 300/400 都不同（多次采样例如
  `44f9e0e3…`/`7362f9a5…`、`c42fc7be…`/`a3c86852…`、`a49fba8b…`/`a8da906e…`），且与 cap=1 不同。
- 关闭 ASLR（`setarch -R`）后 cap=8 才稳定成 `a49fba8b…`/`a8da906e…`；`MALLOC_PERTURB_=0/1/170`
  得到三组不同像素 ⇒ 多包事务路径读取了未初始化的宿主内存，且读到的是字节值而非地址。
- 输入侧已排除：在 profile 构建里给每个 list 事务打印包/顶点/材质/纹理池的 FNV 与并集矩形颜色、
  深度 keys 的哈希（`[m3glist] n=… tris=… rc=… rect=… sig=… pool=… surf=… depth=…`），两次 cap=8
  运行共 5067 行逐位相同；首个分歧事务（`n=8 tris=40 rect=0,111,240x121`）的包流、纹理池、深度
  keys 和输入表面全部相同，但回写 CPU 表面的输出不同。把 journal 自己的 entries/textures/pool/
  命令副本全部 memset 0 也不改变分歧 ⇒ 未初始化读取不在 journal 缓冲，而在 list 事务的 Vulkan
  宿主侧（或输出回读路径）。
- 把 list 换回「逐包调用单包实现」的参考实现后，cap=8 仍不可复现 ⇒ 不是并集矩形批处理算法本身。
- 画面上不是 ±1 量化：帧 300 与逐包基线差 118,408 px（2.41%，最大差 104），帧 400 差 557,012 px
  （11.33%，最大差 125）；可见伪影包括地板上的彩虹棋盘格贴图、门位置偏移、HUD 附近细线。
- 这超出「契约允许与软件光栅器不逐位一致」的范围：同一输入序列必须给出确定输出。

### 12.4 决策与落地

- `nj_vk_renderer_attach` 不再注册 `gfx_triangles_list`/`gfx_triangles_list_ud`，
  `gfx_hd_journal` 恒为 0，`NJ_VK_HD_JOURNAL` 环境变量删除。M3G 仅在端口注册 list 回调时才使用
  journal；list API 与 `nj_vk_renderer_triangles_list` 保留（GLES2 端口与直接调用者使用）。
- `src/port/vulkan/tri_draw.inc` 回退到 `f8cd96fa^` 的参考实现（逐包循环，任一包 decline 则整表
  decline）。带跟踪的批处理版本只留在会话临时文件里，未入库。
- `tests/test_gfx_vulkan.c` 更新注册断言（list 为 NULL），并新增 `batch_equivalence`：5 个包各带
  不同纹理/深度/雾/blend，list 委托结果与顺序单包调用逐字节颜色一致、深度 keys 差 ≤1，重复事务
  无跨事务脏读。这保证直接调用 renderer 的 list API 仍是精确的。
- profile 构建新增 M3GPROF 计数（append ok/fail、list 事务 ok/fail、replay、destroy、纹理快照）
  与 `NJ_M3G_JOURNAL_MAX` 诊断上限；release 构建零开销。
- 遗留风险：`nj_gfx_triangles_list_validate` 只做契约/逐包校验与内存不相交检查，不预检后端的尺寸
  与格式能力；如果列表中途被后端 decline，先前提交过的包会和调用者的软件重放重叠（双绘）。当前
  Vulkan 不注册 list，风险只在 GLES2 端口；修法是在列表入口做一次全量后端预检。

### 12.5 下一轮

高清 8× 三维若要提速，方向回到 §四/§五/§八 的架构结论：让整帧镜面（或至少深度）驻留 GPU，
并把 2D 合成放到 GPU 上，而不是在一个「每帧一次事务」的批处理上打转（GLES2 的驻留会话笔记也
证明并集矩形方案不行）。如果将来重开批处理，先做「[m3glist] 式输入哈希 + 逐包输出比对」的自动
闸门，并解决多包事务的未初始化读取；本机没有 Vulkan validation layer（
`/usr/share/vulkan/explicit_layer.d` 不存在），需要先安装或在另一台机器上用 validation/RenderDoc
复现。

### 十三、多包事务的真正根因与 GPU 常驻会话（2026-11-27 深夜）

#### 13.1 根因：journal 纹理池扩容后留下悬垂指针

`m3g_journal_append`（`src/midp/nj_lib_m3g.c`）把每个新出现的纹理快照进 `j->pool`，并把已排队
命令里的 `texture` 指针改写成 `j->pool + offset`。`m3g_journal_reserve` 的 pool 扩容分支会
`alloc + memcpy + nj_host_free(old)`，**但没有把已排队 entries 里指向旧 pool 的纹理指针搬到新
pool**。于是扩容之后，先前排队的三角形继续从已释放的内存采样：读到的是堆上残留字节，随 ASLR 和
`MALLOC_PERTURB_` 变化，于是出现「同一二进制、同一 guest 状态、图片每次不同」以及 §12.3 记录的
彩虹马赛克伪影。

只有多包事务会触发：单包事务里 pool 在首次 append 后立即 flush/销毁，永远不会扩容；多包事务
（`NJ_M3G_JOURNAL_MAX>1`、批处理 list、常驻会话）才会在事务内追加纹理并踩到旧指针。这也解释了
为什么「每事务 1 包」的逐位对照全部精确、而 cap≥2 立刻分叉。

#### 13.2 证据

profile 构建里给会话加临时 trace（`[vsess] record … v=<顶点哈希> t=<纹理哈希>`、
`[vsess] view … src=<单片纹理哈希>`）：两次运行的顶点哈希相同，但**上传给 GPU 的纹理字节哈希从
某一记录开始不同**，且同一 `texture_view` 的源字节哈希在两次运行间不同。由于 journal 的命令流
（包数、矩形、三角字段、池内容）此前已被证明逐位相同，剩下唯一变量就是 journal 实际交给渲染器
的 `texture` 指针所指向的字节——正好是旧 pool。

#### 13.3 修复

`m3g_journal_reserve` 的 pool 分支保存 `old = j->pool`、`used = j->pool_used`，扩容后遍历所有
已排队 entry 的三角形，把落在 `[old, old+used)` 的 `texture` 指针改成 `new + (tex - old)`，再释放
旧池。注释写明「已排队的三角形直指 pool，移动 pool 必须同步移动这些指针」。

#### 13.4 修复后的确定性验证

- 1× 400 帧、dump-every 20、与 journal 关闭基线逐帧对比：`NJ_M3G_JOURNAL_MAX=1/2/4` 与默认
  8192 全部两次运行逐位相同；cap=1/2/4 与基线**每一帧都相同**。默认 cap 与基线在帧 220–400
  不同，但每帧只有 2–5 个像素、最大通道差 8：这是会话内保持 float 深度、不逐包 requantize 的
  语义差（契约允许 half-LSB 内 ±1 key 的深度量化），不是缺陷。
- 8× 确定性闸门（200 帧、dump-every 60）：journal 关 = 会话开 = `846057676cfed67a` /
  `c65baebe4730fcbe` / `dc5cb60109dd03cb`，两次会话运行也完全一致。
- 8× 场内 400 帧：会话两次运行逐位相同；与关闭 journal 相比帧 300/400 差 13,633 px（0.28%，
  最大通道差 22，>8 的 2,173 px），同样是深度量化语义差，且不再随运行变化。

#### 13.5 Vulkan 常驻会话（M1）

`include/nj2me/nj_gfx_vulkan.h` 与 `src/port/vulkan/nj_gfx_vulkan.c` / `tri_draw.inc` 实现
`nj_gfx_triangles_session` 的 Vulkan 后端：

- begin：校验 surface/矩形/深度，创建会话用的 render pass（final layout = TRANSFER_DST）与
  framebuffer，把颜色（以及深度）一次性导入常驻附件，返回 DONE；会话期间 CPU 缓冲允许 stale。
- 提交：每次 `gfx_triangles`/`gfx_triangles_list` 都只记录一个 command buffer——**整批只开一次
  render pass**，用动态 viewport/scissor 把每个包放到自己的矩形，提交并等待 fence，不导出。
- end：颜色附件（写过深度时连同深度）各一次 `copyImageToBuffer`，写回 CPU 表面后关闭会话；
  abort 丢弃，不碰 CPU 表面。
- 状态全部放在 `nj_vk_renderer` 内（无全局变量，满足 `tools/check_no_statics.sh`）。
- 默认路径完全不变：只有 `NJ_VK_SESSION=1` 时 `nj_vk_renderer_attach` 才注册
  list/begin/end/abort 并打开 `gfx_hd_journal`；默认仍是精确单包路径。
- `tests/test_gfx_vulkan.c` 新增 `session_equivalence`：会话逐包提交 / 会话内一次 list 提交 4 包，
  分别与逐包参考对比（颜色逐字节一致、深度 keys 差 ≤1），并覆盖 abort 不改像素、越界包 decline、
  重复 grouped 事务可复现。

#### 13.6 8× 实测：批处理仍不划算

profile 构建、8× 场内 400 帧（keys 200/205/215/216）：

| 路径 | walk | append | pump | commit |
| --- | --- | --- | --- | --- |
| journal 关（逐包 GPU） | 513 ms | 30,212 ms | 0.2 ms | 0.4 ms |
| 会话（NJ_VK_SESSION=1） | 461 ms | 157 ms | 21,899 ms | 10,507 ms |

会话把 38,162 次提交收成 619 次，但 GPU 侧总时间反而略高（32.4 s vs 30.2 s）：会话矩形大多是
整屏（619 个会话里 369 个是 1920×2560，即颜色+深度各约 19.7 MB，一次导入+导出约 39 MB），
传输量与逐包 ROI 之和相当。release 二进制 8× 场内 400 帧墙钟 关闭 51.6/52.5 s、会话
52.1/76.3 s（第二次受机器负载干扰），没有可测量的收益。

结论：会话基础设施保留（deterministic、单测覆盖、opt-in），但不默认开启。真正能省的是**把
CPU↔GPU 往返本身去掉**，而不是把多次往返合成一次。

#### 13.7 下一步：镜面/深度 GPU 驻留与脏代次

1. 给物理镜面/深度加 CPU 写入代次与脏矩形：`m3g_clear` 的清屏改为 GPU 侧清理，2D 图元只累计
   脏矩形。
2. 会话 begin 只导入「自上次导出以来 CPU 写过」的矩形（大多数帧只有 HUD 的几小块），其余像素
   GPU 附件本来就是对的；深度同理。
3. 导出推迟到真正观察点（present 读回、createImage、软件回退、FSR2 采样），并只导出必要矩形。
4. 继续推进方案 B：把 2D 合成也放到 GPU，让 present 直接消费 GPU 图像，去掉每帧整屏读回。

## 十四、保留会话（contract v2）与 8× 时间去向

#### 14.1 保留会话：契约与后端

§13.7 设想的第一步是「会话结束后附件继续持有结果，下一次 begin 只导入脏矩形」。这次把它落成
了契约版本 2：

- `nj_gfx_triangles_session` 末尾追加 `const nj_gfx_session_rect *dirty; nj_u4 dirty_count;
  int retain; int depth_retain;`，`NJ_GFX_TRIANGLES_SESSION_VERSION` 升为 2。v1 调用者没有扩展、
  始终拿全量导入；v2 调用者声明 `retain` 后，backend 只有在「同一 surface/深度/矩形 + 上次 end
  成功保留」时才复用附件，且只把 `dirty` 颜色矩形重新导入；CPU 深度缓冲未动时（`depth_retain`）
  连深度导入也跳过。identity 不匹配、abort、或任何失败都回退全量导入；backend 不得读 stale
  附件。
- Vulkan 后端（`src/port/vulkan/tri_draw.inc`）：`nj_vk_renderer_triangles_begin` 在 abort 前
  捕获 `retained` 提示，`session_matches()` 比对 surface 指针/尺寸/格式/stride、深度指针/stride
  与矩形；`begin` 保留路径用 `transfer_rect_at` 把 dirty 颜色打包进 stage 并逐矩形
  `vkCmdCopyBufferToImage`（≤8 个矩形）。`end` 在 `session_keep` 时把两个附件从
  TRANSFER_SRC 迁回 TRANSFER_DST 并置 `session_retained`；abort 与非会话单包路径都会清掉它。
- `tests/test_gfx_vulkan.c` 新增 `retained_sessions`：第一次 retain 会话走全量导入、第二次
  retain+dirty 会话只导入小矩形，结果与「全量导入参考」颜色逐字节一致、深度 keys 差 ≤1；
  另外覆盖矩形不一致回退全量导入、abort 后保留结果失效、渲染器仍可用。
- 确定性：1× 400 帧会话两次运行逐位相同（帧 300/400 = `a2a5eaadea91ef3b`；与逐包基线
  `c911305c5e3a0210` 差 5 像素、最大通道差 8，仍是 float 深度与逐包 requantize 的语义差）；
  8× 闸门 200 帧与 journal 关基线逐位一致（`846057676cfed67a`/`c65baebe4730fcbe`/
  `dc5cb60109dd03cb`）。

#### 14.2 真实场景里保留路径没有命中

journal 侧已经接好 v2：`m3g_journal` 记住 `hd`，pump 时传 `retain=1`、dirty=完整会话矩形、
`depth_retain = hd_session_valid && hd_session_epoch == hd_depth_epoch`，begin 成功后再记 epoch。
但 8× 场内实测（`NJ_VK_SESSION_STATS=1`）显示 419–598 次 begin 全部
`keep=1 retained=0 depth_retain=0`：`m3g_hd_clear_depth` 每帧都清 CPU 深度 keys 并递增
`hd_depth_epoch`，于是 `depth_retain` 永远是 0，保留路径永不启用。即使启用，journal 目前把
dirty 报成整屏矩形，颜色导入也一分不省。这两点都指向 14.4 的结论：想靠会话省传输，先把 CPU
写入本身变成「诚实的脏矩形」。

#### 14.3 8× 时间去向：CPU 才是大头

profile 构建、8× 场内 400 帧（keys 200/205/215/216），同一台机器连续跑：

| 路径 | 墙钟 | user |
| --- | --- | --- |
| journal 关（逐包 GPU） | 40.1 s | 25.8 s |
| 会话（NJ_VK_SESSION=1） | 40.9 s | 28.1 s |

后端内部计时（`NJ_VK_PROFILE=1`，destroy 时打印 `[vkprof]`）：`sessions=619
import=8.903 export=6.135 render=3.238`，即 619 个会话的导入、导出、渲染等待合计约 18.2 s。
会话每次要整屏导入+导出颜色与深度（约 39 MB/会话，24 GB/400 帧），比逐包 ROI 之和更贵，
所以把提交合成一次并没有省下墙钟；多出来的 user 时间主要是 staging 打包/读回的 CPU 拷贝。

真正的大头是 user 时间：8× 每帧约 70 ms 的 CPU 工作，而 1× 整场只有约 5 ms/帧——差出来的
几乎全是 CPU 侧的 8× 高清 2D 合成（物理镜面 1920×2560 上的填充/文字/图像）与 journal 快照。
**在 CPU 合成和整屏读回不消失的前提下，任何 import/export 层面的优化天花板都低于 20% 目标。**

#### 14.4 结论与下一轮

1. 保留会话（contract v2）作为基础设施保留、继续 opt-in：确定性、单测覆盖、默认路径不变；
   但在 dirty 跟踪与 GPU 侧清屏落地前不会开启。
2. 8× 的下一杠杆不是「把多次往返合成一次」，而是**去掉 CPU 侧的 8× 2D 合成与整屏读回**：
   - 方案 B：2D 图元（填充/曲线/图像/文字）在 GPU 上合成到常驻镜面，present 直接消费 GPU 图像；
     这是唯一能把 user 时间从 ~25 s 压下去的路线。
   - 若继续会话路线：先做 (a) 2D/清屏写镜面的诚实脏矩形，(b) 深度清屏改为 GPU 侧
     `vkCmdClearDepthStencilImage`，两者都要动契约与 M3G 接线，且理论上限约 25% 传输。
3. 诊断工具保留：`NJ_VK_PROFILE=1` 打印会话 import/export/render 分解；`NJ_VK_SESSION=1`
   打开会话（Android 用属性 `debug.nj2me.vk_session`）；`NJ_M3G_JOURNAL_MAX` 限制每事务包数。

## 十五、常驻镜面 M1（2026-11-28）

- 目标③方案 B 的第一步：Vulkan 后端新增常驻颜色镜面对象，
  `nj_vk_renderer_mirror_create/clear/fill/export`（`src/port/vulkan/tri_draw.inc`、
  `include/nj2me/nj_gfx_vulkan.h`），内部用单附件 render pass（CLEAR/LOAD 两种）+
  `vkCmdClearAttachments` 做矩形填充，导出用一次 `vkCmdCopyImageToBuffer`。
- 单测（tests/test_gfx_vulkan.c `mirror_basics`）：320×240 全屏 clear 后逐像素校验、
  两个相互覆盖的矩形 fill 后逐像素校验，结果 `resident mirror: 0 mismatched pixels`。
- 基准（`--bench-mirror`）：1920×2560 per frame「clear + 8 fills + export」
  `min 8.32 ms / median 16.46 ms`（30 次，load≈9 的共享机器），而逐包全屏颜色+深度往返
  单次就要 18.40–20.60 ms（`--bench-resident`）⇒ 常驻合成路线的下限明显更低，
  值得继续 M2（纹理 quad/2D 图元接入、M3G 直接渲染到常驻镜面）。

## 十六、常驻镜面 M2a：纹理 quad 与常驻深度（2026-11-28）

- 常驻镜面补上常驻深度附件；render pass 结构与 `r->pass` 相同（颜色 + 深度），因此可直接复用
  既有三角管线（像素格式/子 pass 布局兼容）。
- 新增 `nj_vk_renderer_mirror_texture(mirror, src, sx,sy,sw,sh, dx,dy,dw,dh, filter, blend)`：
  任意源矩形→目标矩形的纹理 quad，支持 RGB565/RGB888/ARGB8888 源、nearest(208)/filtered 与
  replace(68)/alpha(64) 混合；顶点/纹理走既有顶点与纹理池与 push-constant 材质。
- 单测：2×2 ARGB8888 纹理最近邻放大到 32×32 与 CPU 参考逐像素一致（0 mismatches）；
  半透明黑（0x80000000）覆盖 0xff102030 背景误差 ≤2（0 outliers）。
- 基准（1920×2560，load≈8.7）：clear + 8 矩形 fill + 全屏纹理 quad + 导出
  `min 4.70 / median 6.38 ms`；同机 `--bench-resident` 逐包全屏颜色+深度往返
  `min 21.39 / median 24.40 ms`，纯填充版本 `min 4.64 / median 5.30 ms`。
- 结论：2D 合成搬到常驻镜面后的每帧下限（~5-6 ms）远低于当前 8× 的 ~100 ms/帧，
  下一步 M2b 把 `nj_gfx` 2D 调用点与 M3G HD 三角形接进常驻镜面。

## 十七、常驻镜面三角形路径（2026-11-28）

§十四 证明「会话导入/导出」无法把 8× 高清颜色+深度往返降到足以抵消 CPU 2D 合成成本；
§十六 的常驻镜面（M1/M2a）已能在 GPU 上常驻颜色与深度并做纹理 quad。本轮把三角批处理
也搬到常驻镜面上：

- `nj_vk_renderer_mirror_triangles(mirror, list, import, export_colour, &submitted)`
  （src/port/vulkan/tri_draw.inc）：校验列表后逐包 analyze、累计纹理全局偏移、上传顶点与
  纹理；`import` 时一次 `transfer_rect` 打包颜色+深度并 copyBufferToImage 到常驻附件；
  单个 render pass（renderArea=全镜面）内逐包设置 viewport/scissor 后 `draws_body`；
  一次 submit+fence；`export_colour` 时一次 `nj_vk_renderer_mirror_export` 把颜色写回 CPU
  surface。深度常驻 GPU、不回读（CPU keys 在批次边界保持 stale，符合列表契约的
  「调用者不得在分组包之间观察目标」）。
- 单测 `mirror_triangle_lists`：128×96、3 个包（各 2 个三角形、各自纹理），批量导出与
  「逐包各一次 import/export」逐字节一致（`0 mismatched pixels against sequential draws`）。
- 基准（1920×2560、62 个 64×64 包、load≈8.4）：批量 `min 15.76 ms / mean 25.33 ms`；
  逐包 import/export `min 921.58 ms / mean 1530.49 ms`。即同一批 62 包下，常驻路径约为
  逐包路径的 1/58；对当前场内「每次 journal 事务 ~62 包」的场景，GPU 往返部分可从
  约 40–49 ms 降到 16–25 ms。
- 未接线：MIDP 2D 图元/图像/文字与 M3G HD 渲染仍走旧的逐包/会话路径；下一步把它接到
  `nj_midp_hd_target`/`jr_hd_*` 与 `nj_gfx` 2D 调用点，再做 8× 截图/容差对照与 A/B。

## 十八、常驻镜面接线（M2b wiring，2026-11-28）

§十七 把三角形批处理搬上了常驻镜面，但 MIDP 2D 与 M3G 清屏/背景仍走 CPU，所以每帧仍要
在 CPU 与 GPU 之间搬运两次全屏颜色+深度。本轮把整条 8× 高清路径接到常驻镜面上：

- 端口契约（include/nj2me/nj_port.h）：新增 `hd_mirror_sync`（CPU 绘制前同步，必须返回非零
  才允许 CPU 写物理目标）、`hd_mirror_clear`（GPU 清矩形，颜色/深度可选）、`hd_mirror_image`
  （源矩形→目标矩形的纹理 quad）、`hd_mirror_drop`（HD 条目释放前丢弃 GPU 副本，防止地址
  复用继承旧内容）。`nj_render_mirror` 相应增加 `sync`/`sync_ud`（CPU 2D 物理绘制前调用）。
- Vulkan 后端（src/port/vulkan/nj_gfx_vulkan.c）：`nj_vk_resident` 按 CPU 目标指针匹配常驻
  镜面；`resident_clear` 全屏清走 `mirror_clear`、局部清走 `mirror_clear_rect`（首次使用先
  定义附件并从 CPU 导入颜色）；`resident_image` 走 `mirror_texture`（nearest、replace）；
  `resident_list` 对 ARGB8888 目标走 `mirror_triangles`（import mask：CPU 触碰过则导入颜色，
  深度仅在 CPU keys 可能过期时导入），其它格式委托精确单包列表；`resident_sync` 把 GPU 脏
  内容一次导出；present 前同步。
- M3G（src/midp/nj_lib_m3g.c）：`m3g_clear` 优先 `hd_mirror_clear`（视口非全屏时回退 CPU
  填充）；`m3g_background_image` 的最近邻缩放改由 GPU quad 完成（REPEAT 模式仍走 CPU）；
  三角形经 journal 一次 list 提交到常驻镜面，深度常驻 GPU、不回读。
- 开关：`NJ_VK_RESIDENT=1`（Android：`debug.nj2me.vk_resident`），默认关闭。

### 18.1 确定性与逐位对照

8× 确定性回放（200 帧、dump-every 60）：两次运行逐位相同，帧 60/120/180 =
`846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`；1× 下同样确定（帧
60/120/180 = `584c0249d197ec69`/`3eacb0453a3d0fec`/`d8c677b177ec5c38`），但与 journal 关
基线不同（journal 打开后经 list 提交，属于 opt-in 模式的确定性差异，默认路径未变）。
与 journal 关基线的像素差（8×，同参数）：帧 60 完全相同；帧 100 差 59,482 px、最大通道差 3
（均值 0.013）；帧 120 差 157,853 px、最大差 10（均值 0.059）；帧 180 差 66,104 px、最大差 39
（均值 0.082，>8 的 15,301 px 集中在星空背景单像素）；帧 200 差 66,247 px、最大差 39；
场内帧 300/400 差 13,633 px（0.28%）、最大差 22（>8 的 2,173 px 只在几何边缘细线）。
目视无块状伪影：差异集中在星空单像素与三角边缘，属于 GPU 光栅化/采样与 CPU 参考的固有
差异（与 §十四 的 float 深度语义差同源）。

### 18.2 8× 场内耗时（Doom3 400 帧，keys 200/205/215/216，同机 interleaved A/B）

以每 100 帧的稳定段计（dump 文件时间戳）：journal 关 23–26 s/100 帧；常驻开启
10–11 s/100 帧 ⇒ 约 **2.0–2.5×** 加速（load≈4.2 的共享机器）。粗测整轮 400 帧：
关 56.9/88.2 s，开 41.7/47.0 s。收益来源：每帧不再做 CPU 侧背景图 8× 缩放与逐包
颜色/深度导入导出，只保留一次 present 前的颜色导出。
- 注意：常驻模式改变了渲染语义（GPU 光栅化 2D 背景与三角形），画质对照见 18.1；默认仍是
  精确单包+CPU 2D 路径，常驻模式保持 opt-in，待 AVD present stats 与更多 title 验证后再考虑
  默认开启。

### 18.3 AVD present stats（Android，x86_64 --gles2 --vulkan，8× HD，mode=5 1080x2400）

设备：Medium_Phone_API_36.1（-gpu host，gfxstream 直通 RTX 5080），素材 JBenchmark3D
（`Triangles ps: 49908` 场景）。`debug.nj2me.present_stats=1` 采集，开关由 `debug.nj2me.vk_resident` 控制，
logcat 确认 `vulkan: resident HD mirror enabled` 后才计入常驻数据。

| 配置 | draw median (ms) | swap median (ms) | frame median (ms) |
| --- | --- | --- | --- |
| 冷启动 off（首个 120 帧窗） | 5.4 / 4.5 | 12.4 / 8.5 | 43.9 / 52.2 |
| 温暖 off | 3.175 / 3.133 | 5.801 / 5.295 | 23.350 / 28.731 |
| 温暖 resident on | 3.349 / 3.300 | 5.855 / 5.181 | 24.001 / 29.831 |

⇒ 温暖后 resident 与 off **无可测差异**（帧时 ~24–30 ms 被 GLES2 present/纹理上传主导，CPU
侧 2D 合成已不是瓶颈）；冷启动窗的 44–52 ms 是 JIT/着色器编译伪差。桌面 2.0–2.5× 的收益在
AVD 上体现不出来，说明下一步（M4）要把 present 也接到 GPU 图像上（省掉 GLES2 上传），
而不是继续优化 CPU 合成。证据：`.local-tools/debug-runs/avd-resident-20261128/`。

## 十九、去掉 journal 提交路径上的重复校验（2026-11-28）

常驻镜面的画质/确定性稳定后，gprof（`-pg` 构建、8× 场内 400 帧、`NJ_VK_RESIDENT=1`）
显示 CPU 时间的大头不在渲染而在**契约校验**：

- `nj_gfx_triangles_validate` 占 73%（9.58 s 采样时间、114,486 次调用）。其中 3 个来源：
  journal 入队时 1 次/包、journal commit 的 `nj_gfx_triangles_list_validate` 1 次/包、
  Vulkan 列表入口的 `nj_gfx_triangles_list_validate` 再 1 次/包。后者对内部 journal 是
  完全的重复劳动：包由运行时自己构造、入队时已校验过，且这些调用还会扫描包 ROI 的深度
  keys（大矩形包一次几十万 keys）。

改动（`c5f8a549` 之后）：

- 端口新增可选 `gfx_triangles_list_trusted`（参数同 `gfx_triangles_list`）：journal commit
  在存在该钩子时跳过自己的 `nj_gfx_triangles_list_validate`，直接交给后端；Vulkan 的
  trusted 入口跳过列表校验，只保留逐包 `analyze`（`triangles_list_trusted` /
  `mirror_triangles_trusted` / `resident_list_trusted`）。
- 新增 `nj_gfx_triangles_validate_fast`：与完整校验同契约，仅跳过深度 keys 范围扫描；
  它只用于「journal 已入队的 GPU 包」（keys 来自本引擎的量化器/GPU D32 导出，恒在
  0..16777216 内），直接调用路径仍用完整校验。
- `tests/test_gfx_triangles.c` 增加 fast 变体语义用例（ROI 内越界 key：完整校验拒绝、
  fast 接受；结构错误两者都拒绝），共 118 项检查。

效果（同机 load≈3.8，8× 场内 400 帧、常驻开）：

- 墙钟：24.9/24.9 s → 14.3/14.4 s（−42%）；相对 journal 关的 ~45 s 约 3.1×。
- gprof 采样总 CPU：13.08 s → 3.48 s（−73%）；`nj_gfx_triangles_validate` 9.58 s（73%）
  → 2.87 s（43%，只剩入队时 1 次/包）→ 加 fast 变体后不再出现在热点前列。
- 确定性不变：8× 常驻帧 60/120/180 = `846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`
  （两次相同）；1× 常驻 = `584c0249d197ec69`/`3eacb0453a3d0fec`/`d8c677b177ec5c38`
  （两次相同），与改动前逐位一致。

剩余 CPU 热点（下一轮候选）：`hd_art_span` 1.13 s（32%，`nj_region_draw` 的 2D 高清图像
blit，约 5 次全屏 drawImage/帧）、`jr_draw_walk` 对逻辑面的软件走查 1.22 s（35%，常驻模式
下逻辑面是否仍需要可由开关验证）。两者都属于「2D 合成上 GPU / 跳过逻辑伴生面」的后续工作。

### 19.1 FSR2 互斥与批量 title 冒烟（2026-11-28）

- FSR2 走 CPU temporal mesh walk 写 HD 目标，常驻 GPU 镜面不能参与这条流水线，否则
  CPU 写的 HD 目标会被下一次镜像 sync 用旧的 GPU 内容覆盖。现在：
  - `nj_midp_hd_target` 在 `render_quality.fsr2` 时不安装 `mirror.sync`；
  - `m3g_clear` 的 GPU 清屏与 `m3g_background_image` 的 GPU quad 都加了
    `!vm->render_quality.fsr2` 守卫。
  验证：Doom3 2× + `--fsr2`、120 帧确定性回放，`NJ_VK_RESIDENT=1` 与关掉输出逐位相同
  （`5fad84a4a41dd265`/`04152f145ccade14`）。
- 批量 title 冒烟（12 个 3D 游戏，8×、120 帧、确定性、resident 关/开各两次）：
  - 24 次运行全部 exit 0，且两次运行逐位相同（两种模式都确定）；
  - 11/12 的关/开输出逐位相同（这些 title 在前 120 帧还停在标题/加载画面，没有可见的
    M3G 三角形，所以常驻路径不改变像素）；毁灭战士3 差 157,853 px（3.21%）、最大通道差
    10，与前面帧级统计一致，属 GPU 光栅化/采样 vs CPU 参考的固有差异。
  - 结论：常驻路径在这些 title 上不崩溃、确定性成立，但「可见 3D 内容」的覆盖仍只有
    Doom3；默认开启前需要更多能进入 3D 场景的回归素材（脚本按键/存档）。

### 19.2 常驻路径的剩余成本拆分与 1x 隔离（2026-11-28 晚）

给常驻路径加 `NJ_VK_PROFILE` 计数后，8× 场内 400 帧（load≈2.4，墙钟 14.9 s）分解为：

| 项 | 次数 | 总时间 | 每帧 |
| --- | --- | --- | --- |
| `mirror_export`（GPU→CPU 19.66 MB） | 619 | 5.105 s | 12.8 ms |
| `mirror_triangles`（含 submit+fence） | 619 | 1.228 s | 3.1 ms |
| `mirror_clear` | 619 | 0.552 s | 1.4 ms |
| `mirror_texture`（背景图 quad） | 1240+ | 0.351 s | 0.9 ms |

即每帧仍有约 18 ms 的同步显存往返/提交，其余约 19 ms 是 CPU 2D（`hd_art_span`）、
逻辑面软件走查、Java VM 与 present。导出是最大单项（8.2 ms/次），除非做到 present 直接
消费 GPU 图像（M4），否则难再降；2D 图像上 GPU 需要完整的 2D 合成顺序（M3）。

另外两处正交改动：

- **全屏重绘时跳过颜色导入**：`resident_clear` 的整屏颜色清屏与 `resident_image` 的
  整屏 replace quad 都置 `resident_repaint`，下一次三角批不再从 CPU 导入颜色（GPU 已把
  CPU 旧内容整体覆盖）。8× 确定性哈希与改动前逐位相同。
- **`--resident` CLI 开关 + 1x 隔离**：Vulkan 端口在 `gfx_triangles_hd_only` 下只对
  HD（scale>1）启用 journal 列表；1x 仍走原有同步路径。验证：1x 开/关 `--resident`
  输出逐位相同（`584c0249d197ec69`/`1211aaab3c0e1a15`/`9014a7c26a2327c3`）；8× 常驻
  仍是 `846057676cfed67a`/`b2f32d1c682185d3`/`695e0c5a49938a7f`，默认路径
  `846057676cfed67a`/`c65baebe4730fcbe`/`dc5cb60109dd03cb` 不变。

### 19.3 常驻导出成本再拆分（2026-11-28 深夜）

给 `mirror_export` 加内部计时后（`NJ_VK_PROFILE=1`，8× 场内 400 帧）：

| 项 | 次数 | 总时间 | 每帧 |
| --- | --- | --- | --- |
| export 总计 | 619 | 5.0–5.2 s | 12.8 ms |
| 其中 CPU 行拷贝 → `target->pixels` | 619 | 1.68–1.74 s | 4.2 ms |
| 其中 `sync_buffer`（缓存内存无效化） | 619 | ≈0 s | 0 |
| 其余（submit + fence + GPU→host 拷贝） | 619 | ≈3.4 s | 8.5 ms |

- 行距等于 `width*4` 时已改为单次 `memcpy`（不再逐行），CPU 拷贝约 7 GB/s。
- **readback 缓冲必须用 cached 系统内存**：临时实验强制 `HOST_COHERENT`（write-combined）后
  CPU 拷贝从 1.7 s 涨到 50 s/400 帧，总 export 56 s（≈13× 变慢）⇒ 该实验已回退。
- 2×/4× 对照（同一场景 619 次导出）：2× 0.24 ms/次（1.23 MB）、4× 1.21 ms/次（4.9 MB）、
  8× 8.4 ms/次（19.66 MB），即 8× 的 GPU→host 传输约 2.4–3.4 GB/s，已接近 PCIe 回读带宽，
  无法靠调参再降；剩下的唯一杠杆是「不回读整帧」——present 直接消费 GPU 图像（M4）或
  2D 合成全部留在 GPU。汇报打印里也修掉了 `if (getenv(...))` 缺花括号导致常驻统计行无条件
  打印的问题。
