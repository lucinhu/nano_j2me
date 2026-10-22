# 《极品飞车16：亡命狂飙》回归验证（2026-10-22）

在**当前工作树**上重新验证《极品飞车16：亡命狂飙》(NFS The Run, 240x320) 的 3D 比赛画面，
并把比赛一路开到结算页。

## 结论

- **3D 世界在当前工作树上正常渲染**，不是整片橄榄色平涂。比赛画面里有隧道/桥体、
  路面与车道线、玩家橙色 Mustang、对手车、城市天际线与棕榈树、路口红绿灯、终点格纹旗。
- **跑完整场比赛并进入结算页**：本轮共跑完 **两圈**，两次都到达 `RESULTS / YOU LOSE…` 结算屏
  （第一次 `TIME: 03:49.3`，第二次 `TIME: 03:25.8`），随后出现 `RETRY EVENT / DO YOU WISH TO PLAY AGAIN?`
  对话框。这比此前「跑到 `TIME: 04:55.8`」的历史证据更进一步：**第一次完整跑完一圈并拿到结算屏**。
- **HUD 持续刷新且操控进入游戏逻辑**：进度条 7%→23%→48%→65%→94%→99%→100% 单调推进，
  名次/距离栏从 `P1 514M / P3 508M / P2 501M` 变到 `P2 49M / P1 48M / P3 48M`，
  车速在 30–109 MPH 之间持续变化（30→99→97→90→72→109→105）；玩家车在画面里的横向位置随左右方向键移动
  （窗口 x 214…553 px 区间内摆动，点键后 2 秒内即改变方向）。
- **异常**：两次运行各 **10 条 thrown、0 条 uncaught**（`uncaught exception in thread` 与
  `exception escaped into the frame loop` 均为 0）。全部 10 条 thrown 与此前旧跑完全一致，
  且都是游戏自己 catch 掉的启动期/2D 层异常。
- **JAR 未被改动**：运行前后 SHA-256 均为
  `92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5`。
- 根因仍是**相机语义**（commit `1e47d9a`），不是「帧时序 / present 覆盖清屏」。
  本轮所有取证都在包含该修复的二进制上完成，**没有再出现整片平涂帧**。

## 环境与命令

