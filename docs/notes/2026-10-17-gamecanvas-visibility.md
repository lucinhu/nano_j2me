# GameCanvas 首次绘制缺少可见性回调：地心游记白屏

日期：2026-10-17

## 实际游戏证据

对象：`tests/games/地心探险(地心游记)_12471.jar`，MIDlet `zdxz.zdxz`。

修复前，headless、240×320、16 MiB Java 堆、30 fps、360 帧：

- 最终画面白屏，360 次绘制、1 个线程、17 次 GC。
- Java 字节码计数 5,975,376,276；结束时 Java 堆使用 12,822,448 / 16,630,032 字节。
- FreeJ2ME-Plus 对照在同样 240×320 分辨率下可进入主菜单。
- 游戏 `zdxz.b.showNotify()` 创建 Timer，并以 10 ms 延迟、50 ms 周期调度 TimerTask。模拟器没有交付这个回调，游戏循环没有启动。

实验日志和截图位于 `.local-tools/debug-runs/corpus-20261017/`：`earth-focused/` 为修复前，`earth-fixed/` 为修复后菜单，`earth-play/` 为输入回放，`earth-sdl/` 为 SDL2 dummy 驱动检查，`earth-8mb/` 为较小 Java 堆回放。每次运行使用独立 RMS 目录。

## 根因和修复

`src/midp/nj_lib_lcdui.c` 中普通 Canvas 的绘制使用 `canvas_call_paint_rooted()`；GameCanvas 分支直接使用 `canvas_call_paint()`，绕过 `canvas_visible()`。

复用现有包装函数，使 GameCanvas 也按顺序收到 `sizeChanged(width, height)`、`showNotify()`，然后绘制。包装函数同时将待绘制 Graphics 临时加入 GC 根，防止通知回调触发 GC 后引用失效。持续可见时不重复通知。

生产代码只替换一个调用，不新增依赖、模块、长期存活的堆对象或构建开关。相同构建配置下可执行文件 text 从 2,105,566 增至 2,105,638 字节（+72 字节）；data 155,848、bss 6,688 字节未变。保持现有 LCDUI/GameCanvas 的裁剪边界。

## 验证

`tests/test_game.c` 新增 GameCanvas 子类，覆盖：

- 首次绘制前交付尺寸与可见性通知，顺序和尺寸正确；
- `setCurrent` 仍将通知延迟到事件交付阶段；
- `showNotify()` 内强制 GC，后续 Graphics 仍有效且可绘制绿色像素；
- 后续绘制不重复发送通知。

Game API 测试三轮各 398 条断言，共 1,194 条通过；三轮释放的 host block 均为 854。完整 CTest 80/80 通过，用时 146.64 秒；`git diff --check` 通过。原始 JAR SHA-256 `e79adb92b6c39dcfd9abf5f6846957d03a69d2b9503af3af9c4d0b3f0e2a0850` 与探测时一致。

实际游戏：

- 修复后 360 帧：主菜单正常，2 个线程，定时器线程运行；无异常。
- 900 帧、30 fps，输入 `300:fire,480:fire,600:right,660:fire`：从菜单推进到世界地图，最终地图说明为“机器人研发基地”。实际 29.9 fps，结束正常，无异常。
- SDL2 使用 dummy video/audio 驱动，360 帧、30 fps：结束正常，无异常。此检查验证端口执行路径，不代表人工窗口操作验证。
- 8 MiB Java 堆重复相同 900 帧输入回放：同样进入“机器人研发基地”世界地图，无异常，29.9 fps，结束时 Java 堆使用 3,074,496 / 8,312,928 字节，48 次 GC（16 MiB 回放为 21 次）。这是 Java 堆配置与结束时用量，不是进程总内存或峰值内存。两个末帧字节不同，不声明逐像素等价；已分别检查画面确认同一地图阶段。

帧率受主动 30 fps 限制，不能据此声称运行速度提升；菜单恢复后运行阶段发生变化，修复前后 Java 堆统计也不作为同阶段内存优化结论。包装层仍有较高字节码开销，尚未优化。当前只确认菜单与世界地图推进，未覆盖全部关卡。

## 游戏库启动探测

以修复前构建对 395 个 JAR 进行每款 180 帧、30 fps、240×320 的有界探测，独立 RMS 和末帧截图，结果保存在 `results.jsonl`：390 个退出码 0、2 个退出码 1、3 个超过 35 秒。退出码 0 不代表可玩或无异常。

候选问题包括：`愤怒的小鸟_383.jar` 缺少 SAX `DefaultHandler`；`急速狂飙完美破解版_4964.jar` 工作线程数组越界；若干游戏回调空指针、除零；三个超时标题需单独复现。`1945空战英雄_6105.jar` 的默认平台拒绝和 `鲁宾逊漂流记汉化BT版_4677.jar` 的无有效 MIDlet manifest 需与运行时缺陷区分。不能将这一轮启动探测计为 390 款游戏兼容。
