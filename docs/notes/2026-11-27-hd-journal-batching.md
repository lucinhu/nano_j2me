# 高清面片批处理：把一帧里的多次 3D 绘制合并成一个驻留会话

Android 与桌面上 8× 高清原生面片渲染是当前最慢的一档：桌面 `--graphics vulkan`
跑确定性的 Doom3 场景，8× 为 26.8 s / 240 帧（9.0 fps），2× 只要 13.4 s；
Android 模拟器 8× 时一帧 `frame` 28.6 ms 里 `draw` 4.2 ms、`swap` 8.4 ms，
剩下约 16.0 ms 是游戏自己的帧成本。软件与 GLES2 后端同样在 8× 掉速，
说明瓶颈不在某个后端的滤镜，而在高清面片通路本身。

## 一、问题：每个 M3G 绘制批次都整块进出 CPU 内存

`src/port/vulkan/tri_draw.inc` 与 `src/port/gles2/nj_gfx_triangles_gles2.inc`
的高清路径都是「每批次一次往返」：

1. 把该批次 ROI 的颜色从 CPU 镜像上载到 GPU 图像；
2. 需要深度测试/写入时，再把深度键值（`nj_s4`）换算成浮点后上载；
3. 绘制；
4. 把颜色读回 CPU 镜像；写过深度时还要把深度读回并换算回键值。

1920×2560 的一整屏是 19.6 MB/缓冲。Doom3 每帧约 3.2 个 M3G 三维批次，
颜色与深度各进出一次，单这一项就接近 500 MB/帧；另外
`bench_fsr2`/`conv_bench` 实测 1920×2560 的深度键值↔浮点换算本身
（import 3.37 ms + export 3.59 ms）就有约 7 ms/批次，
与 8× 比 2× 多出的约 49 ms/帧吻合。

## 二、做法：让高清批次也进日志，按会话一次提交

日志（`src/midp/nj_lib_m3g.c` 的 `m3g_journal_*`）本来就是为「多个批次一次提交」
设计的，但入口条件把它限制在 1×：

```c
journaled = !r->hd_scale && port->gfx_triangles_list != NULL;
```

本次实验把它放宽到「端口同时提供 list 与 begin/end（驻留会话）」的高清路径，
并把三处会破坏批处理的刷新让开：`jr_draw` 里进高清网格前的 `ORDER` 刷新、
高清网格之后的 `SOFTWARE` 刷新（伴随的软件走位写的是逻辑缓冲，不是镜像），
以及高清网格回退到软件绘制时先提交已在队列里的高清批次，保证顺序。
为便于同版本 A/B，实验分支带 `NJ_M3G_NO_HD_JOURNAL=1` 开关。

## 三、结果：4× 有小幅收益，8× 反而更慢，因此不启用

确定性 Doom3 场景（240 帧、同一串按键、软件路径 240 帧统计完全一致），
SDL2 GLES2 后端、RTX 5080、`--port sdl2`：

| 渲染倍率 | 日志 | 批处理 | 帧时间 | fps | 会话拒绝 | 批次提交 |
| --- | --- | --- | --- | --- | --- | --- |
| 8× | 关闭 | 关闭 | 49.81 s | 4.8 | 234 | 5044 |
| 8× | 打开 | **57.49 s** | 4.2 | 600 | 5116 |
| 4× | 关闭 | 关闭 | 22.24 s | 10.8 | 1 | 5254 |
| 4× | 打开 | **20.39 s** | 11.8 | 100 | 5453 |

- 4×：批处理省下 8.3% 的帧时间。
- 8×：批处理多花 15.4%，而且 GLES2 会话拒绝次数从 234 涨到 600。

原因是 GLES2 的驻留会话粒度是整个并集矩形：开始时要把该矩形的颜色与深度
从 CPU 上载一次，结束时再把该矩形读回一次。8× 时多个批次的并集接近整屏
1920×2560，读回量约 40 MB；而逐批次路径只碰各自的 ROI，读回量反而更小。
会话拒绝后又走「回放 + 逐批次」的兜底，等于把两种开销都付了。4× 时并集
小得多，批处理才开始赚钱。

## 四、结论与下一步

- 高清批处理**不启用**，`journaled` 恢复为只服务 1× 路径，代码保持原样。
- 8× 三维要真正提速，必须避免「颜色/深度整块进 CPU」这个结构：
  让镜像常驻 GPU（呈现也从 GPU 读）、或至少让深度在多次绘制之间常驻，
  只在帧末同步一次。这和 FSR2 的捕获路径、呈现路径都要一起改，
  属于架构级工作，不能在面片回调里局部解决。
- 桌面与 Android 的 8× 数据已经记录在
  `docs/notes/2026-11-27-android-vulkan-fsr2.md`（`frame` 行归因）和本文，
  后续做架构改造时用它判断收益上限。

## 五、复现

```sh
ninja -C .local-tools/debug-runs/fsr-quality-gl/build -j 8
SDL_VIDEODRIVER=offscreen NJ_SDL2_GFX=gles2 \
__NV_PRIME_RENDER_OFFLOAD=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
./.local-tools/debug-runs/fsr-quality-gl/build/nj2me \
  --graphics gles2 --port sdl2 --render-scale 8 --ssaa 1 \
  --asset-filter pixel-art --size 240x320 --heap 16 --no-heap-grow \
  --fps 1000 --run-frames 240 --stats \
  --keys '200:down:down,205:down:up,215:fire:down,216:fire:up' \
  tests/games/毁灭战士3_10598.jar
```

实验分支的对照是 `NJ_M3G_NO_HD_JOURNAL=1`；该开关与放宽后的入口条件
一起回退了，仓库里没有留下死代码。
