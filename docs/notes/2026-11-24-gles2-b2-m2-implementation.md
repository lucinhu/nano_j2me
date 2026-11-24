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
