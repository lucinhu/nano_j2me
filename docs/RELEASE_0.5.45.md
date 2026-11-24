# nano_j2me 0.5.45

发布日期：2026-11-25 · 内核包 r64 · 标签 v0.5.45

0.5.45 汇总了 0.5.44 之后完成的 Android 体验改进、实验性 GL 呈现路径与
测试/诊断基础设施，软件渲染仍是默认路径。

## 主要变化

- **Android 多存档位（补完）**：游戏内「保存状态」可选择快速存档和 4 个存档位，
  覆盖前二次确认；启动器游戏卡片的「读取存档」列出所有已保存槽位并显示保存时间；
  「删除存档」清空全部槽位。已在模拟器上完成保存/覆盖/读取/持久化验证。
- **Android 倒带**：设置面板新增「倒带」行（关闭 / 1 秒 / 2 秒 / 5 秒，以及「倒带一步」），
  快照每 0.5 秒一张、默认关闭、256 MiB 上限；日志 `rewind: on (...)` 与
  `rewind: restored a snapshot ...` 已在模拟器验证。
- **Android 画面滤镜**：最近邻 / 线性 / 扫描线 / LCD 网格 / CRT 五档呈现滤镜，保存在偏好设置里，
  截图对比显示扫描线约 23.7%、CRT 约 31.6% 的窗口像素被滤镜覆盖。
- **实验性 Android GL 呈现**：通过 `debug.nj2me.glpresent=1` 开启把游戏帧上传为纹理、
  用可插拔 GLSL 呈现的窗口路径（on-screen nearest/sharp/scanline/LCD/CRT），失败自动回退软件呈现。
  该路径**默认关闭**，像素级一致性尚未解决（见 `docs/notes/2026-11-25-android-gl-present-design.md`）。
- **测试/诊断基础设施**：新增静态帧夹具 `tests/fixtures/staticdemo.jar` 用于呈现一致性对比；
  `debug.nj2me.glview` 可钉死逻辑视图；GL 首次呈现会打印 EGL surface / ANativeWindow buffer / 视口尺寸；
  Android APK 构建脚本现可一次产出 5 个 ABI 分片包（含 legacy）。
- **启动器调色板统一**：Android、SDL2 桌面启动器与网页版统一为图标同源蓝色方案；
  `test_client_ui` 的像素断言同步到新调色板。

## 内核包（r64，8 个目标）

| 目标 | 大小 (B) | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 4,052,936 | `d4dfc5e99cb9e25c5472b4d4370d8e848df05146044e3d8296baf7925ae19f05` |
| linux-aarch64 | 3,526,576 | `4ed1de85583d945c76a70851ea4948b665d15ddb4a0fac9474b5a9e473e80e9c` |
| linux-armv7 | 3,179,852 | `647228020618959c6a4cf4284ada991d3ddf192e316f02828683c8fcb6d6e3e5` |
| android-arm64 | 3,772,448 | `429d94ccc41b6256a791ba18bbf49bab4b59b6d0a4fd3658b727e479df43531b` |
| android-armv7 | 2,867,792 | `40e59e3889fcd7d7f66305c9d8b18d9fa89b68cb79222b446cda37a98f1b76a5` |
| android-x86_64 | 3,798,296 | `c6f1d0911385e5089f03504600ffd1f291d8bdeaea5141c0a6eff02632799980` |
| windows-i686 | 3,606,016 | `251b26943f31ac428bae4b78f16689e675775f1cc31cd823a4bbad3f140f2012` |
| windows-x86_64 | 3,375,104 | `096fb8e5c86b1a4f6dd212721e1bc49558236597882cb133ead8acc54f47bc8f` |

`.info` SHA-256：`7ca05549700f384491606c14d2a254732001bda9d662324439530c6b0da86730`
（`display_version = "0.5.45"`）。

内核压缩包：`nj2me-0.5.45-libretro-cores.zip` 11,757,156 B
SHA-256 `2e4a86cfa254a7ba15e0abd2b92f8a11441b537e6c3aa862fdbf5f6a707b7cc3`。

## Android APK（versionCode 545）

| ABI | 大小 (B) | SHA-256 |
| --- | ---: | --- |
| arm64-v8a | 1,071,260 | `f136e4b16268dc650ad10146083ee5ba3dfc5d2336acd174d05ea6738c844418` |
| armeabi-v7a | 1,083,550 | `f2a4f4ab9ec9c4cf08e5739c2f5434ff6d301b9d4d9d57611cbe640afe932bcd` |
| x86 | 1,063,062 | `11809677ec8dd39533f632d2cad295cb6cfdd34e1729827e47ce389913e0abf1` |
| x86_64 | 1,079,449 | `8ef0ffb04d1028a04b4ebc2c243ad89f16b04216aabf1392e0ec9aeec621a932` |
| legacy（API 5 / ARMv5） | 743,749 | `5238c16c5c0e711c16108bae7105ba74109e4256171aeeb62ec8178d945ab1f0` |

## 验证

| 项目 | 结果 |
| --- | --- |
| 全量串行 CTest | 211/211 通过，375.11 s |
| libretro 前端矩阵 | 20/20 |
| 真实 RetroArch 1.22 | LoopDemo 120 帧 PASS |
| Windows x86_64 Wine | 4/4（300 帧） |
| 提交材料校验 | 32/32 |
| StaticDemo / 滤镜 / 模拟器 | 见 `docs/notes/2026-11-25-android-gl-present-design.md` |

## 升级说明

- 存档格式延续 0.5.8 起的版本化快照；0.5.8–0.5.44 的存档仍可读取。
- 快进倍率上限为 8×（Android 设置与 SDL2 快捷键），高倍率受游戏自身 CPU 限制。
- Android GL 呈现是实验特性，需要 `debug.nj2me.glpresent=1`，默认软件渲染。
- libretro 侧多存档位与倒带由前端管理；内核提供单槽状态接口。

## 上游提交

- libretro-super PR：https://github.com/libretro/libretro-super/pull/2204
  （fork 分支 `add-nano_j2me` @ `5fb18f152413eee576af103eba4729053d93d7c0`，基线 `c3c55c2d`）
- libretro/docs PR：https://github.com/libretro/libretro/docs/pull/1239
- 未向 libretro 官方仓库直接提交合并。
