# 平台与格式支持记录（2026-11-13/14 验证）

这份记录只写**实际执行过**的验证，以及验证暴露出的限制。它不是性能承诺：GPU
路径在很多场景下比软件更慢，详见下文和
[GRAPHICS_ACCELERATION.md](GRAPHICS_ACCELERATION.md)、[GLES2_3D.md](GLES2_3D.md)。
复现命令与产物哈希见文末。

## 图形

| 平台 | 纯软件 | GLES2 二维（fill/blit） | GLES2 raster（M3G/Mascot） | GLES2 三角形批量 | 备注 |
| --- | --- | --- | --- | --- | --- |
| Linux / SDL2 | 是 | 可选（默认不安装；`NJ_SDL2_GLES2_2D=1` 才启用，实测比软件慢） | 是 | 是（NVIDIA ES3.2、Mesa ES2/GL2.1） | 三角形路径在 RTX 5080 上对 MC 的真实 packet 仍慢于软件，默认软件 |
| Windows / SDL2（MinGW，Wine 11.21） | 是（MC 真实世界） | 可选（同上，默认不安装 2D 回调） | 未在本轮复验 | 构建包含后端；WGL 动态加载器有真实 GL 上下文证据，未在 Windows 上复验三角形路径 | 只在 Wine 下验证，无原生 Windows 机器 |
| Android 5+（API 21，模拟器） | 是（MC 真实世界） | 可选（默认不安装 2D 回调；旧版可 `NJ_ANDROID_GLES2_2D_ACCEL=1` 构建） | 是（m3gdemo `rasters=16`） | **否**：模拟器缺 `GL_EXT_frag_depth`，门控 SKIP，软件回退已验证 | 真机三角形路径尚未在 APK 内验证 |
| Android 2.0/2.1+（API 5，ARMv5） | 是（v0.5.7 legacy APK） | 否 | 否 | 否 | 仅软件；真实老手机未验证 |
| libretro（硬件 GL） | 是 | 是 | 是 | 是（MC 真实 1189 个 batch） | 真实 EGL、上下文替换、存档/读档已验证 |
| ESP32 | 构建通过（见 `tests/embedded`） | 否 | 否 | 否 | 本轮未在设备上运行 |
| 网页浏览器（WebAssembly，Emscripten 7.0.0） | 是（软件光栅化器；Node 与真实 Firefox 中 240x320 帧已出画） | 否 | 否 | 否 | 单线程构建，没有 GL 后端；见 [WEB.md](WEB.md) |

图形契约要点：

- 三角形批量接口是**可选**的同步事务：`nj_gfx_triangles` 返回 `DONE` 时 CPU
  颜色、深度和 `*submitted` 都已提交；任何失败返回不得写调用方缓冲，调用方
  重新走原软件路径。接口与校验见
  [nj_gfx_triangles.h](../include/nj2me/nj_gfx_triangles.h) 和
  [nj_gfx_triangles.c](../src/gfx/nj_gfx_triangles.c)。
- GLES2 三角形后端要求 `GL_EXT_frag_depth`、`GL_OES_depth24` 和 ≥23 位
  fragment highp；不满足时 SKIP 并保留软件回退。Android 模拟器的
  SwiftShader/host 翻译层都不提供 `EXT_frag_depth`，所以 APK 在该模拟器里
  MC 的约 100 万次三维绘制全部走软件（`declines=1075981`），画面正确。
- 每个回调的上传/读回开销是固定成本；同一主机上 48 个真实 MC packet 的
  基准为：软件 1.173842 ms，GPU ROI 7.694929 ms（6.55×），GPU 全表面
  32.089298 ms。仅 272 三角形的天空 packet GPU 更快。因此默认仍是软件，
  不宣称加速。
- 跨回调的帧级批处理（journal）已实现：bindTarget→releaseTarget 之间的
  多次 render 合并为一次有序 list 事务（≤256 packet/≤131,072 三角形，
  纹理入队快照，DECLINE 时按原顺序软件重放）。SDL2 同一 MC 存档 600 帧：
  软件 8.67 s、单 packet GPU 13.89 s、journal GPU 9.85 s（1.57× 于单
  packet，仍比软件慢约 14%）。libretro 因 2 ms 硬件门会拒绝绝大多数提交，
  明确不接入 list 钩子（详见 `docs/GLES2_3D.md`）。

