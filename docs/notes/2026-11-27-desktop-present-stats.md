# 桌面 present stats：把真实窗口呈现耗时变成可复现的内建数据

本轮（第 6 轮）不加新的放大算法，只补上“测量本身”这块短板。Android 端口早就有
`adb shell setprop debug.nj2me.present_stats 1`，每 120 帧把绘制与交换的
min/median/p95 打进 logcat；桌面上这些数字一直靠临时脚本自己计时
（`sdl-replay.c`、`probe5.c` 等），每次都要重写一遍，也没法让用户或 CI 复现。
现在 SDL2 端口有同样的开关和同样的两行格式。

## 一、用法

```sh
# 真实窗口，FSR1 呈现，8× 高清镜像放大到 960×1280 窗口
SDL_VIDEODRIVER=offscreen NJ_SDL2_GFX=gles2 NJ_GL_PRESENT=5 NJ_PRESENT_STATS=1 \
  ./build/nj2me --graphics gles2 --render-scale 8 --upscale fsr ...
# 日志（stderr），每 120 帧一组：
# present stats: mode=5 960x1280 frames=120 draw ms min=4.043 median=4.404 p95=5.082
# present stats: swap ms min=0.834 median=1.123 p95=1.374
# present stats: frame ms min=5.021 median=5.517 p95=6.628
```

- `NJ_PRESENT_STATS=1` 打开，未设置或为 `0` 时完全关闭（只在启动时读一次
  `getenv`，热路径上没有分支开销以外的成本）。
- 字段含义与 Android 一致：`draw` 是从“本帧开始上传/呈现”到“SDL 拷贝或着色器
  呈现完成”的时间；`swap` 是 `SDL_RenderPresent` 的时间；`frame` 是相邻两次
  `SDL_RenderPresent` 的间隔（第 9 轮加入，Android 端同时有同名行）。
  `frame − draw − swap` 就是游戏自身在这个窗口尺寸下的每帧生产耗时，
  因此不用再外接 SurfaceFlinger 或帧时间工具就能区分“呈现慢”和“游戏慢”。窗口按“请求的呈现
  模式 + 可绘制区尺寸”分组，模式或尺寸一变就重新开窗，不会把两种人群混进同一组
  分位数。
- `mode` 是请求的呈现模式：`0` = SDL 软件最近邻拷贝，`5` = FSR1 着色器呈现；
  着色器不可用时回落软件路径，`mode` 仍是请求值，方便对照。
- 实现：`src/port/posix/nj_port_sdl2.c` 的 `present_stats_probe()` /
  `present_stats_record()`，`sdl_display_frame()` 里包住呈现与交换两段，
  `NJ_SDL2_PRESENT_STATS_FRAMES` 与 Android 的 `NJ_ANDROID_PRESENT_STATS` 同为 120。

## 二、实测（RTX 5080 Laptop，Mesa/NVIDIA，offscreen SDL）

同一帧（`tests/games` 的零售 3D 农场第 1080 帧，`.local-tools/debug-runs/hd8-integration/desktop/farm-8x/dump-frames/01080.ppm`
等真实 4×/8× 高清镜像），每次 380 帧、取最后两组窗口（median / p95，毫秒）：

| 源（HD 镜像） | 窗口 | 模式 | draw | swap |
| --- | --- | --- | --- | --- |
| 8× 1920×2560（20 MB） | 960×1280 | 5 FSR1 | 4.22–4.41 / 4.49–6.25 | 1.09–1.12 / 1.17–1.79 |
| 8× 1920×2560 | 960×1280 | 0 最近邻 | 3.82–4.03 / 4.16–6.39 | 0.86–0.91 / 1.10–1.27 |
| 8× 1920×2560 | 551×751 | 5 FSR1 | 3.23–3.29 / 3.40–3.47 | 0.29 / 0.31–0.33 |
| 4× 960×1280（4.9 MB） | 960×1280 | 5（原生复制） | 0.86–0.88 / 0.97–1.21 | 1.09–1.11 / 1.27–1.52 |

读法与第 5 轮手工计时一致：8× 镜像每帧把 20 MB 推到 GPU，draw 约 4.3 ms；
换成 551×751 窗口只少约 1.0 ms，说明成本在“上传字节数”而不在输出尺寸；
`mode 5` 与 `mode 0` 差 0.2–0.4 ms，GPU 上的立方核重建与 RCAS 只占零头；
4× 镜像（4.9 MB）只有 0.87 ms，与 20 MB 的 4.3 ms 基本成比例（约 5 GB/s 有效
带宽）。结论与 [FSR1 锐度灵敏度与 4×/8× 呈现耗时](2026-11-27-fsr1-sharpness-and-present-cost.md)
相同：**4×–8× 呈现成本以上传为主，结构性解法是让 HD 镜像留在 GPU**，本轮
只是把测量工具固化下来，让后续每轮和用户都能用同一条命令复核。

## 三、验证

* 不设 `NJ_PRESENT_STATS` 时同一回放不输出任何 `present stats` 行；设置后每
  120 帧稳定输出两行。窗口按“模式 + 可绘制区尺寸”分组，尺寸或模式变化会重新
  开窗（本轮每次进程固定窗口尺寸，未在单进程内跨尺寸采样）。
* `mode 0`（不设 `NJ_GL_PRESENT`）与 `mode 5`（`NJ_GL_PRESENT=5`）两条路径都
  能给出数字，后者与第 5 轮手工计时（8× → 960×1280 median 4.268 ms）在
  0.1 ms 量级一致。
* 增强套件 236/236、软件套件 222/222，两套件按 `-j 8` **同时**运行（就是第 5 轮
  暴露 libretro 看门狗误杀的那个负载场景）全绿，日志
  `.local-tools/debug-runs/fsr-round6/{gpu,sw}-suite.log`；改动只涉及 SDL2 端口的
  计时与日志，不影响渲染结果（只读性能计数器，不改变执行路径）。
* 原始输出留档 `.local-tools/debug-runs/fsr-round6/stats-*.log`。

## 四、frame 行（第 9 轮补充）

只看 `draw`/`swap` 仍回答不了“这一帧到底慢在呈现还是慢在游戏”。第 9 轮让两端都输出
第三行按呈现间隔统计的 `frame ms`，并在 Android 上用它定位了高清 8× 的真实瓶颈：

* 桌面回放 8× → 960×1280：`draw` 4.2、`swap` 2.1、`frame` 12.1–12.4 ms，`frame ≈ draw + swap`
  （回放装置不跑游戏逻辑，生产耗时约 0），与手工计时吻合。
* API 36 模拟器运行《毁灭战士 3》、8× 高清、GLES2 面片 + FSR1 呈现：
  `draw` 中位 4.189、`swap` 8.355、`frame` 28.591 ms（窗口期 120 帧），
  即游戏自身生产约 16.0 ms/帧（约 35 fps），启动窗口曾出现 53.5/73.8/81.8 ms 的
  长帧。数字直接来自设备 logcat，无需额外工具，验证截图与日志见
  `.local-tools/debug-runs/fsr1-assets/` 与 `.local-tools/debug-runs/fsr-round9/`。
* Android 端实现见 `src/port/android/nj_android_gles2.c` 的 `present_stats_record()`
  （`last_present` 在禁用或模式/尺寸变化时清零，避免把旧窗口的间隔算进新窗口），
  桌面端见 `src/port/posix/nj_port_sdl2.c` 的同名函数。
