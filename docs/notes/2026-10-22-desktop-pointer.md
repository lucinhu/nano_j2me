# 桌面指针注入：Xwayland 上 XTEST 失效，改用 uinput 虚拟触摸屏（2026-10-22）

## 问题

本会话的 `DISPLAY=:1` 是 **Xwayland**（`ps` 里 `Xwayland :1 -rootless`，宿主的 Xorg 在 vt2）。
在这个显示上：

- `XWarpPointer()` 能移动指针；
- 但 `XTestFakeMotionEvent()` 返回成功却**不移动指针**，`XTestFakeButtonEvent()` 也**不产生**
  `ButtonPress`（自建窗口已选 `ButtonPressMask`，等 6 秒无事件）；
- 因此 `xdotool mousemove / mousedown / click` 全部无效，桌面点击无法送达模拟器窗口。
  键盘的 XTEST 正常，所以所有键盘回放都不受影响。

这曾让“文明5 回合按钮”“家园6 选中村民”等需要点击的验证无法进行
（见 [文明5 回合按钮与指针通道](2026-10-22-civ5-turn.md)）。

## 解决办法：uinput 虚拟触摸屏

宿主 `/dev/uinput` 对当前用户有写权限（ACL `user:lucinhu:rw-`），
于是用 `linux/uinput.h` 建一个绝对坐标触摸屏设备，由宿主输入栈（libinput → 合成器）
当真实触摸处理，合成器再为 X11 客户端（Xwayland）做指针模拟。

工具：`/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/pointer-20261022/vptr.c`
（不在产品源码里，只用于本机实验）：

```
gcc -O1 -o vptr vptr.c
./vptr <屏幕x> <屏幕y> [按住毫秒]     # 建设备、点一下、销毁
```

设备属性：`EV_KEY(BTN_TOUCH,BTN_TOOL_FINGER) + EV_ABS(ABS_X/ABS_Y, 0..2560/0..1600) + INPUT_PROP_DIRECT`，
名字 `nano_vtouch`，每次调用创建/销毁一次，约 0.4 秒完成。

## 验证

1. 模拟器窗口（240×320 内容、960×1280 窗口）里注入一次点击：
   SDL 端口日志出现 `NJMOUSE type=1025 but=1 x=479 y=511`（`SDL_MOUSEBUTTONDOWN`）与
   `type=1026`（`SDL_MOUSEBUTTONUP`），坐标与注入位置一致。
2. MIDP 侧：文明5 战役地图上点击 (94,26)（“Press 0”提示处）后，`NJPTR` 日志出现
   `kind=1 x=94 y=26`（pointerPressed）与 `kind=0`（pointerReleased），类名为游戏的 Canvas 子类。
   → 指针事件已经完整进入 MIDP。
3. 模拟器自带的屏幕键盘覆盖层会拦截点击（落在键帽上的按下会被当成按键，不下发指针，
   见 `src/port/posix/nj_port_sdl2.c` 的 `sdl_pointer_to`）。按 **F12 或 Tab** 可隐藏键盘覆盖层
   （端口帮助文本里写着 `F12/Tab=keypad`），之后再点击就落到游戏画面。

## 边界与后续

- 该通道是宿主触摸模拟，**不是**模拟器代码的改动；`vptr.c` 只在 `.local-tools` 下，
  不属于交付物，也不改变 `src/`。
- 触摸→指针的模拟由宿主合成器完成，因此只在本机环境可用；真机/其他桌面环境需要各自
  的注入方式。
- **按压缩放时间**：虚拟触摸屏的一次“点一下”默认只有几十毫秒，部分游戏（如文明5 手机版）
  只在**长按（约 400 ms）**时才触发界面动作，短按只会画出游戏自己的手指光标。
  `vptr` 的第三个参数就是按住毫秒数，做触摸 UI 时不要用默认的短按。
- 后续可继续用它在文明5 里寻找回合按钮的真实坐标、在家园6 里点选村民（这些游戏需要点击；
  键盘回放无法替代）。
