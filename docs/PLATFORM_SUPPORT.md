# 平台与格式支持记录（2026-11-13/14 验证）

这份记录只写**实际执行过**的验证，以及验证暴露出的限制。它不是性能承诺：GPU
路径在很多场景下比软件更慢，详见下文和
[GRAPHICS_ACCELERATION.md](GRAPHICS_ACCELERATION.md)、[GLES2_3D.md](GLES2_3D.md)。
复现命令与产物哈希见文末。

## 图形

| 平台 | 纯软件 | GLES2 二维（fill/blit） | GLES2 raster（M3G/Mascot） | GLES2 三角形批量 | 备注 |
| --- | --- | --- | --- | --- | --- |
| Linux / SDL2 | 是 | 可选（默认不安装；`NJ_SDL2_GLES2_2D=1` 才启用，实测比软件慢） | 是 | 是（NVIDIA ES3.2、Mesa ES2/GL2.1） | 三角形路径在 RTX 5080 上对 MC 的真实 packet 仍慢于软件；新增 auto 校准后该机自动选择软件 |
| Windows / SDL2（MinGW，Wine 11.21） | 是（MC 真实世界） | 可选（同上，默认不安装 2D 回调） | 未在本轮复验 | 构建包含后端；WGL 动态加载器有真实 GL 上下文证据，未在 Windows 上复验三角形路径 | 只在 Wine 下验证，无原生 Windows 机器 |
| Android 5+（API 21 / API 36 模拟器） | 是（MC 真实世界） | 可选（默认不安装 2D 回调；旧版可 `NJ_ANDROID_GLES2_2D_ACCEL=1` 构建） | 是（m3gdemo `rasters=16`） | 是（ES3 变体在 API 36 GLES 3.1 模拟器上跑通并逐字节对拍；auto 校准仍选择软件） | API 21 模拟器既无 `EXT_frag_depth` 也不是 GLES3，仍 SKIP+软件回退；API 36 模拟器 `m3gdemo` 校准 `gpu=414us software=183us -> disabled`，MC 校准 `gpu=859us software=247us -> disabled`；真机 APK 内三角形路径仍待复测 |
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
- GLES2 三角形后端的能力门控：GLES 2 需要 `GL_EXT_frag_depth`、
  `GL_OES_depth24` 和 ≥23 位 fragment highp；GLES 3.0+ 改用 ES3 着色器变体
  （核心 `gl_FragDepth` + 核心 D24 渲染缓冲），不再要求 `EXT_frag_depth`；
  两者都不满足时 SKIP 并保留软件回退。API 21 模拟器的 SwiftShader/host
  翻译层两条都不满足，所以该环境里 MC 的约 100 万次三维绘制全部走软件
  （`declines=1075981`），画面正确；API 36 OpenHV 模拟器是 GLES 3.1，ES3
  变体已通过 1459 用例/2,759,102 项检查的真实设备对拍（深度键误差 0）。
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
- 持久 GPU 会话（2026-11-14 架构改动）：新增 begin/end/abort 会话契约，
  屏障之间 FBO/颜色/深度 key 常驻 GPU，只在 CPU 观察点（2D 绘制、present、
  `getRGB`/`getPixels`、clear、目标切换、软件回退）一次回读并只提交被提交
  packet 的矩形。SDL2/Android 已接线，libretro 保持单 packet。会话语义与
  无状态路径逐像素一致（`test_gfx_triangles_gles2` 新增 session 等价测试）。
  实测 MC 600 帧：会话 GPU 7414 ms vs 软件 5824 ms（启动 663/692 ms），
  即 ≈11.25 vs ≈8.55 ms/帧——**会话对 MC 仍无净收益**，因为 MC 每帧 3D/2D
  交替约 9 次，每次 2D 绘制都强制结束会话并回读（commit 合计 ≈1.9 ms/帧）。
  纯软件路径不执行任何会话代码，新增钩子为空指针检查（0.88 ns/次）。

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
`njw_frame(dt)` 推进一次时钟，`sleep_ms` 只按小步快进使阻塞中的 Java 线程能继续；
此外每帧把宿主真实流逝时间折进时钟（单调、上限 250 ms），使 `System.currentTimeMillis()`
忙等的标题（如魂斗罗）不会在单帧内卡死。构造器永不返回的标题（如毁灭战士3）
会在 10 秒/20000 次帧泵后以明确错误结束，而不是冻住页面。

