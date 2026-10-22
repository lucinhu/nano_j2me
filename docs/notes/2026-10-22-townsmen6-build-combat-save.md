# 家园6之革命完美汉化版（Townsmen 6）N73 240×320 —— 建造 / 战斗 / 存档验证

日期：2026-10-22
执行人：townsmen（子代理）
运行目录：`/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/townsmen6-bcs-20261022/`

---

## 结论

1. **游戏可运行**：JAR `家园6之革命完美汉化版_诺基亚 N73系列(240x320).jar` 在 nano_j2me 上能启动，能进入剧情战役第一关（家园概述）村庄，能打开村庄信息面板、行动菜单、建造菜单、战役地图、帮助页。
2. **决定性兼容性结论：本 N73 版本完全忽略触摸输入。**
   JAR 内唯一的游戏 Canvas 类 `t` 的 `pointerPressed` / `pointerReleased` / `pointerDragged` 三个方法经 `javap` 确认全部是空实现（`0: return`），全 JAR 只有 `t` 引用 "pointer" 相关字符串。
   → 因此 `docs/notes/2026-10-22-townsmen6-village-ui.md` 第 3 轮"底部行动菜单能画出来、但任何按键/触摸都没有像素响应"的现象得到解释：**菜单是画出来了，但触摸事件根本不会被游戏接收**，uinput 虚拟触摸（`vptr`）永远无法驱动本游戏。
   → 本游戏**只能纯键盘操作**：方向键（等价数字键 2/4/6/8）在地图上移动一个"手形指针"，确认键 = Fire（Return）/ 左软键 F1 / 数字键 5，右软键 F2 = 取消/返回。
3. 验收结论：

   | 验收项 | 结果 | 主要证据 |
   |---|---|---|
   | 1 教程（村民选择）完成 | **达成** | runC `c16.png`（教程"翠林满目"、蓝箭头指向伐木场），教程目标链一路推进到"人口膨胀" |
   | 2 实际建成建筑 + 面板变化 | **达成（runC 在途二进制 + runF 当前构建各一组证据）** | runC `z18/z20/z22`（住房）、`z32/z33`（采石场）、`z48/z49`（水井）、`B4`（养鱼场）；**当前构建 `e8459c13…`：runF `P5.png`（绿色可建区 + 标签"住房"）→ `P6.png`（新建筑脚手架 + 其信息面板，标签"建筑工人"）** |
   | 3 战斗 | **阻塞（未达成）** | 战役法国地图可达、地区面板显示"国王士兵 3 / 红军士兵 5 / 派遣"，但"派遣"按钮无法用键盘激活；详见"兼容状态" |
   | 4 存档→退出→同一 --rms-dir 重启→继续 | **达成** | runC 写入 `Townsmen62.rms`（966 B），runD 用同一 `--rms-dir` 重启后 `R09.png` 恢复村庄 |
   | 5 异常记录 | **达成** | runC / runD / runE / runG 四次会话的全部异常均为游戏内部 `thrown`（已捕获），**无未捕获异常、无 MediaException**（当前构建见 `runG/runtime.log`） |

4. **建造流程在当前构建上已复现（对第 4 条保留意见的修正）**：验收 2 的第一批证据来自 **runC 会话**，其二进制 SHA-256 `2f10619898453398d799138bd31d2fc2f99a0fd1665140540e1ad6d520a5dbfd`（带 `NJ_ENABLE_SUPERINSTR` + `NJ_ENABLE_JUMP_THREADING` 的在途构建）**已被其他会话从磁盘删除**（`/proc/<pid>/exe` 显示 `(deleted)`），无法再复现。
   用 **HEAD 干净构建 `9f6325a7…`（runD/runE）** 重做建造流程时行动菜单图标、建筑影子、地块绿/红高亮均不渲染（该构建下"未复现"）。
   但随后在 **当前工作区构建 `e8459c132b2ca9bb4f0230a902caf24f4f6147bdea942d3d2011cd39ba03e94f`（runF/runG）** 上用 runC 的存档副本重做，**建造完整复现**：
   行动菜单 4 个图标正常绘制 → 建造菜单卡片（住房 + 3 星 + 绿色造价按钮 "3"）正常绘制 → 放置模式显示**绿色可建区**（`runF/P5.png`，底部标签"住房"）→ 移动指针到绿色格后按 Fire → **房屋落地**（原位置出现木质脚手架工地，`runF/P6.png`）并自动弹出该新建筑的信息面板（底部标签"建筑工人"，即需要指派建筑工人）。
   ⇒ 结论：**建造（放置 + 落地）在当前构建上是可用的**；HEAD 干净构建 `9f6325a7…` 上的"未渲染"是该构建自身的渲染差异，不应据此判定"不支持建造"。
5. **自由模式（自由建造）用键盘可以进入**（对第 5 条边界项的修正）：主菜单 → 开始游戏 → 启动画面 → 模式菜单 → 自由模式 → 进入**自由模式设置页**（`地图尺寸 / 水域面积 / 敌人数量` 三条滑块）→ 进入自由模式村庄地图（runG `P3.png` / `Q_left.png` / `Q_space.png`）。自由模式地图同样支持行动菜单与绿色/红色放置提示。
6. **战斗仍然阻塞**，且阻塞原因现在是**可证明的**（不再是"没找到路径"）：`javap` 全量反编译证明，地区面板（含"派遣"按钮，类 `o`）只由**触摸路径**驱动，而唯一的游戏 Canvas 类 `t` 的 `pointerPressed/Released/Dragged` 全部是 `0: return`；键盘确认路径 `hg/HG.a(int,boolean)` 只会做"按当前指针格子做**网格**命中测试"（`s.k()/s.l()` → `s.a(II)Z` → `s.c()`），而战役地图实现 `v` 的 `a(II)Z` 只写入网格坐标 `g/h` 并做范围检查，**不存在任何屏幕像素级控件命中测试**。因此"派遣"按钮在纯键盘下不可达。

---

## 环境与命令