| 项 | 值 |
|---|---|
| 工作树 | `/home/lucinhu/workspace/c/nano_j2me` |
| 提交尖端 | `2a6292ef514c68d2dacf087b821aef3ec298a402`（`docs(perf): record the independent verification of the frozen tip`） |
| 相机修复 | `1e47d9aef89178594159fc924d607570a79eb512 fix(m3g): render a World through its own camera and honour node transforms`（`git merge-base --is-ancestor 1e47d9a HEAD` 成立） |
| JAR | `/home/lucinhu/Downloads/jar/极品飞车16：亡命狂飙.jar` |
| JAR SHA-256（跑前 = 跑后） | `92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5` |
| 画布 / 堆 | 240x320 / heap 16 |
| 显示 | `DISPLAY=:1`（Xwayland）；指针 XTEST 不可用，键盘 XTEST 可用；本标题全程只用键盘 |
| 取证根目录 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs16-regress-20261022/` |

### 用到的两个二进制

| 运行 | 二进制 | SHA-256 | 来源 |
|---|---|---|---|
| `run-head/` | `bin/nj2me-head` | `9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813` | 干净树构建 `.local-tools/head-build-20261022/build/nj2me`，其树内容经 `diff -r` 比对等于提交 `cc3dc0e`（2026-10-22 04:23，`docs: measure the ARGB-to-ARGB blend inline on real titles`）；`src/midp/nj_lib_m3g.c` 的 SHA-256 与 `git show HEAD:src/midp/nj_lib_m3g.c` 完全一致（`089989e0…`），即含相机修复；`src/core/nj_interp.c` 中**不含** `NJ_ENABLE_SUPERINSTR` / `NJ_ENABLE_JUMP_THREADING` |
| `run-worktree/` | `bin/nj2me-worktree-0736` | `e8459c132b2ca9bb4f0230a902caf24f4f6147bdea942d3d2011cd39ba03e94f` | 工作区当时的 `build/nj2me` 快照（07:19 构建，`src/core/nj_interp.c` 含 13 处 `NJ_ENABLE_SUPERINSTR`，即在途的快路径工作区） |

两个二进制都跑到了结算页；**结论不依赖于用哪一个**。

### 驱动脚本

`drive4.py`（本目录内自建，未改共享工具）是 `corpus-round20-20261021/drive.py` 的自适应改写：
启动同样的 SDL2 窗口、用 `xdotool` 对该窗口发键、用 `import -window <id>` 截屏，
但每帧先做像素分类（race / paused / dialog / overlay / results）再决定下一步：

- `race`：按住 `Up` 持续油门，每 5 轮点一次 `left`、每 9 轮点一次 `right`；
- 连续 3 帧像素完全相同时判定为**游戏在等按键**（日志同期刷
  `WARN: all threads are waiting; assuming the app is done`），补一次方向键把它唤醒；
- `paused`：菜单高亮可能是 `RESUME EVENT / RESTART / SETTINGS / HELP / QUIT TO MENU` 任一项，
  先探测哪个方向键会移动高亮，再一路移到 `RESUME EVENT` 并按 `fire` 恢复；
- `results`：截图后退出。

## 证据

### 1. 3D 世界在渲染（不是整片平涂）

窗口是 960x1280（240x320 的 4 倍放大）。下表统计**只取 3D 区域**
（窗口 x∈[240,760)、y∈[0,900)，避开模拟器自绘的键盘/D-pad 浮层）：

| 截图 | 绝对路径 | SHA-256 | 3D 区不同颜色数 | 旧 bug 平涂色 (135,128,100) 占比 | 每行不同颜色数 min/中位/max |
|---|---|---|---|---|---|
| 起点 7% | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs16-regress-20261022/run-head/race-001.png` | `3789d63438fdd51cee29d53e364a3ce9f1b97b3909b252a847abc010a0116bc2` | 215 | 0.0000 | 7 / 16 / 31 |
| 23% | `…/run-head/race-101.png` | `770a3b63b5e6319de5fc7b6bbb24f815cec7f579fcf8a568ff97af70d098bb21` | 531 | 0.0000 | 8 / 33 / 61 |
| 48% | `…/run-head/race-201.png` | `d11f83cda030beedd43171b02ac5210ed695d00ae9087cef0e9f825cb0731695` | 331 | 0.0000 | 8 / 18 / 44 |
| 65% | `…/run-head/race-301.png` | `d205821b6e9d1ea6348c6b6e70a24875449e6e49cd663fef4876b070d28d4c89` | 320 | 0.0000 | 8 / 19 / 48 |
| 94%（并排对手车） | `…/run-head/race-451.png` | `2110f1f7f53dfbd93e0ee848e22bdc29ea4e41766b88c8fb38c65924f14cc9a1` | 649 | 0.0000 | 9 / 29 / 56 |
| 99%（路口红绿灯/格纹旗） | `…/run-head/race-476.png` | `75d90b7d3edf98956404d43a24a9dd5d63828dc1be60eddfc3080ad9505176b3` | 678 | 0.0000 | 2 / 20 / 57 |
| 100%（`YOU FINISHED LAST`） | `…/run-head/race-1011.png` | `1c99f9aa7a74a0ebdd00bac3bede24aa924acb0c1cb9c0002bb028adceb129f3` | 385 | 0.0000 | 1 / 20 / 41 |
| 在途工作区二进制 66% | `…/run-worktree/race-300.png` | `b858166dfcdcdc225ae54e40a716450ea181386c4a629549ee70606e6cc3df88` | 476 | 0.0000 | 14 / 22 / 52 |
| **对照：修复前的平涂帧** | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-pixfmt-20261021/live1/r5.png` | `51f7349e386c4b33483b383c6991da9297c6d1d328476f5d1e51452c3355d1b8` | **27** | **0.9133** | **1 / 1 / 10** |

判别结论：旧 bug 帧的 3D 区只有 27 种颜色、每行恒为 1 种颜色（平涂）；
本轮比赛帧每行有 1–61 种颜色（中位 16–33）、整区 215–944 种颜色，且**旧 bug 的橄榄平涂色
(135,128,100) 在全部 1509 张比赛帧里出现 0.0000 次**（`measure.py` 逐帧核对）。
注意游戏自己的 2D 面板底色是 (132,130,99)（RESULTS / 剧情提示面板用色），
与 bug 平涂色 (135,128,100) 不是同一个颜色，两者已分开统计。

另：**1022 张 `run-head` 比赛帧中有 989 张（96.8%）3D 区不同颜色数 ≥ 50**；
剩下 33 张全部落在「冲线后/结算页」区间（`race-479`…`race-501`、`race-1013`…`race-1022`），
是游戏的 2D 结算/剧情面板，不是比赛画面。

画面内容（肉眼核对）：`race-001` 是隧道/高架桥段，左侧桥墩、右侧桥面与棕榈树、
前方路面与虚线车道线、前景玩家橙色 Mustang 尾部与尾灯；`race-451` 是市区路段，
玩家橙车右侧有一台亮着大灯的黄色对手车，路面双黄线、护栏、棕榈树与城市高楼；
`race-476` 是市中心直道，两侧路灯与楼群、远处红绿灯与格纹旗；`race-1011` 顶部横幅
`YOU FINISHED LAST`，下方仍是渲染正常的路面与格纹终点线。

### 2. 跑完整一圈并到达结算页

`run-head` 的时间线（`run-head/manifest.json` + `run-head.out`）：

| 时刻 | 事件 |
|---|---|
| 0–80 s | 序章：`ENABLE SOUND?` → 连按 `fire` 过场 → `NEW GAME`（LOCATION: SAN FRANCISCO / RACE TYPE: SPRINT，车型 FORD MUSTANG GT BOSS 302）→ 选车 → 起跑 |
| 80.5 s | 比赛开始，`race-001` = 7% |
| 276 s | `race-476` = 99% |
| 277 s | `race-479` 出现 `YOU FINISHED LAST` 横幅（100%） |
| 282 s | `z-dialog-490.png` = 结算屏 `RESULTS / YOU LOSE… / TIME: 03:49.3` |
| 284 s | `z-dialog-491.png` = `RETRY EVENT / DO YOU WISH TO PLAY AGAIN?` |
| 290 s 起 | 第二圈重新开始（`race-503` 起，进度从 0 重新爬升） |
| 495 s | `race-1011` = 100% + `YOU FINISHED LAST` |
| 499.9 s | `z-results.png` = 结算屏 `RESULTS / YOU LOSE… / TIME: 03:25.8` |
| 560 s | 帧预算耗尽，exit 0 |

结算屏截图：

| 截图 | 绝对路径 | SHA-256 | 内容 |
|---|---|---|---|
| 第一圈结算 | `…/run-head/z-dialog-490.png` | `c580ec5be87f5501bf9ae9b085c82b8b2528ac3b66866149f84297870ae7ab43` | `RESULTS` / `YOU LOSE…` / `TIME: 03:49.3` |
| 重赛对话框 | `…/run-head/z-dialog-491.png` | `d652ac10ab8412e84f16c42acd45e4b2ae59c7f6bb5d78b5954539798d9ff659` | `RETRY EVENT` / `DO YOU WISH TO PLAY AGAIN?` |
| 第二圈结算 | `…/run-head/z-results.png` | `f4ad318c480ec9928d30389608476dd83818f323092255f8272b74bd20f7474c` | `RESULTS` / `YOU LOSE…` / `TIME: 03:25.8` |
| 在途工作区二进制结算 | `…/run-worktree/z-results.png` | `80f2a3e8ac57efbde0098da8a75014adc08f03d67125630c72e3e0e87147b6bd` | `RESULTS` / `YOU LOSE…` / `TIME: 03:40.6`（第一圈即到结算） |

### 3. HUD 持续刷新、操控进入游戏逻辑

从 `race-001 / race-101 / race-201 / race-301 / race-451 / race-476 / race-1011`
逐帧读 HUD：

| 帧 | 进度 | 名次与距离 | 速度 |
|---|---|---|---|
| race-001 | 7% | P1 514M / P3 508M / P2 501M | 30 MPH |
| race-101 | 23% | P3 1005M / P1 999M / P2 910M | 99 MPH |
| race-201 | 48% | P3 1184M / P1 1180M / P2 1098M | 97 MPH |
| race-301 | 65% | P2 1289M / P3 1289M / P1 1289M | 90 MPH |
| race-451 | 94% | P2 255M / P3 255M / P1 255M | 72 MPH |
| race-476 | 99% | P2 84M / P3 84M / P1 84M | 109 MPH |
| race-1011 | 100% | P2 49M / P1 48M / P3 48M | 105 MPH |

（速度数字由 HUD 大号数字位逐位模板匹配读出，`race-451` 的第二位被模拟器按键浮层压住，
该帧读作 `72`+浮层残影；其余各帧位形完整。）

- 进度单调推进：`run-head` 进度条像素位置 51 → 87 → 143 → 183 → 251 → 263 → 267（0 → 100%），
  第二圈从 39 重新爬到 267；HUD 橙色占比从 0.054 升到 0.559。
- 速度数字区域哈希在 1020 次采样里出现 **470 个不同值**，即速度显示一直在变（油门生效）。
- 转向生效：把玩家车橙色车身的横向质心量出来（`run-head` 168 帧可测），
  它在窗口 x = 214…553 px 之间摆动；每次 `left` / `right` 点键后 2 秒内，
  质心位移中位数分别为 3.2 px / 16.9 px，最大位移 216 px，
  说明左右输入真的进了游戏逻辑，不只是 HUD 在动。
- 小地图（右上角）、JACK 队友条、格纹旗进度条也逐帧变化。

### 4. 运行统计

`run-head/runtime.log`（二进制 `9f6325a7…`）：

```
INFO: stats: frames=16848 paints=16848 gfx=4816689 tris=6064969 m3g=863914 time=560.062s fps=30.1 bytecodes=1952185744 (3.49 M/s) natives=61733262 heap=10450544/16777216 bytes cpcache=170659312/2733 findcache=0/0
INFO: observed: threads=3 gc=51 tones=0 pcm_frames=0 file_opens=0 file_writes=0 file_reads=0 bytes_written=0
```

`run-worktree/runtime.log`（二进制 `e8459c13…`）：

```
INFO: stats: frames=9989 paints=9989 gfx=2491024 tris=3173656 m3g=498932 time=332.933s fps=30.0 bytecodes=1009924675 (3.03 M/s) natives=24724077 heap=13919696/13922416 bytes cpcache=92458464/2725 findcache=0/0
INFO: observed: threads=3 gc=50 tones=0 pcm_frames=0 file_opens=0 file_writes=0 file_reads=0 bytes_written=0
```

`tris`（M3G 三角形数）607 万 / 317 万，`m3g` 调用 86 万 / 50 万 —— 3D 管线确实在跑，
不是「跳过绘制」。

## 异常

两次运行（`run-head`、`run-worktree`）的异常**完全相同**，共 **10 条 thrown、0 条 uncaught**。
`grep -c "uncaught exception in thread"` = 0，`grep -c "exception escaped into the frame loop"` = 0
（后者是 `src/midp/nj_lib_midlet.c` 里 paint 抛出后逃出帧循环的标志位）。

thrown 原文（`INFO: thrown: …`，逐字）：

```
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at af.<init>(Lcom/ea/nfs10/MonkeyApp;)V pc=471: (no message)
INFO: thrown: java/lang/NullPointerException at af.<init>(Lcom/ea/nfs10/MonkeyApp;)V pc=479: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at af.aY()V pc=232: (no message)
INFO: thrown: java/lang/NullPointerException at af.aY()V pc=240: (no message)
INFO: thrown: java/lang/ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672: (no message)
INFO: thrown: java/lang/ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672: (no message)
INFO: thrown: java/lang/ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672: (no message)
INFO: thrown: java/lang/ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672: (no message)
```

判定依据：

- 前 6 条是**启动期**异常（RMS 存档不存在、构造函数里的 NPE），发生在序章还没进比赛时；
- 后 4 条 `ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672`
  是 **2D 层**（`Graphics` 绘制方法）的越界，与 3D 无因果关系：出现后下一帧画面照常刷新，
  且与旧笔记 `2026-10-21-nfs-the-run-m3g.md` 记录的一致；
- 全部由游戏自己 catch —— 运行日志里没有任何 `uncaught` 行，
  两次运行都以 `nano_j2me: exit status 0` 正常收尾。

另有非异常的常规告警：`WARN: all threads are waiting; assuming the app is done` 54 次
（`run-head`），这是游戏在等玩家按键时的正常状态（同期画面完全冻结，
补一次方向键即恢复）；`run-final`（更早的一次跑）同样有 55 次。

补充说明：更早的一次跑（`run-final/`，二进制 `c48a1f12…`）在 teardown 阶段还多出 4 条
`java/lang/RuntimeException at af.aZ()Z pc=29: writeInt` / `pc=101: close` /
`af.A()Z pc=39: writeInt` / `pc=236: close`（存档写盘）。
本轮两次运行**没有**这 4 条（存档只写出 `nfs12dat_3D.rms`）。

## 边界与未验证项

- 本轮只验证《极品飞车16：亡命狂飙》一个 JAR，**没有**对 30 余个语料标题做整体回归。
- 比赛是 **SPRINT（短程）** 赛道：跑完的是这个事件的整条赛程（进度 100% + `YOU FINISHED LAST`
  + 结算屏），但它不是多圈计时的环绕赛道，所以「完整一圈」=「完整跑完这个 SPRINT 事件」。
- 成绩是 `YOU LOSE`：脚本驾驶不会走线、撞车与超车逻辑不能等同真人操作，
  所以本轮**不评价**游戏性，只评价「3D 渲染 + HUD + 事件流程是否正确」。
- `run-head` 的二进制对应提交 `cc3dc0e` 的干净树（2026-10-22 04:23），
  而不是本轮开始时的尖端 `2a6292e`；两者之间只有解释器快路径与文档提交。
  为了不依赖这一区分，**又用当时工作区的 `build/nj2me`（`e8459c13…`，含在途快路径改动）
  复跑了一遍**，同样渲染正常并到达结算页（`run-worktree/`）。
- 只用了键盘。指针 XTEST 在本机不可用，本标题也不需要指针；uinput 指针锁**未使用**。
- 序章靠固定按键序列推进（`soft2` → 连按 `fire` ×8 → `down` → `fire` ×4），
  不同 SDL 窗口焦点顺序下可能需要重试；本轮两次都一次通过。
- 帧预算耗尽时进程由 `--run-frames` 正常结束（exit 0），并非崩溃或被杀。

## 复现命令

```bash
cd /home/lucinhu/workspace/c/nano_j2me
R=.local-tools/debug-runs/nfs16-regress-20261022
JAR="/home/lucinhu/Downloads/jar/极品飞车16：亡命狂飙.jar"