已实际执行的检查（Node，产物为当前 `build/web/js/nj2me.js`）：

| 项 | 结果 |
| --- | --- |
| hello.jar 渲染 | 启动 rc 0，240 帧全部 `DREW`，240x320 RGBA 共 76800 像素非黑，PNG 内容正确（"Hello World!"） |
| 固定语料批处理（42 款：39 游戏 + 午夜台球回归标题 + audiodemo/bench，120 帧/款） | 47/47 检查通过（8.7 s）；42 款游戏全部启动并 120/120 出画、非黑；audiodemo/bench 按 `notifyDestroyed()` 正常退出（分别 1、4 帧）；唯一排除 `1945空战英雄_6105.jar`，它在原生 headless 上同样失败（标题自身 `IllegalAccessException`） |
| bubblet.jar 构造器 | 修复前 `NullPointerException @ Bubblet.<init>+15`：构造器读取 `System.getProperty("com.nokia.keyboard.type")`；端口补上 `device_property`（回答 `None` 等）后正常出画 |
| 存档往返（game15.jar） | `can_save=1`，保存 360,171 字节，读回 rc 0，读档后继续 `RUNNING\|DREW`，epoch 41 |
| 声音（audiodemo.jar） | 修复前可读 0 帧：运行阶段快进把 `playTone` 在单帧内跑完，`tone_stop` 清空了宿主还没拉取的队列；改为 `tone_stop`/`pcm_close` 不清队列（有界环）后 2020 帧可读 |
| JAR 图标 | hello.jar 的 `njw_app_icon` 返回 191 字节 PNG（修复 MIDlet-1 图标路径以 `/` 开头时查不到 zip 项的问题，与 `nj_app_resource` 的规则一致） |
| 单帧内时钟修复（忙等标题） | 修复前 `System.currentTimeMillis()` 忙等的标题（魂斗罗_5652 等 16 款）在 wasm 单帧内死循环/超时 150 s 以上；把真实流逝时间折进虚拟时钟后，魂斗罗 90 帧 **6340 ms**（原生 **6322 ms**），原 16 个 timeout 标题 15 个转 ok，1 个（毁灭战士3）转为明确的构造器拒绝 |
| 构造器已装 Displayable 却跳过 startApp（真实 bug） | `午夜台球圣诞中文版_8677.jar`（GloftCHPO，极限台球挑战赛）的构造器里 `setCurrent()` + 启动游戏线程，真正初始化在 `startApp()`；修复前 runner 因 `displayable_ready` 已真而跳过 startApp → 画面全黑且每帧 NPE（`Graphics.drawImage ... image is null`）；改为**无条件** queue `startApp()`（与原生 `nj_run` 一致）后，20 帧起 76800 非黑像素并正常显示菜单/游戏画面 |
| 构造器永不返回的标题 | `毁灭战士3_10598.jar` 在构造器里 join 运行游戏循环的线程；原生 CLI 靠 `--run-frames` 预算中止构造器，浏览器版改为 10 s/20000 次泵帧上限并返回明确错误；`njw_start` 约 10.0 s 内失败，不再挂死 |
| 全语料审计（403 标题，90 帧/款，Node；`tools/check_web.sh --sweep` 已自动化） | **392 ok / 8 black / 1 fatal / 1 构造器拒绝 / 1 无效 JAR**，117 s、零 timeout、RESULT PASS；每标题独立子进程（25 s 超时），每 6 帧采样 + 末帧，black 只作警告（宿主速度/墙钟相位会让采样结果偏移）；8 个 black 中 6 个与原生 `--fps 0` 同帧全黑，乱世Q三国_1649/有女倾城破解版_11525 为时序相关加载画面；火焰纹章改判 ok（采样已捕获内容）；愤怒的小鸟为 Java heap 不足（原生默认 heap 同样 OOM，`NJW_OPT_HEAP_BYTES` 64 MiB 下 wasm 与原生均正常 90 帧、76639 非黑像素）；鲁宾逊原生同样拒绝；工具 `tools/web_test/web_sweep_test.mjs`，原始结果 `.local-tools/debug-runs/web-round8-sweep-clean/{sweep-403.jsonl,summary.txt}` |
| 游戏内存设置 + fatal 错误提示（网页壳） | 游戏面板新增「游戏内存」选择（16/32/64/128 MiB，`njw.settings.heapMb` 持久化、下次启动生效）→ `NJW_OPT_HEAP_BYTES`；`njw_error()` 现在附带最后一条引擎错误（如 `the display loop failed (status -1); last error: thrown: java/lang/OutOfMemoryError ...`），壳对 OOM 文案追加“把游戏内存调高后重开”。回归：`web_api_test` 的 heap 检查（默认 16 MiB 失败、64 MiB 90 帧 76639 非黑像素）+ `web_browser_test` 的内存选择器持久化检查 |
| 无静态状态门禁 | `tools/check_no_statics.sh` 通过（83 个目标文件）；web 端口与 libretro 一样按 ABI 套豁免，MIDlet 状态仍在 `nj_vm` 内 |
| 真实浏览器冒烟（headless Firefox + `build/web` 静态服务） | 导入 hello.jar → 库条目显示 "Hello World / frog-parrot.net" → 点击出画 240x320（内容为 "Hello World!"）→ 方向键/Enter 无异常 → 返回库；无 page error，截图 `.local-tools/debug-runs/web-manual-smoke/` |
| 与原生帧对照 | `1942空战_10403.jar` 的标题屏（"¿SONIDO?" 选择音效）在浏览器里与原生 headless `--dump-frames` 的 240x320 帧逐像素一致；该标题在原生与 wasm 上都抛同一个 `ArrayIndexOutOfBoundsException`（商品自身行为，不是端口回归） |
| 真实游戏浏览器实测（Playwright Firefox，可玩性） | `蹦球传说美化版_2959.jar`（Bounce Tales 修改版，自动演示画面）在真实页面中：rAF pump **60.1 次/秒**、6 秒内 9 个不同画面帧（30 次采样）、方向键/Enter 后画面改变且 pump 不停、打开工具面板后画面冻结（自动暂停）并在关闭后恢复、退出回库后刷新游戏仍在库中；同一页面运行 `audiodemo.jar` 3 秒，引擎日志出现 10 行音频事件（`wav=200000`、`amr=80000`、`adpcm=252500`、state=400，`http=unsupported` 符合无网络预期）；0 console/page error，证据 `.local-tools/debug-runs/web-playtest/` |
| 浏览器页面（Playwright Firefox + `tools/check_web.sh --browser`） | 26/26 检查通过（约 7.9 s）：静态服务 `build/web` → 真实引擎 `kind=wasm`、API 1、版本 0.5.7 → zh-CN 界面 → 导入 hello.jar（元数据/图标进 IndexedDB）→ 画布 240x320 非黑（4 色）→ 方向键/Enter（nj_key 15/16/17/6）帧号推进 → 面板自动暂停（帧冻结）+ 继续 → 存档槽 1 blob 329,724 字节、读档往返不重启 → 存档槽 1 导出为 `.njst` 下载（字节数与槽内一致、magic `NJSS`）、再经文件选择器导入槽 2 并生效 → 游戏面板内存选择 64 MiB 持久化并弹提示 → 模拟标准手柄（`navigator.getGamepads` + `gamepadconnected`）：A→nj_key 17、左摇杆→16、B→21，断开后全部释放 → 退出回库 → 刷新后游戏与存档仍在 → 缺 wasm 时降级 mock 并显示指向 `tools/build_web.sh` 的横幅；0 page/console error、0 外部请求；证据 `.local-tools/debug-runs/web-round8-clean/browser-0*-*.png` |
| 手柄（Gamepad API） | 游戏循环每帧轮询 `navigator.getGamepads()`（浏览器只给状态、不给事件），按边沿差分发 `njw_key`；标准映射 A=Fire(17)、B/X/Y/LB=GAME_A..D(21..24)、RB=Clear(20)、LT/Start=SOFT_LEFT(18)、RT/Back=SOFT_RIGHT(19)、十字键 12..15 与左摇杆（0.45 死区）→13..16；`gamepadconnected`/`gamepaddisconnected` 显示 toast 并在断开时 `releaseAll`；浏览器套件用假手柄验证按下/释放 |
| 存档导出/导入（`.njst`） | 存档槽按钮组含 导出/导入：导出把引擎快照原样下载为 `<标题>-slot<N>-<时间>.njst`；导入先 `njw_load_state` 校验（JSON 头记录 JAR 身份/ABI/屏幕尺寸，跨标题或损坏会被拒绝），成功后才写入 IndexedDB 槽位并立即应用；浏览器套件完整验证 导出字节数 == 槽内 size、magic `NJSS`、导入槽 2 生效 |
| PWA / 离线（service worker） | `manifest.json` 可解析（`display=standalone`，4 个图标含 maskable）→ `sw.js` 接管页面 → 缓存含 `js/nj2me.js` 与 `js/nj2me.wasm`（20 项）→ 库页脚显示“离线可用”（`#library-engine[data-offline=1]`、`body[data-offline=ready]`）→ **停掉静态服务器后重载**仍 boot shell 且 `kind=wasm` → 断网从库启动已导入的 `hello.jar`，240×320 画面非黑且帧号 13→38 继续推进；证据 `.local-tools/debug-runs/web-offline-badge/browser-09-offline.png`、`browser-10-offline-game.png` 与 `browser-shell-test.json`；注意 Playwright Firefox 的 `context.set_offline()` 对 service worker 不生效，离线用停服务器模拟 |

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
   默认关闭，不构成加速承诺。2026-11-15 新增 auto 校准：SDL2/Android 端口在首次
   `Graphics3D.render()` 时用 1024 三角/128×128 ROI 的合成批次分别计时软件与 GPU，
   只有 GPU 快 ≥15% 才启用（决定缓存在 VM 内并打印一行 INFO）。桌面实测选软件；
   API 36 模拟器（ES3 变体后端可用）实测也选软件：`gpu=414us software=183us`
   （m3gdemo）与 `gpu=859us software=247us`（MC），同步事务比软件光栅贵。
   纯软件路径不执行校准。
