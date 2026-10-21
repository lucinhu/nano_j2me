# 极品飞车16：亡命狂飙 3D 世界“纯橄榄色”的机制定位（2026-10-21）

## 结论

比赛已经真的跑起来：HUD、名次、对手距离、小地图、速度表和左右转向都工作，
但 3D 世界只剩一整片 `RGB(132,130,99) ≈ 0x848263` 的橄榄色。它不是“3D 没画”，
而是 **JSR-184 软件光栅器缺少纹理映射**：赛道、山地、车身都贴图，运行时把贴图
丢掉后每个三角形退化为材质漫反射色，整屏就成了单一颜色。

这条是能力边界（feature gap），不是一行 bug：要还原画面需要给
`src/midp/nj_lib_m3g.c` 的光栅器加纹理采样（以及深度缓冲/背面剔除才谈得上正确遮挡）。

## 证据

- 原始 JAR `/home/lucinhu/Downloads/jar/极品飞车16：亡命狂飙.jar`（SHA-256
  `92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5`）
  真机运行证据见 [下载语料清单](2026-10-17-download-corpus.md) jar-26 行：
  HUD 39→133 MPH、名次 P1/P3/P2、`JACK 66M→809M`。
- JAR 内含完整 `.m3g` 资源：`city.m3g` 318,579 B、`mountain.m3g` 330,308 B、
  `fx.m3g` 201,647 B，另有 `texture_tracktex_*.m3g`、`car_*.m3g`、
  `animrig_*.m3g` 等——3D 世界走的是 M3G，不是 2D 伪 3D。
- 为确认渲染是否发生，做了 out-of-tree 诊断构建（`git archive HEAD` 解出后只加
  `NJ_M3G_LOG` 日志，不改主仓）：
  - 每帧都能看到 `NJMG3D render=… root=javax/microedition/m3g/World meshes=7 bg=0x00000000 cam=yes`；
  - `world_drawn=313…323`，即每帧确实光栅化约 300+ 个三角形；
  - `--stats` 里 `m3g=248737` 次调用、`tris=73038`，与日志一致。
  所以橄榄色来自这些三角形本身的颜色，而不是背景清屏色（World 背景色是 0，
  且 `_nj_cclear` 未开启；截图里也没有天空/地面的分界）。
- `src/midp/nj_lib_m3g.c` 的光栅器注释（`m3g_render_jr` 上方）写明当前实现：
  完整模型/相机/投影/视口变换、三角形列表与带、材质漫反射色或顶点色，
  **但还没有**深度缓冲（三角形按提交顺序覆盖）、背面剔除、纹理、光照。
  贴图丢失后 `fillTriangle` 用材质的漫反射色填充，正是 0x848263 这种整片底色。

## 同场发现的另一个异常（与此无关）

- 每帧 2D 层 `ad.a(Graphics,IIIIIIZ)` 的绘制游标 `f` 越过固定 8 个槽位时会抛
  `ArrayIndexOutOfBoundsException`（`ad.e=[Lh;` 由 `ad.d(int)` 建 8 个对象，
  `ad.b(int)` 每轮把 `f` 归零；`ad.a` 以 `e[f++]` 发射精灵）。
- 用离树 `NJ_THROW_STACK` 构建抓到完整栈（比赛起跑附近共 4 次，之后不再出现）：
  `ad.a(...) pc=2672` ← `f.a(Graphics,IIIII) pc=52` ← `f.a(Graphics,IIIIZ) pc=76`
  ← `an.a(Graphics) pc=41` ← `af.paint(Graphics) pc=305`。
  `af.paint` 对这条调用没有 try/catch，异常会打断那一帧的 2D 层与 HUD，
  但下一帧恢复；它与 3D 世界缺失没有因果关系（M3G 渲染独立进行）。

## 复现

```sh
cd /home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-round20-20261021
python3 drive.py --jar "/home/lucinhu/Downloads/jar/极品飞车16：亡命狂飙.jar" \
  --size 240x320 --frames 6000 --out nfsrun-live --plan "$(cat plan-nfs.json)"
```

- 起跑前节奏：关音效 → 序章剧情 → NEW GAME → 选车 → 起跑；确认键用回车，
  转向/加速用方向键（长按 Up 可到 133 MPH）。
- 本轮根因追查的离树构建与日志在
  `.local-tools/debug-runs/nfsrun-gap-20261021/`（`src/` 为 `git archive` 副本，
  `run1/`、`run2/` 为带 `NJ_THROW_STACK` / `NJ_M3G_LOG` 的两次运行）。

## 边界

- 未实现：纹理映射、深度缓冲、背面剔除、光照；因此赛道/车辆/山体都无法按原样显示。
- 未验证：完整一圈/冲线、存档、Android 真机；HUD 与操控只能说明游戏逻辑在跑。
- 该限制不通过改 2D 绘制或异常处理解决，属于 M3G 光栅器能力范围。