## 音视频与图像格式

| 格式 | 解码器 | 本轮验证 | 限制 |
| --- | --- | --- | --- |
| WAV PCM8/16、IMA ADPCM，单/双声道 | 内置 | 与 FFmpeg 逐样本一致（`test_wav_stereo` 22 case/5208 检查）；修复了双声道 IMA 的越界写和长度错误 | 无 |
| AMR-NB（含 DTX/舒适噪声） | 内置 | 203 流/23622 帧语音 PCM 逐位不变；DTX CN SNR 8.45–12.10 dB；纯 SID RMS 343.80346 vs OpenCORE 343.14002；48 万恶意帧 ASan/UBSan | DTX 非逐位一致；明确列出 LSF 加权、hangover、SID 模式位等差异 |
| MP3 | 内置 | Android 真机 PCM 对照 SNR 80.50 dB，lag 1152，ratio 1.0000001 | 无 |
| AAC（ADTS、audio-only M4A） | **仅 Android 平台** MediaExtractor/MediaCodec | Android API21 真机探针 11/11 PASS；PCM 对照 mono 42.23 dB、stereo 68.53 dB、M4A 43.62/69.42 dB（lag 1024）；含视频轨的 MP4 全部拒绝 | Linux/Windows/libretro 核心没有 AAC 解码器；旧 Android 无此 provider |
| MIDI | 内置合成器 | ctest `test_midi`；Android 平台合成器路径 | 无 |
| H.263（3GP） | 内置 | Android APK 内解码 176×144 并绘出真实帧；Linux 测试 | 无 |
| MPEG-4 Part 2（M4V） | 内置 | Android APK 内解码 176×144、RGB888 快照、`createRGBImage` 绘制成功；Linux 测试 | 只支持 Simple Profile；B 帧/重排 CTTS 容器会被明确拒绝 |
| PNG | 内置 | Android 真机 17×13 RGB8 解码；`test_png`；修复 29 字节内 heap 越界读 | 无 |
| GIF | 内置 | Android 真机 4×4、1×1 解码 | 无 |
| JPEG | 内置 | Android 真机 baseline 35×23、progressive 120×90 解码 | 无 |
| MP4/3GP 容器 | 内置 | ESDS、CTTS v0/v1、ELST（单段 rate 1 或前导空编辑）；`test_mp4_timing` 14359 检查 + 8.2M fuzz；电影 AMR 音轨时间线映射 `test_movie_amr_timing` 19 case/753426 检查 | 复杂编辑列表拒绝；没有重排队列，CTTS 非单调容器拒绝 |

播放语义：

- 电影播放器的画面按容器 PTS 调度（capped 端口）；uncapped 端口（Android
  默认、libretro）保留“每次显式 pump 前进一帧”的确定性步进，但**带音轨的
  电影改为跟随音频时钟**，否则 15 fps 片头会在 60 fps 渲染循环里 4 倍速
  放完、声音和画面分离。
- `VideoControl.getSnapshot` 按 MIDP 声明返回 `byte[]`（只支持 `"RGB888"`）。
  之前注册成 `Ljava/lang/Object;` 会让真实 MIDlet 抛
  `NoSuchMethodError`，已修复并有 `test_lib` 覆盖。
- 直接视频 overlay 需要 Player `STARTED` 且 `setVisible(true)`；在
  libretro 前端只支持单平面呈现。

## 网页 / WebAssembly（2026-11-14 验证）