2. API 21 模拟器既无 `EXT_frag_depth` 也不是 GLES3，APK 内三角形批量不可用；
   API 36 模拟器与桌面真机 GPU 的 ES3/ES2 变体均已在 APK/测试程序内跑通，
   但真实手机（Adreno 725）仍只有离屏原生程序证据，尚未在手机 APK 内复测
   三角形路径。
3. Android 旧版（API 5）只有软件渲染；没有真机验证。
4. AAC 只在 Android 5+ 平台可用；其他平台遇到 AAC 会按“不支持的格式”拒绝。
5. B 帧（重排）视频容器被拒绝；Simple Profile 解码器不支持。
6. 视频无音轨且端口 uncapped 时，仍按 pump 步进（片头可能快于实时）。
7. journal 批处理 + 持久会话在 SDL2 上仍慢于软件（MC 约 2.7 ms/帧，
   ≈31%）；原因是每帧 3D/2D 交替会强制结束会话并同步回读，只有把 2D 也留在
   GPU 或直接从 GPU 纹理 present 才可能扭转。libretro 的硬件门模型会拒绝
   VM 线程的提交，因此该平台不启用批处理；Android 模拟器因缺
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

本轮产物（源码已提交，下列哈希对应当前提交）：

| 产物 | 大小 | SHA256 |
| --- | --- | --- |
| `.local-tools/debug-runs/gfx-full-20261113/android/final-build/nj2me.apk` | 1796322 | `e9d3e0476ac17c2a8150f3835d1bd7ce6f886ed72e5298ef896fe1964d2e42f7` |
| `.local-tools/debug-runs/gfx-full-20261113/windows/nj2me.exe` | 4045359 | `070f3578c6751e514b9d619a275999cdbb764422bfb1310d2ba8c1323eceab64` |
| `.local-tools/debug-runs/gfx-full-20261113/linux/libretro/nano_j2me_libretro.so` | 4083320 | `4a88f993258b01d59de55d78bfce2ed91c6ed55af2640e8b91d3937cf7523032` |
| `.local-tools/debug-runs/gfx-full-20261113/linux/nj2me` | 4019296 | `c46c21c315e9252b0f99e1844f34a8894813d27d8e1638bc21e3c0a2284712ec` |
| `.local-tools/debug-runs/gfx-full-20261113/android-legacy/nj2me-legacy.apk` | 694718 | `0352c5fe4764747d910b98bf6cdd430bdf537e113beca1f82d92d784247139d7` |
| `build/web/js/nj2me.js` | 93889 | `77652b2e6c8ef1a2e22e95eeec54e66530a1fe069728cf9a7bf6944ef424885f` |
| `build/web/js/nj2me.wasm` | 2144557 | `ddbbe3980c56d40a21c86de4dbf72ba163da0cd842e4588e73e5c71ebd78046b` |

