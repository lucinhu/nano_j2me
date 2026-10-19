# 孤岛余生2 N97 原JAR实际推进验证（2026-10-19）

初轮使用现有 build/nj2me，不构建、不改C源码/测试/原JAR/原有存档；后续另行集成快照时钟修复，构建与验证记录见末节。目标是确认实际游戏流程与后续按键响应，exit 0仅记录进程退出。N97版本与CU928版本不同，各轮以指定SHA为准。

- 原JAR：`/home/lucinhu/Downloads/jar/孤岛余生2时间之谜汉化版_诺基亚 N97系列(360×640)-665244.jar`。
- JAR SHA-256：`ab374c0705d779e1e44cc1bd4c26ce573430ed3bb7b88b633d37165d07db22a1`。
- binary：`/home/lucinhu/workspace/c/nano_j2me/build/nj2me`，SHA-256 `2f45d25a9f400061728d7ef0254132325148bc194980f6c62adf8b51093da113`。
- 每组独立新RMS，headless、360×640、heap16、fps30、每150帧保存原PNG，超时上限为帧预算秒数+20秒；未启用网络。
- [运行脚本](../../.local-tools/debug-runs/island-20261019/gameplay/run.py)将完整argv和前后hash写入各组result.json；原JAR/binary hash在已完成各组前后相同。

所有下述PNG已用read_image查看原图。

| 组 | 精确按键（帧:key） | 结果与时间 | 证据目录 |
|---|---|---|---|
| menu | `350:fire 550:fire 1000:fire` | 1200帧，39.725秒，30.2fps；末帧中文主菜单，“开始游戏”高亮 | [命令](../../.local-tools/debug-runs/island-20261019/gameplay/menu/command.json)、[日志](../../.local-tools/debug-runs/island-20261019/gameplay/menu/runtime.log)、[1200帧](../../.local-tools/debug-runs/island-20261019/gameplay/menu/frames/01200.png) |
| newgame | `350:fire 550:fire 1000:fire 1250:fire 1450:fire 1650:fire` | 1800帧，59.677秒，30.2fps；1250确认新游戏，1500帧“我是艾力克斯…我需要你的帮助”，1800帧后续“这也许是我们的最后一次见面了…我要告诉你我经历的故事…”；确认后续fire推进对白 | [命令](../../.local-tools/debug-runs/island-20261019/gameplay/newgame/command.json)、[日志](../../.local-tools/debug-runs/island-20261019/gameplay/newgame/runtime.log)、[1500帧](../../.local-tools/debug-runs/island-20261019/gameplay/newgame/frames/01500.png)、[1800帧](../../.local-tools/debug-runs/island-20261019/gameplay/newgame/frames/01800.png) |
| movement | `350:fire 550:fire 1000:fire 1250:fire 1450:fire 1650:fire 1850:fire 2000:fire 2150:fire 2300:fire 2450:fire 2600:right 2630:right 2660:down 2690:down 2750:left 2780:left 2850:soft1` | 3000帧，99.416秒，30.2fps；2550帧丛林地图与角色，2850帧室内地图和人物对白“哇！这一定是幻觉”，3000帧soft1后对白变为“怎么回事？外面怎么那么吵？”；方向键位于转场期间，不能用场景变化证明角色移动 | [命令](../../.local-tools/debug-runs/island-20261019/gameplay/movement/command.json)、[日志](../../.local-tools/debug-runs/island-20261019/gameplay/movement/runtime.log)、[2550帧](../../.local-tools/debug-runs/island-20261019/gameplay/movement/frames/02550.png)、[2850帧](../../.local-tools/debug-runs/island-20261019/gameplay/movement/frames/02850.png)、[3000帧](../../.local-tools/debug-runs/island-20261019/gameplay/movement/frames/03000.png) |

以上三组日志均有6条`RecordStoreNotFoundException`及3条`InvalidRecordIDException`，出现在新RMS初始化；流程在异常之后持续推进，无`uncaught`或`exception escaped into the frame loop`。已完成组均正常调用destroyApp并返回。不能将这些caught记录写为运行无异常。

