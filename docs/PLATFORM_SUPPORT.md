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
| 浏览器页面（Playwright Firefox + `tools/check_web.sh --browser`） | 43/43 检查通过（约 23–39 s）：静态服务 `build/web` → 真实引擎 `kind=wasm`、API 1、版本 0.5.7 → zh-CN 界面 → 导入 hello.jar（元数据/图标进 IndexedDB）→ 画布 240x320 非黑（4 色）→ 方向键/Enter（nj_key 15/16/17/6）帧号推进 → 面板自动暂停（帧冻结）+ 继续 → 存档槽 1 blob 329,724 字节、读档往返不重启 → 存档槽 1 显示 96×128 PNG 缩略图（空槽隐藏）→ 存档槽 1 导出为 `.njst` 下载（字节数与槽内一致、magic `NJSS`）、再经文件选择器导入槽 2 并生效 → 存档命名（隔离上下文：保存槽 1 后把名称改为「Boss 战」，名称写入 IndexedDB 记录，导出文件名以 `boss-战-slot1-` 开头，刷新页面重新进入后输入框仍显示「Boss 战」） → 游戏面板内存选择 64 MiB 持久化并弹提示 → 面板「诊断」区数字刷新（堆 0.3/16.0 MiB、帧/绘制递增、GC 次数、字节码速率约 2.6 K/s） → 隔离上下文里给 hello.jar 把屏幕尺寸切到 176×220 后重启，画布实际变为 176×220（设置存在游戏记录里），再导入 game15.jar 启动仍是 240×320，重开 hello.jar 又回到 176×220 → 音量滑块设 40%（输出 40%、WebAudio gain 0.4、写入设置），静音后再拖动自动取消静音并对齐 gain → 模拟标准手柄（`navigator.getGamepads` + `gamepadconnected`）：A→nj_key 17、左摇杆→16、B→21，断开后全部释放 → 内存不足恢复（隔离上下文导入 愤怒的小鸟_383.jar，默认 16 MiB 启动得到 OOM 错误浮层，点「以 64 MB 重启」后引擎以 64 MiB 恢复并持续出画）→ 库收藏（隔离上下文导入 hello.jar + game15.jar，给 Hello World 点星标，点「只看收藏」后只剩该卡片，刷新后星标仍在、筛选恢复为全部，取消收藏后显示「还没有收藏的游戏」）→ 从链接导入（隔离上下文 + 第二个本地服务带 CORS：在对话框输入链接后下载 hello.jar、卡片出现并可启动，帧号推进；再输入 404 链接，错误只显示在对话框内）→ 自动读档（隔离上下文：暂停时保存槽 2，勾选「启动时自动读取最近存档」，退出并刷新页面后再启动，出现「已从存档槽 2 继续（第 N 帧）」提示并持续出画） → 退出自动保存（隔离上下文：同时勾选「退出时自动保存」+「启动时自动读取最近存档」，玩 60 帧后退出，游戏记录 `autoState.frames` > 10；刷新后再启动，出现「已从自动存档继续（第 N 帧）」提示并持续出画） → 自动存档管理（隔离上下文：勾选退出自动保存，玩 30 帧后退出，再次进入并打开面板：只读「自动存档」行显示时间与帧数，点「读取」出现已读取提示、点「清除」后行隐藏且游戏记录里 `autoState` 已删除） → 库卡片兼容标记（隔离上下文导入 hello.jar 与 愤怒的小鸟_383.jar：前者出现绿色「已验证」徽标，后者出现琥珀色「需更大内存」徽标且 tooltip 说明堆不足，点徽标后弹出同样内容的 toast 且仍停留在库页） → 继续上次游玩（隔离上下文：玩 30 帧后退出，库页顶部出现卡片且标题为「Hello World」；在搜索框输入文字后卡片隐藏、清空后恢复；点卡片可重新启动该标题） → 帧率上限（隔离上下文：默认 60 fps；打开面板后先恢复自动暂停，再分别切换 30/15 fps 并实测泵帧率约 30/15、切回「显示刷新率」后恢复逐刷新率出帧；再选 30 fps 后回库刷新并重开该标题，选择器与实测泵帧率都回到 30，游戏记录 `frameCap` 已写入） → 退出回库 → 刷新后游戏与存档仍在 → 缺 wasm 时降级 mock 并显示指向 `tools/build_web.sh` 的横幅 → 同一 context 两个标签页：第一个运行游戏时第二个启动被拒并弹「另一个标签页正在运行」提示，第一个退出后第二个可正常启动；0 page/console error、0 外部请求；证据 `.local-tools/debug-runs/web-final-all/check-web.log` |
| 线上演示（GitHub Pages） | 仓库 Pages 已启用（`build_type=workflow`），`.github/workflows/web-demo.yml` 全绿（build+smoke 1m12s、deploy 13 s）；https://lucinhu.github.io/nano_j2me/ 的 index/js/wasm/manifest/sw/data 均 HTTP 200；`tools/check_web.sh --live` 用 Python Playwright + headless Firefox 打开线上站点，导入 hello.jar 后画布 240x320 全绘制（76800 非黑像素）、帧号 74→134、0 console/page error，7/7 PASS（8 s）；证据 `.local-tools/debug-runs/web-live-all/`、`.local-tools/debug-runs/web-live-demo/` |
| 多 MIDlet 选择（启动 MIDlet） | 全语料中仅 book5/book6/book7 为多 MIDlet（book6/7 各 3 个：Dungeon/Checkers/Messenger，book5 2 个）；工具面板出现「启动 MIDlet」下拉，选择写入游戏记录并在下次启动生效；API 套件验证 `NJW_OPT_MIDLET_CLASS` 选择 Checkers 后帧缓冲哈希 `0x4a7f464c`（默认 Dungeon）→ `0x5639fd17` 且 60/60 帧出画；浏览器套件在隔离上下文导入 book6.jar 验证下拉标签顺序、切换后画面哈希变化、IndexedDB 中 `midletIndex=1`；证据 `.local-tools/debug-runs/web-midlet-api/`、`.local-tools/debug-runs/web-midlet-browser2/` |
| 自动读库存档（网页壳） | 面板勾选「启动时自动读取最近存档」后，启动游戏时 `_prepare()` 在引擎启动后、帧循环开始前调用 `njw_load_state` 读取候选里帧数最大的存档（三个手动槽 + 游戏记录上的退出自动存档）：手动槽弹「已从存档槽 N 继续（第 M 帧）」，自动存档弹「已从自动存档继续（第 M 帧）」并继续出画；失败（旧存档/ABI 不匹配等）弹警告并自动干净重启一次（失败的 load 会让 VM 不可用）。设置写 `nj2me.settings.autoResume`；证据 `.local-tools/debug-runs/web-resume-browser3/`、`.local-tools/debug-runs/web-resume-all/`、`.local-tools/debug-runs/web-autosave-all/` |\n| 帧率上限（网页壳） | 工具面板新增「帧率上限」（`#fps-select`）：默认 60，可选显示刷新率（不限制）/60/30/24/15 fps；选择立即生效，并像屏幕尺寸一样按游戏记忆（写 `game.frameCap`，外壳设置 `nj2me.settings.frameCap` 只是新游戏的默认值），刷新页面后仍保留。循环按 `requestAnimationFrame` 累计真实时间、每次按 `1000/fps` 补固定长度帧（每刷新最多 3 帧，积压封顶 3 帧），因此模拟时钟仍按真实时间前进，而 120 Hz 屏幕不会让 30 fps 游戏动画跑成双倍速；选「显示刷新率」回到逐刷新率出帧。浏览器套件验证默认值、30/15 fps 实测泵帧率、切回不限制，以及选 30 fps 后回库刷新再启动时选择器与泵帧率都恢复为 30；证据 `.local-tools/debug-runs/web-fpsgame-browser/` |
| 兼容性徽标（网页壳） | `tools/gen_web_compat_data.mjs` 把最近一次 `--sweep` 的 JSONL 转成提交进仓库的 `web/data/compat.json`（schema 1，按 `文件名|字节数` 建条目）；库卡片按精确匹配显示徽标：绿色「已验证」或琥珀色警告（需更大内存/前 90 帧黑屏/浏览器不支持/JAR 异常/加载超时），悬停显示原因；警告徽标是按钮，手机上点一下会用 toast 显示同样原因（无需悬停）；未知标题不显示。浏览器套件验证 hello.jar 绿标与 愤怒的小鸟 警告徽标及 tooltip，并点击徽标断言 toast 出现、仍停留在库页；证据 `.local-tools/debug-runs/web-badge-browser/`、`.local-tools/debug-runs/web-final-all/` |
| 存档槽命名（网页壳） | 每个手动存档槽多一个名称输入框（最多 24 字符）：名称与快照一起写进 IndexedDB 状态记录，刷新页面后仍保留；导出 `.njst` 时以名称为文件名基础（未命名回退到游戏标题）。浏览器套件验证记录持久化、导出文件名前缀与刷新后回填；证据 `.local-tools/debug-runs/web-slotname-browser/`、`.local-tools/debug-runs/web-slotname-all/` |
| 自动存档管理（网页壳） | 存档区新增只读行 `.slot--auto`：显示退出自动保存的时间与帧数、96×128 缩略图，可「读取」（与手动槽同样的失败契约：失败需重启）或「清除」（从游戏记录删除 `autoState`，下次退出会再写入）；未产生自动存档时行隐藏。浏览器套件验证行出现、时间/帧数、读取提示、清除后记录删除；证据 `.local-tools/debug-runs/web-autoslot-browser/`、`.local-tools/debug-runs/web-autoslot-all/` |
| 继续上次游玩卡片（网页壳） | 库页顶部的 `#continue-card` 显示 `lastPlayedAt` 最新的游戏（标题 + 自动存档缩略图/帧数；无自动存档时显示「上次游玩的游戏」），点击直接 `onPlay` 启动；仅在无搜索、无收藏筛选且有已玩游戏时出现。浏览器套件在隔离上下文验证卡片出现、标题、搜索时隐藏、清空后恢复、点击可启动；证据 `.local-tools/debug-runs/web-continue-browser/`、`.local-tools/debug-runs/web-continue-all/` |
| 退出自动保存（网页壳） | 面板勾选「退出时自动保存」后，在游戏运行中退出（回库按钮）时 `close()` 会在 `runtime.stop()` 之前调用 `canSave()`/`saveState()`，把状态和 96×128 缩略图写进游戏记录的 `autoState` 字段（不动三个手动槽）；MIDlet 自己退出时 VM 已结束、`canSave()` 为 false，自然跳过。失败只警告不阻塞退出；证据 `.local-tools/debug-runs/web-autosave-browser/`、`.local-tools/debug-runs/web-autosave-all/` |
| 多标签页单引擎（BroadcastChannel 租约） | 同一浏览器 context 两个页面：第一个运行游戏时第二个点游戏被拒并弹「另一个标签页正在运行游戏」，仍停在库页；第一个退出后第二个可正常启动出画面；浏览器套件覆盖（36/36）；证据 `.local-tools/debug-runs/web-tabs-all/` |
| 诊断面板（`njw_stats`） | 工具面板「诊断」区读取 `njw_stats()`（与原生 `--stats` 同源）：Java 堆用量/上限、帧/绘制计数、GC 次数、字节码速率（低于 1 M/s 时用 K/s 显示），仅在面板打开时以 2.5 Hz 刷新；API 套件验证 13 个计数器的完整性与范围（堆用量 0.3 MB/16 MB、60 帧、绘制 60、字节码 2986、GC 0），浏览器套件验证面板数字在真实运行中递增；证据 `.local-tools/debug-runs/web-diag-api/`、`.local-tools/debug-runs/web-diag-browser2/` |
| 手柄（Gamepad API） | 游戏循环每帧轮询 `navigator.getGamepads()`（浏览器只给状态、不给事件），按边沿差分发 `njw_key`；标准映射 A=Fire(17)、B/X/Y/LB=GAME_A..D(21..24)、RB=Clear(20)、LT/Start=SOFT_LEFT(18)、RT/Back=SOFT_RIGHT(19)、十字键 12..15 与左摇杆（0.45 死区）→13..16；`gamepadconnected`/`gamepaddisconnected` 显示 toast 并在断开时 `releaseAll`；浏览器套件用假手柄验证按下/释放 |
| 存档导出/导入（`.njst`） | 存档槽按钮组含 导出/导入：导出把引擎快照原样下载为 `<标题>-slot<N>-<时间>.njst`；导入先 `njw_load_state` 校验（JSON 头记录 JAR 身份/ABI/屏幕尺寸，跨标题或损坏会被拒绝），成功后才写入 IndexedDB 槽位并立即应用；浏览器套件完整验证 导出字节数 == 槽内 size、magic `NJSS`、导入槽 2 生效 |
| 存档缩略图（IndexedDB） | 本地保存时把当前画面缩成 96×128 PNG 存进槽记录（`.slot__thumb`，48×64 CSS 显示）；空槽和 `.njst` 导入的槽不显示预览，重新保存后出现。浏览器套件断言已存槽的 `<img>` 为 `data:image/png` 且解码为 96×128、空槽隐藏；证据 `.local-tools/debug-runs/web-thumb-browser2/`、`web-thumb-all/` |
| 内存不足一键重启（网页壳） | 错误浮层识别到 `OutOfMemoryError`（来自 `njw_error()` 追加的最后一条 Java 异常）时出现「以 64 MB 重启」按钮（当前 16/32 → 64、64 → 128，已是 128 则隐藏）；点击后把 `settings.heapMb` 写入 `nj2me.settings` 并立即重启引擎。浏览器套件在隔离上下文导入 `愤怒的小鸟_383.jar`、以默认 16 MiB 启动得到 OOM 浮层，点击后引擎以 64 MiB 恢复并持续出画；证据 `.local-tools/debug-runs/web-oom-browser/`、`.local-tools/debug-runs/web-oom-all/` |
| 按游戏记忆屏幕尺寸（网页壳） | 屏幕尺寸预设从外壳全局设置改为**游戏记录字段**（`screenPreset`）：面板里切换后只在当前游戏下次启动生效，「按游戏记忆」提示写在选择框下方；未设置的游戏回退引擎默认 240×320，选回「自动」会删除该字段。浏览器套件隔离上下文验证：hello.jar 设 176×220 重启后画布 176×220、记录里 `screenPreset=176×220`，再启动 game15.jar 仍 240×320，重开 hello.jar 又是 176×220；证据 `.local-tools/debug-runs/web-perscreen-browser/`、`.local-tools/debug-runs/web-perscreen-all/` |
| 从链接导入 JAR（网页壳） | 工具栏「从链接」按钮打开对话框，输入 http(s) 链接后流式下载（带进度、上限 64 MiB），成功走既有导入管线（探测 + IndexedDB + 引擎 FS），失败（HTTP 错误、非 JAR、CORS 被拦）只显示在对话框内并给出 CORS 提示；文件名取链接最后一段（缺 `.jar` 时补）。浏览器套件用第二个本地 CORS 服务验证成功下载+启动与 404 报错两条路径；证据 `.local-tools/debug-runs/web-url-browser3/`、`.local-tools/debug-runs/web-url-all/` |
| 库收藏与筛选（网页壳） | 游戏卡片带星标按钮，收藏状态写入游戏记录（IndexedDB 的 `favourite` 字段，跨刷新保留）；工具栏「只看收藏」按钮用 `aria-pressed` 表示状态，即时过滤并显示「找到 n / m」计数；无收藏时显示专门的空状态。浏览器套件在隔离上下文导入两个 JAR 验证星标、筛选、刷新持久与取消后的空状态；证据 `.local-tools/debug-runs/web-fav-browser/`、`.local-tools/debug-runs/web-fav-all/` |
| 性能快速回归（`--perf`） | 4 个轻/中量标题各预热 60 帧 + 实测 600 帧（固定 1000/60 ms 步长）：game15 **6533 fps**/91.8 ms、bubblet **3880 fps**/154.6 ms、slimevolley **7909 fps**/75.9 ms、1942空战_10403 **7892 fps**/76.0 ms，均 drew 600/600；堆 64→64 MiB、无引擎错误；`tools/check_web.sh --perf` 9/9 检查通过（0.5 s），下限取参考机约 1/10（抓数量级回退、不惩罚慢 CI），实测写入 `perf-summary.{txt,json}` |
| PWA / 离线（service worker） | `manifest.json` 可解析（`display=standalone`，4 个图标含 maskable）→ `sw.js` 接管页面 → 缓存含 `js/nj2me.js` 与 `js/nj2me.wasm`（20 项）→ 库页脚显示“离线可用”（`#library-engine[data-offline=1]`、`body[data-offline=ready]`）→ **停掉静态服务器后重载**仍 boot shell 且 `kind=wasm` → 断网从库启动已导入的 `hello.jar`，240×320 画面非黑且帧号 13→38 继续推进；证据 `.local-tools/debug-runs/web-offline-badge/browser-09-offline.png`、`browser-10-offline-game.png` 与 `browser-shell-test.json`；注意 Playwright Firefox 的 `context.set_offline()` 对 service worker 不生效，离线用停服务器模拟 |