以上产物均由同一份当前源码构建（含 2026-11-14 的 journal 批处理、持久
GPU 会话与 2026-11-15 的自动选择改动）。legacy APK 也已用恢复的旧工具链重建：
NDK r10e 解压到 `.local-tools/android-legacy/android-ndk-r10e`，API 5 平台包解压到
`.local-tools/android-legacy/sdk5/android-2.0_r01-linux/android.jar`（脚本内置回退路径），
`tools/build_android_legacy.sh` 直接可用。重建产物的静态检查：versionName
`0.5.7-legacy`、sdkVersion/targetSdkVersion 5、`lib/armeabi/libnj2me.so` 为 ARM
EABI5 v5TE、无 VFP 标记（软浮点）、不含加速校准字符串（`NJ_ENABLE_GFX_ACCEL=0`
按预期编译排除）。运行时复验目前不可行：本机已无 API 5/7/10 AVD 与旧系统镜像，
现代 emulator 36 已移除 32 位 ARM 支持（`CPU Architecture 'arm' is not supported
by the QEMU2 emulator`），旧版 emulator 29.2.1 可以下载但其 AVD 系统路径校验失败。
因此 legacy 的运行时证据仍是 v0.5.7 早期构建在 API 5/7/10 上的历史记录
（见 `.local-tools/release-v0.5.7-legacy/api5/`），新构建只做了构建期与静态验证。
源码已提交；Windows/Android/libretro 产物由同一份源码在提交前构建，
Web 产物也在提交前重新构建并重新跑过 `tools/check_web.sh --all` 与 `--sweep`。

