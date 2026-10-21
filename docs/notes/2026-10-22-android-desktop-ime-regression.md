# Android 与桌面原生中文输入 / 受控网络回归（2026-10-22，当前 HEAD）

## 结论

在**当前 HEAD**（`cc3dc0e1370074d051814fc096b0b10c319c1284`）上重新走通了两条中文输入链路，
并用受控 HTTP 服务重新区分了「网络成功」与「网络失败」两种结局：

- **Android**：系统 Google 拼音输入法 → `GameActivity$ImeEditor` →
  `InputConnectionWrapper.finishComposingText/commitText` → JNI `updateNativeText` →
  MIDP `TextField`；最后在游戏自己的注册页上看到 `游戏账号: 你好`。
- **桌面**：本机 fcitx5 5.1.23 真实拼音（`Control+Space` 切上下文 → `nihao` → 空格提交），
  提交后的截图 SHA-256 与 2026-10-21、2026-10-22 之前的证据**逐字节一致**。
- **受控网络**：503 → 5 个 `GameComm` POST 后弹网络错误页；200 空 body → 2 个 POST 且**无**错误页。

**边界**：本轮只证明**输入链路**与**失败可区分**；没有提交注册、没有登录、没有验证线上战斗；
Android 全部在 **API 26 x86_64 模拟器**上做，**没有真实 arm64 手机**。

## 环境与命令

### 桌面

```bash
cd /home/lucinhu/workspace/c/nano_j2me
sha256sum build/nj2me                       # 记录当次二进制
python3 .local-tools/debug-runs/desktop-ime-20261022/run_desktop_ime.py xy-realime9-ime
```

驱动脚本是 `.local-tools/debug-runs/uc-ime-20261021/run_xy_ime_regress2.py` 的副本，
输出 `{"exit_code": 0, "sdl2_mapped": ["/usr/lib/libSDL2-2.0.so.0.3200.74"], "posts": 5}`。
它启动 SDL2 窗口（360x640）、挂一个本机 503 代理、按固定时间线发键：
F1@6.24/11.69/17.14 → F2@22.53 → F1@28.00/33.42 → Return@38.83（打开 MIDP 编辑器）
→ `ctrl+space`@43.04 → `nihao`@46.81 → 空格提交@47.53 → F1 确定 → 退出@57.60。

桌面真实拼音的前提（缺一不可）：

- `XMODIFIERS=@im=fcitx`、`LANG=zh_CN.UTF-8`；
- 首次必须按 `Control+Space`，因为输入上下文一开始落在 `keyboard-us`；
- 用**默认构建** `build/nj2me` + 系统 `sdl2-compat`；
  `/usr/lib/libSDL2-2.0.so.0.3200.74`。

### Android

```bash
# 用 HEAD 的干净副本构建 APK，避免另一个会话在途的 libretro/JIT 改动混入
git archive HEAD | tar -x -C .local-tools/android-head-20261022
cp build/android/debug.keystore .local-tools/android-head-20261022/build/android/   # gitignored
cd .local-tools/android-head-20261022
tools/build_android.sh     --abi "x86_64 arm64-v8a" --out "$PWD/build/android-classic"
tools/build_android_apk.sh --abi "x86_64 arm64-v8a" --out "$PWD/build/android-classic"
```

- `lib/x86_64/libnj2me.so` 1,550,208 B、`lib/arm64-v8a/libnj2me.so` 1,692,408 B
  （NDK 30.0.16248370，API 21）。
- APK `build/android-classic/nj2me.apk` 1,284,320 B，
  SHA-256 `ad1b4f4d7fc312f5222fdcb2932daac8d021add23481b92a1dfff5ef779bc6ef`。
- 模拟器：`/home/lucinhu/Android/Sdk/emulator/emulator -avd OpenHV_API26_googleapis
  -no-window -no-audio -no-boot-anim -no-snapshot-load -gpu swiftshader_indirect -port 5560`
  → `emulator-5560`，API 26，320x640，density 160。
- 安装：`adb -s emulator-5560 install -r -t build/android-classic/nj2me.apk` → `Success`。
- IME：`com.google.android.inputmethod.pinyin/.PinyinIME`（系统 Google 拼音）已启用；
  首次弹「Allow Google Pinyin Input to access your contacts?」需
  `adb shell input tap 253 367`（ALLOW）。
- 受控服务：本机代理 `.local-tools/debug-runs/android-regress-20261022/proxy.py`
  监听 `127.0.0.1:8000`，写 JSONL；模拟器的全局代理沿用 AVD 里已有的
  `10.0.2.2:8000`。
  启动写法（`pkill -f "python3 proxy.py"` 会连调用方 shell 一起杀掉，必须按 PID 停）：
  ```bash
  setsid env STATUS=503 LOGNAME_OVERRIDE=requests-503.jsonl \
      python3 ./proxy.py > proxy-503.log 2>&1 < /dev/null &
  ```

### 两个必须记住的 Android 坑

