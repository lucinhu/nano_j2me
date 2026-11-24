# nano_j2me 0.5.43

发布日期：2026-11-25  
核心版本：r62（`0.5.43`）  
libretro 元数据：`nano_j2me_libretro.info` SHA-256 `46f0677f0041c8c2c6eacadb74060297ab2838ab2fcea7e1ded355055ab44893`

本次发布把最近一轮功能整理为可发布的版本：多即时存档位、倒带、GL 呈现后处理与统一图标。软件渲染仍是默认路径，所有新增呈现路径均为显式开启。

## 亮点

- **多即时存档位（CLI / SDL2 / Android）**
  - CLI：`--state-dir`、`--save-slot N`、`--load-slot N`（默认目录 `<jar 目录>/states`，文件名 `<jar 名>-slot<N>.state`）。
  - SDL2：F5 保存、F6/F7 切换槽位、F8 读取、F9 删除；槽位目录可用 `NJ_SDL2_STATE_DIR`/`NJ_SDL2_STATE_STEM` 覆盖。
  - Android：游戏面板「保存状态」选择槽位（快速存档 + 存档位 1–4，覆盖前确认）；启动器游戏卡「读取存档」列出已保存槽位并按所选槽位启动。
  - 存档格式沿用版本化快照，旧存档仍可读取；libretro 侧继续由前端管理槽位（文档说明）。
- **倒带**
  - SDL2：内存环形缓冲，`NJ_SDL2_REWIND=<帧间隔>` 开启，`NJ_SDL2_REWIND_COUNT`（默认 16 槽）、`NJ_SDL2_REWIND_MAX_MB`（默认 256 MiB）；F4 回退一张快照。
  - Android：游戏面板「倒带」可设关闭/1 秒/2 秒/5 秒，或「倒带一步」立即回退；每 0.5 秒一张快照，槽位数 = 秒数×2，上限 256 MiB；默认关闭。
  - 原理与代价见 `docs/REWIND.md`：快照复制整个堆，大堆（Minecraft 类）代价高；libretro 前端 rewind 使用同一序列化路径。
- **GL 呈现后处理（SDL2，实验性、默认关闭）**
  - 新增 `--upscale nearest|sharp|scanline|lcd|crt|fsr` 与 `--upscale-scale 1..8`：私有 GLES2 上下文把帧缓冲上传为纹理，用可插拔 GLSL 着色器呈现；失败时自动回落到原软件呈现。
  - FSR1 `EASU + RCAS` 已按 AMD FidelityFX FSR1（MIT）移植，`textureGather` 在 GLSL ES 1.0 下用四次 `texture2D` 等价实现。
  - 帧一致性：nearest 1x 与软件呈现窗口截图 **0/997,440** 像素差异；CRT 2x 为预期的 65.4% 像素变化。
- **统一图标与品牌资源**：README、SDL2 桌面版窗口图标、网页版 favicon/PWA 图标全部改用 Android 版设计；`tools/gen_brand_assets.py` 从 `tools/gen_android_icon.py` 的同一几何确定性生成。
- **回归与工具修正**：libretro 前端矩阵的 audiodemo 用例改为 30 帧（该 fixture 会自行 `notifyDestroyed()`，60 帧时保存请求被正确拒绝而导致误报）。

## 验证

| 项目 | 结果 |
|---|---|
| 全量串行 CTest（发布树） | 211/211 通过，375.42 s |
| libretro mock 前端矩阵 | 20/20 通过 |
| 真实 RetroArch 1.22.2 LoopDemo（120 帧） | PASS |
| Windows x86_64 Wine（300 帧） | 4/4 通过 |
| 提交材料 bundle 校验 | 32/32 通过 |
| 全语料 libretro 视觉探针 | 1034 部：1031 通过 / 1 已知限制 / 2 失败 |

探针失败均为已记录项：`PAC-MAN (176x220)`（标题侧空 Muglet URI）与 `Asphalt4EliteRacing`（单帧过长导致 4 秒状态服务窗口拒绝）；已知限制为 `毁灭战士3_10598`（MIDlet 构造函数内运行游戏循环，加载极慢）。

## 核心产物（8 目标）

| 目标 | 大小 | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,052,936 | `5b7c5e655059922c185d6cf881723f4d22a052e278234e042909b103e49700d6` |
| linux-aarch64 | 3,526,576 | `93e66e8f871e732f489f16e94e22f858fa203219d7fa7a3b7af7c631afbadeff` |
| linux-armv7 | 3,179,852 | `3d55c307deef598fe38d81aa09df96fca699191c11dd35ff8ded9efaf1b75523` |
| android-arm64 | 3,772,448 | `68655d1bb753871051f737ca57a505dad794463d8eb23b36bb99b832dd6cdf03` |
| android-armv7 | 2,867,792 | `688c55ba7d32c654c1599326f5646c28ca206d5b004259f281610127808df2b3` |
| android-x86_64 | 3,798,296 | `bb825c95c1d5ca24c53d76bd905627494ebe4257cb9ce83bbbe2058a0292d16b` |
| windows-i686 | 3,606,016 | `18f8b0786f6458e7133109fe4df6acaf099b4ea544ddceef5b06bc557be503f6` |
| windows-x86_64 | 3,375,104 | `cb520e68b8fede15161905c5e78fdcb41d9b1ba7e267afa126e556ee5fc5064c` |

核心合集：`nj2me-0.5.43-libretro-cores.zip`（128 个文件）**12,189,054 B**，SHA-256 `cf80765d257a172774247a02ddcbd1b35b438142ccfa58227b589c9a9886534e`。

## Android APK（versionCode 543）

| ABI | 大小 | SHA-256 |
|---|---|---|
| arm64-v8a | 1,071,260 | `e1d66186efa01adbe1325d86d8a02b3e42f81084dfb3ae0ff73468220244c573` |
| armeabi-v7a | 1,079,454 | `2b7b8f4c6134d14915b88bb0e6efd700ad4110c3195c974dfc298acfb7cd21e7` |
| x86 | 1,058,966 | `014f007df5d8d828572dec27dfafd9bc947edaf7ed5de0c7a2e39f3efec9bc0e` |
| x86_64 | 1,075,353 | `7d3b9ce1c21383598d8eed2741d2fab8c6eca459e03e75b31a6c682694ca67b7` |
| legacy（ARMv5/API 5） | 743,748 | `23e6fe7f2206c772ecfbc8209e24edcc13431abb66b95437d4309f9167e815b2` |

## 升级说明

- 存档格式未变，0.5.42 及更早的存档/即时存档仍可读取（版本 CRC 不符时会明确拒绝）。
- 倒带默认关闭；开启后每张快照复制整个运行时堆，Minecraft 等 128 MB 堆标题会明显占用内存并带来 CPU 开销。
- GL 呈现（`--upscale`）为实验功能，需要 `--graphics gles2` 与可用 GL 上下文；默认软件呈现不受影响。
- libretro 侧：`.info` 未新增字段，前端 rewind 使用状态序列化；B2 frame-resident 渲染按用户决定停止，B1/B1.5 实验代码保留但默认关闭。

## 上游提交

- libretro-super PR #2204：fork 分支 `lucinhu/libretro-super:add-nano_j2me` 更新到本次 `.info`（提交 `28a861c77caaab39361831f7eb5fdc846cee9c5b`，基于 `c3c55c2d`）。
- libretro/docs PR #1239：页面与补丁保持与 0.5.43 一致（fork 提交 `4f360b1`）。
- 未向上游仓库执行合并操作。
