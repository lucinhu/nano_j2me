# libretro 保存状态看门狗只应盯“卡住”，不应盯“慢”

## 症状

`test_libretro_frontend` 的 `audiodemo audio (--audio-required, 30 frames)` 用例
偶发失败，且只在机器繁忙时出现：

```
FAIL serialize_writes_state: retro_serialize(157593664) returned 0
```

`retro_serialize` 返回 0 时核会记录 `save-state request refused (-7); the game
continues`，即前端拿不到状态但游戏继续。单独运行时必过；两套 ctest 并行
（16 个作业抢 24 核）时约一半概率复现。

## 原因

`src/port/libretro/nj_port_libretro.c` 里保存/恢复状态是“前端线程排队，VM 线程在
帧边界服务”。前端线程阻塞等待时用墙钟截止时间做看门狗：

```c
deadline = lr_real_ms() + LR_STATE_TIMEOUT_MS;   /* 4000 ms */
while (!s->req_done && !s->vm_done) {
    uint64_t now = lr_real_ms();
    if (now >= deadline && !s->req_busy) break;
    (void)lr_cond_wait_ms(&s->cond, &s->lock, 100u);
}
```

这个看门狗本意是防止“MIDlet 卡在 native 调用里，让 RetroArch 永久阻塞”。
但它把**等待帧边界的全部时间**都算进去了：CPU 被抢占、ping 到大状态（该用例
状态 150 MB）、或某帧只有推进虚拟时钟才结束时，4 s 墙钟很容易被吃光，
于是请求被当成“卡死”拒绝——其实 VM 一直在正常前进。

## 修改

截止时间改为**无进展超时**：只要 VM 帧计数变化、或请求阶段（`req_busy`）变化，
就重新计时；只有连续 `LR_STATE_TIMEOUT_MS` 毫无进展才放弃。

```c
seen_frames = s->req_start_frames;
seen_busy = 0;
while (!s->req_done && !s->vm_done) {
    uint64_t now = lr_real_ms();
    uint64_t frames = s->vm ? nj_vm_frames(s->vm) : 0u;
    if (frames != seen_frames || s->req_busy != seen_busy) {
        seen_frames = frames;
        seen_busy = s->req_busy;
        deadline = now + LR_STATE_TIMEOUT_MS;
    }
    if (now >= deadline && !s->req_busy) break;
    (void)lr_cond_wait_ms(&s->cond, &s->lock, 100u);
}
```

有界性没有被破坏：真正卡死的 MIDlet 不推进帧，4 s 后照旧被放弃；
`LR_STATE_CLOCK_MAX_FRAMES`（1200 帧）仍然限制“暂停时用虚拟时钟把游戏推到
下一个帧边界”的预算，所以最坏情况仍是有界的。

## 验证

* 复现场景：两套 ctest 并行各 `-j 8`（此前 221/222 失败一次）。
* 修复后同场景：增强套件 236/236、软件套件 222/222（日志
  `.local-tools/debug-runs/fsr-round5/gpu-suite2.log`、`sw-suite2.log`）。
* 单独运行软件套件同样 222/222，`test_libretro_frontend` 无失败。
* 保存/恢复的语义没变：用例里 `serialize_size_stable`、
  `unserialize_restores_state`、版本拒绝等断言全部通过；仅有“拒绝”不再
  发生在负载下。