构建：Emscripten 7.0.0（`777e6e5`）。`tools/build_web.sh` 产出单线程
`build/web/js/nj2me.js` + `nj2me.wasm`，没有 Asyncify、pthreads 或
SharedArrayBuffer。完整类库、软件光栅化器、解码器与 M3G/Micro3D 都在 wasm 内；
与原生移植的差异只在端口钩子：网络、套接字、蓝牙、传感器和 JIT 钩子为 `NULL`
（相应 API 按名字明确拒绝），文件系统走 Emscripten libc FS，画面由 C 侧从
RGB565 展开成 RGBA8888 交给 `<canvas>`。虚拟时钟由宿主驱动：加载阶段
（`nj_vm_new_midlet` 之前）`sleep_ms` 全量快进，运行阶段每次
`njw_frame(dt)` 推进一次时钟，`sleep_ms` 只按小步快进使阻塞中的 Java 线程能继续。

已实际执行的检查（Node，产物为当前 `build/web/js/nj2me.js`）：

| 项 | 结果 |
| --- | --- |
| hello.jar 渲染 | 启动 rc 0，240 帧全部 `DREW`，240x320 RGBA 共 76800 像素非黑，PNG 内容正确（"Hello World!"） |
| 固定语料批处理（41 款：39 游戏 + audiodemo/bench，120 帧/款） | 46/46 检查通过；39 款游戏全部启动并 120/120 出画；audiodemo/bench 按 `notifyDestroyed()` 正常退出（分别 1、4 帧）；唯一排除 `1945空战英雄_6105.jar`，它在原生 headless 上同样失败（标题自身 `IllegalAccessException`） |
| bubblet.jar 构造器 | 修复前 `NullPointerException @ Bubblet.<init>+15`：构造器读取 `System.getProperty("com.nokia.keyboard.type")`；端口补上 `device_property`（回答 `None` 等）后正常出画 |
| 存档往返（game15.jar） | `can_save=1`，保存 360,171 字节，读回 rc 0，读档后继续 `RUNNING\|DREW`，epoch 41 |
| 声音（audiodemo.jar） | 修复前可读 0 帧：运行阶段快进把 `playTone` 在单帧内跑完，`tone_stop` 清空了宿主还没拉取的队列；改为 `tone_stop`/`pcm_close` 不清队列（有界环）后 2020 帧可读 |
| JAR 图标 | hello.jar 的 `njw_app_icon` 返回 191 字节 PNG（修复 MIDlet-1 图标路径以 `/` 开头时查不到 zip 项的问题，与 `nj_app_resource` 的规则一致） |
| 无静态状态门禁 | `tools/check_no_statics.sh` 通过（83 个目标文件）；web 端口与 libretro 一样按 ABI 套豁免，MIDlet 状态仍在 `nj_vm` 内 |
| 真实浏览器冒烟（headless Firefox + `build/web` 静态服务） | 导入 hello.jar → 库条目显示 "Hello World / frog-parrot.net" → 点击出画 240x320（内容为 "Hello World!"）→ 方向键/Enter 无异常 → 返回库；无 page error，截图 `.local-tools/debug-runs/web-manual-smoke/` |
| 与原生帧对照 | `1942空战_10403.jar` 的标题屏（"¿SONIDO?" 选择音效）在浏览器里与原生 headless `--dump-frames` 的 240x320 帧逐像素一致；该标题在原生与 wasm 上都抛同一个 `ArrayIndexOutOfBoundsException`（商品自身行为，不是端口回归） |
| 真实游戏浏览器实测（Playwright Firefox，可玩性） | `蹦球传说美化版_2959.jar`（Bounce Tales 修改版，自动演示画面）在真实页面中：rAF pump **60.1 次/秒**、6 秒内 9 个不同画面帧（30 次采样）、方向键/Enter 后画面改变且 pump 不停、打开工具面板后画面冻结（自动暂停）并在关闭后恢复、退出回库后刷新游戏仍在库中；同一页面运行 `audiodemo.jar` 3 秒，引擎日志出现 10 行音频事件（`wav=200000`、`amr=80000`、`adpcm=252500`、state=400，`http=unsupported` 符合无网络预期）；0 console/page error，证据 `.local-tools/debug-runs/web-playtest/` |
| 浏览器页面（Playwright Firefox + `tools/check_web.sh --browser`） | 18/18 检查通过（约 5 s）：静态服务 `build/web` → 真实引擎 `kind=wasm`、API 1、版本 0.5.7 → zh-CN 界面 → 导入 hello.jar（元数据/图标进 IndexedDB）→ 画布 240x320 非黑（4 色）→ 方向键/Enter（nj_key 15/16/17/6）帧号推进 → 面板自动暂停（帧冻结）+ 继续 → 存档槽 1 blob 329,724 字节、读档往返不重启 → 退出回库 → 刷新后游戏与存档仍在 → 缺 wasm 时降级 mock 并显示指向 `tools/build_web.sh` 的横幅；0 page/console error、0 外部请求；证据 `.local-tools/debug-runs/web-lead-final-all/browser-0*-*.png` |

