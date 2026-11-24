# nano_j2me 0.5.44

发布日期：2026-11-25  
核心版本：r63（`0.5.44`）  
libretro 元数据：`nano_j2me_libretro.info` SHA-256 `a3855729efc0444c90cc700307c9288a73d3098ba5366ad76a5df7a4cb0987e9`

0.5.44 是 0.5.43 之后的界面与证据更新版：三端统一品牌配色、网页 PWA 主题色对齐，以及真实游戏上的 FSR 后处理质量/CPU 记录。运行时、存档格式与 libretro 接口保持不变。

## 亮点

- **三端统一品牌配色（F 项）**
  - Android（自绘 Material 3）：baseline 调色板从 Material 默认紫换成图标同源深蓝/藏青；暗色 primary `#AFC6FF`、FAB 容器 `#1B4B96`、surface `#10131A`；亮色 FAB 使用品牌蓝 `#2F5FA8`、surface `#F7F9FF`。为保证启动器/游戏面板/网页/图标一致，停用 Material You 动态取色（代码保留，可再开启）。
  - Web：强调色由旧绿改为品牌蓝（`--accent #6F9BFF` / `--accent-strong #4C7BFF`），表面色改为同源藏青；PWA `theme_color`/`theme-color` 对齐 `#11141B`。
  - SDL2 启动器：底色、分隔线、选中行、文件夹色统一为品牌蓝与藏青（选中行 `#2F5FA8`）。
  - 布局审计：三端间距/圆角刻度本就一致（Android 16dp 边距、72dp 行高；web 16/10/13；SDL2 48dp 行），本轮只统一颜色，避免无意义改版。
- **FSR 真实游戏验证（A 项证据）**
  - Galaxy on Fire 2 启动画面（SDL2 GLES2，2x）：FSR1 EASU+RCAS 与 nearest 2x 相比 **63.31% 像素差异**，文字/UI 边缘明显更平滑；nearest 1x 与软件呈现 **0/997,440** 差异，确认呈现层替换本身无几何偏差。
  - CPU（同机 RTX 5080）：Galaxy 560 帧 @30 fps 为软件 4.663 s vs GL+FSR 5.872 s user（≈+26%）；纯 2D LoopDemo 3000 帧后处理开销约 **0.74 ms/帧**。
  - 截图见 `docs/images/upscale-nearest-galaxy.png` 与 `docs/images/upscale-fsr-galaxy.png`，数据记录在 `docs/UPSCALING.md`。

## 验证

| 项目 | 结果 |
|---|---|
| 全量串行 CTest（发布树） | 211/211 通过，377.29 s |
| libretro mock 前端矩阵 | 20/20 通过 |
| 真实 RetroArch 1.22.2 LoopDemo（120 帧） | PASS |
| Windows x86_64 Wine（300 帧） | 4/4 通过 |
| 提交材料 bundle 校验 | 32/32 通过 |

0.5.43 的全语料视觉探针结果（1034 部：1031 通过 / 1 已知限制 / 2 已记录失败）继续有效：0.5.44 只改颜色与文档，未触碰运行时。

## 核心产物（8 目标）

| 目标 | 大小 | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,052,936 | `ca1122708414e099e9090786ed5bee504db4760f418167834ac0924a7e575b31` |
| linux-aarch64 | 3,526,560 | `1fed161448fb1deb2e4bab77444d19e9355209a207c17f1f04143fd714a50047` |
| linux-armv7 | 3,179,836 | `08c842fd80865b7860d46d10cafb5fe2bb3052780da204753a26a1057f3becaa` |
| android-arm64 | 3,772,432 | `522387aba50086f1ab05d8fa0210f0ec71815980af610a3c89edd3d7e64032fc` |
| android-armv7 | 2,867,776 | `17104546373e0b3fc9bdb5c21a8f33becc4909e6d3e2c1373479110c5337966b` |
| android-x86_64 | 3,798,280 | `0d20f9379c35fffffe6f8c07b43f8259d74781f93b7dfac34c477ea528207b89` |
| windows-i686 | 3,606,016 | `26590dc3219f98735e422f331469bbdbb5881b280fb87d2d2bd4452d5f20c4d7` |
| windows-x86_64 | 3,375,104 | `2dd5a9527df4ab30c55c379b22f49f0f40e04727729bf70277eb41d0c893d804` |

核心合集：`nj2me-0.5.44-libretro-cores.zip`（128 个文件）**12,188,910 B**，SHA-256 `38120c5a1340cc53f23aadd3e1ebfea05b6db3b4c6528db9c4af5187b62a9077`。

## Android APK（versionCode 544）

| ABI | 大小 | SHA-256 |
|---|---|---|
| arm64-v8a | 1,071,260 | `032fbfff30bcc8fdc1602d520cf97b40eb4199f097cfbf8e261d8fb280e3c31c` |
| armeabi-v7a | 1,079,454 | `24867d883c3b3f076d42371d20c09c6159998f4c4568189cdfa5fd77c1761939` |
| x86 | 1,058,966 | `1165d1480286333b94b879f048853bbf67fdc404c242ffc05d9b4ad69e81e2e0` |
| x86_64 | 1,075,353 | `6ea51c0b1d0882e944f78cf0d5b549fe3665ea975110cef59952bf02dcfd529f` |
| legacy（ARMv5/API 5） | 743,748 | `3bdf0c2dacbe6d2a134773a48bd2892bf85223d07e4b9104f24a277988dc03a8` |

## 升级说明

- 运行时与存档格式未变，0.5.43 的存档/即时存档可直接读取。
- Android 配色不再跟随壁纸（Material You）；如想恢复动态取色，可在 `Md3.java` 的 `init()` 中重新启用 `dynamicPalette(ctx)`。
- 网页版仅颜色/主题色变化，无功能变化。

## 上游提交

- libretro-super PR #2204：fork 分支更新到本版 `.info`（提交 `bfec0fabb8e3f8ee3bea7f8e743505df5747b7e0`，基于 `c3c55c2280a0eac7e8343a7f15e3c2e35f9a69e6`）。
- libretro/docs PR #1239：页面内容保持一致（fork 提交 `4f360b1`）。
- 未向上游仓库执行合并操作。
