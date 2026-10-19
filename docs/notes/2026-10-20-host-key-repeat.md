# 区分宿主按键重复与真实按下

SDL2 的自动重复 keydown 过去被作为新的物理按下发送。长按方向键、Enter 或右软键时，Canvas 可能重复触发按键回调和命令，GameCanvas 也可能在释放后重新积累按键锁存状态。简单丢弃宿主重复事件又会破坏原生 List/Form 的连续导航和文本编辑。

增加 NJ_EV_KEY_REPEAT 并在 MIDP 层分流：Canvas/GameCanvas 保持 hasRepeatEvents()==false，不重复派发 pressed/released 或修改按键状态；原生 List/Form 继续使用重复事件导航。SDL2 原生文本编辑仍在游戏按键翻译之前处理重复删除等操作。

`tests/test_text_input.c` 覆盖 SDL2 物理按下、三次重复及释放的顺序，Canvas 回调与命令次数，GameCanvas 持有/释放/锁存状态，以及 List、Gauge、TextBox 连续导航/编辑。Gauge 使用原有每次增加 5 的规则；测试最大值设为 100，避免饱和掩盖重复事件是否送达。

验证：完整 CTest 84/84 通过；SDL2 dummy 后端使用原版《3D版田园物语》、8 MiB 堆、30 fps 和独立 RMS 回放 600 帧，退出码 0。自动重复的精确事件验证由 SDL2 注入和 MIDP 回归完成；游戏回放检查一般输入流程。证据位于 `.local-tools/debug-runs/input-repeat-20261020/`。
