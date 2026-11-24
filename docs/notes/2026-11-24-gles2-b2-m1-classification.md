# B2 M1：2D 图元分类的现状与第一次测量（2026-11-24）

B2 原型的第一步是给运行时的 2D 图元分类：**opaque-safe**（可先画到 CPU 覆盖层、
不依赖当前 3D 场景背景）与 **needs-background**（半透明/文字/mask/XOR 等，必须在
3D 场景提交之后按序绘制）。本文记录可复用的现成工具、第一次测量结果，以及对
B2 后续步骤的影响。

## 现成工具

`src/gfx/nj_gfx.c` 里已经有一套 2D 操作分类/计数代码（round 202–203 做 2D 捕获
研究时加入，编译期开关 `NJ_GFX_2D_STATS`）：

* `nj_gfx_2d_stats_start_interval(journal_pending)`：每次 `gfx_load()` 调用时开始
  一个"区间"，记录此刻 M3G journal 是否有 pending 的 3D 批次；
* `nj_gfx_2d_note_op(kind, capturable)`：每次 2D 形状/图像/文字操作记一条，
  `capturable` 由 `nj2d_opaque()` 判定（无 XOR 且颜色 alpha=255）；
* 进程退出时 `atexit` 打印 `[gfx2d]` 汇总。

它本来是估计 2D 捕获成本用的，但"capturable"正是 B2 M1 要的
opaque-safe/needs-background 分类，所以直接复用。

构建（只影响测量，不改默认行为）：

```
cmake -S . -B /tmp/r14-stats -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_CLI=ON -DNJ_BUILD_SDL2=ON \
  -DNJ_BUILD_LIBRETRO=OFF -DNJ_BUILD_GLES2_ACCEL=ON -DNJ_ENABLE_GFX_ACCEL=1 \
  -DCMAKE_C_FLAGS="-DNJ_GFX_2D_STATS=1"
cmake --build /tmp/r14-stats -j10
NJ_GFX_2D_STATS=1 DISPLAY=:1 /tmp/r14-stats/nj2me --port sdl2 --graphics gles2 \
  --size 240x320 --heap 24 --fps 30 --run-frames 300 \
  --keys @tools/corpus_keys/浴火银河2完整版_7689.txt \
  tests/games/浴火银河2完整版_7689.jar
```

## 第一次测量（Galaxy on Fire 2，300 帧）

```
[gfx2d] intervals pending=0 empty=557776 ops_after_pending=0
        first_fallback=0 intervals_with_fallback=0 max_ops=0
[gfx2d] all-ops: line=1736(cap=1736,uncap=0)
                  rect=434(cap=434,uncap=0)
                  image_rop=548930(cap=548930,uncap=0)
```

解读：

1. **观测到的 2D 操作全部是 opaque-safe**（`uncap=0`，`first_fallback=0`），
   与 round 202/203 的结论一致（该工作负载 99.7% 是不透明 image blit、无文字）。
   M1 的"opaque-safe 快路径覆盖绝大多数操作、fallback 极少"假设成立。
2. 但这次运行里 **`journal_pending` 在每个 `gfx_load()` 都是 0**，且 GLES2 统计
   的 `triangles=0`——说明这 300 帧（该按键脚本）并没有真正走到 M3G 3D 提交路径
   （3D 仍由软件光栅化，或游戏还在菜单阶段）。因此"2D 与 3D 提交交错"的时序
   结论不能从这次数据得出，**在 M2 之前必须先用 GL 活跃的运行重新测一次**
   （用 `NJ_GLES2_PROFILE=1` 的 profiler 构建确认 `triangles>0`，再开
   `NJ_GFX_2D_STATS=1` 看区间数据）。
3. 待办：M1 落地时把分类从编译期开关改为常驻代码（无 env 时只判一个条件、
   零输出），并输出 damage 矩形；`NJ_GFX_2D_STATS` 保留为诊断开关。

## 对 B2 计划的影响

* 分类器不必新写，直接把 `nj2d_opaque()` 的判定提升为运行时 API，并在
  `nj_gfx_fill_rect`/`nj_gfx_draw_image*` 等入口累加 damage。
* 决定 M1 是否值得独立提交，取决于 GL 活跃运行下的分类比例；如果与本次软件
  运行同样乐观，M2 的 overlay 合成路线成立；否则退回"仅在 present 提交一次"
  的更保守方案（不跨 2D 保留场景）。
