# 全 29 个下载 JAR：阶段 / 截图 / 异常 / 兼容状态 / 复现流程（2026-10-22 汇总）

## 结论

`/home/lucinhu/Downloads/jar` 共 29 个文件、27 个唯一 SHA-256（`2023012708463745568.jar`、
`孤岛余生2时间之谜汉化版.jar`、`孤岛余生2时间之谜汉化版_诺基亚 N73系列(240×320)-631579.jar`
三者字节相同，SHA-256 `ffe21f75…`）。本表把每个文件的**实测最远阶段**、**可核对的截图**、
**原文异常**、**兼容状态**、**复现命令**补齐成一张可逐行核对的矩阵，取代「跑过就行」的写法。

**兼容状态的口径（本表统一）**

| 状态 | 含义 |
|---|---|
| 可用 | 已进入实际游戏画面并可用输入推进到有意义的交互点；**不代表通关** |
| 部分可用 | 已进入实际游戏，但仍有明确未验证/未推进的功能（回合、建造、存档、完整一局等） |
| 受限 | 能启动到某个阶段，但被应用自身的逻辑或平台判定挡住，模拟器侧无对应缺陷 |
| 外部阻塞 | 需要真实服务器或账号才能继续，模拟器无法解决 |
| 应用侧缺陷 | 应用自身抛异常后退出，与模拟器实现无关（已用 FreeJ2ME-Plus 对照） |

**统一红线**：主菜单成功不等于完整可玩；比赛证据不等于完成一局；退出码 0、帧数、被捕获的
异常一律不作为可玩证明。原 JAR 全部保持未修改（运行前后 SHA-256 一致），用户存档未被清空。

**环境**：默认构建 `build/nj2me`（运行前用 `sha256sum build/nj2me` 记录；二进制会随其他会话
的重建而变，**不要拿旧哈希当凭据**）、系统 `sdl2-compat` `/usr/lib/libSDL2-2.0.so.0.3200.74`、
`DISPLAY=:1`、fcitx5 5.1.23（`XMODIFIERS=@im=fcitx`、`LANG=zh_CN.UTF-8`）。
Android 侧：`OpenHV_API26_googleapis` AVD、`emulator-5560`、APK `build/android-classic/nj2me.apk`。

**通用复现骨架**

骨架 A（headless，出帧）：

```bash
cd /home/lucinhu/workspace/c/nano_j2me
R=.local-tools/debug-runs/<run>; mkdir -p "$R/rms"
./build/nj2me --port headless --size <W>x<H> --fps 30 --heap 16 --network \
  --rms-dir "$R/rms" --run-frames <N> --keys '<帧:键 帧:键 …>' \
  --dump-frames "$R/frames/" --dump-every 60 --stats \
  "/home/lucinhu/Downloads/jar/<文件>.jar"
```

骨架 B（SDL2 真窗口 + 定点按键/截图，`drive.py` 会自动 LD_PRELOAD 只读垫片
`netfail.so` 并把代理指向 `127.0.0.1:9`，让网络等待立刻失败以免错拍）：

```bash
cd /home/lucinhu/workspace/c/nano_j2me
python3 .local-tools/debug-runs/corpus-round20-20261021/drive.py \
  --jar "/home/lucinhu/Downloads/jar/<文件>.jar" --size <W>x<H> --frames <N> \
  --out .local-tools/debug-runs/<run> --plan '[["wait",3],["shot","01"],["key","soft1"],…]'
```

计划步骤：`["shot",名称]`、`["key",名称]`、`["wait",秒]`、`["hold",名称,秒]`；
键名 `fire/soft1/soft2/clear/up/down/left/right`。
**Android 侧不要用 `am start -n com.lucinhu.nj2me/.GameActivity`**（API 26 报
`SecurityException: … not exported from uid 10078`），改为启动 `LibraryActivity` 再点条目；
游戏自绘按钮不响应 `adb shell input tap` 的瞬时点按，要用
`adb shell input swipe X Y X Y 500` 的长按。

## 全 29 文件兼容矩阵