补充interior组：3900帧，129.204秒，30.2fps；完整按键为 `350:fire 550:fire 1000:fire 1250:fire 1450:fire 1650:fire 1850:fire 2000:fire 2150:fire 2300:fire 2450:fire 2800:fire 2900:fire 3000:fire 3100:fire 3200:fire 3300:fire 3400:right 3430:right 3460:down 3490:down 3550:left 3580:up 3700:soft2`。[完整命令](../../.local-tools/debug-runs/island-20261019/gameplay/interior/command.json)、[日志](../../.local-tools/debug-runs/island-20261019/gameplay/interior/runtime.log)、[前后hash](../../.local-tools/debug-runs/island-20261019/gameplay/interior/result.json)保留。已实际查看[3300帧](../../.local-tools/debug-runs/island-20261019/gameplay/interior/frames/03300.png)、[3600帧](../../.local-tools/debug-runs/island-20261019/gameplay/interior/frames/03600.png)、[3900帧](../../.local-tools/debug-runs/island-20261019/gameplay/interior/frames/03900.png)：对白消失后角色仍位于室内床边，方向输入后没有可确认的净位移；3700 soft2后明确打开“暂停菜单”，含返回游戏/游戏设置/介绍说明/主菜单。不能以细微动画差异证明移动。日志仍仅相同9条新RMS初始化caught，无uncaught/frame-loop escape，destroyApp正常返回，hash前后相同。

本轮四个后台job（menu、newgame、movement、interior）均已通过job_output收集完毕，无遗留运行任务。

初轮结论：已超过标题/菜单，进入新游戏对白、丛林角色地图及室内剧情，fire/soft1推进对白，soft2打开暂停菜单，证明进入实际场景后仍响应输入。初轮短按没有确认角色移动；保存退出再载入未验证；战斗或完整任务可玩未验证。

## SDL2 持键与真实窗口验证补充

后续使用 `1b20a08` 构建，binary SHA-256 `ee99418d5a7193de9ab8a04881fa3dede57e03fce399de9f8553950998ccaf51`，原JAR SHA保持不变。`SDL_VIDEODRIVER=x11`，`--port sdl2 --size 360x640 --heap 16 --fps 30`，独立新RMS。通过[独立窗口脚本](../../.local-tools/debug-runs/island-n97-control-20261019/sdl_probe.py)启动并仅操作该进程的窗口；完整启动参数、按键down/up时间和二进制SHA保存在[manifest](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-movement-fresh/manifest.json)。截图来自窗口实际呈现，窗口为720×1280，游戏分辨率为360×640。

初始脚本按键与上文interior组到3300帧一致；118秒后依次发送Up保持1秒、Return保持0.15秒、Up保持2.5秒、Right保持1秒、Escape保持0.15秒，每次释放后再截图。已逐张打开检查：

- [操作前](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-movement-fresh/01-interior-before.png)：人物在床边；首次短Up及Return后的图片相同，不能以这两次输入证明移动。
- [第二次持续Up后](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-movement-fresh/04-after-held-up.png)：人物从床边上移至正上方绿色门前，确认净位移；随后Right没有可见净位移，未据此判断右向输入失效或地图碰撞根因。
- [Escape后](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-movement-fresh/06-menu.png)：打开暂停菜单，证明移动之后仍响应输入。

[运行日志](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-movement-fresh/runtime.log)记录3886帧、1967次paint、129.098秒、30.1fps，9条新RMS初始化caught，与初轮同类；无uncaught或frame-loop escape，destroyApp正常返回。脚本主动终止所拥有的进程，exit0只记录收尾，不作为交互证明。此处已确认SDL2持续方向输入导致人物上移和后续暂停；仍未验证走出房间、战斗、完整任务或保存重开。

## 混合触摸/按键回放修复