性能（600 帧空跑，两边都是 Release；原生用 `--fps 0` 即 uncapped）：

| 游戏 | wasm | 原生 x86_64 | 比值 |
| --- | --- | --- | --- |
| game15.jar | ≈6,117 fps | ≈16,216 fps | 2.7× |
| bubblet.jar | ≈3,826 fps | ≈7,692 fps | 2.0× |
| slimevolley.jar | ≈7,908 fps | ≈20,690 fps | 2.6× |
| 1942空战_10403.jar | ≈7,925 fps | ≈12,500 fps | 1.6× |

这是余量不是帧率：游戏按虚拟时钟计时，60 Hz 只需要每秒 60 次 pump。首次加载
成本是约 2.1 MiB 的 wasm 下载与实例化，堆可增长，超过初始 64 MiB 不会直接失败。

## 已知限制（全部为实测结论）

1. GPU 三角形路径在 RTX 5080 笔记本上对真实 MC packet 比软件慢（见上表），
   默认关闭，不构成加速承诺。
2. Android 模拟器缺 `EXT_frag_depth`，APK 内三角形批量不可用；需要真机
   （Adreno 725 已用离屏原生程序验证后端本身，但未在 APK/游戏内验证）。
3. Android 旧版（API 5）只有软件渲染；没有真机验证。
4. AAC 只在 Android 5+ 平台可用；其他平台遇到 AAC 会按“不支持的格式”拒绝。
5. B 帧（重排）视频容器被拒绝；Simple Profile 解码器不支持。
6. 视频无音轨且端口 uncapped 时，仍按 pump 步进（片头可能快于实时）。
7. journal 批处理在 SDL2 上仍慢于软件（约 14%）；libretro 的硬件门模型
   会拒绝 VM 线程的提交，因此该平台不启用批处理；Android 模拟器因缺
   `EXT_frag_depth` 只能走 DECLINE→软件重放。
8. Windows 只在 Wine 下验证。
9. 网页版没有网络、套接字、蓝牙、传感器和 JIT 钩子；iOS Safari、WebKitGTK、
   >512 MiB 堆、同源多标签页共用同一 IDBFS 前缀都未验证。
10. wasm 目标不能用 `-DNJ_WERROR=ON` 构建：clang 把现有头文件里重复的
   `typedef` 当作 C11 扩展告警，而该开关会把警告变成错误。

## 复现

```sh
# Linux 桌面 + 测试
cmake -S . -B build -DNJ_ENABLE_MEDIA=ON -DNJ_ENABLE_VIDEO=ON
cmake --build build -j6
ctest --test-dir build -j4

# Android 5+（现代，含 GLES2 实验后端）
tools/build_android_apk.sh --out build/android --abi "armeabi-v7a x86_64" --gles2

# Android 2.0+（API 5，ARMv5 软浮点，仅软件）
tools/build_android_legacy.sh --out build/android-legacy

# Windows（MinGW 交叉编译）
tools/build_windows.sh --out build/windows     # 见脚本头部参数

# 网页 / WebAssembly（Emscripten）
tools/emsdk_setup.sh
tools/build_web.sh
tools/check_web.sh --node --corpus             # 另有 --browser（Playwright Firefox）
```

本轮产物（工作树未提交，`git status` 仍保留全部既有脏改动）：