性能（600 帧空跑，两边都是 Release；原生用 `--fps 0` 即 uncapped）：

| 游戏 | wasm | 原生 x86_64 | 比值 |
| --- | --- | --- | --- |
| game15.jar | ≈6,533 fps | ≈16,216 fps | 2.5× |
| bubblet.jar | ≈3,880 fps | ≈7,692 fps | 2.0× |
| slimevolley.jar | ≈7,909 fps | ≈20,690 fps | 2.6× |
| 1942空战_10403.jar | ≈7,892 fps | ≈12,500 fps | 1.6× |

上表由 `tools/web_test/web_perf_test.mjs` 可复跑（`tools/check_web.sh --perf`，
写入证据目录的 `perf-summary.{txt,json}`）。这是余量不是帧率：游戏按虚拟时钟计时，
60 Hz 只需要每秒 60 次 pump。首次加载
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
   >512 MiB 堆未验证（同源多标签页已由 `BroadcastChannel` 单引擎租约协调）。
10. wasm 目标不能用 `-DNJ_WERROR=ON` 构建：clang 把现有头文件里重复的
   `typedef` 当作 C11 扩展告警，而该开关会把警告变成错误。

## 复现

```sh
# Linux 桌面 + 测试
cmake -S . -B build -DNJ_ENABLE_MEDIA=ON -DNJ_ENABLE_VIDEO=ON
cmake --build build -j6
ctest --test-dir build -j4

# Android 5+（现代，含 GLES2 实验后端）。arm64-v8a 必须列进去：只在
# 64 位设备上装 armeabi-v7a 会让进程跑 32 位解释器（寄存器少、生成代码差），
# 实测在真机上明显更慢；三个 ABI 一起打，Android 会自动选 64 位。
tools/build_android_apk.sh --out build/android --abi "arm64-v8a armeabi-v7a x86_64" --gles2
# 真机无头基准（32 位 vs 64 位解释器 2.29×）见 docs/notes/2026-11-15-android-arm64-slice.md

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
| `build/web/js/nj2me.js` | 93928 | `c6f2de60e08048d5519e4d98a98ecdb1801fec60e4f4e098ad5011c843aa7bf8` |
| `build/web/js/nj2me.wasm` | 2145920 | `cd049b3f4327619ce890d4519cc6cf440567051d38e080df2ce04f36a7d31b82` |

以上产物均由同一份当前源码构建（含 2026-11-14 的 journal 批处理、持久
GPU 会话与 2026-11-15 的自动选择改动）；Web 产物从当前提交的干净 worktree
重建（`git worktree add --detach` + `tools/build_web.sh`，两次构建字节一致），
与 GitHub Pages 线上部署的 `nj2me.wasm` 大小相同。legacy APK 也已用恢复的旧工具链重建：
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
- 网页版：`.local-tools/debug-runs/web-final-all/`（20 轮收尾最终 `--all`：node 36/36、语料 47/47、浏览器 42/42、48 s；帧率上限上线后浏览器套件 43/43 见 `.local-tools/debug-runs/web-fps-browser3/`）、
  `.local-tools/debug-runs/web-final-perf-smoke/`（最终 `--perf` 9/9 与 `--smoke` 7/7：game15 4689 fps、bubblet 2861、slimevolley 5581、1942空战 5520）、
  `.local-tools/debug-runs/web-head-all/`（当前 HEAD 干净构建产物的 `--all`：node/corpus/browser 全 PASS、58 s）、
  GitHub Actions `Web demo (GitHub Pages)` 已验证全绿（build+smoke、deploy、verify 三个 job 均 success、零 annotation：run 43663734822 为 upload-artifact v7 后的最新运行，构建 34 s/部署 10 s/线上验证 40 s；此前 43661880888、43662608035、43662849158、43663535473 亦全绿）、
  `.local-tools/debug-runs/web-live-all/`（`--live` 线上演示 7/7、8 s：站点 200、wasm 引擎启动、hello.jar 导入、画布 240x320、帧号 74→134）、
  `.local-tools/debug-runs/web-live-demo/`（线上演示首次手工验证：`live-check.json` + `live-game.png`）、
  `.local-tools/debug-runs/web-final-sweep/`（最终 403 标题全语料审计：392 ok / 8 black / 1 fatal / 1 start / 1 probe、0 timeout、117 s，`sweep-403.jsonl` + `sweep-summary.txt`）、
  `.local-tools/debug-runs/web-round11-all/`（存档导出/导入后的 `--all`：
  node 36/36、语料 47/47、浏览器 32/32、check-web.log、页面截图与 downloads/）、
  `.local-tools/debug-runs/web-badge-browser/`（可点击警告徽标浏览器套件 42/42、38 s）、
  `.local-tools/debug-runs/web-slotname-all/`（存档槽命名后的 `--all`：node 36/36、语料 47/47、浏览器 41/41、48 s）、
  `.local-tools/debug-runs/web-slotname-browser/`（存档槽命名浏览器套件 41/41、38 s）、
  `.local-tools/debug-runs/web-autoslot-all/`（自动存档管理后的最新 `--all`：node 36/36、语料 47/47、浏览器 40/40、44 s）、
  `.local-tools/debug-runs/web-autoslot-browser/`（自动存档管理浏览器套件 40/40、31 s）、
  `.local-tools/debug-runs/web-continue-all/`（继续上次游玩后的 `--all`：node 36/36、语料 47/47、浏览器 39/39、41 s）、
  `.local-tools/debug-runs/web-continue-browser/`（继续上次游玩浏览器套件 39/39、28 s）、
  `.local-tools/debug-runs/web-autosave-all/`（退出自动保存后的 `--all`：node 36/36、语料 47/47、浏览器 38/38、39 s）、
  `.local-tools/debug-runs/web-autosave-browser/`（退出自动保存浏览器套件 38/38、27 s）、
  `.local-tools/debug-runs/web-resume-all/`（自动读档后的 `--all`：node 36/36、语料 47/47、浏览器 37/37、31 s）、
  `.local-tools/debug-runs/web-resume-browser3/`（自动读档浏览器套件 37/37、26 s）、
  `.local-tools/debug-runs/web-perscreen-all/`（按游戏记忆屏幕尺寸后的 `--all`：node 36/36、语料 47/47、浏览器 36/36、28 s）、
  `.local-tools/debug-runs/web-perscreen-browser/`（屏幕尺寸按游戏记忆的浏览器套件 36/36、18.5 s）、
  `.local-tools/debug-runs/web-url-all/`（从链接导入后的 `--all`：node 36/36、语料 47/47、浏览器 36/36、26 s）、
  `.local-tools/debug-runs/web-url-browser3/`（加入从链接导入检查后的浏览器套件 36/36、17 s）、
  `.local-tools/debug-runs/web-fav-all/`（库收藏后的 `--all`：node 36/36、语料 47/47、浏览器 34/34、27 s）、
  `.local-tools/debug-runs/web-oom-all/`（内存不足一键重启后的 `--all`：node 36/36、语料 47/47、浏览器 33/33、25 s）、
  `.local-tools/debug-runs/web-oom-browser/`（加入 OOM 恢复检查后的浏览器套件 33/33、16 s）、
  `.local-tools/debug-runs/web-diag-all/`（加入诊断面板检查后的全套件 36/36 + 47/47 + 31/31、23 s）、
  `.local-tools/debug-runs/web-thumb-browser2/` 与 `web-thumb-all/`（存档缩略图检查：槽 1 显示 96×128 PNG、空槽隐藏；全套件 36/36 + 47/47 + 32/32、23 s）、
  `.local-tools/debug-runs/web-screen-all/`（加入屏幕尺寸预设检查后的全套件 30/30、21 s）、
  `.local-tools/debug-runs/web-round11-browser/`（加入导出/导入检查后的浏览器套件 26/26）、`.local-tools/debug-runs/web-midlet-all/`（加入多 MIDlet 检查后 node 35/35、浏览器 27/27）、`.local-tools/debug-runs/web-volume-all/`（加入音量检查后浏览器 28/28）、`.local-tools/debug-runs/web-tabs-browser/`（加入多标签页检查后浏览器 29/29）与 `.local-tools/debug-runs/web-tabs-all/`（全套件 29/29、22 s）、
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