```text
项目根目录        /home/lucinhu/workspace/c/nano_j2me
DISPLAY           :1  (Xwayland, 2560×1600)
XTEST 指针        本机上不可用（XTestFakeMotionEvent 返回成功但指针不动），指针只能用 uinput 虚拟触摸
                  → 但本游戏不处理触摸，所以全程只用键盘
键盘输入          xdotool keydown/keyup --window <winid>（XTEST 键盘可用）
JAR               /home/lucinhu/Downloads/jar/家园6之革命完美汉化版_诺基亚 N73系列(240x320).jar
JAR SHA-256       5a32a9881af0c479c194a25c9180cf6471d0cb7f45777bbbebfdc58d3cca9317（全流程前后一致，未修改）
```

二进制：

| 二进制 | SHA-256 | 用途 |
|---|---|---|
| `build/nj2me`（会话期间被其他会话反复重建） | 运行中 runC：`2f10619898453398d799138bd31d2fc2f99a0fd1665140540e1ad6d520a5dbfd`（已被删除）<br>runF/runG 运行期间且报告时刻：`e8459c132b2ca9bb4f0230a902caf24f4f6147bdea942d3d2011cd39ba03e94f` | runF/runG（当前构建，建造复现 + 自由模式） |
| `.local-tools/head-build-20261022/build/nj2me`（HEAD 干净构建，= Lead 的基准） | `9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813` | runD（读档/战役地图）、runE（重做建造） |

runF / runG（当前构建 + runC 存档的**副本**，`runC/rms` 本身未被改动）：

```bash
cd /home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/townsmen6-bcs-20261022
BIN=/home/lucinhu/workspace/c/nano_j2me/build/nj2me
JAR="/home/lucinhu/Downloads/jar/家园6之革命完美汉化版_诺基亚 N73系列(240x320).jar"
sha256sum $BIN          # e8459c132b2ca9bb4f0230a902caf24f4f6147bdea942d3d2011cd39ba03e94f
mkdir -p runF && rm -rf runF/rms && cp -a runC/rms runF/rms          # 副本，绝不动 runC/rms
sha256sum runF/rms/*.rms > runF/manifest.txt
sha256sum "$JAR" >> runF/manifest.txt ; date >> runF/manifest.txt
rm -f cmd6.fifo && mkfifo cmd6.fifo
setsid bash -c "cd $PWD && tail -f cmd6.fifo | python3 drive4.py \
  --jar '$JAR' --size 240x320 --out runF --frames 400000 \
  --bin $BIN --rms-dir $PWD/runF/rms > drive6.out 2>&1" < /dev/null > /dev/null 2>&1 &
# runG 同理（--out runG --rms-dir $PWD/runG/rms，fifo 名 cmd7.fifo）
```

回放驱动（本次自建，位于本运行目录）：

```text
.local-tools/debug-runs/townsmen6-bcs-20261022/drive3.py   # runC：交互式 stdin 命令循环
.local-tools/debug-runs/townsmen6-bcs-20261022/drive4.py   # runD/runE：drive3 的副本，额外支持 --bin / --rms-dir
.local-tools/debug-runs/townsmen6-bcs-20261022/track.py    # 在截图中定位手形指针（纯色 231,178,99 + 连通域）
.local-tools/debug-runs/townsmen6-bcs-20261022/check.py    # 统计截图中的手形/绿色/红色像素
```

启动会话（runD，HEAD 干净构建 + 既有存档目录）：

```bash
cd /home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/townsmen6-bcs-20261022
JAR="/home/lucinhu/Downloads/jar/家园6之革命完美汉化版_诺基亚 N73系列(240x320).jar"
rm -f cmd4.fifo && mkfifo cmd4.fifo
setsid bash -c "cd $PWD && tail -f cmd4.fifo | python3 drive4.py \
  --jar '$JAR' --size 240x320 --out runD --frames 400000 \
  --bin /home/lucinhu/workspace/c/nano_j2me/.local-tools/head-build-20261022/build/nj2me \
  --rms-dir $PWD/runC/rms > drive4.out 2>&1" < /dev/null > /dev/null 2>&1 &
```

驱动实际执行的模拟器命令行（runC）：

```text
build/nj2me --port sdl2 --size 240x320 --fps 30 --heap 16 \
            --rms-dir runC/rms --run-frames 200000 --stats "<JAR>"
```

驱动命令协议（写入 `cmd.fifo`，每行一条）：

```text
shot <名字>        截图到 runX/<名字>.png 并打印 SHA-256
key  <名字>        fire=Return soft1=F1 soft2=F2 clear=BackSpace
                   up/down/left/right=方向键（也接受任意 xdotool 键名，如 5、Tab、F12）
hold <名字> <秒>
wait <秒>
tap/tapwin <x> <y>  虚拟触摸（本游戏无效，保留用于对照）
geom / rms / logtail / quit
```

---

## 复现步骤（精确按键序列）

### A. 启动 → 剧情战役 → 村庄（全新存档）

```text
打开声音? 对话框（否 高亮）      → Fire
主菜单（开始游戏 高亮）          → Fire
HandyGames 启动画面              → Fire
模式菜单（剧情战役 高亮）        → Fire
法国战役地图（继续游戏 高亮）    → Fire
进入教程村庄
```

### B. 教程 → 建造（runC 成功复现过的路径）

```text
手形指针落在"小型伐木场"上      → Fire            # 打开建筑信息面板（标签：分配任务）
面板内把指针移到"分配任务"      → Fire            # 给伐木场指派工人
村庄空地上                      → Fire            # 打开行动菜单（4 个图标围绕手形）
行动菜单                        → Up              # 进入建造菜单
建造菜单（住房 / 采石场 / 水井 / 养鱼场，由当前关卡目标固定）
                               → F1              # 绿勾 = 进入放置模式
移动手形指针直到地块高亮为绿色  → Fire            # 放下建筑
```

### B2. 当前构建（`e8459c13…`）上的建造复现（runF，逐键记录自 `drive6.out`）

```text
村庄空地上                      → Fire        # 行动菜单：4 个图标围绕手形指针（runF/P1.png）
行动菜单                        → Up          # 建造菜单卡片：住房 + 3 星 + 绿色造价按钮"3"（runF/F10.png）
                                            #   注意：F1(✓) 在本构建上是 no-op（F11 与 F10 逐字节相同）；
                                            #         Fire 不是"按下建造"，而是对当前指针格子做命中测试（F12）
```

放置并落地（runF 的实际成功路径，截图 P1→P6）：