1. **`GameActivity` 没有导出**：API 26 上
   `am start -n com.lucinhu.nj2me/.GameActivity …` 直接
   `java.lang.SecurityException: Permission Denial: starting Intent { flg=0x10000000 cmp=com.lucinhu.nj2me/.GameActivity (has extras) } from null (pid=4508, uid=2000) not exported from uid 10078`
   （`src/port/android/manifest/AndroidManifest.xml` 里 `android:exported="false"`，这台 shell 没有
   START_ANY_ACTIVITY）。改成启动 `com.lucinhu.nj2me/.LibraryActivity` 再点条目 (160,165)。
2. **自绘按钮不吃瞬时点按**：`adb shell input tap` 的 down+up 立刻完成，游戏收不到；
   要用 `adb shell input swipe X Y X Y 500` 约 500 ms 长按。
   网络错误页的「返回」(218,185)、「游客注册」(160,283)、「注册协议-接受」(68,331) 同理。

## 复现步骤（Android 中文输入）

1. `adb -s emulator-5560 shell am start -n com.lucinhu.nj2me/.LibraryActivity`
2. 点条目 (160,165) → splash → 网络错误页
3. 长按「返回」(218,185) → 回到 splash
4. 长按「游客注册」(160,283) → 注册协议 → 长按「接受」(68,331) → 游戏注册表单
5. 长按「游戏账号」输入框 (175,73) → `GameActivity$ImeEditor` 出现；
   `adb shell dumpsys input_method` 显示 `mInputShown=true`、
   `mServedView=com.lucinhu.nj2me.GameActivity$ImeEditor{… 0,594-320,640}`
6. `adb shell input text nihao` → 拼音键盘显示「ni hao」与候选 你好/你/尼/泥/拟
7. **点候选不管用**（`input tap` 与 300 ms 长按都失败），改用
   `adb shell input keyevent 62`（空格）提交 → 编辑框出现「你好」，候选变为 吗/啊/像/的/好/了
8. `adb shell input keyevent 4`（BACK）收起软键盘
9. `adb shell input keyevent 82`（MENU = MIDP 左软键）→ 回到游戏注册表单，
   **账号框显示「你好」**

## 证据

目录 `.local-tools/debug-runs/android-regress-20261022/`：

| 文件 | SHA-256 | 内容 |
|---|---|---|
| `02-library.png` | `fc2cb272fce72b5fa7bd46b6d25ed0651e7fda278e09b8af1d43e180e500642e` | 库列表 |
| `04-game-launch.png` | `a44d9ff08ba4a32b8b2bd41d96315cb4ecc0fa2f87dfa81451c58be0a3a28f33` | 启动后的 splash（**与 200 空 body 那次逐字节相同**） |
| `05-game-proxy.png` | `dd2716ebe65e1d837f8c200ed7a9f373cde7e1b667fd79ed635970c10e9dd228` | 503 下的网络错误页「请检查设备网络连接是否已经开启！」+「返回」 |
| `09-guest-menu.png` | `38f8fd2fed5b44ba3ea5708bc2f31565fceadd4a0ee25596344c65a1c9477546` | 注册协议 |
| `10-register-form.png` | `9902b59f2b60769db8d31812dd45179abe9969926acb4756d7c2fe58a218fc7f` | 游戏注册表单 |
| `11-editor-ime.png` | `64617f27091832b524a4bc77299eb3c84b0d61574eab395c1d4944ba3658738f` | 拼音键盘已升起 |
| `12-pinyin-candidates.png` | `5f2e3f653c94f598c18e1ce10c136f0626b15424e2ef1f9228aaf91399f67067` | 「ni hao」+ 候选 你好/你/尼/泥/拟 |
| `13c-committed-space.png` | `f165acb0d3163bfd7f6514ba9fbff5137746720012b5e51fa2a75da76bc8a1fb` | 空格提交后编辑框为「你好」 |
| `14-editor-hidden.png` | `f082c63163dcd5cd058ad324cfaff4e7252e2fad6ccebbf480eabddc2280cdb4` | 收起软键盘后的 MIDP 编辑屏 |
| `15-after-menu.png` | `46fc730bc67bb1ba8824517ae69920380f063fde070a7296fe395ec23bbca28a` | **游戏注册表单：`游戏账号: 你好`**（决定性证据） |
| `16-game-200.png` | `a44d9ff08ba4a32b8b2bd41d96315cb4ecc0fa2f87dfa81451c58be0a3a28f33` | 200 空 body 下的 splash，无错误页 |

桌面目录 `.local-tools/debug-runs/desktop-ime-20261022/xy-realime9-ime/`：

| 文件 | SHA-256 |
|---|---|
| `01-start.png` | `77b6170910c504fd0c527620dfdd2b81dc15b19ad83f8d0990a6b2333c5edbfa` |
| `03-editor.png` = `04-ime-on.png` = `05-composing.png` | `aced73f8033802610ee97a5ca7b5a79574f087cf5cf7b088c525291ea0322c83` |
| `06-committed.png` | `b5ccef81115db43326d169368e36540258e1518844c2e1c8c80709e442e7e694`（与 2026-10-21 / 2026-10-22 前次**逐字节一致**） |
| `07-confirmed.png` = `09-back.png` | `c84090017b742605e9ee9070d3216d7f312d145a2930cf3448ee45e13674f034` |
| `08-final.png` | `c2fb86875cf79e84223be19b811d35e5133443423e8700d4bbbd7b8ac1c515d1` |

