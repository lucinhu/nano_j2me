# nano_j2me 0.5.39 (r58)

前一个正式版本是 [0.5.38 / r57](RELEASE_0.5.38.md)。本次把 0.5.38 引入的
libretro 时钟修复补成**全程单调**：load、run、teardown 三个阶段使用同一时间标尺，
内核不再出现“某帧耗时数秒 / 前端推进但 VM 不推进”的情况。

## 本版内容

- **libretro 时钟连续性加固**（`src/port/libretro/nj_port_libretro.c`）
  - 新增 `stop_ms` / `stop_start_us`：进入 `LR_PHASE_STOP` 时记录 VM 时钟，
    `destroyApp()` 期间 `System.currentTimeMillis()` 返回“停表时刻 + 经过的墙钟时间”，
    不再混入主机 uptime（之前会把 VM 时钟推到数小时之后）。
  - `lr_load()` 以当前 VM 时钟为起点锚定 LOAD 时钟；`retro_reset()` 的重载
    也保持同一标尺（之前会被清零重来）。
  - LOAD → RUN 切换把 LOAD 时钟交给运行时钟：
    `vclock_us = (load_ms + elapsed) * 1000`，VM 时钟全程单调、不再回跳。
- 效果（mock 前端对比 r57 发布核心）：
  - `MidnightPool1_1.0.0_320x240`：黑屏 + 存档拒绝 → 正常渲染，最高 8,348 非黑像素、
    85 个不同画面，45/45 前端检查通过（含存档往返，`serialize_size=157,593,664`）。
  - `Midnight Pool2`：黑屏 + 存档拒绝 → 正常渲染（7,427 非黑像素、20 个不同画面），PASS。
  - `Skater_by_BFLM`、`家园5汉化版` 在 CLI 与内核两侧行为不变。

## 制品

### libretro 核心（`.info` SHA-256 `c2a4d8797c3cc5a648f3b3b5ca2c89252adf0242f292044c1ad3bca7842034cf`）

| 目标 | 大小 (B) | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 4,048,832 | `6d51ddcdf9efc80a270e4bf5c57407e9b65348f508871596fa492f98eb73cd8d` |
| linux-aarch64 | 3,523,320 | `3f13e149bd1f445d2692692705eaf4953ae2ef2be1dbdfe36e2e57565f61899e` |
| linux-armv7 | 3,176,104 | `d139de32405417924610f0e3db2e518d0200f83bcd89352f98fe7588d49fd96a` |
| android-arm64 | 3,768,968 | `5e8df5b06654ff3e10322be6cb749136101c9e219e2a6d24c31fb010fc7d07d1` |
| android-armv7 | 2,865,116 | `aeed9dab4d6792bec9b83b874075f80fa991e4c1e0c68aa34ee450feb02f33bb` |
| android-x86_64 | 3,794,960 | `01c2c89a250b7504a59b508cfc502a3887f7248e245822086142fb33dbb6e82f` |
| windows-i686 | 3,601,408 | `a2a7b01354d97e84fcfacd88ecde79baee0519f304e7584ffbc784e40ca77fe6` |
| windows-x86_64 | 3,371,008 | `4152abc18e848c71afdd426d330ba6d6f7647dc6771454ad73626b0db01ef8dd` |

合集：`nj2me-0.5.39-libretro-cores.zip`，12,176,496 B，128 个条目
（8 目标 × 16 文件），SHA-256
`a56e406d6e72928b9405082e84db45a93a2a4980ae0fa86caf7b48bdad8ce0f2`。

### Android APK（versionCode 539，minSdk 21）

| 变体 | 大小 (B) | SHA-256 |
| --- | ---: | --- |
| arm64-v8a | 1,058,972 | `e70c0e13a52e1c87020cdda1b0bdd6deb2ad540035328525d44ec3cce6196d74` |
| armeabi-v7a | 1,067,166 | `334900d2bd5c5e9b8e906108954578be35d0e62bbe2e54e1e29dbb6143064145` |
| x86 | 1,046,678 | `b00c903810906eceb0b4c4e7a0b28122f6d00ab2206b9c2f5591c39ad3563e37` |
| x86_64 | 1,058,969 | `7612688b921ef298d9da780608f5d654470e364138841a21f79d76ac5052c29b` |
| legacy (ARMv5/API 5) | 741,351 | `3baaddf7c795cb3f4957256903fc7566189392d6840c2694008fd33342768060` |

## 验证

- 全量串行 CTest：**210/210 通过**（383.46 s，r58 树）。
- libretro mock 前端矩阵：**20/20 通过**（内存窗口、存档、rewind、cheat、多指触摸、选项）。
- 真实 RetroArch 1.22.2：LoopDemo 120 帧 **PASS**（核心日志标记校验通过）。
- Windows x86_64 under Wine：**4/4 通过**（300 帧）。
- 提交材料自检：`tools/check_libretro_submission_bundle.sh` **32/32 通过**。
- 全语料 libretro 渲染探针（1,664 个有判定行）：**1,658 通过 / 1 个已知限制 /
  5 个失败**（`TheMummy_K700F`、`PAC-MAN (176x220)`、`assault`、`Skater_by_BFLM`、
  `Asphalt4EliteRacing`），失败集合与 r57 完全一致，时钟加固没有引入判定回归。

- Android APK 冒烟（API 36 AVD `Medium_Phone_API_36.1`）：安装
  `nj2me-0.5.39-x86_64.apk`（versionCode 539），媒体库显示 `1 个游戏`，
  LoopDemo 启动后正常渲染 240x320 画布（绿色进度条 + 完整屏幕键盘），
  截图见 `.local-tools/debug-runs/r237/`。

## 升级说明

- 0.5.8 – 0.5.38 的存档会因版本 CRC 不符而被拒绝；跨版本请用本版重新保存。
- 软件渲染仍是默认路径；GLES2 后端为实验性选项，本版未改变其定位。
- 内核选项、`.info` 元数据与 0.5.38 一致，仅版本号变化。

## 上游提交

- `libretro-super` 补丁包：`docs/libretro/upstream/libretro-super-nano_j2me-info.patch`，
  基线 `libretro-super` master `55e50a81195eaf8dd1b7720a601282459832ced1`，
  分支 `add-nano_j2me` 提交 `a04d259`（本版 `.info`）。
- PR：libretro/libretro-super#2204、libretro/docs#1239（本版只更新描述与 `.info`）。
- 本版内核是 r58 包；发布资产与上面的哈希一一对应。
