# B2 M2：持久 scene FBO 的实现清单（2026-11-24）

M1 测量已经把问题收敛：Galaxy 每帧约 5.2 次事务全部由 `Graphics3D.clear()`
前的 flush 触发；Doom3 的 1.6 次/帧则来自真正的像素观察者。M2 要做的就是把
前者的 `flush(读回+提交) + CPU clear` 换成 `pump(按序画进 FBO) + GL clear(FBO)`，
一帧只在 present / 真读像素处读回一次。

## 现有结构（读代码后的结论）

- `m3g_journal_pump()`（src/midp/nj_lib_m3g.c:15338）把队列交给 persistent
  session（`gfx_triangles_begin/list`），**不** `end`，所以 session 可以跨多次
  pump 保持打开；`m3g_journal_end()` 才提交读回。
- `m3g_journal_flush()` 当前 = pump + end + 读回，`Graphics3D.clear()`
  （`NJFN(m3g_clear)`，约 4225 行）与 `bindTarget` 都直接调它。
- GLES2 session（`src/port/gles2/nj_gfx_triangles_gles2.inc:913-1054`）：
  `triangles_begin` 分配 `tri_session_fbo`（colour 用 `t->keys` 纹理，depth 用
  renderbuffer），`triangles_end` 读回颜色+深度 key、写 CPU surface/depth。
  session rect 只覆盖提交包的并集，`triangle_session_seed()` 从 CPU 像素/深度
  初始化 FBO。
- M3G clear 在 flush 之后直接写 CPU surface + 深度数组（`m3g_clear`）。

## M2 实现步骤（下一轮按此落地）

1. `nj_gfx_accel` 增加可选回调（`src/include/nj2me/nj_gfx_triangles.h`）：
   ```c
   int (*triangles_clear)(void *ud, const nj_gfx_triangles_clear *c);
   ```
   字段：surface、x/y/w/h、rgba 颜色、clear_depth 标志、depth 值。SDL2/GLES2
   port 转发到 `nj_gles2_triangles_clear()`。
2. 后端在 `triangles_begin` 时若 `scene_enabled`：把 session rect 扩为整个
   surface（或至少保持“FBO 覆盖整屏”的稳定目标），保证 clear 覆盖区域与
   FBO 一致；`triangles_clear` 实现为 `glEnable(SCISSOR)+glClearColor+glClear`
   （深度用与 seed 相同的 far key 编码），保持 “先画完再清背景” 语义。
3. M3G 侧：`m3g_clear` 优先走新路径——`m3g_journal_pump()`（只画进 FBO）+ 新
   回调做 GL clear；仅当 port 没有 `triangles_clear`、background 是 Image、
   或 target 不是 screen/受支持的 surface 时回退到当前的 flush+CPU clear。
   CPU surface/depth 的 clear 可以保留（与 FBO 同步），但不再触发读回。
4. 提交时机：`present`/`bindTarget`/真实像素观察者（`gfx`/`readback` reason）
   仍然调用完整 `flush`（读回一次）。CLEAR reason 的 flush 改成 pump+GL clear。
5. 验收：Galaxy 560 帧事务数从 ≈5.2/帧降到 ≈1/帧（`NJ_GLES2_PROFILE` 计数），
   CPU 相对当前 GL 路径下降；LoopDemo/Galaxy/Doom3 三帧 PPM 与软件逐像素一致；
   全量 CTest 210/210；GLES2 保持默认关闭（`NJ_GLES2_SCENE=1` 才启用）。
6. 风险：`Background` 为图片时的 clear 必须回退；深度 key 编码必须与 seed/read
   完全一致；session rect 改成整屏后 readback 面积变大，需要只在 present 读回
   一次才能净收益（这正是目标）。

## 现状

本轮完成了触发源归因（`b477fad`），M2 代码尚未开始；下一步按上面 1–4 实现。

## M2 上限实验（2026-11-24，临时 benchmark 开关，已还原）

在 `m3g_clear` 里加临时开关 `NJ_M3G_UB_NO_CLEAR`：clear 时只 `nj_m3g_pump()`
（把队列画进已打开的 session、不提交不读回），跳过 flush；像素正确性**故意
不保证**，只用来测 M2 的 CPU 上限。Galaxy on Fire 2 560 帧 @30 fps 强制 GL：

| 版本 | user | sys | real |
| --- | --- | --- | --- |
| 基线（每 clear 提交读回） | 6.14 / 5.96 | 1.02 / 1.15 | 19.21 / 19.09 |
| M2 上限（clear 不提交） | 5.88 / 6.31 | 0.29 / 0.27 | 19.18 / 19.12 |

结论：M2 最多把总 CPU 从 **≈7.0–7.1 s 降到 ≈6.2–6.6 s**（约 10–13%，收益主要在
sys / 驱动侧），而软件路径同场景约 5.0 s。也就是说：

* M2 单独**不足以达到验收标准**（GL 仍比软件慢约 1.1–1.6 s）；
* 与之前 zero-readback 上限（5.55 s vs 软件 5.01 s）一致：剩余差距在
  "每次事务的 setup/上传/提交" 而不是 readback 本身；
* B2 若继续，必须同时做 M2.5（持久顶点/深度纹理、减少每次 seed/upload）才有
  接近打平的可能；只做 M2 的 ROI 很低。

该实验开关已还原（`git checkout -- src/midp/nj_lib_m3g.c`），工作区干净。

## 决定：停止 B2 实施（2026-11-24，按用户选择）

用户在 M2 上限实验后确认：**停止 B2 的实施**，保留现有 B1/B1.5 实验代码与
完整测量记录。具体状态：

* 软件渲染保持默认；GLES2（含 B1 frame-resident 2D、B1.5 hold-context）继续
  是 opt-in 实验路径（`--graphics gles2` + `NJ_M3G_FORCE_ACCEL=1` 等），不影响
  任何默认行为、存档或兼容性。
* 测得的上限（Galaxy on Fire 2，560 帧）：每 clear 读回全免也只能把 GL 总 CPU
  从 ≈7.0–7.1s 降到 ≈6.2–6.6s；zero-readback 上限 5.55s，均仍高于软件 ≈5.0s。
  因此 B2/M2 的预期收益（约 10–13%，主要在 sys）不足以达到验收标准。
* 结论与依据完整保留在：
  * `docs/notes/2026-11-22-gles2-whole-frame-renderer.md`（总体设计）
  * `docs/notes/2026-11-24-gles2-frame-resident-2d-layer.md`（B1/B1.5 实现与
    Phase 拆分、zero-readback 上限）
  * `docs/notes/2026-11-24-gles2-b2-plan.md`（B2 方案与里程碑）
  * 本文（M1 分类、drain 归因、M2 上限、决定）
  * `docs/notes/2026-11-24-gles2-b2-m1-classification.md`（2D op 分类数据）
* 若未来重启 GL 性能线，唯一有理论机会的路线是 **M2.5（持久顶点/索引缓冲 +
  真实深度纹理 + 减少 seed/upload + EGL 窗口呈现）**；按现有数据其现实目标是
  接近打平（≈5.0s）而非胜出，需重新立项评估。
* 未启动项：B3（EGL 呈现）与 GLES2/FSR 后处理 shader 管线仍属于目标三的后续
  候选（需先有窗口呈现路径）。
