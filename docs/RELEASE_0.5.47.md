# nano_j2me 0.5.47 / r66

发布于 2026-11-25。这一版以「界面自释其意」为主线：三端 UI 统一为「图标 + 短标签 + 行内当前值」，
删除长副标题；同时把 Android 的 GL 呈现与 FSR 打磨到像素正确，并补齐多存档槽、倒带、放大等设置项。

## 亮点

### 界面

- **Android 游戏面板分组**：画面 / 文字 / 游戏 / 存档 / 高级 五个小标题，行内只留当前值；
  调整画面和按键、画面滤镜、GL 呈现、放大、帧率、字体、速度、存档、倒带、作弊等行全部保留。
- **Android「放大」**：GL 呈现新增 `放大：自动 / 2× / 3× / 4×`，选择后即时生效并持久化。
- **SDL2 桌面启动器**：设置面板底部提示改为按行上下文的一句话；F1 帮助与配置路径合并成单行。
- **网页版**：跟随系统浅色/深色主题，导入与存储提示精简为短句，卡片层级与图标不变。
- 统一品牌配色（深海军蓝 + 白色十字键 + 蓝色按键）贯穿 Android、SDL2 与网页。

### 画面

- **Android GL 呈现修复**：自定义画面比例（`NJ_VIEW_CUSTOM`）不再回退；窗口呈现采用垂直翻转；
  呈现目标改用窗口尺寸预算，960×1280 / 1080×2400 不再被 1M 像素上限拒绝。
- **Android FSR1**：EASU + RCAS 直接渲染到窗口表面，无需回读；选择后立即可见。
- **SDL2 自动放大**：`--upscale-scale auto` 按实际显示像素输出，宿主验证 4× 最近邻与软件呈现逐像素一致。
- FSR1 的 EASU/RCAS 移植自 MIT 许可的 AMD FidelityFX FSR 1.0（详见 `docs/UPSCALING.md`）。

### 兼容与稳定

- Android 大画布 + FSR 黑屏修复（自定义比例视图被 GL 呈现拒绝）。
- 存档多槽位（CLI / SDL2 / Android）、倒带（SDL2 / Android 环形缓冲）在 0.5.45 起提供，本版保留。
- 全量回归通过：CTest 211/211、libretro mock 前端矩阵 20/20、真实 RetroArch LoopDemo PASS、
  Wine 4/4、提交包校验 32/32。

## 构建产物

核心（8 个目标，`.info` SHA-256 `095f3c16a14d2d7f4a3c0a8bffd40408fc16e71b6c40ba963b53936ea7e56f0c`）：

| 目标 | 文件 | 大小 (B) | SHA-256 |
| --- | --- | --- | --- |
| linux-x86_64 | `nano_j2me_libretro.so` | 4,052,936 | `910857921278a41dc9b15efe9cc90c516ef15f4aa115f674d68e8e4fd6cfa081` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,526,576 | `e0d715ce47e25d0cf85c51c1eca450bece56468da439a4e4c3ba7e935154682a` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,179,852 | `bb19f678420bfde453d14797ab40d9db8d16101f5bee99c134ea3ce5ff455248` |
| android-arm64 | `nano_j2me_libretro.so` | 3,772,448 | `fd4a6e99164e6627611d22e0f5d78bf4f521cc5daceebb9878dcff09e8f9cf54` |
| android-armv7 | `nano_j2me_libretro.so` | 2,867,792 | `3aa159ea6ad0a4c7feb729008c70cc82d82b03c87f0ecd0f9828f7fb363ddcce` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,798,296 | `1c35a1c2269b53543bf6a46011a3c2ae0f3c576f388d67decd4fa00b420f5c4b` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,606,016 | `01976b3fa36d413fb804253911585ce3f77a216743540be24e990177e6a1dc2f` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,375,104 | `e011d93cf631be31d954e3729e65d53e31a46be11d656b0e8f71ca5b93e19622` |

核心归档 `nj2me-0.5.47-libretro-cores.zip` **12,188,997 B**，
SHA-256 `e7d0df1952fd75cf666f4fef0e51573ef16b7fa694cc75795ae6fed5d3382d76`（128 个文件）。

Android APK（versionCode 547）：

| ABI | 文件 | 大小 (B) | SHA-256 |
| --- | --- | --- | --- |
| arm64-v8a | `nj2me-0.5.47-arm64-v8a.apk` | 1,075,356 | `7c83f6c5e1ceeaa48fcc924e751e3308733bb9d59b5872c346a63d14b96029c2` |
| armeabi-v7a | `nj2me-0.5.47-armeabi-v7a.apk` | 1,083,550 | `68eab51b6cdc8b56a1f7135ecc923bc05e80fa9f68c21d60d2a9d42538f90631` |
| x86 | `nj2me-0.5.47-x86.apk` | 1,067,158 | `902edf6a91c9e66e42cfd667bb6d97f2dbf942878f03430d4a7f64810ae245e0` |
| x86_64 | `nj2me-0.5.47-x86_64.apk` | 1,079,449 | `73c433ca30d55a1a11005af90dfc84f9b300c524e2c7a45a1009e36385c52220` |
| legacy (API 5, ARMv5) | `nj2me-0.5.47-legacy.apk` | 743,750 | `9ee999fe999821e6e74a0960d1f0c7fea9706203ff2b904c26738a227ebca3fe` |

## 验证

| 项目 | 结果 |
| --- | --- |
| 全量串行 CTest（发布树） | 211/211 通过，376.65 s |
| libretro mock 前端矩阵 | 20/20（r66 linux-x86_64 核心） |
| 真实 RetroArch 1.22.2 LoopDemo | 120 帧 PASS，核心日志标记齐全 |
| Windows x86_64 Wine | 4/4 通过（300 帧） |
| 提交包校验 | 32/32（含 `.info` 与 super 补丁载荷一致性） |
| 全语料 libretro 渲染探针 | 继承 r65 结果：1,031 / 1,034（1 已知限制、2 已归档失败项）；本版核心渲染路径无改动 |

## 升级说明

- 存档格式与 0.5.46 一致；更早版本（0.5.8–0.5.45）的存档仍会因版本 CRC 不同被拒绝。
- Android GL 呈现仍为实验开关（设置面板「GL 呈现（实验）」），默认软件呈现；失败自动回退。
- 网页版与桌面的 shader/FSR 选项见 `docs/UPSCALING.md`；倒带与多存档槽见 `docs/REWIND.md`、`docs/SAVE_SLOTS.md`。
- 已知限制详见 `docs/LIMITATIONS.md`。

## 上游

- libretro-super PR：libretro/libretro-super#2204（head `15130ac2b2b53da2ae1a046119c08ec047dce1d0`，OPEN/MERGEABLE）
- libretro/docs PR：libretro/docs#1239（head `4f360b1`，OPEN/MERGEABLE）
- 未执行合并；如维护者要求调整元数据，按反馈更新补丁与 PR 描述。