| 产物 | 大小 | SHA256 |
| --- | --- | --- |
| `.local-tools/debug-runs/gfx-full-20261113/android/final-build/nj2me.apk` | 1784034 | `3c536b91919529e40f5292e958392b5d612006a27061c13c22950f71cabf634d` |
| `.local-tools/debug-runs/gfx-full-20261113/windows/nj2me.exe` | 4025246 | `4c2440a43d0aec5b670b3cde26b53e471645c05637618a28ffac968511698e7c` |
| `.local-tools/debug-runs/gfx-full-20261113/linux/libretro/nano_j2me_libretro.so` | 4061992 | `c9fccfdd3b1863ec7750f8d0094827561b7878ca7a3bad13e6b3472d55a2db3f` |
| `.local-tools/debug-runs/gfx-full-20261113/linux/nj2me` | — | `6f83ecf62b30be9399d4a9098da78498489c78cfb52adc8313f6d26c806ade99` |
| `.local-tools/debug-runs/gfx-full-20261113/android-legacy/nj2me-legacy.apk` | 694454 | `4fcae0408bb83b34a9a4e097e8c61124881973900d48c559520ca48ad396446d` |
| `build/web/js/nj2me.js` | 93864 | `5f0919d8cc7286832e3e777225b3cd93969bd944b388606084aee018488d6be9` |
| `build/web/js/nj2me.wasm` | 2143771 | `5ed9f715d889945edd529f45647c27206cd0479f327fd574c5d2f1ce1fad050b` |

以上产物均由同一份当前源码构建（含 2026-11-14 的 journal 批处理改动；
Windows 目标在最后一次修改后增量构建，`ninja` 报告无待编译对象）。源码树
未提交，哈希只对应本轮工作树。

## 证据

- Linux 构建与全量测试：`.local-tools/debug-runs/gfx-full-20261113/linux/`（195 项 ctest，本轮 195/195 通过）
- journal 批处理 A/B：`.local-tools/debug-runs/gfx-full-20261113/sdl-mc-journal/`（`replay.sh`、`shot-off.png`、`final-on.png`）
- Android journal 回放（模拟器无 `EXT_frag_depth`）：`.local-tools/debug-runs/gfx-full-20261113/android/final-build/shot-mc-journal4.png`、`shot-maze4.png`
- Android 媒体：`.local-tools/debug-runs/gfx-full-20261113/android-media-demo/`（截图、视频快照、图像解码日志）
- Android AAC/AMR/MP3 探针：`.local-tools/debug-runs/gfx-full-20261113/android-aac/`（`pcm-compare.log`）
- Android 最终 APK 与 GLES2/MC：`.local-tools/debug-runs/gfx-full-20261113/android/final-build/`
- libretro 真实 EGL：`.local-tools/debug-runs/gfx-full-20261113/libretro-live/`（`mc-current`、`bounce-current`、`REPORT.md`）
- Windows/Wine：`.local-tools/debug-runs/gfx-full-20261113/windows-audit/`（`frames/`、`sdl2-mc-window.png`、`sdl2-mc.log`、`win-gles2-maze.log`、`wine-wgl.log`）
- AMR/编解码报告：`.local-tools/debug-runs/gfx-full-20261113/amr/`、`.local-tools/debug-runs/gfx-full-20261113/codec-audit/`
- 网页版：`.local-tools/debug-runs/web-lead-final-all/`（Lead 的最终 `--all` 运行：check-web.log、
  41 张语料 PNG、`browser-01…08-*.png`、`browser-shell-test.json`、`browser-console.log`）、
  `.local-tools/debug-runs/web-verify-<日期>/`（Node API/语料日志、PNG 帧）、
  `.local-tools/debug-runs/web-shell/`（外壳 mock 与真实 wasm 自测）和
  `.local-tools/debug-runs/web-manual-smoke{,2}/`（Lead 的 headless Firefox 冒烟 + 原生帧对照）和
  `.local-tools/debug-runs/web-playtest/`（Lead 的真实游戏可玩性实测：60.1 rAF/s、面板暂停、
  刷新持久化、AudioDemo 音频事件）；本记录的网页数字来自这些目录