```text
村庄空地上                      → Fire        # 行动菜单                                   → P1
行动菜单                        → Up          # 无变化（该指针位置 Up 不进建造菜单）           → P2
行动菜单                        → Fire        # 进入放置模式，指针落在矿石上 → 地块为**红色** → P3
放置模式                        → Down ×3     # 移动指针                                   → P4
放置模式                        → Left ×3     # 指针移到草地 → 地块变为**绿色可建区**          → P5
放置模式                        → Fire        # **房屋落地**：原位置出现木质脚手架工地，
                                            # 并自动弹出该新建筑的信息面板（标签"建筑工人"）→ P6
```

> 自由模式村庄里同样能看到放置提示：runG `Q_space.png` = 行动菜单打开时同时出现**绿色可建格 + 红色不可建格**（压在树林上）与手形指针；
> `S1.png` / `S2.png` 逐字节相同 ⇒ 在**红色（非法）格**上按 Fire 什么也不会发生（游戏拒绝非法落点），这与"绿色格 + Fire ⇒ 落地"互为印证。

### B3. 自由模式（自由建造）的键盘路径（runG）

```text
主菜单（开始游戏 高亮）          → Fire
HandyGames 启动画面              → Fire
模式菜单（剧情战役 高亮 / 自由模式）→ 选择"自由模式"
自由模式设置页                   # 地图尺寸 / 水域面积 / 敌人数量 三条滑块（runG/P3.png）
                                → Fire 进入自由模式村庄地图（runG/Q_left.png：水域、树林、城堡）
村庄空地上                      → Fire        # 行动菜单 + 绿/红放置提示（runG/Q_space.png）
```

> `敌人数量` 滑块说明自由模式**可以配置敌军**，是本 JAR 唯一存在敌军的玩法入口；
> 但滑块左右两侧的 `◀ / ▶` 是触摸型控件，且自由模式内的兵营/士兵链路本轮未走通（见"边界与未验证项"）。

> 补充（runC 实测）：**剧情关卡**的建造菜单里 Up/Down/Left/Right **不会**改变建筑种类（实测 5 次 Right + 5 次 Down 后仍是"住房"），
> 建筑种类由剧情关卡的目标固定。放下"采石场"必须落在灰色矿脉上，"水井"必须落在住宅区附近，"养鱼场"必须落在岸边。

### C. 存档（runC 实际执行，得到 `Townsmen62.rms`）

```text
地图上                           → F2      # 打开暂停菜单（继续游戏/关闭游戏/选项设置/帮助指南）
暂停菜单（继续游戏 高亮）        → Down    # 移到"关闭游戏"
                                → Fire   # 弹出对话框"退出并保存游戏？"
对话框（否 高亮）                → Down   # 移到"是"
                                → Fire   # 存档并回到主菜单
```

（备注：F1 **不能**激活暂停菜单项，只能翻页/推进教程；选项设置被 Fire 激活时会写一条 13 字节的设置记录 `Townsmen60.rms`。）

### D. 读档（runD，**同一** `--rms-dir runC/rms`）

```text
打开声音?（注意：语言选择对话框已消失，设置已从 RMS 恢复） → Fire
主菜单 → Fire
启动画面 → Fire
模式菜单 → Fire
法国战役地图（"继续游戏" 高亮） → Fire
→ 村庄恢复（第 28 天、人口 3、城堡 + 3 座农舍 + 伐木场 + 水井 + 采石场 + 养鱼场）
```

### E. 战役地图上的战斗入口（阻塞点，已尽量尝试）

```text
法国战役地图自动弹出地区面板：
  红军士兵 5   国王士兵 3   完成条件:人口 500   胜利条件:歼灭敌军…   部队 3   [派遣]
按键尝试（全部无效，面板区域逐像素不变）：Fire / F1（→ 打开帮助页）/ 数字键 5 / Up / Right
```

---

## 证据

所有截图与日志均在 `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/townsmen6-bcs-20261022/` 下，SHA-256 为实测值。

### 1. 教程与建造（runC）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/townsmen6-bcs-20261022/runC/c16.png` | `ed13e2bfb14b6ab6d04e2dc88efb50e62d0cc82bd188983ffa24b88803cb4840` | 教程页"翠林满目 / 木材: 0 / 2"，蓝箭头指向伐木场（手形指针阶段已过） |
| `…/runC/q01-mill.png` | `2aa830464800a76aa9dd892e3dd58a0c0df4e57e23ecdb6b4175b57d67b79665` | 小型伐木场 + 村庄全景 |
| `…/runC/u03.png` | `d3096a4aaef838e7e13c22111d4e6ec71754a830adb1f0d99aa831f63b9e7535` | 游戏自带"按键控制"帮助页（说明方向键移指针、确认键） |
| `…/runC/w01.png` | `99bf4adc0c7e80923fb27ffdd01e430fb290fd2fabae2803d0334cb6b5d3421b` | 帮助指南"建筑介绍/资源列表"等页面 |
| `…/runC/z18.png` | `42d9bb38753f06a7bb54ae803026d0f622c0e77f199af93618d2ecf9484a21ac` | 住房放置模式：可建区域高亮为**绿色** |
| `…/runC/z20.png` | `ae094c1442c4e54f97270b2e8e79898f7a50d395d5304a49205363b29236a21e` | 住房建成后的信息面板（床位图标 + 睡眠村民 "Z 1"） |
| `…/runC/z22.png` | `f30768ac5e71024bfa91bc1c823b73ca792c48f6b578a34720bc9de5eb0d1a5d` | 村庄里出现第二座农舍（建筑数量变化） |
| `…/runC/z32.png` | `6bd9a9e2827c0a1837a21e5a19daacc06ef9b41996a9108cd2900240d7bed509` | 采石场放置模式：灰色矿脉上出现绿色可建区 |
| `…/runC/z33.png` | `39020c3181dd05d6c7340359678156b0563e4702c8af6c8ec5e5486f548991e9` | 采石场建成（面板出现石料图标与"建筑工人"） |
| `…/runC/z48.png` | `1d68f9f327512624cd320b9693fbd26c8838991bf35a17f17fc8b3a21531e31b` | 水井放置模式：住宅区附近绿色可建区 |
| `…/runC/z49.png` | `797f1abf3b243051601b61ef59e9ee64850186c9bbea5c67c929f8bb5649ee2c` | 水井建成（屋顶水井出现在住房旁） |
| `…/runC/B4.png` | `32b324e18b7081e1f86f5413d038d3edfc22815cdea8f857abe0b7a5d3fccd77` | 养鱼场建成（岛上出现钓鱼平台） |
| `…/runC/runtime.log` | `2cd7fcd47ec4116c2371a6a04cbd1769d079afd901d307f4425fc6fa5963764d` | runC 完整运行日志（含全部异常，见"异常"章） |

