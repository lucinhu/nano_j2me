# Displayable.getKeyChar() 与「自绘输入框」收不到字符（2026-10-22）

## 结论

- **运行时根本没有 `Displayable.getKeyChar()`**。MIDP 2.0 把它放在 `Displayable` 上：
  「Gets the character representing the key that was pressed.」凡是调用它的
  MIDlet，**第一次按键就抛 `NoSuchMethodError`**，输入全部丢失。
  修复前实测（探针 `tests/keychardemo/KeyCharDemo.java`）：
  `thrown: java/lang/NoSuchMethodError at KeyCharDemo$1.keyPressed(I)V pc=0: KeyCharDemo$1.getKeyChar()I`。
- **现在它按 MIDP 键盘表回答**：数字键返回该数字（`KEY_NUM5` → `'5'` = 53），
  `KEY_STAR` → `'*'`（42），`KEY_POUND` → `'#'`；方向键/FIRE 等没有字符，返回 0。
- **端口可以把「用户真正输入的字符」交给运行时**（`nj_event.key_char`，
  `nj_midp_key_char()`），于是**自绘输入框的 Canvas 也能收到字符**——包括中文。
  桌面上实测：`a` → `code=97 char=97`；fcitx5 拼音提交「你好」→
  `code=20320 char=20320`（U+4F60 你）、`code=22909 char=22909`（U+597D 好）。
- **代价**：二进制 **+136 字节**（3,049,280 → 3,049,416，+0.004 %）。
  ctest **92/92**；`check_keychar` 新增为第 92 个测试。

## 环境与命令

| 项 | 值 |
|---|---|
| 工作树 | `/home/lucinhu/workspace/c/nano_j2me` |
| 基线二进制 | `.local-tools/size-base-20261022/build/nj2me`，SHA-256 `07dd3a01ac62f709fffa7f895638e720dadcb6697100815624998a027ddaced9`，3,049,280 B（`git archive HEAD` 的干净树，Release） |
| 加了本轮改动后 | 同一棵树的 `build/nj2me`，SHA-256 `1c695ddfaa532848ca90c1131146e609f8d9485abb049802a44c57b9d1a56d54`，3,049,416 B |
| 工作区二进制 | `build/nj2me`（含其他会话在途改动，**不可**用来做体积对比） |
| 探针 | `tests/keychardemo/KeyCharDemo.java`，fixture 由 `tools/make_keychardemo_jar.sh` 生成（`tests/fixtures/` 在 .gitignore 里，**不入库**） |
| 显示 | `DISPLAY=:1`（Xwayland）；键盘 XTEST 可用，指针 XTEST 不可用 |
| 证据根目录 | `.local-tools/debug-runs/keychar-20261022/` |

### 代码改动

| 文件 | 改动 |
|---|---|
| `src/midp/nj_lib_lcdui.c` | 新增 `midp_key_char(code)`（MIDP 键盘表）、`NJFN(d_getKeyChar)`、`nj_midp_key_char(vm, key, down, ch)`；`displayable_fields` 增加 `dKeyChar`，`displayable_methods` 与 native 表注册 `getKeyChar() ()I` |
| `src/midp/nj_midp.h` | `NJ_F_D_KEYCHAR "dKeyChar"`；声明 `nj_midp_key_char()` |
| `src/midp/nj_lib_midlet.c` | 事件分发把 `ev.key_char` 传给 `nj_midp_key_char()` |
| `include/nj2me/nj_port.h` | `nj_event` 增加 `nj_u4 key_char`（按键携带的字符，0 = 无） |
| `src/port/posix/nj_port_sdl2.c` | 新增 `sdl_push_char()` / `sdl_push_text()` / `sdl_utf8_next()`；`SDL_TEXTINPUT` 在没有 MIDP 文本框时把字符作为按键送出；没有按键映射的可打印字符（字母等）在 SDL 文本输入未开启时由按键本身送出；`NJ_SDL2_TEXT=always` 让文本输入常开（自绘输入框要用 IME 时需要） |
| `tools/midp_stubs.py` | `Displayable` 补上 `public final int getKeyChar()`（MIDP 里是 final） |
| `tests/keychardemo/KeyCharDemo.java`、`tools/make_keychardemo_jar.sh`、`tools/check_keychar.sh` | 探针与检查脚本（house style） |
| `tests/test_text_input.c` | 新增 `sdl_canvas_char_test()`：字母按键与 IME 提交都变成「带字符的按键」 |
| `CMakeLists.txt` | 生成 fixture + `add_test(NAME check_keychar …)` |

## 复现步骤

```bash
cd /home/lucinhu/workspace/c/nano_j2me

# 1. 探针（修复前后都可跑，修复前抛 NoSuchMethodError）
bash tools/make_keychardemo_jar.sh "$PWD/.local-tools/debug-runs/keychar-20261022/kc.jar"
./build/nj2me --port headless --heap 8 --size 176x208 --run-frames 120 \
    --keys '20:5 60:fire 100:star' --rms-dir "$PWD/.local-tools/debug-runs/keychar-20261022/rms" \
    --where "$PWD/.local-tools/debug-runs/keychar-20261022/kc.jar"
# 期望：KEYCHAR code=53 char=53 / code=-5 char=0 / code=42 char=42

# 2. 自动检查
bash tools/check_keychar.sh

# 3. 桌面上让自绘 Canvas 收到字符（ASCII）
DISPLAY=:1 ./build/nj2me --port sdl2 --size 176x208 --where "<kc.jar>" &
sleep 6; xdotool type --delay 200 "ab"          # 需要窗口有输入焦点

# 4. 桌面上让自绘 Canvas 收到中文（IME 提交）
DISPLAY=:1 NJ_SDL2_TEXT=always ./build/nj2me --port sdl2 --size 176x208 --where "<kc.jar>" &
sleep 6; fcitx5-remote -o; xdotool type --delay 250 "nihao"; xdotool key space
```