| ID / 文件（SHA-256 前 12 位） | 实测最远阶段 | 截图证据（SHA-256 前 12 位） | 异常（原文） | 兼容状态 | 复现 |
|---|---|---|---|---|---|
| jar-01 `2023012708463745568.jar` `ffe21f75` | 孤岛余生2 / 240x320：语言选择→音效→Glu→标题→中文主菜单 | `download-corpus-replay-20261017/jar-01-contact.png` `f87dc5c1a5f4` | 基线 9 次 thrown，均被应用捕获；无 uncaught | 可用（尚未开始场景） | A：`--size 240x320 --run-frames 900 --keys '210:fire 360:soft2 540:fire 720:fire 810:down'` |
| jar-02 `app(1).jar` `6979335c9b19` | City Bloxx / 240x320：**必须** `--device nokia6230`（去掉后第 0 帧 `getResourceAsStream` 失败并 `System.exit()`）；菜单→Continue quick game→吊车场景→连续落块（7 次操作 / 5 个不同画面）→F2 暂停/继续→「You've bought yourself a piece of land…」购买土地对话 + 城建界面 | `city-bloxx-round14-20261020/04-drop-0.png` `aaf06bcaa720` | `java/lang/NullPointerException at House.f()V` ×2、`at k.m()V` ×1；`javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)` ×5（均被应用捕获） | 部分可用：完整一局/多层堆叠/存档重启仍未验证 | B + `--device nokia6230`（drive.py 不带此开关时需手工加 `--device`）；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) |
| jar-03 `app(2).jar` `17a806bbfd3d` | 网易云音乐 / 240x320：主列表→搜索 Form（内联 TextField）→真实 fcitx5 输入「你好」→回车提交→回主列表 | `download-corpus-replay-20261017/jar03-item-fixed/frames/00300.png` `0f15dede2a60` | 修复前 `exception escaped into the frame loop: java.lang.IllegalArgumentException: default command must be an ITEM command @ g.<init>+93`（`Item.setDefaultCommand` 误把 BACK 当 ITEM，已修） | 可用（搜索结果/播放未验证） | A：`--size 240x320 --run-frames 600 --keys '210:down 240:fire 480:soft2 720:down'`；中文输入见 [网易云音乐内联中文输入](2026-10-21-netease-ime.md) |
| jar-04 `app(3).jar` `a6675ab8fb36` | 帝国时代3 / 240x320：音效→标题→剧情→难度(普通/对战)→教程→**等距地图**（资源条 100/100/100 + 村庄）；**必须补两次 fire** 才到地图，固定时序会错位 | `corpus-round20-20261021/aoe3-live/14-after-soft1.png` `dee1034bb3f1` | `java/lang/NullPointerException at a.a(Ljava/lang/String;Z)[B pc=?: (no message)` ×1（被捕获） | 部分可用：移动/采集/建造/战斗/存档未验证 | B：`--size 240x320 --frames 20000`，计划按「音效→标题→任务→难度→确定→教程→地图」逐步截图（注意要补两次 fire）；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) |
| jar-05 `app(4).jar` `575a30d5b7d3` | 文明5 / 240x320：Gameloft logo→标题(按 5)→文明选择(美利坚/华盛顿)→主菜单→重新开始→公元前 4000 年六边形地图（勇士+移民、金 98(-2)、回合 0）；按 `0` 开顾问/科技面板；方向键移光标、`C` 循环单位 | `corpus-round20-20261021/civ5-turn/03-map.png` `b42e050e8c5a` | 修复前每会话 31 次 `MediaException: not a PCM WAV`（整张 soundtrack 是 IMA ADPCM，补解码后 0 次）；`InvalidRecordIDException`（游戏捕获） | 部分可用：回合恒为 0，回合为 AP 驱动，AP 消耗路径未证；指针事件已确认送达 Canvas（`pointerPressed/Released` at 217,296），但地图与右下回合按钮仍无命中 | A：`--size 240x320 --run-frames 2400 --keys '…'`；`0` 开面板、`C` 换单位、方向键移动；细节见 [回合按钮与指针通道](2026-10-22-civ5-turn.md) |
| jar-06 `app.jar` `38e7c3f7a9d0` | UC 浏览器 / 240x320：中文欢迎协议→空白页→主菜单(1.打开…8.退出)→Down 移动选中→F2 关闭→焦点巡游让地址栏「请输入网址」高亮→**Return 打开 UC 的 URL 输入页**（标题「L 请输入网址 … R ok」、空输入框+光标+▼、「搜索:」行）；**真实网络 200 时 UC 自己渲染出完整首页**（顶栏「uc浏览器|帮助」、搜索框、图标行 首页/★/游戏/音乐/我的、底部「历史」） | `uc-regress-20261022/01-welcome.png` `85d217c8bcb5`；`uc-regress-20261022/02-blank.png` `bb8aa1f0370c`；`uc-regress-20261022/03-menu.png` `a27726f95d11`；`uc-regress-20261022/04-menu-down.png` `81f7615712dd`；真实首页接触表 `cur-hreal-contact.png` `2a0bfcf4cdb6` | 修复前缺 `Integer.shortValue()S` 触发页面解析失败（已补齐）；**503 时应用自己调 `Displayable.addCommand(null)` → 模拟器按 MIDP 规范抛 `java/lang/NullPointerException at javax/microedition/lcdui/Displayable.addCommand(Ljavax/microedition/lcdui/Command;)V pc=0: command is null` → 初始化中断，停在「初始化: 测试网络[第1次]」**；**URL 编辑页输入曾触发 SIGSEGV（本轮已修，见下节）**；`NoSuchMethodError at aa.a(ILjava/lang/String;)I pc=12: [I does not implement java/lang/String.length` ×118~127（应用把 int[] 当 String 用） | 部分可用：受控链路可用、真实首页可渲染；**地址栏输入仍未达成**（UC 地址栏是自绘控件，模拟器 IME 桥只对 MIDP `TextBox/TextField` 生效）；真实 UC 服务外网不可达 | B + 本机受控响应代理（`.local-tools/debug-runs/uc-regress-20261022/uc_harness.py --mode assignment`，`--out` 必须绝对路径）；`--mode 503` 复现应用侧停顿，`--mode real` 复现真实首页；详见 [UC 回归](2026-10-22-uc-browser-regression.md) 与 [UC 修复与诊断](2026-10-20-uc-browser.md) |
| jar-07 `乱世Q三国.jar` `754fa4a5a9af` | 乱世Q三国 / 240x320：**第 0 帧应用自己退出**，MIDlet 调 `notifyDestroyed()`；日志里多出一条应用自己的提示「授权过期」 | `download-corpus-replay-20261017/jar-07-contact.png` `bfa1e8e19ba7` | `javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore; pc=0: (no message)` ×1 + `INFO: 授权过期`；FreeJ2ME-Plus 同样抛 NotFound | 应用侧缺陷：语义与 FreeJ2ME-Plus 一致，非模拟器差异（授权过期属于外部条件） | A：`--size 240x320 --run-frames 900 --keys '210:soft2 390:fire 570:fire 750:down'`；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) |
| jar-08 `仙镜OL-羊羊得意_诺基亚 N97系列(360×640)-1121281.jar` `a9b1d88f87cf` | 仙镜online / 360x640：「羊羊得意」splash 动画→**标题/角色场景 + 提示框**（比共享表早先记的「仅闪屏」更远）；按键与点击均不推进 | `corpus-round20-20261021/xianjing/01-start.png` `38afeed908166` | `java/io/IOException at javax/microedition/io/HttpImpl.getResponseCode()I pc=0: the HTTP request failed` ×21（应用栈 `at a.run()V` / `at ch.run()V` / `at cs.run()V`）；**已确认在线的本机 503 代理日志里没有任何请求**（游戏不走 HTTP 代理） | 外部阻塞：MMO 服务器不可用（且非 HTTP 通道） | A：`--size 360x640 --run-frames 6840 --network --keys '210:soft1 450:fire 720:soft1'`；B + `plan-jar08.json`；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) |
| jar-09 `侠义OL-八龙聚首_诺基亚 N97系列(360×640)-871204.jar` `bfa87c2046aa` | xyOnline / 360x640：网络错误页→返回→游客注册→注册协议→游戏注册表单→**系统拼音提交「你好」→游戏账号回填「你好」** | `.local-tools/debug-runs/android-regress-20261022/15-after-menu.png` `46fc730bc67b` | logcat 无 `FATAL EXCEPTION`/`UnsatisfiedLinkError`；仅有 Android 自身探针 `Unexpected response code for CONNECT: 501` | 部分可用：注册/登录/在线未验证；服务器不可用 | 桌面 A/B + 本机 fcitx5（`Control+Space`→`nihao`→空格）；Android 走 `LibraryActivity`→点条目→长按游戏账号框→`input text nihao`→`keyevent 62`→`keyevent 4`→`keyevent 82`；见 [Android/桌面中文输入回归](2026-10-22-android-desktop-ime-regression.md) |
| jar-10 `孤岛余生2时间之谜.jar` `be0da0e4b3e5` | 孤岛余生2 英文版 / 240x320：`Enable sounds?`→`STRANDED MYSTERIES OF TIME`→Main Menu→Play Game→序章第 1 页→第 2 页→**F1/F2 软键**→第一幕（等距岛屿+角色+HUD） | `corpus-round20-20261021/jar10-live2/i1.png` `ae1aaad014c9` | `RecordStoreNotFoundException` / `InvalidRecordIDException`（应用捕获） | 可用：「序章停滞」结论已于 2026-10-22 **撤回**——翻页挂在软键上，`Return`/数字/方向键本就不负责翻页 | A：`--size 240x320 --run-frames 1800 --keys '210:fire 360:soft2 540:fire 720:fire 810:down'`，再补 `soft1/soft2`；见 [英文版按键复查](2026-10-22-stranded2-english.md) |
| jar-11 `孤岛余生2时间之谜汉化版.jar` `ffe21f75` | 与 jar-01 字节相同；汉化版可继续到第一幕：弩/火把/生命 HUD，对白「我是艾力克斯…」，按 5 推进下一段。**2026-10-22 复检**：游戏确实写 RMS（`stranded2_0.rms` SHA-256 `710fd626…`），但**重启不续档**（`01-reboot` 与 `01-start` 逐字节相同），且游戏自己抛 `InvalidRecordIDException`；F1/F2/方向键都打不开游戏内菜单，保存/载入入口未找到 | `corpus-round20-20261021/jar11-live/01-scene.png` `25c920353c56` | `javax/microedition/rms/InvalidRecordIDException at javax/microedition/rms/RecordStore.setRecord(I[BII)V pc=0: (no message)` ×3 | 部分可用：进入第一幕可；**存档重启不续档**（存/取入口未找到） | A：同 jar-01（另见 `jar11-cn/`、`jar11-live/`）；B + `plan-jar11c.json`（含 `01-reboot` 重启对比）；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) |
| jar-12 `孤岛余生2时间之谜汉化版_大显 CU928系列(240×320)-637190.jar` `c41d0766ffa6` | 240x320：MINISOYO→主菜单→新游戏对白→丛林→室内→soft2 暂停；2026-10-22 桌面 SDL2：游戏内菜单（继续/保存/载入/退出），RMS 自行写出 | `island-cu928-20261019/scene/frames/03600.png` `a966561f763c` | 无 uncaught；RMS 自行写出 | 可用（保存/载入往返未验证） | A：`--size 240x320 --run-frames 3900`；见 [CU928 变体](2026-10-19-island2-cu928.md) |
| jar-13 `孤岛余生2时间之谜汉化版_诺基亚 E60系列(352×416)-676805.jar` `3004fcc9531b` | 352x416：新游戏对白→2D 室内人物场景→soft2 暂停；2026-10-22：`F1`×2 进第一幕丛林场景，`F2` 推进对白 | `island-e6x-20261019/e60-interior/frames/03900.png` `aa49f06cc071` | IMA 修复后 `MediaException` 0 条 | 可用 | A：`--size 352x416 --run-frames 3900`；见 [E6x 变体](2026-10-19-island2-e6x.md) |
| jar-14 `孤岛余生2时间之谜汉化版_诺基亚 E62系列(320×240)-664471.jar` `cc152020195f` | 320x240：新游戏对白→**M3G** 室内人物场景→soft2 暂停；2026-10-22：`F2` 进第一幕丛林场景与对白（439 色） | `island-e6x-20261019/e62-interior/frames/03300.png` `a11d1d8e1d4d` | 同上 | 可用 | A：`--size 320x240 --run-frames 3900` |
| jar-15 `孤岛余生2时间之谜汉化版_诺基亚 N73系列(240×320)-631579.jar` `ffe21f75` | 与 jar-01 字节相同，复用 jar-01 全部证据 | `download-corpus-replay-20261017/jar-01-contact.png` `f87dc5c1a5f4` | 同 jar-01 | 可用 | 同 jar-01 |
| jar-16 `孤岛余生2时间之谜汉化版_诺基亚 N97系列(360×640)-665244.jar` `ab374c0705d7` | 360x640：MINISOYO→中文主菜单→`F1` 开始→第一幕丛林场景（1293 色）；SDL2 持续 Up 净位移；Escape/F2 暂停与返回；**新区域自动保存→冷启动从 RMS 继续→移动/暂停/返回** | `island-20261019/gameplay/interior/frames/03300.png` `5b1416328ba0` | 快照时钟修复后 headless 快照可恢复到 SDL2 并刷新/移动/响应 soft2 | 可用（战斗与完整任务未验证） | A/B：先跑首段，再用 `--save-state` / `--load-state` 验证 RMS 继续；见 [N97 变体](2026-10-19-island2-gameplay.md) |
| jar-17 `家园6之革命完美汉化版_摩托罗拉 A1000系列(208x320).jar` `cfc79627f661` | Townsmen 6 / 208x320：语言/声音→主菜单→战役地图→村庄对白 | `corpus-round20-20261021/jar17-menu/08-after-fire3.png` `5f6d2c446d36` | 旧日志每运行时 66 次 `MediaException: not a PCM WAV`（IMA 修复后 0 次） | 可用（建造/战斗/存档待复核） | A：`--size 208x320 --run-frames 1800 --keys '1260:fire 1440:soft1 1620:fire'`；见 [家园6 八变体](2026-10-21-townsmen6.md) |
| jar-18 `家园6之革命完美汉化版_摩托罗拉 A760系列(240x320).jar` `572b7f3e9927` | 240x320：同 jar-17；**A760 仍有 90 度旋转显示问题** | `corpus-round20-20261021/jar18-menu/08-after-fire3.png` | 同 jar-17 | 部分可用：旋转显示 | A：`--size 240x320 --run-frames 1800` |
| jar-19 `家园6之革命完美汉化版_索爱 K700系列(176x220).jar` `324ea5a379b4` | 176x220：语言/声音→主菜单→村庄+剧情对白 | `corpus-round20-20261021/jar19-menu/09-after-41s.png` | 同 jar-17 | 可用 | A：`--size 176x220 --run-frames 1800` |
| jar-20 `家园6之革命完美汉化版_索爱 K790系列(240x320).jar` `876dfdaa8a2c` | 240x320：语言/声音→主菜单→战役开场白 | `corpus-round20-20261021/jar20-menu/08-after-fire3.png` | 同 jar-17 | 可用 | A：`--size 240x320 --run-frames 1800` |
| jar-21 `家园6之革命完美汉化版_诺基亚 E60系列(320x240).jar` `70188d6f9801` | 320x240：语言/声音→主菜单→村庄+剧情对白 | `corpus-round20-20261021/jar21-menu/09-after-41s.png` | 同 jar-17 | 可用 | A：`--size 320x240 --run-frames 1800` |
| jar-22 `家园6之革命完美汉化版_诺基亚 E62系列(320x240).jar` `a37915bf0860` | 320x240：语言/声音→主菜单→战役开场白 | `corpus-round20-20261021/jar22-menu/08-after-fire3.png` | 同 jar-17 | 可用 | A：`--size 320x240 --run-frames 1800` |
| jar-23 `家园6之革命完美汉化版_诺基亚 N70系列(176x208).jar` `fb56034f8f54` | 176x208：语言/声音→主菜单→战役开场白 | `corpus-round20-20261021/jar23-menu/08-after-fire3.png` | 同 jar-17 | 可用 | A：`--size 176x208 --run-frames 1800` |
| jar-24 `家园6之革命完美汉化版_诺基亚 N73系列(240x320).jar` `5a32a9881af0` | 240x320：家园6_光辉革命：标题→战役地图→村庄对白→**村庄内交互**：回车推进对白、`F1` 全屏面板、`Escape` 返回、方向键滚图、数字 `1`/`2` 打开村庄信息面板（人口 3+3、工人 3、进度条）、`F2` 暂停菜单、`*` 教程浮层、长按底部「行动菜单」可开居民/建造/其他三栏 | `corpus-round20-20261021/jar24-menu/09-after-41s.png` `3a2f6276e6c6` | 同 jar-17；菜单项 0 像素响应的真因是**进入村庄时教程模态页尚未结束，输入被教程吃掉** | 部分可用：建造/战斗/存档重启正在复核 | B：`--size 240x320 --frames 4000`，先按 `*` 关教程再长按行动菜单；见 [村庄内行动菜单](2026-10-22-townsmen6-village-ui.md) |
| jar-25 `极品飞车14之热力追踪中文版.jar` `77d10a56e51a` | 极品飞车：热力追踪 / 240x320：音效→标题→教程赛道→2/11，42→73→114→121→101 MPH，左右转向改变车辆姿态，`F2` 打开暂停菜单（可见「继续赛事」）。**2026-10-22 复检**：开机「是否开启音效」提示**必须用 `F2`** 越过（`Return` 不行；用 `Return` 会整场卡在 splash），随后到主菜单→教程提示→「选择赛事」世界地图 | `nfs-hot-pursuit-drive-round15-20261020/11-steer-right.png` `be4122c74e6b` | `java/lang/NullPointerException at ah.<init>(Lcom/ea/nfs10/MonkeyApp;)V` ×2；`at ah.w()V` ×2（被捕获） | 部分可用：未完成教程赛/正式赛事/保存 | B：`--size 240x320 --frames 1600`，计划 fire→fire→soft1→right→left→F2；**开场用 `F2`**；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) 与 [驾驶证据](2026-10-20-nfs-hot-pursuit.md) |
| jar-26 `极品飞车16：亡命狂飙.jar` `92c5f29e1d66` | NFS The Run / 240x320：PROLOGUE→NEW GAME 选车（SAN FRANCISCO/SPRINT/FORD MUSTANG GT BOSS 302）→**真实比赛**：隧道、路面、玩家车与前车、赛道标线与里程牌；HUD 进度/名次/对手距离/小地图/速度表 39→133 MPH，Up 提速、左右转向；可跑到结算页 `RESULTS / YOU LOSE… TIME: 04:55.8` | `nfs-cam-20261022/run4/02-race-2.png` `ce0b1d51d505` | 4 次 `ArrayIndexOutOfBoundsException at ad.a(Ljavax/microedition/lcdui/Graphics;IIIIIIZ)V pc=2672`（2D 精灵游标越界，下一帧恢复，与 3D 无因果）；启动期 `NullPointerException at af.<init>(…) pc=471/479`（被捕获）；55 次「all threads are waiting」 | 可用（3D 世界已修复，根因是**相机语义**不是帧时序：commit `1e47d9a`） | B：`--size 240x320 --frames 2400`，计划推进到选车→Return 起跑→hold up/left/right；见 [NFS 亡命狂飙 3D 定位](2026-10-21-nfs-the-run-m3g.md) 与 [NFS16 回归](2026-10-22-nfs16-regression.md) |
| jar-27 `狂野飙车4-亡命都市.jar` `7d4369008c5d` | 狂野飙车-亡命都市 / 240x320：音效→标题→快速游戏→赛道说明→比赛（1/8 圈、`狂奔中`、AI 车辆、金钱与速度 HUD）；倒计时 3→1，静止即 74 MPH，按住上键 20→26→59 MPH，左右键使车辆驶向路肩/草地 | `asphalt4-20261021/run5/09-idle-4.png` `3ebdee1ee286` | 游戏自身 `com/qq/sms/QQSMS.init/getInt/desString` 抛 `NumberFormatException` / `IllegalArgumentException: Length Not Double Num`（被捕获，不阻断） | 部分可用：未完成整圈/冲线/存档 | B + 只读垫片 `netfail.so`：`--size 240x320 --frames 2400`，计划 hold up→hold left/right；见 [狂野飙车4](2026-10-17-download-corpus.md#2026-10-21-更新狂野飙车4-进入实际比赛) |
| jar-28 `远古帝国2汉化版.jar` `778b6e881b56` | Ancient Empires II / 240x320：**冷启动 + 连续 `fire`**（1 s 一次、≥16 次）→ logo→教学提示（滚轴提示语）→任务对话（将军头像 + 剧情）→**村庄场景 + 救援对白**；第 44 帧之后画面不再变化。2026-10-22 早先的「冷启动只到标题」是**按键脚本不同**造成的（用 `fire/soft1/soft2/right/down` 混按、间隔 2 s），不是游戏更差；另有一份早先的「战役地图 + 求救对白」证据其实来自**快照续跑**（`ancient-empires-round5/fixed-map/runtime.log` 首行 `resumed b from …/campaign/resume.njs`），两者不要混为一谈 | `corpus-gap-20261022/head-jar28-ancient/04-after-fire16.png` `8117727ff2a5`；`…/08-after-fire32.png` `acc91ee14f68`；`…/11-after-fire44.png` `54b3ba4ebcdf`；接触表 `cs-jar28.png` `5a0e2976f2a4` | 读取快照后 MIDI 旧指针崩溃（已修）；`java/lang/NullPointerException at h.<init>(Ljava/lang/String;I)V pc=72: (no message)` ×3（与既往 `PLAYER_DEALLOCATE_HIT` 证据一致）；`RecordStoreNotFoundException` ×6；JAR 自带 `FullCn` 固定 128×116，而 `splash.png` 为 132×176，越出重绘区 | 部分可用：**画面在第 44 帧后冻结**，单位移动/战斗/完整关卡/存档未验证 | B + `plan-jar28.json`（`drive2.py`，连续 `fire` 1 s 一次共 60 次，2700 帧 / 89.8 s / 退出码 0）；见 [语料缺口巡检](2026-10-22-corpus-gap-sweep.md) 与 [远古帝国2 复现记录](2026-10-17-ancient-empires-replay.md) |
| jar-29 `都市赛车6火线追击.jar` `323942169575` | Asphalt 6 / 240x320：音效→标题→NORMAL RACE/CHALLENGES 说明→比赛（1/8 圈、`WANTED`、1/3 挡、116→127 MPH、金钱 $1,084、AI 车辆），右转驶上右侧路肩 | `asphalt6-20261021/run2/12-race.png` `eab4f4d1c295` | 启动期 `MediaException: not a PCM WAV` 与游戏自身 `NullPointerException`（被捕获） | 部分可用：未跑完整圈/冲线/名次结算/存档 | B + `netfail.so`：`--size 240x320 --frames 4200`（2854 帧/68.8 s/41.5 fps），计划 down→fire→hold up→hold right；见 [都市赛车6](2026-10-17-download-corpus.md#2026-10-21-更新都市赛车6-进入实际比赛) |

## 外部限制：模拟器解决不了的部分

以下四类不是缺陷，也不能靠多跑几轮解决，写在这里避免被当成「兼容性缺口」：

1. **服务器已下线 / 端口不可达**：仙镜OL（jar-08）、侠义OL（jar-09）、UC（jar-06）的真实服务端。
   仙镜OL 用**已确认在线**的本机 503 代理重跑 140 s，代理日志**零请求**，说明它根本不走
   HTTP 代理而是直连固定地址；侠义OL 的 `xy.liujin.cn` / `xy.ljsy.net` / `117.135.140.227`
   在受控代理下表现为「可连、但内容不是协议期望的响应」。
2. **需要真实账号与凭据**：注册、登录、选服、创建角色一律没有做，也不该由模拟器伪造。
   本轮所有「账号框里出现中文」只证明**输入链路**通，不证明账号可用。
3. **受控响应的性质**：UC 用的分配响应是**本地按字节码构造的 41 字节测试数据**，
   不是旧 UC 服务的真实回包；它只用来推进解析路径，不能用来推断线上行为。
4. **真机缺失**：本轮 Android 全部在 **API 26 x86_64 模拟器**上完成，**没有真实 arm64 手机**；
   模拟器的 IME（Google 拼音）与真机输入法实现不同，虚拟触屏与真机触摸也不同。

另有一条**模拟器自身**的已知边界：桌面原生输入法的**预编辑（preedit）文本不会进入控件**
（`src/port/posix/nj_port_sdl2.c:1128-1130` 只设置 `ime_composing`），游戏内也不画候选框；
因此「拼音候选窗由宿主输入法绘制」，提交后才进 MIDP 文本框。

## 本轮新增的复核（2026-10-22）

- **复现骨架自检（骨架 A 现场跑通）**：用 HEAD 的干净构建
  （`.local-tools/head-build-20261022/build/nj2me`，SHA-256 `9f6325a76fb7…`）对
  jar-01 跑表内 A 命令，退出码 0、900 帧全部出图、第 900 帧 **142 种颜色**（中文主菜单）、
  `uncaught` 0 次、`thrown` 9 次（应用自己捕获的
  `RecordStoreNotFoundException` / `EOFException`）。
  证据：`.local-tools/debug-runs/matrix-repro-check-20261022/jar01-head/`。
- **骨架 A 批量自检（4 个 JAR，HEAD 干净构建）**：

  | 文件 | 尺寸/帧数 | thrown | uncaught | 末帧颜色数 | 末帧实际内容 |
  |---|---|---|---|---|---|
  | `app(2).jar` 网易云音乐 | 240x320 / 600 | 0 | 0 | 85 | 主列表（正在播放/搜索/热歌榜/热门列表） |
  | `孤岛余生2…E60…jar` | 352x416 / 3900 | 9 | 0 | 158 | HUD + 对白「我是艾力克斯 蒙达尔斯的医生,我需要你的帮助。」 |
  | `家园6…K700…jar` | 176x220 / 1800 | 9 | 0 | 409 | 村庄场景 + 对白「别管我！我还不想起来！」 |
  | `远古帝国2汉化版.jar` | 240x320 / 1200 | 7 | 0 | 146 | **只到标题**（`开始新游戏` 选中）——注意这是我用的**混按**键脚本；换成连续 `fire`（1 s 一次）能到任务对话+村庄，见 jar-28 行 |

  证据：`.local-tools/debug-runs/matrix-repro-check-20261022/sweep/`。
- **骨架 B 的两条注意**：(1) SDL2 窗口上会叠着模拟器的**屏幕软键与数字键盘**
  （左下方向键、右下 0-9、顶部 L/R），它**会吃掉点击**，点屏幕前先按 `F12` 或 `Tab` 隐藏；
  (2) 截图里出现「…」是**游戏还在加载**，不是黑屏——机器满载时加载会明显变慢，
  要等画面真的变了再判定失败。
- **在途构建的坑（2026-10-22 已更正）**：先前记的「工作区 `build/nj2me` 会打挂真实游戏」
  其实是**中间版本**的病态，**不是已提交的 tip**——对方会话在同日把解释器快路径提交了
  （`06bedce`…`2a6292e`），并在 `docs/PERFORMANCE.md`（commit `2e0f0b2`）独立复现确认。
  本轮用 **tip 的干净构建**（`git archive HEAD | tar -x -C .local-tools/head-tip-20261022`，
  二进制 SHA-256 `4d2625ebf7dd…`）重跑：孤岛余生2 退出码 0、uncaught 0、堆 2.59 MB、末帧
  **142 色**（与对方数字完全一致），都市赛车6 1200 帧退出码 0、gfx 226,524。
  完整更正与隔离构建配方见
  [在途解释器快路径回归](2026-10-22-superinstr-regression.md)。
- **Android 中文输入端到端**（当前 HEAD 构建的 APK）：系统 Google 拼音 → `ImeEditor` →
  `InputConnectionWrapper.finishComposingText/commitText` → JNI → MIDP `TextField`，
  最终在游戏注册页看到 `游戏账号: 你好`。详见
  [Android/桌面中文输入与受控网络回归](2026-10-22-android-desktop-ime-regression.md)。
- **桌面原生 fcitx5 提交**：提交后截图 SHA-256 `b5ccef81115db433…` 与 2026-10-21、
  2026-10-22 之前的证据**逐字节一致**；用 HEAD 干净构建连跑 3 次，其中 1 次未提交
  （**该回归有约 1/4 的偶发失败率**，判定通过要看截图哈希而不是看退出码）。
- **Android 受控网络**：503 → 5 个 `GameComm` POST（`xy.liujin.cn`×1、`xy.ljsy.net`×2、
  `117.135.140.227`×2，body 48 B）后弹网络错误页；200 空 body → 2 个 POST 且**无**错误页。
- **原 JAR 与用户存档**：本轮涉及的所有原 JAR 运行前后 SHA-256 一致；未清空任何用户 RMS。

## 复现自检清单

跑完任何一行后按顺序核对：

1. `sha256sum "/home/lucinhu/Downloads/jar/<文件>.jar"` 与表内前 12 位一致（证明没改原 JAR）。
2. `--rms-dir` 指向**新建**目录；测存档重启时**复用同一个**目录，不要删。
3. 截图要真的用 `read_image` 看过，不能只看文件存在。
4. 日志里同时搜 `uncaught` 与 `thrown`：`uncaught` 为空**不足以**判通过，还要确认到了
   交互点、后续按键仍在推进。
5. 退出码 0 / 帧数达标**不算**可玩证明。