## 证据

- Linux 构建与全量测试：`.local-tools/debug-runs/gfx-full-20261113/linux/`（作者构建含 fixtures 与 GLES2 加速注册项，197 项 197/197 通过；默认构建 190 项，190/190 通过）
- 持久会话后端测试（本机 GLES2 加速构建）：9 项全部通过，含 `test_gfx_triangles_gles2`（会话输出与无状态提交逐字节一致、重复 begin/end 被拒、abort 后 CPU 缓冲不变）、`test_m3g_triangles`（begin 拒绝时回退无状态路径并软件重放）、`test_sdl2_gles2`、`test_gfx_gles2`、`test_gfx_raster_gles2` 等
- 2026-11-15 事务开销复测（journal 描述符复用）：GPU 会话 7140/7162 ms，纯软件 5512/5477 ms
  （600 帧 MC，比值约 1.30×）；上限分析见 `docs/GLES2_3D.md`：CPU 像素权威契约下桌面 MC 只能接近 parity。
- 持久会话 A/B 与截图：`.local-tools/debug-runs/gfx-full-20261113/sdl-mc-journal/`（`fin-gpu*.log`、`fin-sw*.log`、`fin-session.png`）
- Android 最终 APK 安装/启动冒烟（API 36 模拟器）：`.local-tools/debug-runs/gfx-full-20261113/android/final-build/shot-api36-launch.png`。
  本轮未在 Android 上跑游戏：原 API 21 AVD 已被删除，API 36 镜像为 production 构建无法 root，
  不能推送测试 JAR；持久会话的运行时正确性由 SDL2 真实 MC 运行与后端会话等价测试覆盖。