### 2. 存档 / 读档（runC 写、runD 读）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/runD/W0.png` | `ca1a5c2d41cd6e5ec362301e9cb5169c8f54d5a4a24711f81829c14e673a32bb` | 暂停菜单，"关闭游戏"红色高亮（存档入口 1） |
| `…/runD/W1.png` | `a64e31f61f4cc88da0d8f41424acd41cbb72c5a5bb75f2afe36fd11a60f7915c` | 对话框"退出并保存游戏？"，选项 否 / 是 / 退出不保存（存档入口 2） |
| `…/runD/R00.png` | `e154100a6a7142ca6fb5ae51ecbe76a7ecf546817e5063a73b8d21c05ff84648` | 重启后只有"打开声音?"对话框，**语言选择对话框已消失**（设置由 RMS 恢复） |
| `…/runD/R03.png` | `e6817cd155bcb1aa168da4335c74c3e3aad2eda8dac60ecdc067834c637a491c` | HandyGames 启动画面 |
| `…/runD/R09.png` | `582e7d4c0c07f48c27ac8994585cf513bbc8cca4810aa5c5f758fc0443b93d0c` | **读档成功**：同一 `--rms-dir` 重启后村庄完整恢复（人口 3、城堡 + 3 农舍 + 伐木场 + 水井 + 采石场 + 养鱼场） |
| `…/runD/Z1.png` | `e2b5617f23e1dd39bf1e8f39c28e2e22061bcacc80252790174f92b2a029eb74` | 恢复后的村庄（手形指针指向城镇中心） |
| `…/runD/E2.png` | `b81603e196429fd7fa51ca88e2f9fa811c03eed2a9e136dfdce7b00647d118ab` | 城镇中心信息面板：人口 3、幸福指数 60、资源与工人槽位 |
| `…/runD/runtime.log` | `d9a8bf10b1b405d0380cd0305ca7c0205dc1643f07f302107fa869cce4d43c34` | runD（HEAD 构建）完整运行日志 |

RMS 文件（同一目录，runC 写 / runD 读）：

| 绝对路径 | 大小 | SHA-256 | 说明 |
|---|---|---|---|
| `…/runC/rms/Townsmen60.rms` | 29 B | `896f095a8f78c38878a0833ec4d9aca8a9eb804135fc6e2b5dd4ae4fb9e0b7d1` | 设置记录（13 B 有效载荷） |
| `…/runC/rms/Townsmen61.rms` | 123 B | `e411a12c962df4376f2465f76ba6585ecbf15f59954454ece22518b704fac16c` | 进度/关卡记录 |
| `…/runC/rms/Townsmen62.rms` | 966 B | `6411cf2fb718b6dba968b3c0e78155ce5211da5135c30877e9115a0816ef76f2` | **村庄状态存档**（950 B 记录），读档后据此恢复 |

证据副本（内容与上表逐字节一致，保留 06:44/06:59 的原始时间戳）：`…/runC/rms-evidence/Townsmen6{0,1,2}.rms`，SHA-256 同上。

RMS 文件格式（nano_j2me 自有格式，文件头 16 字节）：

```text
00000000: 4e4a 5253 0001 0000 0002 0001 0001 03b6   NJRS..........
00000010: 0002 03b6 000a 0a00 3000 4b00 c100 0000   ........0.K.....
...  （随后 950 字节村庄状态）
```

### 3. 战斗相关（runD，HEAD 构建）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/runD/W3.png` | `9d3b1746b32f1b54c0b4b3d7245056c8127d5ed94264282561601a20df2824ce` | 剧情战役法国地图 + 地区面板（红军士兵 5 / 国王士兵 3 / 完成条件:人口 500 / 胜利条件:歼灭敌军 / 部队 3 / **派遣**按钮） |
| `…/runD/X0.png` | `fb0dbdf1f694c0ffcc0bae08237de84d248382ee86b2c37d6ba798802090d395` | 同上（"派遣"按钮位于屏幕右下角，被模拟器自带键盘覆盖层遮挡） |
| `…/runD/D2.png` | `a06972be5cf891dd6972aca7ef3d50b8e1a081de309f52661d6e27744dffd48b` | 键盘扫描后地区面板无变化（面板区域与 D3–D6 逐像素一致） |
| `…/runD/D6.png` | `64e2a52099cfd985d4a443b342dd541535b8f444090fdbca9e905355788a9997` | 战役地图上按 F1 打开的**上下文帮助页**（"行动菜单用于管理家园…剧情战役模式中，此栏显示当前关卡任务"） |
| `…/runD/I2.png` | `269afe51be00dc5729cadcdce03d067fbf04ab5f596700ed59b66659b1ba37a1` | 战役地图（结束前最后一帧） |
| `…/runD/G1.png` | `4b61f2a5a146d3b4b2cf9d70a643246615ce93e46300f3b5932842ae1737e472` | 地图上连续 15 次 Right + 38 次 Down 之后：画面不变、手形指针不再绘制 |

### 5. runE（HEAD 构建 + 同一 `--rms-dir`，重做建造的尝试）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/runE/E03.png` | `8ec72fe775ed72d7abbfef4c0c1c3c023b413f86837dd3d4de9c8042d9b8fb6a` | 读档后村庄，手形指针纯色块 bbox (236,324)-(311,399)，1392 px |
| `…/runE/E04.png` | `469e9c45772520e39d49f9989c6b60bd79ab702381890d6d4a9aeee935b03636` | 按一次 Left 之后：手形指针**该帧未绘制**（纯色 231,178,99 计数为 0） |
| `…/runE/E05.png` | `102dbda2efa796acc1417409afad8ed0f3bb4c068a740bc0738db9d7a9e6bb6c` | 连续 Left 之后 |
| `…/runE/E06.png` | `130ce057a7dc71f7f90e12a00e084f7696e71671ed72176d0cbec4eca8221d93` | 连续 Left 之后 |
| `…/runE/E07.png` | `e0b2ab8e3ff4a7d308ec8f291be8c5d282dbaa0d9dc13a73f1dc87caffcdcaa0` | 6 次 Left + Fire 之后：手形指针 bbox (228,324)-(303,399)（指针确实在移动，精灵只是**间歇绘制**；地图相机跟随会抵消大部分位移） |
| `…/runE/runtime.log` | `cc3335180f2f87d902459925746ea27bd844e8ef457d173b516a89e2b3320786` | runE 运行日志（异常与 runD 完全相同） |