* 这次测量本身也说明：**测量脚本要显式校验 3D 加速真的被使用**（`triangles`
  计数），否则会把软件路径的数据当成 GL 路径的证据——记录为方法论教训。

## 第二次测量（GL 活跃，560 帧）

第一次运行没有确认 3D 加速路径，于是用同一二进制跑满 560 帧并检查 GLES2
统计：`triangles=1164290 batches=2988`（≈5.3 批/帧，`INFO: m3g: accel forced
by NJ_M3G_FORCE_ACCEL`），确认走的是 GL 路径。`[gfx2d]` 结果：

```
[gfx2d] intervals pending=0 empty=715148 ops_after_pending=0
        first_fallback=0 intervals_with_fallback=0 max_ops=0
[gfx2d] all-ops: line=16636(cap=16636,uncap=0)
                  rect=4907(cap=4907,uncap=0)
                  fill=1494(cap=1494,uncap=0)
                  image_rop=663670(cap=662951,uncap=719)
```

结论：

1. **99.89% 的 2D 操作是 opaque-safe**（663,670 个 image_rop 中仅 719 个
   non-capturable，约 1.3 个/帧；其余类型全部 capturable）。B2 的覆盖层合成
   路线成立：绝大多数 2D 可先画在 CPU 覆盖层，只有极少数操作需要"先提交场景
   再画"的保守回退。
2. **`journal_pending` 在全部 715,148 次 `gfx_load()` 中都是 0**，而 GL 侧
   确实有 ≈5.3 批/帧。这说明 "2D 调用触发了 3D flush" 的旧假设不成立：这些
   批次提交发生在 M3G 渲染过程本身（多个 session begin/end），与 gfx_load 的
   2D 操作没有交错。于是 M2 的关键约束简化为：**每帧在 present 处提交一次
   场景**，少数 non-capturable 操作做有序回退即可，不需要为每个 2D 操作维持
   复杂的分层合成器。
3. 下一步：给 session begin/end 加来源计数（确认 5.3 批/帧来自哪些 M3G 调用
   边界），然后落 M1 的运行时 damage 跟踪与 M2 的持久 FBO 提交点。

（方法教训更新：确认 `triangles>0` 后重测是必要的——第一次数据只覆盖软件
路径，两次结论方向一致但交错模型完全不同。）

## 第三次测量：批次来源（`-DNJ_M3G_PROFILE=1`）

用同一 Galaxy 560 帧运行加入 M3G profiler（`-DCMAKE_C_FLAGS="-DNJ_M3G_PROFILE=1
-DNJ_GFX_2D_STATS=1"`），结束时计数：

```
M3GPROF walks=66091 walk=367.5ms appends=46200 append=46.5ms
        pumps=1427 pump=0.1ms commits=725505 commit=3913.0ms
gles2 completed triangles=1250191 batches=2888
```

解读：

* 每帧约 **82.5 次 append**（46,200/560），最终合并成 **5.16 次事务**
  （2,888/560）。这与 `journals pending=0 @ gfx_load` 一致：journal 在帧内被
  多次排空，每次排空再触发一次 GL 事务与读回。
* `commits=725,505` 是 `nj_m3g_flush()` 调用次数（≈2D 操作数）。绝大多数是
  空 journal 的空操作（commit 时间主要来自真正有批次的少数次），这也是现有
  路径每帧 CPU 开销的一个来源：**每次 2D 调用都尝试冲刷一次**。
* M2 的明确目标因此变成：把帧内 **≈5.2 次排空合并为 1 次**（present 处提交），
  同时把 725k 次空冲刷降为 O(1) 判断。前者去掉约 5 份重复的 readback/commit，
  后者用 `nj_m3g_pending()` 做早退即可（当前 `nj_m3g_flush` 已经会空转，但
  仍在热路径上）。
* 待办：确认这 5.2 次排空的触发来源（target 切换 / journal 上限 / M3G
  clear / 显式 flush 调用点），如果是 target 切换导致，则 M2 需要按 target
  分组持有多张 frame FBO，而不是只持有一张屏幕 FBO。

（注：profiler 计数在进程退出时打印，`commits` 是累计值；时间字段为 profiler
自身累计，不代表真实开销。）
