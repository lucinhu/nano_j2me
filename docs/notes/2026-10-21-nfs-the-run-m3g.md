# 极品飞车16：亡命狂飙 3D 世界“纯橄榄色”的排查（2026-10-21）

**后续（同日）：根因已找到并修复。** 不是合成，也不是纹理：比赛里游戏同时用了
`Graphics3D.setCamera(H, I)`（`I` 是菜单遗留的变换）和 `World.setActiveCamera()`，
旧实现让前者优先，相机因此停在菜单位姿，世界三角形 95% 以上落在视锥外。修复还
包括 `Transform.setTransform(null)` 与 `Node.getTransformTo()` 的 JSR 184 语义。
见 `docs/notes/2026-10-21-m3g-camera-semantics.md`；本文保留排查过程与已撤回的
中间结论，末尾“下一步”一节已被该文取代。

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
  （该问在下一节由表面转储回答了：世界确实被渲染出来，丢在合成阶段。）

## 第 30 轮：世界确实画出来了，丢在“渲染结果 → 屏幕”之间

1. **先排除像素格式降级**。桌面默认用 16 位 `NJ_PIXFMT_RGB565`，而 3D 软件
   光栅器只写 32 位，按 `src/midp/nj_lib_m3g.c` 的注释在 565 目标上会退回 Java
   平色发射。于是离树重建 `-DNJ_PIXFMT=NJ_PIXFMT_ARGB8888`（同 HEAD 源码）：

   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release          -DNJ_PIXFMT=NJ_PIXFMT_ARGB8888 -DNJ_BUILD_TESTS=OFF
   ```

   跑到比赛仍是一片橄榄色，像素分别 `RGB(135,128,100)`（ARGB8888）与
   `RGB(132,130,99)`（RGB565）——只差 565 量化误差，所以**不是**“32 位光栅器
   在 16 位屏上退化成平色”这条路径。
2. **直接转储光栅器写进去的表面**。在离树构建里给“每帧子网格绘制”加
   `NJ_M3G_DUMP` 探针，把 `r.sw.surf` 的像素写成 PPM：
   - 展厅/选车帧：`run4/dump-0.png`（240×320，
     SHA-256 `efb7daf1880355adfb4f2b4dcc7a51ff9955bc0f9c229dd13fc38c0dd266a3be`）
     是贴图完整、有明暗的 3D 展台与跑车。
   - 比赛帧（启动 100 s 后、每 90 次绘制转一帧）：
     `run10/dump-1.png`（240×320，
     SHA-256 `5e11d3afe92dc01344d786c01656f0f530c9fadd4df82dd54ceda2419973a7b3`）
     是**正确的比赛世界**：天空、海湾、金门大桥、城市楼群、道路和护栏都在。
   - 同一时段的窗口截图 `run10/09.png`（960×1280，
     SHA-256 `e27a892b0c3dbe249ec98360d0e70924c024c4bb5830c4b4287f8ee30b1cabb8`）
     却仍是橄榄底 + HUD（36 MPH、名次、地图）。
3. **结论（本轮收窄）**：JSR-184 渲染与纹理采样没有丢内容，比赛世界被正确
   写进 240×320 的着色表面（从地址与尺寸看是游戏自己的离屏 `Image`）；丢失
   发生在其后的合成：游戏把这张图 `drawImage`/`drawRegion` 到屏幕，或者
   之后又用 2D 图层把它盖住。下一步应追踪比赛帧里对这张图的
   `drawImage`/`drawRegion` 调用（目标、锚点、transform、是否被覆盖），
   而不是继续在光栅器里找。
   实验记录：`.local-tools/debug-runs/nfs-pixfmt-20261021/`（`run4`、`run10`）。

## 第 31 轮：M3G 画的就是 Canvas 后备缓冲，但上屏时被清屏帧覆盖

第 30 轮把范围收到“渲染结果 → 屏幕”。这一轮给同一比赛帧的不同阶段加探针
（仍在离树构建，主仓未改），把范围再收一步：

1. **M3G 的目标就是当前 Canvas 的后备缓冲**。在 `jr_setup()` 取到表面后打印
   Graphics 的 `gTarget` 与当前 Displayable 的 `cBuffer`：
   `NJTG img=0x72425de0 surf=240x320 fmt=2 is_screen=0 cur=0x723ffad0
   cbuf=0x72425de0 is_cbuf=1`，570 个采样全部 `is_cbuf=1`。
   因此“世界画到离屏 Image、没人合成出来”这条不成立：世界直接画进了
   `nj_midp_present()` 要上屏的那张缓冲。
2. **上屏时刻缓冲里没有世界**。在 `nj_midp_present()` 镜像前同时转储
   `cBuffer` 与屏幕（`NJPRESENT_DUMP`，启动 95 s 后每 40 帧采样）：
   run14 的 12 对 `pbus`/`pdst` 都是 44 色，`(120,40)` 白（HUD）、
   `(120,200)` 橄榄 `RGB(135,128,100)`——即 present 取到的就是
   “橄榄底 + HUD”，而不是第 30 轮中途转储到的完整世界。
3. **清屏是游戏自己做的**。给 `fillRect` 加 `NJ_FILL_TRACE`（只记 ≥200 像素的
   边），比赛窗口里能看到整屏填充：`cBuffer` 上 `0,0 240x320` 的
   `ff000000`（黑，344 次）与 `ff878064`（橄榄，167 次）。
   所以橄榄色不是我们合成的默认色，而是游戏在同一条缓冲上的一次全屏清屏。
4. **结论（本轮收窄）**：M3G 渲染正确、目标正确、present 也照常上屏；缺的是
   帧的时序——同一帧里游戏先/后又用整屏清屏（黑/橄榄）覆盖了后备缓冲，而
   我们恰好在“清屏已生效、3D 尚未画回”的时刻取帧上屏。下一步要把
   “渲染 → 清屏 → present”放进同一个帧序号里比较（区分游戏自己线程的
   `serviceRepaints()` 与 runner 的兜底 `paint()`），再决定修 present 时机
   还是修兜底重绘。

## 第 32 轮：比赛帧的三角形被投影到屏幕外（世界并非为空）

第 31 轮把上屏帧被清屏的问题查清后，本轮继续在“渲染调用返回 0 像素”这一层加
计数器（仍在 `git archive` 抄出的离树构建里，主仓未改）：

1. **比赛窗口里渲染确实被调用**：`NJRENDER` 每帧都在，但 `drawn=0`；同时
   `NJMESH` 显示 mesh 存在（例如 47 个）、`vb=1 ib=1 app=1 subs=1 ok=1`，
   模型矩阵也有正常的平移/缩放（如 `mt=0.93,0.67,1.64 md=0.1,0.1,0.1`）。
2. **三角形生成了，但被投影到屏幕外**：`jr_emit`/裁剪计数器 `NJCLIP` 在比赛
   帧给出 `tri≈1954 clipout≈288 emit≈1704 drawn=0`，且首个发射三角形的屏幕
   坐标是 `x≈-11989, y≈483`（视口仅 240×320）——不是没有几何体，而是整片
   世界落在视口左侧外。另一些帧 `first` 仍是 `x≈2833` 这类远在屏外的值。
3. **摄像机参数本身不像坏的**：`NJCAMV` 在比赛帧打印 `arr=1`
   （游戏用 `setCamera(Transform)` 明确设了相机）、`viewT≈(0,-0.376,-5.55)`、
   `proj=(1,1,-1.04,-2.04)`、视口 `0,0 240x320`；同一进程里展厅/开场帧
   `NJCLIP` 显示 `emit≈664, drawn≈305`，即渲染管线对“相机+小模型”场景工作正常。
4. **结论（本轮收窄）**：比赛世界不是空的，也不是合成被清掉；问题在
   视图/投影与模型矩阵的合成——同一套矩阵/相机代码在展厅帧正确，在比赛世界
   把顶点整体推到视口外。下一步应把比赛帧的 `setCamera` 原始矩阵、求逆后的
   view、投影矩阵与一个发射顶点的合成过程逐项打出来比对（尤其确认
   `mat_mul`/`mat_invert` 的行主序约定与游戏给的矩阵是否一致），再决定修
   view 合成还是模型矩阵乘法。

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

## 第 33 轮：比赛画面实际可跑；3D 世界几乎不产出三角形（相机变换疑点）

- 用自适应按键（连续回车推进过场）实际进入比赛，并打完整场到结算：
  比赛 HUD（进度 2%、P3 92M / P2 86M / P1 72M、JACK、50 MPH）见
  `.local-tools/debug-runs/nfs-pixfmt-20261021/live1/r5.png`（960×1280，
  SHA-256 `51f7349e386c4b33…`）；随后结算页 `RESULTS / YOU LOSE… TIME: 04:55.8`
  见同目录 `now.png`（SHA-256 `29862f84d29e14a0…`）。计时、名次、进度、胜负
  流程都在跑，说明此前的“卡住/冻屏”是过场页在等按键，不是游戏线程死掉。
- 同一场比赛用离树探针构建观察（`git archive HEAD` 副本 + `NJ_ZERO_TRACE`
  / `NJ_FRAME_STATS` / `NJ_MESH_TRACE`，主仓 `src/` 未改）：
  - `NJFRAME frame=… drawn=0 n=48..53 vp=0,0 240x320 viewT=0.000000,-0.376094,-5.549621 proj=1.000000,1.000000,-1.040816,-2.040816`
    ——每帧收集到 51–53 个 mesh，但通过投影/裁剪的三角形只有 0–4 个；
    相机视图平移在所有比赛帧**完全恒定**。
  - `NJSTAT`（世界绘制后立刻统计当前 Canvas 后备缓冲）：整屏仍是橄榄色
    `ff878064`（76800/76800 像素），即 3D 没有写出可见像素。
  - `NJMESH`：city 的 mesh 全部 `subs=1 ok=1`，模型矩阵平移在 x≈96…144、
    z≈-24…56，缩放对角 `0.0027/0.01/0.1` 不等；说明几何与材质都读到了，
    问题在视图/投影侧。
- 结论修正（撤回 513832e 中“上屏时被清屏帧覆盖”的表述）：比赛相位下
  M3G 渲染调用存在、world 收集非空，但**几乎不产出屏幕三角形**；相机
  `viewT` 在所有比赛帧恒定，疑似未把相机节点父级变换（getGlobalTransform
  链）合成进来，导致相机停在原点、世界（x≈100+）整体落在视锥外。
  下一轮直接从 `m3g_render_scene()` 的相机解析路径入手核对节点变换合成，
  与 NFS14 的可正常比赛帧做对照。
- 本轮的“画了很多三角形”的窗口（`drawn=226..280`，`vp=0,80 240x240`）出现在
  过场/装载相位，不代表比赛画面；两者不能混为一谈。

## 第 30/31/32/33 轮边界

- 上述转储都在离树诊断构建里完成（`git archive HEAD` 抄出后只加日志与探针），
  主仓 `src/` 未改；修正尚未实现。
- 比赛帧的“合成”结论来自同一时刻的表面内容与窗口截图对比，未逐条核对游戏的
  2D 绘制调用，属于强推断而非直接观测。

- 纹理采样已实现（见上“更正”）；比赛画面仍是纯色的原因未定，深度缓冲/背面剔除/光照的支持程度需另行核对。
- 未验证：完整一圈/冲线、存档、Android 真机；HUD 与操控只能说明游戏逻辑在跑。
- 该限制不通过改 2D 绘制或异常处理解决，属于 M3G 光栅器能力范围。
