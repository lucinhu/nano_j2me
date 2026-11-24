# nano_j2me 0.5.42

发布日期：2026-11-25 · 上游 PR：[libretro-super#2204](https://github.com/libretro/libretro-super/pull/2204)、[libretro/docs#1239](https://github.com/libretro/docs/pull/1239)

## 重点变化

* **独立 SDL2 版新增呈现滤镜**：最近邻（默认）/ 线性 / 扫描线 / LCD 网格 / CRT，
  桌面启动器「设置 → 缩放滤镜」可循环选择，配置键 `filter_mode`（旧
  `filter_linear` 仍兼容读取）；滤镜只作用于呈现层，存档/截图/帧转储不变。
  详见 `docs/UPSCALING.md`。
* **加速倍率上限从 4× 提升到 8×**：Android 游戏内速度菜单增加 4×/6×/8×，
  SDL2 速度角标同步；运行时时钟本就支持 10%–800%，存档格式兼容。
* **M3G 批次 flush 归因诊断**：新增 `NJ_M3G_FLUSH_DBG` 环境变量，按原因
  （bind/clear/present/gfx/readback…）输出非空提交计数；默认零开销（编译期关闭，
  不引入可写静态状态）。仅调试用途。
* TheMummy 事件线程修复（TimerTask 以 Java 方式调度）已包含在 0.5.41/r60，
  本版继续保留。

## 验证

* 完整串行 CTest：**210/210 通过**（367.21 s）
* libretro 前端矩阵：**20/20**；真实 RetroArch LoopDemo 120 帧 **PASS**
* 提交材料自检：**32/32**
* 0.5.42 APK 五架构（arm64-v8a / armeabi-v7a / x86 / x86_64 / legacy ARMv5）
  全部构建通过；Android 原生库可写静态状态 **0 字节** 检查通过

## 核心哈希（r61）

| 目标 | 大小 | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,048,840 | `ba56eea06b2559a5158dd8dd153fb609205bd3df1315df5efa0d0b4512dec09f` |
| linux-aarch64 | 3,524,816 | `29a1d33ead082b9a425ad7a36b09601ee98892aa8e8945f5cdcfcf3af4b9b7cf` |
| linux-armv7 | 3,177,820 | `0765fd3d23f68f2a381f94730538595b0d45eac885a4f6e4a9ec9513674362b3` |
| android-arm64 | 3,770,624 | `4bf3b11fe22a1720bdf1ef0007fea2b489ceffd66688128cdcb99d7ce57b3409` |
| android-armv7 | 2,866,352 | `e5981bc8d7de5b073deffb92fbf059002187f81436304692c4a4f08f7c4ba83c` |
| android-x86_64 | 3,796,568 | `0c9ca730a0b00d85801486be81bb5010c84cad5261587bc243649f37c8011f93` |
| windows-i686 | 3,602,944 | `2a8b61aaa564017c7fa53605a24b3d3d65071759efc44731b635ad61f098221e` |
| windows-x86_64 | 3,372,544 | `972bd396fda9ed6cf95d9d56f8c733e34417e576d3328aaff608df2b2a79c89f` |

核件资料包：`nj2me-0.5.42-libretro-cores.zip`（128 文件）SHA-256
`44ebdb167bd12e7e9799ead00f7a78603d410db7bd04345de32d3c548df16587`。

## 升级说明

* 存档格式与 0.5.8+ 相同；跨大版本读取旧存档会因版本 CRC 被拒绝（与以往一致）。
* GLES2 仍为实验性 opt-in（`--graphics gles2`），软件渲染是默认；B2 原型已
  按用户决定停止实施，测量与结论保留在 `docs/notes/` 相关文档。