- journal 批处理 A/B：`.local-tools/debug-runs/gfx-full-20261113/sdl-mc-journal/`（`replay.sh`、`shot-off.png`、`final-on.png`）
- Android journal 回放（模拟器无 `EXT_frag_depth`）：`.local-tools/debug-runs/gfx-full-20261113/android/final-build/shot-mc-journal4.png`、`shot-maze4.png`
- Android 媒体：`.local-tools/debug-runs/gfx-full-20261113/android-media-demo/`（截图、视频快照、图像解码日志）
- Android AAC/AMR/MP3 探针：`.local-tools/debug-runs/gfx-full-20261113/android-aac/`（`pcm-compare.log`）
- Android 最终 APK 与 GLES2/MC：`.local-tools/debug-runs/gfx-full-20261113/android/final-build/`
- libretro 真实 EGL：`.local-tools/debug-runs/gfx-full-20261113/libretro-live/`（`mc-current`、`bounce-current`、`REPORT.md`）
- Windows/Wine：`.local-tools/debug-runs/gfx-full-20261113/windows-audit/`（`frames/`、`sdl2-mc-window.png`、`sdl2-mc.log`、`win-gles2-maze.log`、`wine-wgl.log`）
- AMR/编解码报告：`.local-tools/debug-runs/gfx-full-20261113/amr/`、`.local-tools/debug-runs/gfx-full-20261113/codec-audit/`
- 网页版：`.local-tools/debug-runs/web-round11-all/`（存档导出/导入后的最新 `--all`：
  node 34/34、语料 47/47、浏览器 26/26、check-web.log、页面截图与 downloads/）、
  `.local-tools/debug-runs/web-round11-browser/`（加入导出/导入检查后的浏览器套件 26/26）、
  `.local-tools/debug-runs/web-round10-all/`（手柄支持后的 `--all`）、
  `.local-tools/debug-runs/web-round10-browser3/`（加入手柄检查后的浏览器套件 25/25）、
  `.local-tools/debug-runs/web-round8-clean/`（轮 8 干净 worktree 重建后的 `--all`）、
  `.local-tools/debug-runs/web-round8-sweep-clean/`（新自动化 403 标题审计：`sweep-403.jsonl` +
  `sweep-summary.txt`，392/8/1/1/1、117 s、RESULT PASS）、
  `.local-tools/debug-runs/web-clean-build/`（上一轮干净 worktree `--all`）、
  `.local-tools/debug-runs/web-final-fixes/`（修复后的 `--all` 运行）、
  `.local-tools/debug-runs/web-corpus-regression/`（加入午夜台球回归标题后的 47/47 语料运行）、
  `.local-tools/debug-runs/web-sweep/`（403 标题全语料审计：`sweep-403.jsonl` + `summary.txt`）、
  `.local-tools/debug-runs/web-lead-final-all/`（更早的 `--all` 运行）、
  `.local-tools/debug-runs/web-verify-<日期>/`（Node API/语料日志、PNG 帧）、
  `.local-tools/debug-runs/web-shell/`（外壳 mock 与真实 wasm 自测）和
  `.local-tools/debug-runs/web-manual-smoke{,2}/`（Lead 的 headless Firefox 冒烟 + 原生帧对照）和
  `.local-tools/debug-runs/web-playtest/`（Lead 的真实游戏可玩性实测：60.1 rAF/s、面板暂停、
  刷新持久化、AudioDemo 音频事件）；本记录的网页数字来自这些目录
