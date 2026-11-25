# nano_j2me 0.5.48 / r67

发布于 2026-11-25。本版是界面「自释其意」的延续：Android 面板行副标题被压缩为「标题即状态、值即副标题」，
并附上五组面板的截图证据；SDL2 启动器与网页文案保持 0.5.47 的精简状态。核心渲染路径不变。

## 亮点

### 界面

- **Android 面板二次精简**：显示状态信息 / 显示菜单按钮 / 文字与画面反锯齿 / 画面滤镜 / 倒带 / 硬件加速
  的说明副标题移除；游戏速度只留当前值，更换字体只留当前字体名（未设置为 `系统字体`）。
- **面板分组截图**：`docs/images/ui/panel-1-画面.png`、`panel-2-文字-游戏.png`、`panel-3-存档-高级.png`。
- 5 个 ABI APK 的版本号 548；JNI 设置链路与持久化行为不变。

### 兼容与稳定

- 核心渲染路径与 r66 相同；本版只含 Android Java/文案改动与文档。
- 全量回归：CTest 211/211（377.43 s）、libretro mock 20/20、真实 RetroArch LoopDemo PASS、Wine 4/4、bundle 32/32。

## 构建产物

核心（8 个目标，`.info` SHA-256 `9960dc74db53ec5283227c62282bc8a4837caecc805f9da5b4ed0676ec27c263`）：

| 目标 | 文件 | 大小 (B) | SHA-256 |
| --- | --- | --- | --- |
| linux-x86_64 | `nano_j2me_libretro.so` | 4,052,936 | `3e7cc2703b00ddb0075ad229b7edcf3c86b5b7906f9d8b29c1faa51d6d3340d1` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,526,576 | `1a74082c5f28e0a4d61dde5b9e139c077e0597d0b058758af270401197ba8710` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,179,852 | `aef8feabf10a28476c08b5e42975701680110cdcceaf6a051ffc0a7f85b40546` |
| android-arm64 | `nano_j2me_libretro.so` | 3,772,448 | `e90b7de47ccfa900840721090d597f601d2e9d292965c6e8ee5d61883e118e9f` |
| android-armv7 | `nano_j2me_libretro.so` | 2,867,792 | `49f4a54270d54eb937736219bf46151fcc2ce51ebf9371c8aa85198a40745d95` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,798,296 | `69fa22b35e58f20b8826dc4a16312b5599454ba6a6da7674589f0f08e4b6a134` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,606,016 | `3188b49e3b051778c3cf4037b2e7f35d90f5f977980bda4fbee83f22b9166878` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,375,104 | `d16b77314dbb9702ae629eed62f905ce471998f4929d881b3511f8ae6a056c7c` |

核心归档 `nj2me-0.5.48-libretro-cores.zip` **12,189,042 B**，SHA-256 `9d0a77bd35196af0e52ac5fd57f8ed595087973c7695615d9de079f2d9c9ff4b`（128 个文件）。

Android APK（versionCode 548）：

| ABI | 文件 | 大小 (B) | SHA-256 |
| --- | --- | --- | --- |
| arm64-v8a | `nj2me-0.5.48-arm64-v8a.apk` | 1,075,356 | `3b8f2272fa0e2630a628be41b3d8658890486beba0a8d5f17d105fc5919725fe` |
| armeabi-v7a | `nj2me-0.5.48-armeabi-v7a.apk` | 1,083,550 | `4cfeb8bd608e8d9150eda4775bfe5cd198a1c3c59a48cfa11eac831b2e24bfdd` |
| x86 | `nj2me-0.5.48-x86.apk` | 1,067,158 | `34a77fa32baaa9fac18bafe01b8a86d71282d1f9575c00a2a615e25af888c0b5` |
| x86_64 | `nj2me-0.5.48-x86_64.apk` | 1,079,449 | `ed3a503f3947a4363c9c03814d17240e5aa2388c191a3959e8d4dbcccb2a784b` |
| legacy (API 5, ARMv5) | `nj2me-0.5.48-legacy.apk` | 743,746 | `f93448b739a521504e628156d2128e957d2135e73a065fa1c7ef17ac2a832466` |

## 验证

| 项目 | 结果 |
| --- | --- |
| 全量串行 CTest（发布树） | 211/211 通过，377.43 s |
| libretro mock 前端矩阵 | 20/20（r67 linux-x86_64 核心） |
| 真实 RetroArch 1.22.2 LoopDemo | 120 帧 PASS |
| Windows x86_64 Wine | 4/4 通过（300 帧） |
| 提交包校验 | 32/32 |
| 全语料 libretro 渲染探针 | 继承 r65/r66 结果（核心渲染路径无改动） |

## 升级说明

- 存档格式与 0.5.47 一致；更早版本存档仍按版本 CRC 拒绝。
- Android GL 呈现仍为实验开关，默认软件呈现；失败自动回退。
- 已知限制见 `docs/LIMITATIONS.md`。

## 上游

- libretro-super PR：libretro/libretro-super#2204（head `3a8b48fdeb92e23e8b3e6de4aa39dfa60568d82a`，OPEN/MERGEABLE）
- libretro/docs PR：libretro/docs#1239（head `4f360b1`，OPEN/MERGEABLE）
- 未执行合并。
