# GameCanvas 隐藏 flush 与读档后的绘制状态

本变更处理两个 GameCanvas 状态边界：隐藏画布调用 `flushGraphics`，以及已运行过其他画布的 VM 成功加载快照。输入回调内切换 Displayable 的额外重绘问题不在本次范围内。

## 隐藏 GameCanvas

MIDP 2 对 `flushGraphics()` 和 `flushGraphics(x, y, width, height)` 都规定：GameCanvas 当前未显示时，调用立即返回，不更新显示。两种 native 入口都应在查找或补建 buffer 之前检查接收者是否等于当前 Displayable。这样隐藏画布既不会覆盖当前屏幕，也不会取得当前画布的 flush ownership。

此检查仅适用于两种 flush 入口。`paint(Graphics)` 仍可把隐藏 GameCanvas 的 buffer 复制到调用者提供的 Graphics；它与 flush 共用内部区域复制代码，不能在共享 helper 中一并禁止。

规范依据：[MIDP 2 GameCanvas](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/game/GameCanvas.html)。

## 成功读档后的 ownership 和 cadence

flush ownership 表示当前应用已通过 `flushGraphics` 主动发布画面，frame pump 因此暂停对 GameCanvas buffer 的自适应重绘。它属于宿主当前运行过程的状态，不是 Java 对象图的一部分。

成功加载快照会替换 Java arena 与当前 Displayable。加载前应用的 ownership 和绘制节奏不应继续支配加载后的应用。在 loader 最后一次 GC 成功之后、最终返回 `NJ_OK` 之前，清零以下四个宿主字段，使 live VM 成功读档后的起点与 fresh VM 一致：

- `app_painted`：绘制历史与 flush ownership。
- `app_painted_frame`：尚未结算到 cadence 的本帧应用绘制标记。
- `app_paint_idle`：先前应用连续没有绘制的帧数。
- `app_paint_period`：先前应用的自适应绘制间隔。

安装前拒绝加载时，原应用仍在运行，应保留其 ownership，否则可能把尚未 flush 的半成品提前显示。不能在 loader 入口、preflight 或所有失败路径中无条件清零。安装后的失败沿用原有 loader 失败处理；本次不重写该语义。

原 Canvas buffer、已序列化的 `repaint_pending` / `painted_in_frame`、builtin 字段布局和 snapshot 格式保持原样。

## 快照图像边界

快照保存当前 Java buffer，没有保存上一次 flush 后的完整 host screen 像素。成功读档后清理四个宿主字段，不代表保存或恢复了上一次 flush 的完整显示帧。

如果保存时 Java buffer 已被下一帧的部分绘制覆盖，恢复后仍只能使用该 buffer；“已 flush 的完整帧”和“正在绘制的半成品 buffer”没有作为两份图像同时保存。这是既有 snapshot 边界，本次不增加另一份图像缓存，也不承诺消除该边界。

## 回归范围与验证状态

[test_gamecanvas_flush.c](../../tests/test_gamecanvas_flush.c) 保留原有慢速生产者、输入、显式 repaint/serviceRepaints、显示切换与 legacy fallback 检查，追加：

- 隐藏画布的 full/rect flush 不改当前屏幕、不抢 ownership，当前非 flush GameCanvas 仍能自适应显示后续 buffer。
- 隐藏画布的 `paint(Graphics)` 仍可复制到离屏目标。
- 同一真实快照在 fresh VM 和先前已 flush 的 live VM 中加载后，显示相同的绿色 buffer，并继续呈现后续 legacy 绘制。
- 坏 CRC 被提前拒绝时，原应用保留已 flush 的蓝帧，未 flush 的半成品红帧不泄漏到屏幕。

fixture 使用 public Display/Graphics、真实 `nj_midp_frame`、`tmpfile` 与 `nj_vm_save` / `nj_vm_load`；不人工设置 ownership/cadence。保存前释放 C roots，成功加载后重新取得 Java 引用，并为每台 VM 附上有效 class source。

独立验证已使用同一最终 fixture 完成旧 normal 与严格 sanitizer 两档复现，均为 14,214 项检查、15 项失败：hidden full/rect 各 6 项，live load 的显示与 ownership 共 3 项。fresh load、启动状态前置条件、坏 CRC 拒绝后保留蓝帧的正例均通过；严格档没有 sanitizer 诊断。首版 fixture 的两处启动画布缺少自然 frame settle，曾引入另外 3 项干扰失败；最终版只补两次自然 frame，保留全部原断言，首版源码和日志也已保留。

修复后使用同一最终 fixture，normal 与严格 sanitizer 两档均为 14,214 项检查、0 失败。normal 最终覆盖 171 个注册目标：153 个首次完整通过的目标、17 个补齐私有依赖或 fixture 前提后的定向恢复目标，以及最后首次执行的 parallel 目标；这是聚合验证，没有重跑已通过目标，也不是单次 171 项全部通过的运行。最初的 3 个内部跳过已被实际执行的恢复结果替换，最终没有待执行目标。配置固有边界仍为 `NJ_ENABLE_ENGINE=0` 下 engine 检查数为零，以及 `no_statics` 排除两个 source：ESP32 示例需要平台 SDK，libretro 则因 process-global 入口的 ABI 约定获得明确豁免。

严格 sanitizer 仅覆盖 9 个相关目标：GameCanvas flush、LCDUI、Display.callSerially、Game、Form scroll、Save、Form snapshot、M3G 与 VM；9 项首次运行全部通过，没有 ASan、UBSan、float-cast-overflow 或 LeakSanitizer 诊断，不将其表述为 171 项全量 sanitizer。旧 [test_game.c](../../tests/test_game.c) 的 flush 像素例补上公共 `Display.setCurrent(c)` 与自然 startup frame，所有原有像素断言保留；严格档三轮各 786 项断言、共 2,358 项、0 失败，三轮销毁均释放 991 个 host blocks。上述结果不改变前述 host screen 未纳入快照的限制。