### 6. 建造复现（runF，当前构建 `e8459c13…` + `runF/rms` = `runC/rms` 的副本）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/runF/manifest.txt` | `d2171f72bd7bca3bcc1c3c4787282899645b99fe5b3ff831ded54ef060af459c` | 二进制/JAR/存档副本哈希 + 时间戳（2026-10-22 07:52:40 CST） |
| `…/runF/F08.png` | `b442c5c6e1c2885f3b762d22b58fb6e317cfd244b7a2912533dc736b7bda2c56` | 用存档副本启动后**恢复的村庄**（第 29 天、人口 3、城堡 + 农舍 + 伐木场 + 水井 + 采石场 + 养鱼场） |
| `…/runF/F09.png` | `c2ad59d985dad986acb0eb1fff88554f3d3db61a5745eb36d1be9b582ab9db37` | 空地上按 Fire → 行动菜单（底部羊皮纸标签"行动菜单"） |
| `…/runF/F10.png` | `20d2dd800c431aa5645b2c2fe8dbb908d37a8fd7b99f7adb45039400fe893b15` | Up → **建造菜单卡片**（两座房屋插画 + 3 星 + 绿色造价按钮"3" + 标签"住房" + 游戏自带 ✓/✗ 软键） |
| `…/runF/F11.png` | `20d2dd800c431aa5645b2c2fe8dbb908d37a8fd7b99f7adb45039400fe893b15` | 与 F10 **逐字节相同** ⇒ F1（左软键 ✓）在建造菜单里是 no-op |
| `…/runF/F12.png` | `d226680c006ccb135c254f192950395f0482ab4a535144ff16bd09f9d70a74dd` | 建造菜单里按 Fire → 打开的是**当前指针格子上建筑的**信息面板（床位图标 + "Z 0"），即 Fire = 地图格子命中测试 |
| `…/runF/P1.png` | `39c0ac694c68b0d661a2badc83571f04ebfefd6b1dec1899f2bc97fb373fbfab` | 行动菜单 **4 个图标完整绘制**（上=建造/黄色▲，左=住房/蓝色红顶/◀，右=粮仓麻袋/▶，下=金色穹顶/▼） |
| `…/runF/P3.png` | `741d735bfe23b82e27cf86450030224c48e646de53ca06fe8d68e119cb112dea` | 放置模式：指针落在矿石上 → 地块为**红色**（不可建） |
| `…/runF/P5.png` | `c4c91fdea6e9aa401091a397396f6d5760e3dcf74f570623cebe83083dc42fca` | 放置模式：指针移到草地 → 地块为**绿色可建区**（3592 px，bbox (552,732)-(902,902)），底部标签"住房" |
| `…/runF/P6.png` | `3a340c6470dda1d98804a8f8c7442e2a0312039ebc093b36899bba770205f401` | **建造落地**：原绿色格位置出现木质脚手架工地（对比 P5 该处为空白草地，区域差异 18400 px），并弹出新建筑信息面板（人脸 + 空工人槽 + ✂/⬛/✓），底部标签"建筑工人" |
| `…/runF/F15.png` | `4ecd4c1d2b941600f56cea1457c873a8a54517a0e69351ba977303326a9a67e8` | 建造菜单里按数字 5 → 回到地图（可见粉色"女村民 ×1"气泡与城堡上的 "!!" 提醒标记） |
| `…/runF/F16.png` | `ced43ed291a0e8e75d494ce39dd10e81a27b52053fe2fcf6a7f334a6926a7d2d` | 房屋面板：绿色"2"（男）+ 粉色"1"（女）人口指示，手形指针停在 ✓ 上 |
| `…/runF/runtime.log` | `aafa6ef6e8c85445819117f404c3438947d3577cc8b796b2ced9e236a2264933` | runF（当前构建）完整运行日志，异常见"异常"章 |

### 7. 自由模式与菜单键位（runG，当前构建 + `runG/rms` = `runC/rms` 的副本）

| 绝对路径 | SHA-256 | 内容 |
|---|---|---|
| `…/runG/manifest.txt` | `cc71b3d37bdde4539e45a9bf7f9f4d099f895b347182486a281be92209fb84de` | 二进制/JAR/存档副本哈希 + 时间戳（2026-10-22 08:12:23 CST） |
| `…/runG/G2.png` | `5239bb65bfeeda270ae5d28a817b67148a0be8a38679dd02431f3a483900fffd` | 主菜单（开始游戏 / 选项设置 / 帮助指南 / 制作信息 / 退出游戏，选中项为红色且左侧有 ▶） |
| `…/runG/M3.png` | `ad3e0e0a06baad0cad8c6b01ea9f6a13a0b4d9a42f48381955c04802ba40a6f3` | **模式菜单**：剧情战役（红色选中）/ 自由模式 |
| `…/runG/P3.png` | `c0c2ea9291b5af59f15766d66e7150e929eca470141fcc7acb82b9b9fd755d1a` | **自由模式设置页**：地图尺寸 / 水域面积 / 敌人数量 三条滑块（每条左右各有一个触摸型 ◀ / ▶ 图标） |
| `…/runG/Q_left.png` | `5d4b3be0afd2cbf2fc280a5c7fd8f67f734ab0147b709f3c97b599e17b941855` | **自由模式村庄地图**（水域、树林、城堡 + 3 星建筑信息卡） |
| `…/runG/Q_space.png` | `2543d69503ed9bbc7886264ddc8947345de8be1738cf8c9474e4bd1ffe8aef42` | 自由模式空地上按 Fire → 行动菜单 + **绿色可建格 + 红色不可建格**（压树）+ 手形指针 |
| `…/runG/S1.png` / `…/runG/S2.png` | `8da7decb2069bb1a5dbf82d1415dcd0a1175d82b62a3d035f24ebf865035f996`（两者相同） | 在**红色（非法）格**上按 Fire 前后逐字节相同 ⇒ 非法落点被拒绝 |
| `…/runG/T7.png` | `e2cd9adaae6a7aa740cfaf2001418a9d308002754a62f9c4899d8373c9c7af33` | 自由模式放置提示的红/绿格细节（放大裁切见下） |
| `…/runG/runtime.log` | `36c37a05163dcc89b3f48a71c35fce438db9eacfc79aecc9868ed4898af7d956` | runG（当前构建）完整运行日志，异常见"异常"章 |