审查[nj_run.c](../../src/runner/nj_run.c)时发现，触摸事件会直接递增松键游标，可能跳过此前按键待发送的KEY_UP，使脚本中的方向键一直保持按下。改为只推进按下游标，由原有松键循环按顺序跳过触摸项并释放按键。[test_poke.c](../../tests/test_poke.c)新增真实输入事件捕获，覆盖同帧按键加点按、松键帧触摸、拖动中夹入按键，检查事件顺序及按键/触摸释放次数。完整及ASan/UBSan回归结果见下节。

## 快照跨端口时钟修复与集成验证

headless 在3490帧保存[室内快照](../../.local-tools/debug-runs/island-n97-control-20261019/seed/interior.njs)，SHA-256 `1d07325814034fb0b15fd3bfdc16fbb564bec998668950252a4802a918ad0e2f`。旧构建将它恢复到SDL2后，日志虽显示resumed，293帧、10.286秒却没有绘图调用，不能视为成功恢复。根因是恢复时平移了VM时钟，却未同步线程sleep/wait及Java对象中的Timer绝对时间；headless与SDL2时钟起点不同，原期限因此变成很长的等待。

[nj_save.c](../../src/core/nj_save.c)改为保留快照中的MIDlet虚拟时刻，把它锚定到当前端口原始时钟。sleep、timed wait、无限等待的0哨兵、halt及Java Timer仍在原虚拟时间轴上；快照格式保持v2。[test_save.c](../../tests/test_save.c)新增原始时钟起点 `0/500/50000000` × 速度 `100%/200%` 六组回归，检查Java currentTimeMillis连续、时间增量、剩余延迟、实际调度唤醒、过期立即唤醒、无限等待与源VM未变。共267项检查、0失败。

集成构建为[build-lead/nj2me](../../build-lead/nj2me)，SHA-256 `4c2f702d50b3fabe78bebd9f5fa5891acc83440458505b515b116db68ef67342`；原JAR不变。恢复同一快照后：

- [原生按键窗口回放](../../.local-tools/debug-runs/island-n97-control-20261019/sdl_snapshot_probe.py)与[动作时间/参数](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-fixed/manifest.json)：已查看[恢复房间](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-fixed/01-interior-restored.png)和[Up后](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-fixed/02-after-up.png)，人物确实上移。[日志](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-fixed/runtime.log)记录384帧、192次paint、60970次gfx、13.304秒，destroyApp返回。该组后续Escape未打开暂停菜单，原因尚未确认，不能记为原生Escape恢复后通过。
- 追加[SDL2脚本回放](../../.local-tools/debug-runs/island-n97-control-20261019/sdl_snapshot_pause.py)：`--load-state .../seed/interior.njs --keys '90:soft2' --run-frames 240`，同一快照、相同构建、独立RMS。[实际窗口截图](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-pause/after-scripted-soft2.png)明确显示“暂停菜单/返回游戏/游戏设置/介绍说明/主菜单”，确认恢复后仍能分发soft2并响应。截图中间有白底区域，未判定菜单背景绘制无缺陷。[完整参数/hash/退出记录](../../.local-tools/debug-runs/island-n97-control-20261019/sdl-snapshot-pause/result.json)保留。

[完整CTest](../../.local-tools/debug-runs/island-n97-control-20261019/ctest-integrated.log)84/84通过；[ASan/UBSan检查](../../.local-tools/debug-runs/island-n97-control-20261019/ctest-asan.log)覆盖save/poke/thread_lifecycle，3/3通过。默认[build/nj2me](../../build/nj2me)也已同步重建，SHA-256 `e712ea6cf551052d562ea88c94b2c735feffeca84b7e04f26f35bf8772978c3f`，默认构建的test_save/test_poke均通过。所有窗口均由脚本启动和回收，截图仅来自其所属窗口。

另将SDL2 fresh组产生的RMS复制到独立实验目录，分别在新进程运行1200/1500帧：[重启读取记录](../../.local-tools/debug-runs/island-n97-control-20261019/rms-restart/result.json)、[额外确认记录](../../.local-tools/debug-runs/island-n97-control-20261019/rms-slots/result.json)。三份RMS文件hash保持不变，未再出现新RMS初始化异常；画面分别到主菜单和“第一章”加载。此证据只能说明已有RMS可读取，尚未证明游戏进度保存后恢复至原位置。

