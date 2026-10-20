# 极品飞车14：热力追踪原 JAR 实际驾驶验证（2026-10-20）

原版已在 SDL2 真窗口进入教程赛道，持续行驶至教程 2/11；画面可见 42、73、114、121、101 英里/时，方向键输入后赛道继续推进，F2 打开“继续赛事 / 重新进行赛事 / 选项 / 帮助 / 退出教程”暂停菜单。此前停在教程询问的记录只覆盖启动流程，不能作为卡死结论。本轮新增实际操作证据，没有修改模拟器生产代码。

## 条件与身份

- 原 [极品飞车14之热力追踪中文版.jar](</home/lucinhu/Downloads/jar/极品飞车14之热力追踪中文版.jar>)，SHA-256 `77d10a56e51ae65c9b081ea1d192260fbb30c7c44c78cfe31cf221c79fe6cc52`，运行前后相同。
- 二进制来自 `25ef93f` 隔离源码，加 UC 专用调用/异常诊断；SHA-256 `eb6bd2dc39288244ab077c6bc73efafc7ed0becc02a87bc61cca9f7d138a3573`。诊断仅输出日志，未改变应用控制流；不是工作区中同期媒体改动的验证。
- `--port sdl2 --size 240x320 --heap 16 --fps 30`，默认设备配置、独立新 RMS，未加载快照、未开启网络。SDL 渲染缓冲截图为窗口放大后的 960×1280，包含模拟器触控覆盖层。
- 按 PID 和 `^nano_j2me$` 标题同时定位拥有的窗口，用 xdotool 发实际 down/up；`SDL_PollEvent` 诊断记录输入确实送达。所有截图来自实际 SDL backbuffer。

## 操作与画面

第一次有界运行确认菜单、车辆选择、加载及教程提示；第二次从新 RMS 启动并延长操作。没有用旧进程截图冒充本次结果。[首次运行条件](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-round15-20261020/manifest.json>) 和 [驾驶运行的完整动作时间、输入与截图帧号](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/manifest.json>) 均保留。

驾驶运行大致顺序为：启动 6 秒发 F2；10 秒后发 F1；4 秒后 Return；3 秒后 F1；等待加载并检查教程页；继续发 Return 推进提示，再以左右方向键各按住约 1.5 秒，最后发 F2。此前加载阶段的方向键输入不计为驾驶验证。

| 证据 | 检查到的状态 |
|---|---|
| [首次教程驾驶](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/09-tutorial-0.png>) | 教程 1/11，42 英里/时 |
| [后续 Return](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/09-tutorial-1.png>) | 73 英里/时，车辆和道路继续变化 |
| [进一步推进](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/10-soft1-followup.png>) | 教程 2/11，114 英里/时、摧毁分数提示 |
| [Right 后](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/11-steer-right.png>) | 121 英里/时，赛道与车辆姿态变化 |
| [Left 后](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/12-steer-left.png>) | 101 英里/时，后续道路与摧毁提示继续更新 |
| [F2 后](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/13-pause.png>) | 出现暂停菜单，可见“继续赛事”选项 |

[SDL 输入与运行日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/nfs-hot-pursuit-drive-round15-20261020/runtime.log>) 记录驾驶阶段 Right 在 tick 44812 按下、45777 放开，Left 在 47393 按下、48438 放开，F2 在 49966 按下、50204 放开。主机重复按键也有记录；不能将请求发键本身当成送达证明。

第二次运行共 1474 帧、1240 次 paint、210904 个三角形、18803 次 M3G 调用，51.258 秒、28.8 fps；检查暂停画面后主动终止拥有的进程，退出 0。默认 16 MiB 堆下记录 58 次 GC。该 fps 是当前桌面和诊断构建的实测值，不是 Android 或其他设备的性能承诺。

## 异常与验证边界

启动阶段仍有被捕获的 `RecordStoreNotFoundException`、`ah.<init>(MonkeyApp)V pc=518/526` 及 `ah.w()V pc=460/468` 空引用；它们没有阻止本次进入并持续绘制赛道。未记录 `uncaught exception` 或异常逃到帧循环。终止阶段的 `TimerTask.run() threw an exception: timer cancelled` 随受控退出出现，不能当成驾驶阶段的线程崩溃。

本次只验证教程 2/11 之前的短时驾驶、后续输入及打开暂停菜单。未完成全部教程、正式赛事或保存后冷启动，也未验证暂停后的继续、音效或 Android 真机。车辆和道路在左右键后持续变化，但本轮未做相同帧、相同初始状态的无输入对照，不声称已经量化转向角度或修复转向算法。原 JAR 和用户存档均保留。