### 8. 反编译证据（触摸被忽略 + "派遣"按钮不可达的证明）

```text
$ javap -c -p -classpath /tmp/townsmen-inspect/jarx t
  public void pointerPressed(int,int);      0: return
  public void pointerReleased(int,int);     0: return
  public void pointerDragged(int,int,int);  0: return
```

`class t` 是全 JAR 唯一的游戏 Canvas（`paint` → `hg.HG.a(Graphics)`，`keyPressed` → `hg.HG.a(int,true)`）；广告 Canvas `AdvertTorpedoLi` 也没有指针处理。

#### 8.1 "派遣"按钮为什么在纯键盘下不可达

全量 `javap`（70936 行，落在 `/tmp/townsmen-inspect/ALL.txt`）给出的事实链：

```text
1) 触摸入口只有一个：
   class t (extends javax.microedition.lcdui.Canvas)
     protected void pointerPressed(int,int);      0: return
     protected void pointerReleased(int,int);     0: return
     protected void pointerDragged(int,int,int);  0: return
   ⇒ 没有任何指针事件能进入游戏逻辑。

2) 键盘确认路径（hg/HG.a(int,boolean) 的 case 6/7，javap 行 ~50513-50581）：
     getstatic  a:Ls; invokeinterface s.k:()I; invokeinterface s.l:()I;
     invokeinterface s.a:(II)Z; ifeq ...; invokeinterface s.c:()Z
   ⇒ 键盘"确认"= 用当前指针的**格子坐标**做命中测试并激活，**不是**屏幕像素级控件命中。

3) 界面 s 只有两个实现者：
   final class r implements s   → 村庄界面
   final class v implements s   → 法国战役地图界面（字段 g/h = 指针所在网格行列）

4) 战役地图的命中测试 v.a(int,int)Z 只做网格判定：
     putfield #45 (g:I); putfield #46 (h:I);
     … 与 j.a.u.a / j.a.u.b（地图宽高）做范围检查
   ⇒ 战役地图上**不存在任何"派遣"按钮的像素矩形命中测试**；
      v.c() 只是按该网格单元的地区类型（16/80/96/112/128）做查表分支。

5) 地区面板（"红军士兵 / 国王士兵 / 完成条件 / 胜利条件 / 部队 / 派遣"）由类 o 构建：
     o."<init>"();  o.b(Ls;)V;  o.a(Ljava/lang/String;IIIII)V（带矩形区域的文字项）;
     o.c(I)V; o.k()V; o.a(Lo)V; 以及 private static boolean a(int,int)（点-矩形命中）
   其中 o.a(int,int) 只被 o.p() 调用 4 次 —— 这是**纯触摸**路径。

6) 全 JAR 中 s.a:(II)Z 的调用点只有 2 处：
     javap 行 47686（class a 的绘制路径 = 触摸命中）
     javap 行 50545（键盘 handler）
   ⇒ "派遣"按钮（类 o 的控件）只能经由 1) 的触摸路径被点击，而该路径是空实现。
```

**结论：战斗入口"派遣"是触摸专用控件，在本 JAR 上不存在键盘可达路径**（不是"没找到按键"，而是"代码里没有键盘分支"）。

---

## 异常

三次会话的全部异常（runC 13 条、runD 4 条、runE 4 条），**全部为 `thrown`（游戏内部捕获并继续）**；
**无 uncaught 异常，无 `MediaException`（IMA ADPCM 修复仍然有效），退出码 0**。

runC `runC/runtime.log` 逐条原文：

```text
INFO: thrown: java/lang/NullPointerException at g.a()V pc=34: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: java/io/IOException at javax/microedition/lcdui/Image.createImage(Ljava/lang/String;)Ljavax/microedition/lcdui/Image; pc=0: /s3.png
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang.String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang.String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang.String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
```

runD `runD/runtime.log`（HEAD 干净构建）逐条原文：

```text
INFO: thrown: java/lang/NullPointerException at g.a()V pc=34: (no message)
INFO: thrown: java/io/IOException at javax/microedition/lcdui/Image.createImage(Ljava/lang/String;)Ljavax/microedition/lcdui/Image; pc=0: /s3.png
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
```

runE `runE/runtime.log`（HEAD 构建，第三次会话）逐条原文（与 runD 完全相同）：

```text
INFO: thrown: java/lang/NullPointerException at g.a()V pc=34: (no message)
INFO: thrown: java/io/IOException at javax/microedition/lcdui/Image.createImage(Ljava/lang/String;)Ljavax/microedition/lcdui/Image; pc=0: /s3.png
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
```

runF `runF/runtime.log`（**当前构建 `e8459c13…`**，第五次会话）逐条原文（4 条，全部 `thrown`）：

```text
INFO: thrown: java/lang/NullPointerException at g.a()V pc=34: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
```

```text
runF: INFO: stats: frames=108746 paints=62734 gfx=7929329 tris=0 m3g=0 time=2382.085s fps=45.7 bytecodes=2744132227 (1.15 M/s) natives=16166690 heap=12949392/12956768 bytes cpcache=344464662/1627 findcache=0/0
      INFO: teardown: calling destroyApp() / INFO: teardown: destroyApp() returned
      nano_j2me: exit status 0
```

runG `runG/runtime.log`（**当前构建 `e8459c13…`**，第四次会话）逐条原文：

```text
INFO: thrown: java/lang/NullPointerException at g.a()V pc=34: (no message)
INFO: thrown: java/io/IOException at javax/microedition/lcdui/Image.createImage(Ljava/lang/String;)Ljavax/microedition/lcdui/Image; pc=0: /s3.png
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at g.a(I)Z pc=46: (no message)
```