## 原生软键与自动存档闭环（2026-10-20复核）

以下补充使用同一原N97 JAR，固定[实验二进制](../../.local-tools/debug-runs/island-save-followup-20261019/pinned-nj2me) SHA-256 `e00faa8628ca78e3ec0554ee9e72b0bb107267811d7c39820f43a60a25bd5356`，360×640、16 MiB、30 fps。每次复用的都是实验RMS副本，原始JAR与用户存档保持不变。

SDL2恢复室内快照后，通过真实窗口发送Escape，再发送Escape，画面依次为[暂停菜单](../../.local-tools/debug-runs/island-n97-followup-20261019/repeat-controlled-after/02-after-Escape.png)和[恢复房间](../../.local-tools/debug-runs/island-n97-followup-20261019/repeat-controlled-after/03-after-Escape.png)；F2也完成相同的暂停/返回流程。[参数、SHA及动作时间](../../.local-tools/debug-runs/island-n97-followup-20261019/repeat-controlled-after/manifest.json)与[SDL事件日志](../../.local-tools/debug-runs/island-n97-followup-20261019/repeat-controlled-after/runtime.log)保留。这补齐了恢复后原生软键的响应证据；上轮孤立Escape未响应的原因仍未确定。该实验使用早期重复键过滤构建，最终Canvas/GameCanvas与List/Form分流由提交`159e4d3`的[专门回归报告](2026-10-20-host-key-repeat.md)覆盖；两组截图相同不能证明重复键修复前后存在可见差异。暂停菜单中央白底仍待对照。

游戏帮助提示进入新区域时自动保存。沿室内右下绿色箭头走出房间后，[300帧室外基地](../../.local-tools/debug-runs/island-save-followup-20261019/pinned-exit/frames/00300.png)出现，同时游戏进度槽`stranded2_1.rms`从18字节变为5761字节，SHA-256 `47e14ad9465d0c071c5ddb6ccfc9b7933532dada5de7a494c7676b60ae57311e`。配置槽528字节和空槽18字节保持原hash。[完整命令、保存前后文件大小/hash及退出记录](../../.local-tools/debug-runs/island-save-followup-20261019/pinned-exit/result.json)记录300帧、10.399秒、退出0。本组使用快照作为走出房间的起点，随后另起全新进程验证游戏自己的RMS载入。

冷启动组只复制上述三份RMS，[完整启动参数](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/command.json)没有`--load-state`。`--save-state`只在末尾1979帧输出诊断快照，不参与启动。启动确认键为120/300/450/660/840/1020/1200/1380/1500帧fire；1680至1710帧每2帧left；1830与1920帧soft2。已逐张检查：

| 阶段 | 实际结果 | 截图 |
|---|---|---|
| 1650帧冷启动继续 | 从游戏RMS回到室外基地门前，人物约x176 | [载入存档](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/frames/01650.png) |
| 1740帧后续输入 | 人物左移至约x110，确认载入后仍可操作 | [移动后](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/frames/01740.png) |
| 1860帧soft2后 | 打开暂停菜单 | [暂停](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/frames/01860.png) |
| 1950帧再次soft2后 | 返回同一基地与移动后的位置 | [返回游戏](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/frames/01950.png) |

[冷启动结果与hash](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/result.json)及[日志](../../.local-tools/debug-runs/island-save-followup-20261019/lead-pinned-restart/runtime.log)记录1980帧、65.725秒、1059次paint、309706次gfx、退出0，destroyApp正常返回，无thrown/uncaught或frame-loop escape。三份RMS的最终hash与启动副本相同。此次证据确认N97版进入新区域自动保存、退出后冷启动继续，以及继续后的移动/暂停/返回；床上休息保存、战斗、完整任务、Android存档闭环及其他孤岛变体的保存继续仍未验证。
