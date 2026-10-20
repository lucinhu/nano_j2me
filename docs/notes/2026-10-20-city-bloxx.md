# City Bloxx 实际落块与继续操作复核（2026-10-20）

原版 City Bloxx 已在 SDL2 下完成首块落地、后续落块操作、返回菜单及继续游戏。本轮补齐此前只有吊车画面的交互证据，未修改模拟器生产代码，也未验证完整一局、分数结算或冷启动存档。

## 运行条件

- 原 [app(1).jar](</home/lucinhu/Downloads/jar/app(1).jar>)，City Bloxx 1.0.12，SHA-256 `6979335c9b19d510965ad67160a6786a367bb9e2a6997088e434b77c2849b280`；运行前后相同。
- 使用 [固定 RTC 修复二进制](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/build/nj2me>)，SHA-256 `bde0bab93c5794a726a9c4f9eec1319b666ceca3b4ccdb48e16f759f04e0f806`。这是时钟修复批次的隔离构建，排除了在途 SAX 等改动；不将这次操作结果归因于新增引擎补丁。
- `--port sdl2 --device nokia6230 --size 240x320 --fps 30 --heap 16`；全新独立 RMS。Nokia 设备配置仍是该 JAR 越过平台白名单检查的必要运行条件。
- X11 按 PID 和窗口标题同时定位当前实例，发送实际 down/up；通过 `SDL_RenderReadPixels` 捕获渲染缓冲。实际窗口截图为 960×1280，包含模拟器按键覆盖层，不是 MIDlet 原始分辨率。
- [完整命令、哈希、输入与截图帧号](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/manifest.json>)、[SDL 按键及运行日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/runtime.log>)、[回放驱动](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/run_city_sdl.py>)均保留在本地实验目录。

## 已检查的操作与画面

| 操作 | 可观察结果 |
|---|---|
| 启动后 Down、Return，随后数字 5 | 从菜单进入快速游戏；[首块仍悬挂在吊绳上](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/03-help-or-crane.png>)，平台尚空 |
| 数字 5 按住约 600ms，释放后等待 1.5 秒 | [首块位于平台，吊车换上下一块](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/04-drop-0.png>)，确认实际发生了落块 |
| 再次数字 5，后续继续操作 | [后续块已脱离吊绳下降，吊车携带下一块](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/04-drop-1.png>)；这张图处于动画中，不证明多层稳定堆叠或分数结算 |
| F2，等待约 1 秒 | [返回菜单并出现 Continue quick game](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/05-after-pause.png>) |
| Return，等待约 1 秒 | [再次回到已有基座的吊车场景](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-round14-20261020/06-after-confirm.png>)，证明落块后菜单与继续操作仍响应 |

SDL 日志记录了每次数字 5 的 down/up，以及主机产生的 repeat 事件；不将 repeat 次数直接解释为游戏接受了多少次落块。记录的 1200 帧是运行上限，本轮在采集继续画面后主动终止，实际完成 **598 帧、598 次 paint、20.241 秒、29.5fps**。结束日志的帧预算提示及 exit 0 均不作为可玩性证据。

初始化仍记录 `RecordStoreNotFoundException` 与 `NullPointerException at House.f()V pc=54`，但随后游戏线程继续执行到上述场景；本轮未见 `uncaught exception`。这些已抛出日志不能单独认定为致命错误，也不能据此宣称异常路径完全正确。

## 仍待验证

此前 [启动根因报告](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/city-bloxx-20261017/REPORT.md>) 说明默认平台检查会让应用主动退出，`containsKey` 是后续症状；使用 `nokia6230` 到主菜单的结论继续成立，旧回放没有证明落块的限制由本轮补齐。

本轮只证明快速游戏首块落地、后续输入、返回菜单及同进程继续。尚未验证稳定多层堆叠、完整一局、建设城市模式、RMS 退出重启继续或 Android；同进程的 Continue quick game 不等于持久化存档。原 JAR、用户 RMS 及现有在途源码改动均保留。
