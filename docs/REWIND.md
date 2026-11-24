# 倒带（Rewind）

倒带把「过去若干帧的完整模拟器状态」保存下来，按下快捷键时重新载入其中一个
状态，就像把游戏倒回到几秒前。它不改变游戏逻辑，只是反复执行「快照 → 还原」。

## 原理

- 一次快照 = 一次完整的虚拟机序列化：整个 Java 堆（arena）、所有线程栈、
  类静态表、记录存储句柄、媒体播放位置等，格式与 `.state` 存档完全一致
  （版本化 blob，CRC 校验）。
- 快照点必须落在帧边界上：虚拟机不能处于 C 调用、绘制回调或嵌套调用泵中，
  否则 C 栈无法写进 blob。SDL2 端口在每帧的 input_poll 处（私有帧钩子内）用
  `nj_vm_save_mem_boundary()` 取得快照，允许帧钩子内的边界保存，并丢弃
  被 park 的线程的最顶层 native 调度帧（与 Android 的边界存档同一机制）。
- 环形缓冲：按固定帧间隔拍摄快照，最多保留 N 个；内存超过上限时丢弃最旧的
  快照再继续。按下倒带时弹出最新的快照并用 `nj_vm_load_mem()` 还原。
- 代价与堆大小成正比：一次快照要复制整个 arena。

## 实测成本

| 场景 | 堆 | 状态大小 | 保存 | 还原 |
|---|---|---|---|---|
| LoopDemo（SDL2，本机） | 16 MB | 333,542 B | ~2–4 ms | ~4 ms |
| 一般 2D 游戏（libretro 前端 rewind 实测） | 16 MB | 25,473,088 B | ~122 ms | ~118 ms |
| 自动堆（最大机制，Minecraft PE 类） | 157,593,664 B | 157,593,664 B | ~1.0 s | ~1.0 s |

后两行来自 `docs/notes/` 的早期 libretro rewind 成本测量：状态体积由「堆 +
线程栈 + 静态表」决定，所以大堆游戏（Minecraft PE 这类会把堆撑满的）每次
快照约 1 秒，倒带会明显卡顿。小堆的普通 2D/3D 游戏开销可以接受。

## SDL2 端口用法

默认关闭（大堆下代价高）。开启方式（环境变量）：

| 变量 | 含义 | 默认 |
|---|---|---|
| `NJ_SDL2_REWIND` | 每多少帧拍一次快照；>0 即开启 | 关 |
| `NJ_SDL2_REWIND_COUNT` | 环形缓冲最多保留几个快照 | 16 |
| `NJ_SDL2_REWIND_MAX_MB` | 环形缓冲内存上限（MiB） | 256 |
| `NJ_SDL2_REWIND_AT` | 测试用：在第 N 帧自动触发一次倒带 | 无 |

示例：

```sh
# 每 30 帧（约 1 秒）拍一次，保留 8 个快照（可倒回约 8 秒），F4 倒带
NJ_SDL2_REWIND=30 NJ_SDL2_REWIND_COUNT=8 \
  ./nj2me --port sdl2 game.jar
```

按键：**F4** 倒回到上一个快照。每次倒带后有一个冷却期（等于快照间隔），
避免连续按下时瞬间把环形缓冲耗光。

启动日志会打印：

```
INFO: rewind: on (snapshot every 30 frames, 8 slots, 256 MiB cap, F4 steps back)
INFO: rewind: first snapshot 333542 bytes in 3734 us
INFO: rewind: restored a snapshot -> 0 in 4156 us (5 left, 1667710 bytes held)
```

## libretro 内核

libretro 的前端（RetroArch 等）本身用状态序列化实现 rewind/runahead，
核心只需要照常提供 `retro_serialize`/`retro_unserialize`。因此：

- libretro 侧不要把倒带逻辑放进核心，交给前端配置（RetroArch 的
  Settings → Frame Throttle → Rewind，可调 rewind granularity）；
- 成本同样由状态大小决定：auto 堆（`nano_j2me_heap=auto`）下状态可达
  150 MB，前端倒带缓冲需要相应放大或改用显存/磁盘方案；
- `0.5.42/r61` 起的 `check_libretro_rewind` 工具与文档记录了快照成本，
  详细测量见 `docs/notes/` 与 `docs/LIBRETRO.md`。

## Android

Android 的倒带开关与参数属于后续项（计划在游戏内设置面板中提供
「倒带：关闭 / 1 秒 / 2 秒 / 5 秒」以及缓冲大小选项，默认关闭，并在
大堆游戏上提示开销）。本文件会随实现更新。

## 已知限制

- 快照点必须在帧边界；若某一帧极长（例如游戏在一个帧内做重载），倒带
  只能回到该帧开始之前，粒度就是帧。
- 快照不包含外部资源（RMS 之外的网络/文件写操作不会回退），只回退
  虚拟机内部状态。
- 倒带后音频队列会被清空，重新从状态恢复后的时刻开始播放。