## 证据

### 1. 修复前：每次按键都抛 NoSuchMethodError

```
INFO: running KeyCharDemo (KeyCharDemo ?)
thrown: java/lang/NoSuchMethodError at KeyCharDemo$1.keyPressed(I)V pc=0: KeyCharDemo$1.getKeyChar()I
KEYCHAR code=53 threw java.lang.NoSuchMethodError
thrown: java/lang/NoSuchMethodError at KeyCharDemo$1.keyPressed(I)V pc=0: KeyCharDemo$1.getKeyChar()I
KEYCHAR code=-5 threw java.lang.NoSuchMethodError
thrown: java/lang/NoSuchMethodError at KeyCharDemo$1.keyPressed(I)V pc=0: KeyCharDemo$1.getKeyChar()I
KEYCHAR code=42 threw java.lang.NoSuchMethodError
```

### 2. 修复后：按键表回答正确

```
INFO: KEYCHAR code=53 char=53     <- KEY_NUM5 是字符 '5'
INFO: KEYCHAR code=-5 char=0      <- KEY_FIRE 没有字符
INFO: KEYCHAR code=42 char=42     <- KEY_STAR 是 '*'
```

`bash tools/check_keychar.sh` → `PASS check_keychar`（3 条断言 + 退出码 + 无异常）。

### 3. 桌面端口：自绘 Canvas 收到 ASCII 字符

`xdotool type --delay 200 "ab"`（真实 XTEST 按键，宿主 IME 关闭）：

```
INFO: KEYCHAR code=97 char=97
INFO: KEYCHAR code=98 char=98
```

### 4. 桌面端口：自绘 Canvas 收到中文（fcitx5 拼音提交）

`NJ_SDL2_TEXT=always` + `fcitx5-remote -o` + `nihao` + 空格：

```
INFO: KEYCHAR code=20320 char=20320   <- U+4F60 你
INFO: KEYCHAR code=22909 char=22909   <- U+597D 好
```

**中间态不泄漏**：宿主 IME 还在组字（preedit）时，键盘自己的字母**不**作为字符送出，
否则输入框会先得到 `nihao` 再得到「你好」。这靠 `sdl_state.ime_preedit` 判定。

### 5. 回归

| 检查 | 结果 |
|---|---|
| `ctest --test-dir build -j4` | **92/92 通过**（新增 `check_keychar`；`test_text_input` 加了自绘 Canvas 用例） |
| 孤岛余生2 `2023012708463745568.jar`（240x320，900 帧，键脚本 `210:fire 360:soft2 540:fire 720:fire 810:down`） | exit 0、uncaught 0、**末帧 142 种颜色**（与本轮之前逐位一致） |
| UC 浏览器 `app.jar`（assignment 模式，plan-a6，2400 帧） | exit 0、2 个请求（与之前一致） |
| 桌面 fcitx5 提交（MIDP TextField 路径） | `06-committed.png` SHA-256 `b5ccef81115db43326d169368e36540258e1518844c2e1c8c80709e442e7e694` —— 与 2026-10-21 起的证据**逐字节一致** |
| 二进制体积 | **+136 B**（3,049,280 → 3,049,416，+0.004 %） |

## 异常

本轮没有引入新的运行时异常。探针在修复前抛的
`java/lang/NoSuchMethodError at KeyCharDemo$1.keyPressed(I)V pc=0: KeyCharDemo$1.getKeyChar()I`
是**缺失 API 的症状**，不是模拟器缺陷。

## 边界与未验证项

- **Android 端口还没有字符通道**：`GameActivity$ImeEditor` 仍然只喂 MIDP 的
  `TextField`/`TextBox`；自绘 Canvas 在 Android 上仍收不到字符。本轮的
  `nj_event.key_char` 是端口可选字段，Android 的 JNI 桥没有填它。
- **`z`/`x`/`c`/`v` 不会作为字符送出**：桌面把它们映射成游戏 A/B/C/D 键，
  再送一次会让游戏收到两个按键。自绘输入框要这几个字母只能走 IME。
- **数字键也不会作为字符送出**（它们本来就有键码，`getKeyChar()` 直接按 MIDP 表回答）。
- **`NJ_SDL2_TEXT=always` 的代价**：宿主 IME 常开时，在没有 MIDP 文本框的游戏里打字
  也会弹出候选窗，宿主 IME 可能吞掉按键。默认（不设该变量）保持原行为：
  文本输入跟随 MIDP 编辑器，此时自绘 Canvas 只能收到 ASCII 字母（按键路径）。
- **UC 浏览器的地址栏仍未验证**：UC 的 JAR 里**没有** `getKeyChar` 的引用
  （`grep -rl getKeyChar` 在解包后的类里为 0），它用的是 `getGameAction`/`keyPressed`。
  本轮让 Canvas 能收到字符，但「UC 的自绘地址栏能不能显示这些字符」还没有实跑证据。
- 没有真机（arm64 手机）验证；Android 侧的中文输入结论仍然只覆盖 MIDP 文本框。
