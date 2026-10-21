# 语料库缺口巡检（2026-10-22）

对共享语料表里 8 个条目做一次"实际到达阶段"的复检：jar-02、jar-04、jar-07、jar-08、jar-09、jar-11/jar-15、jar-25、jar-28。复检的目的不是重复已有结论，而是把每个条目的**阶段、截图、异常文本、兼容状态、复现命令**补齐，并把"没做到"的边界说清楚。**全部关键取证使用 HEAD 干净构建**，工作区在途改动（superinstr / jump threading）不污染结论。

**摘要**：jar-02 首次进入城建玩法并出现"购买土地"对话（比共享表记录的"首块落地"更远）；jar-04 首次在同一次会话里连续打通标题→剧情→难度→教程→**等距地图**（资源条 100/100/100 + 村庄）；jar-07 仍然在第一帧就退出，日志里多出一条应用自己的提示「授权过期」；jar-08 渲染到标题/角色场景而不是裸闪屏，但 21 次 HTTP 取回失败，MMO 服务器不可用；jar-09 的桌面拼音输入法把「你好」提交进 MIDP 文本框（与 2026-10-17 的证据逐字节一致）；jar-11/15 到达第一章场景且游戏确实写 RMS，但**重启不续档**，且游戏自己抛 `InvalidRecordIDException`；jar-25 越过开机音效提示到达主菜单与「选择赛事」世界地图；jar-28 到达任务对话后画面不再变化。

## 环境与命令

| 项目 | 值 |
|---|---|
| 二进制 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/head-build-20261022/build/nj2me`，SHA-256 `9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813`（HEAD 干净构建，Lead 提供） |
| 对照（工作区构建，早期取证） | `/home/lucinhu/workspace/c/nano_j2me/build/nj2me`，SHA-256 `2849376f1b64e5015bb7421a0932e297f9757a73b4e74447819b67341bdf3991`；Lead 已记录该构建受在途解释器快路径影响，见 [superinstr 回归说明](</home/lucinhu/workspace/c/nano_j2me/docs/notes/2026-10-22-superinstr-regression.md>)（只读） |
| 驱动 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/drive2.py`（在 `corpus-round20-20261021/drive.py` 基础上加 `--device`、`--net off|netfail|proxy503|real`、`--rms`、`--binary`，并把 `rms_files` 的 SHA-256 记进 manifest） |
| 输入法驱动 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/run_xy_ime.py`（从 `uc-ime-20261021/run_xy_ime_regress2.py` 复制并改造：输出改到自己目录、用 `fcitx5-remote -o` 强制 pinyin、记录焦点窗口 ID） |
| 显示 | `DISPLAY=:1`（Xwayland），键盘可用，XTEST 指针在本机失效（所以全部交互只用键盘） |
| RMS | 每个条目一个新 RMS 目录，全部在 `.local-tools/debug-runs/corpus-gap-20261022/<run>/rms`；未触碰任何既有用户存档；原 JAR 运行前后 SHA-256 一致 |

| 条目 | JAR | SHA-256 | 字节 | 尺寸 |
|---|---|---|---|---|
| jar-02 City Bloxx | `/home/lucinhu/Downloads/jar/app(1).jar` | `6979335c9b19d510965ad67160a6786a367bb9e2a6997088e434b77c2849b280` | 169285 | 240×320（`--device nokia6230`） |
| jar-04 帝国时代3 | `/home/lucinhu/Downloads/jar/app(3).jar` | `a6675ab8fb3674950e994a1d995738cc57664bb18b261ff316aa918c5f4244f7` | 278149 | 240×320 |
| jar-07 乱世Q三国 | `/home/lucinhu/Downloads/jar/乱世Q三国.jar` | `754fa4a5a9aff6e683e0d25f02da35a96b11492bd5cb69100fb90591fb908988` | 1001327 | 240×320 |
| jar-08 仙镜OL | `/home/lucinhu/Downloads/jar/仙镜OL-羊羊得意_诺基亚 N97系列(360×640)-1121281.jar` | `a9b1d88f87cf9efc0df5c940272c2b74b561143c4bbb01585a9a0e3524f6fce3` | 1121281 | 360×640 |
| jar-09 侠义OL | `/home/lucinhu/Downloads/jar/侠义OL-八龙聚首_诺基亚 N97系列(360×640)-871204.jar` | `bfa87c2046aa0d2b3847521ffdd34baf27a3de252b07031830d3b966eb2e66d2` | 871204 | 360×640 |
| jar-11 / jar-15 孤岛余生2 中文版 | `/home/lucinhu/Downloads/jar/孤岛余生2时间之谜汉化版.jar` | `ffe21f751ac64fbc75404a8ea0a003238845067b39e76d9cba9062a5bbb16607` | 631579 | 240×320 |
| jar-25 极品飞车14 | `/home/lucinhu/Downloads/jar/极品飞车14之热力追踪中文版.jar` | `77d10a56e51ae65c9b081ea1d192260fbb30c7c44c78cfe31cf221c79fe6cc52` | 1249241 | 240×320 |
| jar-28 远古帝国2汉化版 | `/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar` | `778b6e881b556e224de19d898cb65bc38c8bb2c910ec0deb04bc2d7713d66393` | 172168 | 240×320 |

## 复现步骤

通用骨架（每个条目只改 JAR / 尺寸 / 计划 / 设备）：

```bash
cd /home/lucinhu/workspace/c/nano_j2me
export DISPLAY=:1
G=$PWD/.local-tools/debug-runs/corpus-gap-20261022
H=$PWD/.local-tools/head-build-20261022/build/nj2me

