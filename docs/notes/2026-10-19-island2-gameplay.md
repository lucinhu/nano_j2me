# 孤岛余生2 N97 原JAR实际推进验证（2026-10-19）

本轮使用现有 build/nj2me，不构建、不改C源码/测试/原JAR/原有存档、不提交。目标是确认实际游戏流程与后续按键响应，exit 0仅记录进程退出。N97版本与CU928版本不同，本轮以指定SHA为准。

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

最终结论：已超过标题/菜单，进入新游戏对白、丛林角色地图及室内剧情，fire/soft1推进对白，soft2打开暂停菜单，证明进入实际场景后仍响应输入。方向键未确认角色移动；保存退出再载入未验证；战斗或完整任务可玩未验证。