runG 会话收尾：

```text
WARN: run ended: the frame budget ran out (exit code 0)
INFO: stats: frames=36461 paints=21534 gfx=2242438 tris=0 m3g=0 time=810.465s fps=45.0 bytecodes=691114320 (0.85 M/s) natives=5603753 heap=6187440/6193984 bytes cpcache=83951062/1721 findcache=0/0
INFO: observed: threads=8 gc=15 tones=0 pcm_frames=0 file_opens=0 file_writes=0 file_reads=0 bytes_written=0
INFO: teardown: calling destroyApp()
INFO: teardown: destroyApp() returned
INFO: released 3322 host blocks at teardown
nano_j2me: exit status 0
```

会话收尾：

```text
runC: WARN: run ended: the frame budget ran out (exit code 0)
       INFO: stats: frames=174269 paints=109389 gfx=16776971 tris=0 m3g=0 time=4005.721s fps=43.5 bytecodes=4838224141 (1.21 M/s) natives=30261854 heap=3390928/16777168 bytes cpcache=530806003/1876 findcache=0/0
       INFO: observed: threads=8 gc=16 tones=0 pcm_frames=0 file_opens=0 file_writes=0 file_reads=0 bytes_written=0
       INFO: teardown: calling destroyApp() / INFO: teardown: destroyApp() returned
       INFO: released 3331 host blocks at teardown
       nano_j2me: exit status 0

runD: WARN: run ended: the frame budget ran out (exit code 0)
       INFO: stats: frames=105525 paints=62643 gfx=8030032 tris=0 m3g=0 time=2350.137s fps=44.9 bytecodes=2832476613 (1.21 M/s) natives=15859978 heap=14016688/14025824 bytes cpcache=330527340/1601 findcache=0/0
       INFO: observed: threads=6 gc=9 tones=0 pcm_frames=0 file_opens=0 file_writes=0 file_reads=0 bytes_written=0
       INFO: teardown: calling destroyApp() / INFO: teardown: destroyApp() returned
       INFO: released 3320 host blocks at teardown
       nano_j2me: exit status 0
```

### 异常归因

| 异常 | 归因 | 是否模拟器缺陷 |
|---|---|---|
| `RecordStoreNotFoundException` (openRecordStore) | 游戏在没有存档时先读后写，属正常首启路径 | 否（游戏已捕获） |
| `NullPointerException at g.a()V pc=34` / `g.a(I)Z pc=46` | 记录库读写辅助类 `g` 在空记录上的返回值被解引用 | 否（游戏已捕获） |
| `IOException at Image.createImage … /s3.png` | **JAR 本身就没有 s3.png**（`unzip -l` 只含 `s0.png`/`s1.png`/`s2.png`/`icon.png`/`torpedoAdv/adv.png`），是广告启动图缺失，与模拟器无关 | 否（游戏已捕获） |

---

## 兼容状态

| 功能 | 状态 | 说明 |
|---|---|---|
| 启动 / 语言 / 声音 / 主菜单 / 战役地图 / 村庄 | **works** | 全键盘可达 |
| 村庄信息面板、城镇中心面板（人口/幸福/资源/工人槽位） | **works** | 指针 + 确认键可打开、可浏览 |
| 帮助页 / 按键控制说明 / 帮助指南 | **works** | F1 打开上下文帮助；暂停菜单 → 帮助指南 → FIRE 打开各页 |
| 教程流程（含村民/建筑选择） | **works**（runC 二进制） | 教程可逐页推进，目标链一路推进到"人口膨胀" |
| 建造（放置模式 + 落地） | **works**（当前构建 `e8459c13…`） | runF 用存档副本完整复现：行动菜单 4 图标 → 建造菜单卡片 → 放置模式绿/红地块 → Fire 落地（`P1/F10/P5/P6`，新建筑带信息面板）。runC 在途二进制还额外建成 住房×3 / 采石场 / 水井 / 养鱼场。**唯一例外**：HEAD 干净构建 `9f6325a7…`（runD/runE）上图标、影子、地块高亮都不渲染，放置模式无可见反馈 |
| 自由模式（自由建造，含"敌人数量"配置） | **partially works** | 键盘可达：模式菜单 → 自由模式 → 设置页（地图尺寸/水域面积/敌人数量）→ 自由村庄地图，放置提示（绿/红格）同样出现（runG `P3/Q_left/Q_space`）；但自由模式内的兵营/士兵/敌军链路未走通 |
| 存档 / 读档 | **works** | 暂停菜单 → 关闭游戏 → 是 → 写入 `Townsmen6*.rms`；同 `--rms-dir` 重启后完整恢复村庄 |
| 触摸输入 | **blocked（游戏侧）** | `t.pointerPressed/Released/Dragged` 全为空实现，游戏根本不处理触摸事件；模拟器无需修改 |
| 战斗（战役地图派遣部队进入战斗） | **blocked** | 见下 |

### 战斗被阻塞的确切原因

1. **战斗入口是纯触摸控件**。剧情战役法国地图上，选中地区后右下角弹出地区面板：
   `红军士兵 5 / 国王士兵 3 / 完成条件:人口 500 / 胜利条件:歼灭敌军… / 部队 3 / [派遣]`。
   "派遣"按钮只能用触摸点按，而该 JAR 的 Canvas 不处理任何指针事件（`javap` 证据见上）。
   键盘穷举结果：对该面板依次发送 Fire、F1（→ 打开上下文帮助页）、数字键 5、Up、Right，
   面板区域在 `D2…D6` 五张截图中**逐像素一致**，没有任何变化；方向键在地图上也不产生任何可见滚动（`G0/G1` 连续 15×Right + 38×Down 前后画面无差异）。
   注：手形指针精灵在部分帧**间歇绘制**（runE/E04 该帧纯色 231,178,99 计数为 0），且地图相机会跟随指针，
   所以指针位移在截图上大部分被抵消（runE：E03 手形 bbox x=236 → 6 次 Left 后 E07 x=228，净 −8 px）——
   即"指针不动"是**观测误差**而不是"方向键失效"。