python3 $G/drive2.py --jar "/home/lucinhu/Downloads/jar/app(3).jar" --size 240x320 \
  --frames 6000 --net off --binary "$H" --out $G/head-jar04b-aoe3 \
  --plan "$(cat $G/plan-jar04b.json)"
```

各条目的具体参数：

| 条目 | 额外参数 | 计划文件 | 运行目录 |
|---|---|---|---|
| jar-02 | `--device nokia6230` | `plan-jar02.json` | `head-jar02-citybloxx` |
| jar-04 | — | `plan-jar04b.json` | `head-jar04b-aoe3` |
| jar-07 | — | `plan-jar07.json` | `head-jar07-q3` |
| jar-08 | `--net netfail --size 360x640` | `plan-jar08.json` | `head-jar08-xianjing` |
| jar-09 | `NJ_BIN=$H python3 $G/run_xy_ime.py head-jar09-ime3` | 固定时间轴 | `head-jar09-ime3` |
| jar-11 | — / 续档 `--rms $G/head-jar11-cn/rms` | `plan-jar11c.json` / `plan-jar11b.json` | `head-jar11-cn`、`head-jar11-restart` |
| jar-25 | — | `plan-jar25c.json` | `head-jar25c-nfs14` |
| jar-28 | — | `plan-jar28.json` | `head-jar28-ancient` |

## 证据

### jar-02 City Bloxx（`app(1).jar`，`--device nokia6230`）

实际到达阶段：**主菜单 → Quick game → 塔吊吊着方块连续下落的玩法循环（计划里 7 次落块操作，画面出现 5 个不同状态）→ F2 暂停 / 继续 → 「You've bought yourself a piece of land …」购买土地对话 + 城市规划界面**。

| 截图 | SHA-256 |
|---|---|
| [01-menu](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar02-citybloxx/01-menu.png>)（Build city / Quick game / High scores / Instructions） | `f3d8d940d64c59cce149e371a01e3e29f35f6eb6878471439a3e180ad991eb6f` |
| [04-drop-0](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar02-citybloxx/04-drop-0.png>)（塔吊 + 方块） | `4157807c20e7ad45efcdfdce8688d59943cb8b93c69269a50fe85b99bd274856` |
| [12-drop-6](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar02-citybloxx/12-drop-6.png>) | `6f8dba1a925e6984b0cdde7ad953c8a18dd7f3e012efd48567194dc509f023db` |
| [15-after-continue2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar02-citybloxx/15-after-continue2.png>)（购买土地对话 + 土地放置界面） | `b4d203a56a65dbd6f13e325b1b4f5c159f5da5a2a38a4c44333c99f3273ea064` |

接触表：[cs-jar02.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/cs-jar02.png>)（SHA-256 `9442660de1df135617ffb8f8f74f6732646c3da2ab990fd702b41dc74d18ea90`）。运行 2400 帧 / 79.854 s / 30.1 fps，退出码 0。

**机型白名单对照（不带 `--device nokia6230`）**：目录 `head-jar02-nodevice`，第 0 帧退出，**没有窗口也没有截图**；[runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar02-nodevice/runtime.log>)：

```
INFO: running House (City Bloxx 1.0.12)
INFO: thrown: java/lang/RuntimeException at com/nokia/mid/appl/bloxx/a.a(I[Ljava/lang/String;)Ljava/lang/String; pc=68: getResourceAsStream
INFO: thrown: java/lang/NullPointerException at javax/microedition/lcdui/Command.<init>(Ljava/lang/String;II)V pc=0: label is null
INFO: thrown: java/lang/NullPointerException at g.b(I)Ljava/io/DataInputStream; pc=66: (no message)
INFO: thrown: java/lang/NullPointerException at g.e(I)I pc=29: (no message)
INFO: thrown: java/lang/NullPointerException at g.c(I)Ljavax/microedition/lcdui/Image; pc=16: (no message)
INFO: thrown: java/lang/RuntimeException at o.a(IZ)V pc=15: containsKey
INFO: run ended: the MIDlet called System.exit()
```

⇒ 白名单结论得到直接证据：应用按机型查资源表，非 Nokia 6230 时 `getResourceAsStream` 失败并 `System.exit()`。**这是应用侧行为，`--device nokia6230` 是必须项。**

### jar-04 帝国时代3（`app(3).jar`）

实际到达阶段：**Glu 启动画面 → 「帝国时代3（简体中文）」标题 → 剧情/简报文本 → 难度菜单（普通 / 对战）→ 教程文本 → 等距地图（顶部资源条 100 / 100 / 100、村庄与建筑、右下角提示条）**。

| 截图 | SHA-256 |
|---|---|
| [01-boot](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/01-boot.png>)（Glu 标志） | `708cc62b69c60a24f8003c8f6828e0440e4d311f0def26c6731668b7456a4161` |
| [03-after-soft1](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/03-after-soft1.png>)（标题「帝国时代3 简体中文」） | `7696ff50a2027fcb84d443c2dc5411a445d67da8e1aa40ff420c376462505b5a` |
| [07-after-difficulty](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/07-after-difficulty.png>)（难度菜单 普通/对战） | `2269c0d0da3c1b25b3de5c905ddc80089ac31e6228b44c3eef8a420f90ebcb35` |
| [14-fire3](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/14-fire3.png>)（教程鼠标说明） | `180bfbba4d8cfd5bd9dce50fd3a7f6cd9129ec1ee12f553d75c08b224f41069a` |
| [19-right](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/19-right.png>)（**等距地图** + 资源条 100/100/100） | `d11b54b121d4805b01e029fcee2c9645002ebee45eb5a3b77fc87750484dac4a` |
| [20-num5](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar04b-aoe3/20-num5.png>)（地图 + 将军头像 + 右下提示） | `3deb61de2b2755029513bd5efb31e48bdc7a71c80b63407ff2c874c70b7a0bc6` |

接触表：[cs-jar04b.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/cs-jar04b.png>)（SHA-256 `20f651de8b5cbc2cf527d1404a769d88b5fe633ffcb13a1d7968617159b5eb52`）。运行 6000 帧 / 198.960 s / 30.2 fps，退出码 0。

> 关键差异：只按到「将军对话」就停下时（早一轮 `head-jar04-aoe3`）到不了地图；**再补两次 fire**（`plan-jar04b.json` 的 13/14 步）后才进入教程与地图。共享表里的「固定时序脚本会错位」结论在本轮依然成立。

### jar-07 乱世Q三国

实际到达阶段：**第 0 帧退出**。MIDlet 打印自己的提示「授权过期」后调用 `notifyDestroyed()`，模拟器正常收尾（退出码 0），没有窗口、没有截图。

```
INFO: running engineModule.MainMIDlet (乱世Q三国 1.0)
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: 授权过期
WARN: run ended: the MIDlet called notifyDestroyed() (exit code 0)
INFO: stats: frames=0 paints=0 gfx=0 tris=0 m3g=0 time=0.014s fps=0.0 bytecodes=132 (0.01 M/s) natives=39 heap=3760/3760 bytes
```

日志：[head-jar07-q3/runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar07-q3/runtime.log>)；manifest（无截图条目）：[head-jar07-q3/manifest.json](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar07-q3/manifest.json>)。

### jar-08 仙镜OL（360×640，`--net netfail`）

实际到达阶段：**标题 / 角色场景 + 消息框**（羊羊得意标题logo、角色动画、山林与火焰场景，第一帧还有一个提示框），不是裸闪屏；但每次网络取回都失败，无法进入游戏。

| 截图 | SHA-256 |
|---|---|
| [01-start](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar08-xianjing/01-start.png>)（标题场景 + 提示框） | `4a0472ae4df625dea5c87b4c8d13219d87e00e2d0fc00e413355fc8df5e1ef43` |
| [05-after-soft1](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar08-xianjing/05-after-soft1.png>) | `dccd3e84efcd6827c0f96632f8e0dc427fe3c7a8b086af8d1c41e426a0872b31` |
| [06-after-fire3](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar08-xianjing/06-after-fire3.png>) | `790326f6cc9fe8b402c572159ecdef5f0305787b2b96ac7305a340adeb6bb833` |

接触表：[cs-jar08.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/cs-jar08.png>)（SHA-256 `3005dad1549cf402ff23913b79fad173583deaea8dac9f4ad824e5a07536a292`）。运行 1200 帧 / 40.093 s / 29.9 fps，退出码 0。RMS 写出 31 个数据仓库（`XJB2.rms`、`ingame.rms`、`task.rms`…），内容大多为空模板。

### jar-09 侠义OL（360×640）

实际到达阶段：**注册页（游戏账号 / 游戏密码 / 真实姓名 / 用户密码或提示问题 / 玩家昵称）→ Return 打开 MIDP 文本编辑器 → 桌面 fcitx5 拼音输入 → 提交「你好」进入文本框**。

| 截图 | SHA-256 |
|---|---|
| [01-start](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar09-ime3/01-start.png>)（启动） | `77b6170910c504fd0c527620dfdd2b81dc15b19ad83f8d0990a6b2333c5edbfa` |
| [03-editor](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar09-ime3/03-editor.png>)（注册页） | `33cb1d260b901cfc9a78221ee7276738d2c3fac674e518039fc81c7450c12c6e` |
| [04-ime-on](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar09-ime3/04-ime-on.png>)（MIDP 编辑器打开，尚未输入） | `aced73f8033802610ee97a5ca7b5a79574f087cf5cf7b088c525291ea0322c83` |
| [06-committed-1](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar09-ime3/06-committed-1.png>)（**「你好」已提交进文本框**） | `b5ccef81115db43326d169368e36540258e1518844c2e1c8c80709e442e7e694` |
| [08-final](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar09-ime3/08-final.png>) | `c2fb86875cf79e84223be19b811d35e5133443423e8700d4bbbd7b8ac1c515d1` |

`06-committed-1` 与 2026-10-17 记录的 [06-committed.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-ime-20261021/xy-ime-regress2/06-committed.png>)（SHA-256 `b5ccef81115db433…`）**逐字节一致**，说明这条输入法路径在 HEAD 干净构建上稳定复现。manifest 记录 `ime_state: pinyin`（用 `fcitx5-remote -o` 强制，替代会因全局状态而反向的 Ctrl+Space）。

> 复现要点：Ctrl+Space 是**切换**语义，方向取决于 fcitx5 的全局状态。若上一轮把输入法切到 keyboard-us，同一段脚本会输入 ASCII `nihao` 而不是「你好」（本轮 `head-jar09-ime` 就出现过一次，SHA `3964b67e187a…`）。因此脚本必须先 `fcitx5-remote -o` 并用 `fcitx5-remote -n` 确认是 `pinyin`。

### jar-11 / jar-15 孤岛余生2 中文版

实际到达阶段：**是否开启音乐？→ STRANDED 2 标题（按住屏幕继续）→ 主菜单（开始游戏 / 游戏设置 / 音乐设置 / 关于游戏 / 退出游戏）→ 第一章过场 → 第一章场景 HUD（生命 55/100）+ 连续对话 → 场景（绿地、人物、旗帜）**。

| 截图 | SHA-256 |
|---|---|
| [05-act1](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar11-cn/05-act1.png>)（第一章 HUD + 对话） | `25c920353c5635c7b97e540ae74d08359211db9c98b3c50eb369a81445a613a0` |
| [06-dialog](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar11-cn/06-dialog.png>) | `5356b70d03be94a512f18b8a7531ee48149e0753788bfe26b7e4ae0cb558ae4a` |
| [12-fire](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar11-cn/12-fire.png>)（推进到场景画面） | `22db9df1fed98ade02cca576ae14a637d60e993cde1133311fb1cc7301725ca0` |

接触表：[jar11-contact.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/jar11-contact.png>)（SHA-256 `8a0decb541936eb73bd3899bf22af3adf7ee2f7c075552941e87de5ccb126236`）、[jar11-menu-contact.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/jar11-menu-contact.png>)（SHA-256 `90198b5747a479c26fad922d6618d6031403b85363d40d1522bea30a13395504`）。运行 2700 帧 / 89.796 s，退出码 0。

**存档 / 续档（未通过）**：游戏确实写 RMS（`stranded2_0.rms` SHA-256 `710fd626253082d7a47cb6258e42445ddeaa9ab9bc32d0cee335bb8b9cdf6bd5`、`stranded2_1.rms` / `stranded2_2.rms` SHA-256 `ad958425249dc2e4aae93bbeaeda87cbfdb1923affef63ae753d2e64eb035d7c`），但用同一 RMS 重启（`head-jar11-restart`）后**从第一屏重新开始**，没有续档：`01-reboot` 的 SHA 与首次运行的 `01-start` 相同（`8eec8c479dd276430227e12d635e5f09bf02a63f6289a6ab4afa2b9b573aa9a1`）。同时游戏自己抛 `InvalidRecordIDException`（写不存在的记录号）。此外，**F1 / F2 / 方向键都打不开游戏内菜单**（`07-right`、`08-soft1` 与 `06-dialog` 逐字节相同），因此「保存进度 / 载入进度」入口在本轮没有找到。

### jar-25 极品飞车14之热力追踪

实际到达阶段：**开机 splash + 「是否开启音效」提示（停在这里，Return 不响应）→ F2 越过提示 → 主菜单（快速比赛 / 热血追踪 / 实况! / 选关 / 帮助 / 关于 / 退出）→ Down+fire → 「您希望重新加载吗?」对话框 → 「选择赛事」世界地图与城市列表（距离 6.69 英里、圈数 00:00.0）**。

| 截图 | SHA-256 |
|---|---|
| [01-splash](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/01-splash.png>)（splash + 是否开启音效） | `e558de09813810e8e29f6a222ff103df4276ca1b5d47dce59482e6a819dfc3d7` |
| [02-after-soft2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/02-after-soft2.png>)（F2 越过音效提示） | `be8326b31d81607f76e3bbd8a897b55c71b5423ceba19131e1aceb4f70f8aadd` |
| [03-menu](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/03-menu.png>)（主菜单） | `5d5e5e9063af7d51f4b16c5c682a7b768b47309dde269e634d08d2ec03ba008d` |
| [05-after-fire](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/05-after-fire.png>)（「您希望重新加载吗?」对话框） | `5360e995b8a7331e3c1732327309bd72f9df3ed52feb34c4a10a71c2f8fd9d17` |
| [09-pause](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/09-pause.png>)（教程提示对话） | `6a9dc47787b7773e7653ca4fffac4afdc1ce2652f0783634b801632f4e476ffc` |
| [12-after-fire2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar25c-nfs14/12-after-fire2.png>)（**选择赛事**世界地图） | `587fa12123c9a883635ee4bb1568d03a7c4a43643dffab9c7ae9bbe3a74fce78` |

接触表：[cs-jar25c.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/cs-jar25c.png>)（SHA-256 `aaa5d83ddd58ee3557acc8a67e0db77b322eb537a09dc2b652ff81d3b1936edc`）。运行 3979 帧 / 132.429 s / 30.0 fps，退出码 0。

> 之前用固定时序（`plan-tut.json`）的运行会**整场卡在 splash**（21 张截图 SHA 全同 `e558de09813810e8e29f6a222ff103df4276ca1b5d47dce59482e6a819dfc3d7`）；本轮发现必须用 **F2** 越过「是否开启音效」提示，用 Return 不行。共享表里的「教程赛道 42→121 mph」结论来自另一条时序，本轮未复现到速度表。

### jar-28 远古帝国2汉化版

实际到达阶段：**logo → 教学提示（滚轴提示语）→ 任务对话（人族别的攻击下已经死…、快来救救我们! 我们正遭受攻击）+ 村庄场景**；从第 44 帧之后画面不再变化。

| 截图 | SHA-256 |
|---|---|
| [01-boot](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar28-ancient/01-boot.png>) | `2858b2765c458d965e63b1906c553fffa7b926cf64dc55a675df4e4c12c559a7` |
| [04-after-fire16](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar28-ancient/04-after-fire16.png>)（教学提示） | `8117727ff2a568619ef256c7a08a9a7085d54f589efaedd57e025f61e48d8a02` |
| [08-after-fire32](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar28-ancient/08-after-fire32.png>)（将军头像 + 剧情） | `acc91ee14f6876f32600dafa64171c0039d4ee870fa1562c5f266f1ad4c57d1e` |
| [11-after-fire44](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/head-jar28-ancient/11-after-fire44.png>)（村庄场景 + 救援对话） | `54b3ba4ebcdfbd9f1c9983728e4172d664fb8c2e9b684505ef3df9641a5fb2a8` |

接触表：[cs-jar28.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-gap-20261022/cs-jar28.png>)（SHA-256 `5a0e2976f2a41dc14fec22799f4e7907f92656e01ea5445cd18a8c01a4c7a853`）。运行 2700 帧 / 89.812 s，退出码 0。

## 异常（精确文本）

所有异常都是**应用抛出、模拟器记录**的（`INFO: thrown: …`），没有未捕获异常终止游戏线程；jar-07 是应用主动 `notifyDestroyed()`，不是崩溃。

| 条目 | 异常文本（出现次数） |
|---|---|
| jar-02 | `java/lang/NullPointerException at House.f()V pc=?: (no message)` ×2；`java/lang/NullPointerException at k.m()V pc=?: (no message)` ×1；`javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)` ×5 |
| jar-04 | `java/lang/NullPointerException at a.a(Ljava/lang/String;Z)[B pc=?: (no message)` ×1 |
| jar-07 | `javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)` ×1；应用输出 `INFO: 授权过期` |
| jar-08 | `java/io/IOException at javax/microedition/io/HttpImpl.getResponseCode()I pc=0: the HTTP request failed` ×21，应用自身栈 `at a.run()V` / `at ch.run()V` / `at cs.run()V`；另有 `java/lang/NullPointerException at ay.b(Ljava/lang/String;)[B pc=?: (no message)` ×1、`java/lang/NumberFormatException at java/lang/Integer.parseInt(Ljava/lang/String;)I pc=0: (no message)` ×3、`java/lang/ClassNotFoundException at java/lang/Class.forName(Ljava/lang/String;)Ljava/lang/Class; pc=0: emulator.Emulator` ×1 |
| jar-09 | 无（0 条 `thrown`） |
| jar-11 | `javax/microedition/rms/InvalidRecordIDException at javax/microedition/rms/RecordStore.setRecord(I[BII)V pc=0: (no message)` ×3；`javax/microedition/rms/RecordStoreNotFoundException …` ×6 |
| jar-25 | `java/lang/NullPointerException at ah.<init>(Lcom/ea/nfs10/MonkeyApp;)V pc=?: (no message)` ×2；`java/lang/NullPointerException at ah.w()V pc=?: (no message)` ×2；`javax/microedition/rms/RecordStoreNotFoundException …` ×2 |
| jar-28 | `java/lang/NullPointerException at h.<init>(Ljava/lang/String;I)V pc=72: (no message)` ×3（与既往 `PLAYER_DEALLOCATE_HIT` 证据一致）；`javax/microedition/rms/RecordStoreNotFoundException …` ×6 |

## 兼容状态

| 条目 | 状态 | 说明 |
|---|---|---|
| jar-02 City Bloxx | **partially works** | 主菜单 → Quick game → 连续 7 次塔吊落块 → F2 暂停/继续 → 购买土地对话 + 城建界面都跑通；「完整一局」「稳定多层堆叠」「存档重启」仍未验证 |
| jar-04 帝国时代3 | **partially works** | 标题 → 剧情 → 难度 → 教程 → **等距地图** 打通；单位移动/采集/建造/战斗/存档仍未验证（受限于本机指针不可用，见边界） |
| jar-07 乱世Q三国 | **blocked-by-app** | 第 0 帧应用自己退出（`授权过期`），模拟器行为正确；FreeJ2ME-Plus 同样语义，不是模拟器差异 |
| jar-08 仙镜OL | **blocked-by-server** | 标题场景能渲染，21 次 HTTP 取回全部失败，MMO 服务器不可用；本轮比共享表多确认了"标题/角色场景"，仍未进入游戏 |
| jar-09 侠义OL | **partially works**（注册/登录/在线=**blocked-by-account** + **blocked-by-server**） | 桌面 fcitx5 拼音把「你好」提交进 MIDP 文本框（与既往证据逐字节一致）；账号注册、登录、在线玩法需要账号与活服务器，本轮未验证 |
| jar-11 / jar-15 孤岛余生2 中文版 | **partially works** | 标题 → 主菜单 → 第一章场景与对话可达；**存档/续档未通过**（游戏写 RMS 但重启不续档，且自抛 `InvalidRecordIDException`；游戏内菜单入口找不到） |
| jar-25 极品飞车14 | **partially works** | 越过音效提示 → 主菜单 → 教程提示 → **选择赛事**世界地图；教程赛未跑完、无正式赛事、无保存重启 |
| jar-28 远古帝国2汉化版 | **partially works** | 任务简报与村庄场景可达；单位移动、战斗、完整关卡未验证（第 44 帧之后画面不再变化） |

## 边界与未验证项

- **二进制**：本轮关键取证用 HEAD 干净构建 `9f6325a7…`；早期部分对照用工作区构建 `2849376f…`（`jar11-cn`、`jar11-menu`、`jar09-ime` 等目录的 manifest 里记录了各自的 `binary_sha256`）。Lead 记录了工作区在途快路径会打挂真实游戏，因此**任何新结论都应以 HEAD 干净构建为准**。
- **指针**：本机 Xwayland 的 XTEST 指针事件无效，所以 jar-04 的"选中单位 / 点目标 / 建造"这类必须落点的操作，本轮只能用键盘替代动作尝试，未能验证；`vptr`（uinput 绝对触摸）可用但是全局设备，需与其他会话抢锁，本轮未使用。
- **jar-02 的 `--device nokia6230`**：共享表记录该游戏有机型白名单。本轮所有 jar-02 取证都带这个参数；另做了一次不带该参数的对照运行（目录 `head-jar02-nodevice`），结果见证据一节：非 Nokia 机型时应用 `getResourceAsStream` 失败并 `System.exit()`，第 0 帧退出。
- **jar-25**：`plan-tut.json` 那条固定时序在本轮**整场卡在 splash**；必须用 F2 越过音效提示。共享表里「教程赛道 42→121 mph」的数值本轮没有复现到，不能沿用。
- **jar-11 存档**：已确认「游戏写 RMS」与「重启不续档」两件事，但没有找到游戏内的保存/载入菜单入口（F1/F2/方向键均无反应），因此不能判定是"模拟器不保存"还是"应用需要特定入口"，这一项只能记为未验证。
- **jar-04/jar-25/jar-28 的"完整关卡/完整赛事"**：都需要长时间连续操作与落点输入，本轮只到"进入场景"。共享表里"未验证"的项在本轮依旧未验证，本笔记不把它们升级为 works。
- **共享语料表**（`docs/notes/2026-10-17-download-corpus.md`）按要求**未修改**；本笔记是补充材料，由 Lead 统一合并。