桌面 `manifest.json`：二进制 SHA-256
`2f10619898453398d799138bd31d2fc2f99a0fd1665140540e1ad6d520a5dbfd`
（**注意**：`build/nj2me` 会被另一个会话随时重建，本轮同一时段它还曾是
`af3f8df87b7669fbade9149889655d01d4d6a60e15347dcd8dc16191819321f4`（3,011,240 B）
与 `55227ba1eb6de6fc3efffa6f2f54541d274ae0309e1a3a9a75b8e953d1b1ffcc`；
**不要拿旧哈希当凭据**，跑之前自己 `sha256sum` 一遍）；
JAR 运行前后 SHA-256 均为 `bfa87c2046aa0d2b3847521ffdd34baf27a3de252b07031830d3b966eb2e66d2`；
退出码 0；受控 503 下 5 个 POST。

### 用 HEAD 干净构建复跑（2026-10-22 第 4 次）

因为 `build/nj2me` 会被别的会话随时重建，本轮又用 **HEAD 的干净构建**
（`git archive HEAD | tar -x -C .local-tools/head-build-20261022` 后自建，
SHA-256 `9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813`）
重跑了 3 次，驱动脚本用 `NJ_BIN` 环境变量指定二进制
（`.local-tools/debug-runs/desktop-ime-20261022/run_desktop_ime_headbin.py`）：

| 运行 | `06-committed.png` | `07-confirmed.png` |
|---|---|---|
| `xy-headbin-ime` | `aced73f8…`（**未提交**） | `aced73f8…` |
| `xy-headbin-ime-r2` | `b5ccef81115d…` ✅ | `c84090017b74…` ✅ |
| `xy-headbin-ime-r3` | `b5ccef81115db433…` ✅ | `c84090017b74…` ✅ |

**这条回归有约 1/4 的偶发失败率**：4 次里 1 次提交没进游戏，画面停在空的账号框
（`aced73f8…` 与「编辑器打开」那张一致）。原因在宿主输入法一侧（fcitx 的输入上下文
与提交时机），不是模拟器的缺陷——所以**判定通过必须看截图哈希，不能看退出码**。
现场看到的现象是：偶尔 `Control+Space` 没把上下文切到拼音，于是 `nihao` 只是英文字符、
空格也没有候选可提交。

## 受控网络的区分结果

| 代理返回 | 记录的 GameComm 请求 | 游戏表现 |
|---|---|---|
| 503 | 5 个 POST，body 各 48 B：`http://xy.liujin.cn:8000/center/GameComm` ×1、`http://xy.ljsy.net:8000/center/GameComm` ×2、`http://117.135.140.227:8000/center/GameComm` ×2 | 弹出网络错误页（`05-game-proxy.png`） |
| 200 空 body | 2 个 POST，均为 `http://xy.liujin.cn:8000/center/GameComm` | 无错误页，停在 splash，屏幕哈希等于 `16-game-200.png` == `04-game-launch.png` |

与 2026-10-21 的记录一致。计数时要把 AVD 自己的
`connectivitycheck.gstatic.com/generate_204` 探针过滤掉——它也走代理。

## 异常

- logcat 里**没有** `FATAL EXCEPTION`、没有 `UnsatisfiedLinkError`；
  唯一带 "exception" 字样的行是 Android 自己的网络探针：
  `PROBE_HTTPS https://www.google.com/generate_204 Probably not a portal: exception java.io.IOException: Unexpected response code for CONNECT: 501`
  ——这是代理对 CONNECT 的回应方式，不是游戏的问题。
- 桌面与 Android 两轮结束后，原 JAR
  `侠义OL-八龙聚首_诺基亚 N97系列(360×640)-871204.jar` 的 SHA-256 仍是
  `bfa87c2046aa0d2b3847521ffdd34baf27a3de252b07031830d3b966eb2e66d2`，**未被修改**。

## 边界与未验证项

1. **预编辑不进控件**：桌面端 `src/port/posix/nj_port_sdl2.c:1128-1130` 只设置 `ime_composing`，
   拼音候选窗由宿主输入法绘制，**游戏内看不到候选列表**；提交后才有中文进 MIDP 文本框。
2. **注册/登录/在线战斗全部未验证**，账号也没有创建。
3. **真实服务器不可用**：受控代理只证明「请求发出去了、能区分成功与失败」，
   不能证明 UC/侠义/仙镜的服务还活着。
4. **没有真实 arm64 手机**：所有 Android 证据来自 API 26 x86_64 模拟器；
   真机的输入法与触摸行为可能不同。
5. **Socket / HTTPS / UC 真实响应语法**在 Android 上仍未验证。
