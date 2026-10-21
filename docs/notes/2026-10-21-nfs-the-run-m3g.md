# 极品飞车16：亡命狂飙 3D 世界“纯橄榄色”的排查（2026-10-21）

## 更正：不是“缺少纹理映射”

同日追加的证据推翻了早先的结论：JSR-184 的纹理采样在 `src/midp/nj_lib_m3g.c`
里已经实现，而且实际生效。

- 离树诊断构建（同为 HEAD 源码，只在 `jr_setup` 加 `NJ_M3G_LOG` 日志）打印
  `NJMG3D jr mesh_tex=256x256 bpp=4 dif=0xffffffff def=0x00000000 cull=0 dtest=1 dwrite=1 blend=64 tpos=1`：
  appearance 确实绑定了 256×256、4 字节/像素的贴图和纹理坐标，不是“无纹理”路径。
- 同一构建跑 NFS16 的选车界面，3D 预览台地面是清晰的贴图颗粒纹理
  （`.local-tools/debug-runs/nfsrun-gap-20261021/live1/06-wait2.png`，
  SHA-256 `94e94137555cced92a67ffae06dca293f7b301287840bf6fb48ffa70600d3f6c`）；
  主菜单背景也是贴图木纹（`live2/07.png`，
  SHA-256 `f8cb49ba7d9f719e627eae7544ad31c26403c618c4137e1ff4fd786e59943cba`）。
- 同引擎的 NFS14《热力追踪》比赛画面 3D 正常（天空/山脉/道路/车辆，
  `.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/09-tutorial-1.png`，
  SHA-256 `7218af3ebcb12b1cbe12b7c0007490fb33802d418dc1c9df2e4cb12298ac79a7`）。
  所以“软件光栅器没有纹理”对当前 HEAD 不成立，`62620a9` 的结论已撤回。

## 目前仍未解释的部分

- 比赛进入后（`live4/12.png`，SHA-256 `443a8e3b5ba2c09748f511c03881e45e8491693c1e40b3c526f2c14d15d139c3`；
  早前 `live2/12-race.png`，`17268461379735a00e17eb965897e04c6a4f8b2665fab29fdd974bf9c1960d7b`）
  仍是整片 `0x848263`。但同一次运行里 `NJMG3D world_drawn` 在比赛段有
  302…311 的样本（日志 200 个采样，其余多为 0…16 的菜单帧，结尾降回 0）——
  也就是说世界图元确实进了光栅器，却看不出内容。
- 同场 `NJMG3D fog on=0`：比赛帧没有 Fog，排除“雾把世界涂成雾色”。
- 还不能断言三角形画到了屏幕目标上：`Graphics3D` 的目标既可能是 MIDP 的
  `Graphics`，也可能是离屏 `Image`；后者在 `m3g_render_scene()` 里要先取
  `Image.getGraphics()`，路径不同（`src/midp/nj_lib_m3g.c:3526` 起）。
  用 `NJ_M3G_OFF` 关掉世界渲染做 A/B 时游戏在选车后提前退出（3D 参与后续
  流程），这条路本身不成立，需要改成“只记录目标类型/视口”的无副作用探针。
- 结论：`0x848263` 的来源、以及比赛帧三角形是否落在可见目标上，仍是开放问题；
  现有证据只支持“不是缺纹理、不是雾”。

## 已撤回的旧结论（同日早先，保留为排查史）

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

- 纹理采样已实现（见上“更正”）；比赛画面仍是纯色的原因未定，深度缓冲/背面剔除/光照的支持程度需另行核对。
- 未验证：完整一圈/冲线、存档、Android 真机；HUD 与操控只能说明游戏逻辑在跑。
- 该限制不通过改 2D 绘制或异常处理解决，属于 M3G 光栅器能力范围。