# 0) 校验 JAR
sha256sum "$JAR"        # 92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5

# 1) 取一个含相机修复的干净二进制（也可直接用工作区 build/nj2me）
git merge-base --is-ancestor 1e47d9a HEAD && echo "camera fix present"
cp .local-tools/head-build-20261022/build/nj2me $R/bin/nj2me-head
sha256sum $R/bin/nj2me-head
#   9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813

# 2) 跑（自适应驾驶：进比赛 → 持续油门 + 左右转向 → 处理暂停/对话 → 抓结算屏）
NJ2ME_BIN="$PWD/$R/bin/nj2me-head" python3 $R/drive4.py \
    --jar "$JAR" --size 240x320 --frames 46000 --race-seconds 1100 \
    --out $R/run-head

# 3) 量化「不是平涂」：3D 区不同颜色数 / 旧 bug 平涂色占比 / 每行颜色数
python3 $R/measure.py \
    $R/run-head/race-001.png $R/run-head/race-451.png $R/run-head/race-476.png \
    $R/run-head/race-1011.png $R/run-head/z-results.png \
    .local-tools/debug-runs/nfs-pixfmt-20261021/live1/r5.png

# 4) 异常清点（thrown vs uncaught）
grep -c "uncaught exception in thread"          $R/run-head/runtime.log   # 0
grep -c "exception escaped into the frame loop" $R/run-head/runtime.log   # 0
grep "thrown:" $R/run-head/runtime.log

# 5) 再次校验 JAR 未被改动
sha256sum "$JAR"        # 92c5f29e1d6649d7c5696a0022e8abe4396993bc0d0e66f9e59b46051d9d38e5
```

原始日志与截图：`/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs16-regress-20261022/`
（`run-head/`、`run-worktree/`、`drive4.py`、`measure.py`、`evidence-index.txt`）。