2. **教程关卡没有敌军**。`CN.CAMPAIGN.lng` 明确写道：
   "剧情战役模式中，革命以与国王军队作战为线索…轰开巴黎大门，取得国王头颅"；
   法国地图上带王冠的区域才是国王领地（可进攻），金星为已占领、银星为待占领；
   "每关结束后最多可有 3 名士兵加入到列表中"；"行动菜单中，完成任务可获得行动数，使用行动数可以调遣军队"。
   而第一关（家园概述）的目标链是 `翠林满目 → 蓬门荜户 → 安居乐业 → 饥餐渴饮 → 辘辘饥肠 → 人口膨胀 → 家有小猪 → 猪肉宴会`，**没有任何战斗目标**。
   → 士兵与行动数必须先把教程关剩余目标做完（当前停在"人口膨胀"）才能获得。
3. **村庄里造不出兵营**。建造菜单的建筑种类由当前关卡目标固定（实测 Up/Down/Left/Right 均不改变），因此在第一关里无法建造军营/兵营，也就无法在村庄里生成士兵。
4. 城镇中心面板只有工人槽位与人口/幸福指数（`E2.png`），没有"派遣士兵"行；游戏提示里"点击此建筑，选择士兵或演说家人数…派遣最多 5 个单位前往作战"指的是**战役地图上的地区面板**，不是村庄建筑。

   → 因此"派遣"按钮在纯键盘下不可达（这是**代码层面的证明**，不是"没找到按键"）。
5. **自由模式是唯一存在敌军的玩法入口**（runG 已验证可达）：自由模式设置页里就有"敌人数量"滑块。理论上若能在自由模式里建兵营、造士兵，再把士兵派往地图上的敌军，就能进入战斗；但本轮未走通该链路（自由模式设置页的 `◀/▶` 滑块按钮本身也是触摸控件，见"边界与未验证项"）。

**结论：战斗阶段在本 JAR + nano_j2me + 纯键盘条件下无法进入，卡点是游戏本身只支持触摸派遣（代码层证明，见 8.1）+ 需要先完成教程关以获得士兵/行动数。若后续需要突破，方向是（a）用触摸注入无法解决（游戏不处理指针），（b）在**自由模式**里配置敌军并把兵营/士兵链路走通（该模式的"敌人数量"是唯一的敌军来源），或（c）在剧情模式里先完成教程关剩余目标拿到士兵与行动数，再在战役地图上用键盘找到派遣按钮的可达路径。**

---

## 边界与未验证项

1. **仅验证了 N73 240×320 变体**。其余 7 个分辨率变体（A1000 208×320、A760 240×320、K700 176×220、K790 240×320、E60 320×240、E62 320×240、N70 176×208）**本轮未运行**；由于同一套代码逻辑只有分辨率不同，预计触摸被忽略的结论同样成立，但**未经实测**。
2. **建造流程的二进制敏感性**（已澄清）：建造在 **runC 的在途二进制 `2f106198…`**（带 SUPERINSTR/JUMP_THREADING，已被其他会话从磁盘删除，无法再做 A/B 对比）与 **当前构建 `e8459c13…`（runF）** 上都能完成；只有 **HEAD 干净构建 `9f6325a7…`（runD/runE）** 上"图标 / 影子 / 地块高亮"不渲染、Fire 无效。
   即：HEAD 构建上的"未复现"是**渲染差异**（可能是那一次构建本身的绘制回退），不能推广为"nano_j2me 不支持建造"。
   若要彻底定位，需要在 HEAD 干净构建上重跑一次完整教程并对比同一存档。
3. **战斗未验证**（阻塞原因见上），没有任何战斗画面证据。
4. **runE 会话**（HEAD 构建 + 同一 `--rms-dir` 重新读档，用于重做建造）读档成功并测量了手形指针的位移，
   但**没有再次打开建造菜单**（会话为释放资源而主动结束，退出码 0），因此 HEAD 构建上的"建造"流程仍属**未复现**；
   其截图与日志仅作过程记录（见证据 5）。
5. **自由模式（自由建造）只验证到"可达 + 有敌军配置项"**（runG）：模式菜单里 `自由模式` 可以进入，随后是"地图尺寸 / 水域面积 / 敌人数量"设置页，再进入自由模式村庄地图，放置提示（绿/红格）与剧情模式一致。
   **未验证**：① 自由模式是否放开建筑种类限制（兵营能否建）；② "敌人数量"滑块只能用触摸 `◀ / ▶` 调整，键盘是否可改未验证（`Q_left`/`Q_space` 期间该滑块区域未响应任何按键）；③ 自由模式里敌军是否真的出现、能否交战；④ 自由模式的存档/读档。
6. **其它未验证项**：帮助指南的"游戏规则/资源列表"未逐页读完；战役地图上其它地区（带王冠的国王领地）未被逐个点开；存档只验证了"村庄状态"这一条记录能正确回读，多个并存记录（`Townsmen60/61/62`）在重启后的选择逻辑未验证。
7. **未修改任何源码**。本轮为纯验证任务：`src/`、`include/`、`tests/`、`CMakeLists.txt` 一行未改；未执行任何 git 写操作；原始 JAR 未修改（SHA-256 前后一致）；存档全部写在自建的 `runC/rms` 目录内，未触碰任何既有用户存档。

---

## 附：模拟器侧可能需要的改进（仅描述，未实施）

1. `src/port/posix/nj_port_sdl2.c` 的键盘映射与游戏的 keymap 表一致（MIDP `UP=-1/DOWN=-2/LEFT=-3/RIGHT=-4/FIRE=-5/SOFT1=-6/SOFT2=-7`、`STAR=42`、`POUND=35`、数字 48-57 与 `hg/HG` 的 `b:int[][]` 表逐项吻合），**不需要**改键码映射。
2. 若要让这类"只支持触摸的老游戏"可玩，需要模拟器侧补齐"指针 → 键盘"的无障碍映射（例如把 pointerPressed 派发成方向键+确认键序列），但这属于产品需求而非缺陷修复；本 JAR 本身**不应**被判为"触摸不兼容缺陷"。
3. 建议在下载语料表里给该 JAR 标注：`touch: unsupported (Canvas pointer handlers are empty) / input: keyboard-only (D-pad moves a pointer, FIRE/F1 confirms)`，避免后续继续用虚拟触摸做无效尝试。
