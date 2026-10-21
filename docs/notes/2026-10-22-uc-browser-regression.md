# UC 浏览器回归取证（2026-10-22）

在当前代码上重跑 UC 浏览器（`app.jar`）的受控应答、真实网络与输入路径，回答三个问题：受控链路是否仍然可用；HTTP 503 到底卡在哪里、是服务端问题还是模拟器缺陷；以及如何完整复现。

**摘要**：受控分配响应（本地构造的 41 字节负载）下，欢迎协议 → 空白页 → 主菜单 → 菜单项移动 → F2 关闭菜单 → 焦点巡游（地址栏黄色高亮）→ 按 Return 打开 UC 的 URL 输入页，全部仍然可用（全部在 HEAD 干净构建 `9f6325a7…` 上复跑确认）。真实 HTTP 503 流程的停顿点已经定位到应用自身的错误处理路径：UC 构造网络错误提示时调用 `Displayable.addCommand(null)`，模拟器按 MIDP 规范抛出 `NullPointerException: command is null`，该异常打断了 UC 的初始化，界面因此永久停在「初始化: 测试网络[第1次]」+「取消」；这是**服务端不可用 + 应用错误处理缺陷**的共同结果，不是模拟器缺陷——对照实验显示，只要服务端返回 200（真实网络路径同样返回 200），UC 自己就能越过网络测试并渲染完整首页。本轮唯一确认的**模拟器缺陷**是：在 UC 的 URL 编辑器里输入文本时，解释器在 MIDlet `paint()` 期间 `SIGSEGV`（在 HEAD 干净构建上同样复现，可排除在途 superinstr/jump-threading 改动的影响）。UC 的地址栏是自绘控件而非 MIDP `TextBox`，模拟器的原生输入法桥不会在其中激活，所以「地址输入」这一项目标本轮仍未达成。

## 环境与命令

| 项目 | 值 |
|---|---|
| 干净 HEAD 构建 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/head-build-20261022/build/nj2me`，SHA-256 `9f6325a76fb71857ee6b21606b5219771cf70df03e753b36e79fb1226f42d813`（mtime 2026-10-22 06:09） |
| 工作区构建（早期取证用） | `/home/lucinhu/workspace/c/nano_j2me/build/nj2me`，SHA-256 `2849376f1b64e5015bb7421a0932e297f9757a73b4e74447819b67341bdf3991`（mtime 06:11，06:31 重新链接） |
| ASan+UBSan 构建（本轮自建，仅用 HEAD 源码） | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-asan-build/nj2me`，SHA-256 `b9f1e639a96a2880b982dbaf9304b5a70c9e14253f289c60bb68347e1d24eb2b` |
| 被测 JAR | `/home/lucinhu/Downloads/jar/app.jar`，UC 9.0.0，530861 字节，SHA-256 `38e7c3f7a9d06c8f6d04d4e6f83b33dda787fa7191445e619c4d9d11c3bcb3a2` |
| 受控负载 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/derived-assignment/assignment.bin`，41 字节，SHA-256 `e4ea0b6c768c4e588aed995aad79811e07739c5801fd979733bfda860d9b4c72` |
| 复用夹具 | `/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/uc_harness.py`（loopback HTTP fixture + SDL2 截图 + xdotool 输入）、`uc_headless.py`（同一 fixture 的 headless 版，支持 `--mode real`） |
| 显示 | `DISPLAY=:1`（Xwayland），键盘可用；XTEST 指针在本机失效，故全部证据只用键盘 |

按 Lead 的要求，**本轮关键取证全部用 HEAD 干净构建复跑**（`head-*` 运行目录）；工作区构建的结果保留为 `cur-*` 目录，仅作对照。Lead 已记录工作区在途解释器快路径（`NJ_ENABLE_SUPERINSTR` + `NJ_ENABLE_JUMP_THREADING`）会打挂真实游戏，详见 [superinstr 回归说明](</home/lucinhu/workspace/c/nano_j2me/docs/notes/2026-10-22-superinstr-regression.md>)（只读，未修改）。

所有实验均使用各自目录下的新 RMS（`--rms-dir`），不触碰任何既有用户存档；JAR 运行前后 SHA-256 一致。

## 复现步骤

### A1 受控分配响应（HTTP 200 + 本地 41 字节负载）

```bash
cd /home/lucinhu/workspace/c/nano_j2me
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_harness.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment \
  --mode assignment --plan .local-tools/debug-runs/uc-regress-20261022/plan-a1.json \
  --frames 2400 \
  --binary .local-tools/head-build-20261022/build/nj2me
```

`plan-a1.json` 的动作序列：等待 → 截图 `01-welcome` → F1 → 截图 `02-blank` → F1 → 截图 `03-menu` → Down → 截图 `04-menu-down` → F2 → 截图 `05-menu-closed` → Up → 截图 `06-up` → Return → 截图 `07-fire` → Return → 截图 `08-fire2`。

> 注意：`--out` 必须是**绝对路径**。相对路径下模拟器（工作目录为仓库根）会把 `NJ_SDL2_CAPTURE` 的帧写到不存在的相对目录，夹具的收帧线程永远等不到帧，截图会抛 `TypeError: cannot unpack non-iterable NoneType object`，随后夹具在 finally 里终止模拟器，看起来像「早早退出」。

### A2 HTTP 503 对照（真实故障路径）

```bash
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_headless.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-h503 --mode 503 \
  --frames 600 --keys '240:soft1 300:soft1' \
  --dump-frames $PWD/.local-tools/debug-runs/uc-regress-20261022/head-h503-frames/ --dump-every 30 \
  --binary .local-tools/head-build-20261022/build/nj2me
```

`--dump-frames` 指向的目录必须**事先存在**（`src/port/posix/nj_port_headless.c:785-805` 不会创建目标目录）。

### A3 真实网络对照（不加代理，直接出网）

```bash
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_headless.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-hreal --mode real \
  --frames 600 --keys '240:soft1 300:soft1' \
  --dump-frames $PWD/.local-tools/debug-runs/uc-regress-20261022/head-hreal-frames/ --dump-every 30 \
  --binary .local-tools/head-build-20261022/build/nj2me
```

### A4 地址栏 / 输入法路径

```bash
# A4a 焦点巡游：空白页 → Down×3 / Up×3 → 地址栏黄色高亮 → Left / Right
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_harness.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-a2-focus-walk \
  --mode assignment --plan .local-tools/debug-runs/uc-regress-20261022/plan-a2.json --frames 2400 \
  --binary .local-tools/head-build-20261022/build/nj2me

# A4b 地址栏按 Return 打开 UC 的 URL 输入页（再 Return / F2 关闭）
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_harness.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-a4-fire-address \
  --mode assignment --plan .local-tools/debug-runs/uc-regress-20261022/plan-a4.json --frames 2400 \
  --binary .local-tools/head-build-20261022/build/nj2me

# A4c 只按键不输入的对照组（40 秒空转）
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_harness.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-a7-noinput \
  --mode assignment --plan .local-tools/debug-runs/uc-regress-20261022/plan-a7.json --frames 2400 \
  --binary .local-tools/head-build-20261022/build/nj2me

# A4d 在 URL 编辑页内输入文本（HEAD 上仍 SIGSEGV）
DISPLAY=:1 python3 .local-tools/debug-runs/uc-regress-20261022/uc_harness.py \
  --out $PWD/.local-tools/debug-runs/uc-regress-20261022/head-a6-editor \
  --mode assignment --plan .local-tools/debug-runs/uc-regress-20261022/plan-a6.json --frames 2400 \
  --binary .local-tools/head-build-20261022/build/nj2me
```

### A5 服务端可用性取证（与模拟器无关）

```bash
getent hosts uc.ucweb.com ucus.ucweb.com          # 28.0.0.163 / 28.0.0.164（mihomo TUN 的 fake-IP 段）
ip route | grep 28.0.0.0                           # 28.0.0.0/30 dev Mihomo src 28.0.0.1
curl -sS -D- http://uc.ucweb.com/ | head            # 200 / Server: TwistedWeb/2.2.0 / Content-length: 11 / "hello world"
curl -sS -D- -H 'Host: bogus.invalid' http://28.0.0.163/ | head -3   # 403 Forbidden（TwistedWeb），按 Host 拦截
curl -sS "https://dns.google/resolve?name=uc.ucweb.com"              # 168.235.205.6 / 168.235.206.1 (TTL 600)
curl -sS -o /dev/null -w '%{http_code}\n' "https://api.allorigins.win/raw?url=http%3A%2F%2Fuc.ucweb.com%2F"  # 522
curl -sS -o /dev/null -w '%{http_code}\n' "https://api.codetabs.com/v1/proxy?quest=http://uc.ucweb.com/"        # 522
```

## 证据

### A1 受控应答（HEAD 干净构建，退出码 0，2 次 POST）

| 操作 | 截图 | SHA-256 |
|---|---|---|
| 启动 | [01-welcome](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/01-welcome.png>) | `85d217c8bcb5ebc01cb1d5b4b5ae0458242d8ee110993738049080cbd44cb4d5` |
| F1 确定 | [02-blank](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/02-blank.png>)（含地址栏、历史、帮助） | `bb8aa1f0370c233ccfd9f8d708ca5c9d2b20fbc242f64391e525f19a571a4ade` |
| 再 F1 | [03-menu](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/03-menu.png>)（主菜单 1.打开 ▶ … 8.退出） | `a27726f95d1155ad31162d06e520202d344319e04368883bbc9ec0fae8e92a61` |
| Down | [04-menu-down](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/04-menu-down.png>)（选中移到「2.窗口」） | `81f7615712dd9252584565300678d42571bb945653a91b5505133e2be102b8ab` |
| F2 关闭 | [05-menu-closed](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/05-menu-closed.png>) | `bb8aa1f0370c233ccfd9f8d708ca5c9d2b20fbc242f64391e525f19a571a4ade`（与 02-blank 逐字节相同） |
| Up / Return | [06-up](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/06-up.png>)、[07-fire](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/07-fire.png>) | `c7f9027131ab640922915742b78432e404d92c23a147f16be6f26c69f6950e06`、`d439705dcdcacd9a095c4798e4a10be2d7a65aef6d0f5725574713a043a70804` |

完整清单见 [manifest.json](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/manifest.json>)、[运行日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/runtime.log>)、[2 次 POST](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a1-assignment/proxy-requests.json>)。

焦点路径（**HEAD 干净构建** `head-a2-focus-walk`，退出码 0，2 次 POST）：空白页上 Down → **「历史」蓝色高亮** → Down → **「帮助」蓝色高亮** → Down → 无变化（与上一帧逐字节相同 `73776e20dcd73dcd19b6e83fd31da183ae128ea2174b5d8ab49831f749df2864`）→ Up → 回到「历史」→ Up → 无高亮 → Up → **地址栏「请输入网址」黄色高亮** → Left → 高亮消失 → Right → 高亮消失。

| 帧 | SHA-256 |
|---|---|
| [02-blank](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a2-focus-walk/02-blank.png>) | `783313214ec3bcc850633c9f312237a44a64a22364c0193f968ab823baca6d14` |
| [04-down2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a2-focus-walk/04-down2.png>)（「帮助」高亮） | `73776e20dcd73dcd19b6e83fd31da183ae128ea2174b5d8ab49831f749df2864` |
| [08-up3](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a2-focus-walk/08-up3.png>)（**地址栏黄色高亮**） | `3b25a3091567277009e9e2aee3289a131f591d637e1e6c3a7d7e016f6eafc02c` |

聚焦顺序的完整对照见接触表 [head-a2-focus-contact.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a2-focus-contact.png>)（SHA-256 `249468245c8f9edf012b2634264bc5981ac7d7226b382e9c1fa02d31124c985e`）。

在地址栏上按 Return 会打开 UC 自己的 URL 输入页（标题「L 请输入网址 … R ok」、空输入框 + 蓝色光标 + ▼、下方「搜索:」行）；再按 Return 或 F2 会回到空白页。**HEAD 干净构建证据**（`head-a4-fire-address`，退出码 0）：

| 帧 | SHA-256 | 画面 |
|---|---|---|
| [03-address-focused](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a4-fire-address/03-address-focused.png>) | `26dd5b4f528af0b7d3ff4707a28465c4646cc1f313f4bb4c025cfca4ddf962a0` | 空白页，地址栏黄色高亮 + 光标 |
| [04-after-fire](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a4-fire-address/04-after-fire.png>) | `b51b4a03ab91b6f0727f2c93f365424f10080bf8880b39f4c617cfe9b910b9e1` | **URL 输入页**（标题「请输入网址 … ok」、空输入框 + 蓝色光标 + ▼、「搜索:」行） |
| [05-after-fire2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a4-fire-address/05-after-fire2.png>) | `26dd5b4f528af0b7d3ff4707a28465c4646cc1f313f4bb4c025cfca4ddf962a0`（与 03 逐字节相同） | 再按 Return 关闭编辑器，回到空白页 |
| [06-after-f2](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a4-fire-address/06-after-f2.png>) | `7ed567b4a2c246a76268eb1a9217d9c7d228a70770c3827adac5c18f1d9d7dc3` | F2 关闭编辑器，回到空白页 |

另有一组**只按键不输入**的对照组（`head-a7-noinput`，HEAD 干净构建，退出码 0）：启动后 40 秒内 `01-welcome`…`06-blank-plus40` 全部停留在同一张空白页（`c2351f5f96f9bfc0b900e9b1b8e1d619e216c7f2a2ab551ea1c05b3ecb500cc8`）⇒ 触发首页渲染/崩溃的是**输入动作本身**，不是时间流逝。

> 注：工作区构建 `2849376f` 的同一条焦点路径（`cur-a2-focus-walk`）在最后两步还看到「搜索」按钮高亮，HEAD 构建则看不到可见高亮；这属于焦点指示的渲染差异，不影响「地址栏可聚焦、Return 可打开 URL 编辑页」这一结论。

### A2 HTTP 503：停顿点（HEAD 干净构建，退出码 0，4 次 POST）

20 帧全部逐字节相同（SHA-256 `b7053e86114562e6…`，帧目录 [head-h503-frames](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-h503-frames/>)）：画面停在 UC 9.0 启动页「初始化: 测试网络[第1次]」，右下角「取消」按钮，即「取消操作中…」。日志见 [runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-h503/runtime.log>)。

**精确停在哪里**（异常顺序即执行顺序，见日志）：

1. `RecordStoreNotFoundException`（首次运行没有 RMS，属正常探测，抛出后被应用处理）；
2. 网络测试失败 → UC 构造「网络错误」提示框 → 调用 `Displayable.addCommand(Command)` 时传入的 `Command` **为 null** → 模拟器按 MIDP 规范抛 `java/lang/NullPointerException … pc=0: command is null`；
3. 该 NPE 打断了 `bi.o()V pc=29` 与 `bi.run()V pc=1635` 的初始化链，**后续的「创建命令 / 装载首页 / 显示主界面」全部没有执行** → 界面永远停在启动页。

2026-10-20 那轮定位到的调用链（`docs/notes/2026-10-20-uc-browser.md`）给出了**为什么这个 null 会存在**：首次运行的顺序是 `bi.v()V pc=45` 的网络测试**先于** `bi.v()V pc=492 → bi.b(Z)V → g.c(Z)V → g.a(ZZ)V pc=139 → n.e()` 的命令初始化，而错误提示框恰恰要挂在这个还没建好的命令列表上。于是：503 → 应用走错误分支 → 错误分支自身有 bug → 初始化中断。**模拟器这一侧的行为（对 `addCommand(null)` 抛 NPE）是符合 MIDP 规范的正确行为**，不该为了"让它跑过去"而改模拟器。

### A3 真实网络：能越过网络测试（HEAD 干净构建，退出码 0）

20 帧分成 3 组：`b364b1a1e44800b0…`（8 帧，欢迎协议）→ `c8bd4c553a08604d…`（2 帧，按下 soft1 之后）→ `2142e86632e67c01…`（10 帧，**完整首页**：顶栏「uc浏览器 | 帮助」、搜索框「uc浏览器」+ 搜索按钮、图标行 首页/★/游戏/音乐/我的、我的导航/手机酷站/生活服务/小游戏库/游戏专区、底部「历史」、软键「菜单 / 切换」）。接触表见 [cur-hreal-contact.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/cur-hreal-contact.png>)（SHA-256 `2a0bfcf4cdb6c755dae56d9f59ef9620a79043373cb4f92344ab9641b3f51425`）。也就是说：**只要服务端给的不是 5xx，UC 自己就能走完初始化并渲染首页**，503 停顿是错误分支的问题。

### A4 输入路径

| 场景 | 结果 | 证据 |
|---|---|---|
| 空白页地址栏直接输入 `example.com`（**HEAD 干净构建** `head-a5-type`） | 退出码 0，**不 SIGSEGV**；123 次 `NoSuchMethodError`；**地址栏仍然是空的**（仍显示占位符「请输入网址」），画面主体换成 UC 首页内容（UC浏览器 / 关注动态 \| 反馈意见 / 新版功能 / 界面优化 / 功能优化 / 无痕浏览） | [03-address-focused](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a5-type/03-address-focused.png>) `06ce96db36ba8823231e8ffa8213e814d686b1d13ada63528fb5d16d6cec7a4d` → [04-typed-ascii](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a5-type/04-typed-ascii.png>) `049b85b6c4c2b6e82b232e95974a4f741fc8d7400d24d029ef484e9669c1cad2`（04/05/06 三帧逐字节相同） |
| 空白页地址栏直接输入 `example.com`（工作区构建 `2849376f`，对照） | 退出码 0，**不再 SIGSEGV**；127 次 `NoSuchMethodError`；同样跳到 UC 首页、地址栏无字符 | [cur-a5-contact.png](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/cur-a5-contact.png>)（SHA-256 `5624c0b69b0adcb25b34503184f9256a0e4873481f407eb5d1e67f48ae045f8e`） |
| URL 编辑页内输入 `http://example.com`（HEAD 干净构建） | **退出码 -11（SIGSEGV）**，118 次 `NoSuchMethodError`；05/06/07/08 截图逐字节相同 `412bf8b041bf64fac7e88ff3d4c29fc7e6745400883df83994e8d0b7e3cf3870` | [head-a6-editor](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a6-editor/>) |
| URL 编辑页内逐字符输入 a/b/c（工作区构建） | 退出码 0，**画面完全不变**（05–09 与 04 逐字节相同）→ 字符根本没进编辑框 | [e3-editor-onechar](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/e3-editor-onechar/>) |
| 只按键不输入、40 秒空转（**HEAD 干净构建** `head-a7-noinput`） | 退出码 0，画面始终停在空白页（6 帧逐字节相同）→ 触发首页渲染的是输入动作本身 | [head-a7-noinput](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a7-noinput/>)、[a7-noinput-control](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/a7-noinput-control/>) |

gdb 回溯（工作区构建 `2849376f`，`cur-a6-gdb/runtime.log` 第 240 行起）：

```
Thread 1 "nj2me" received signal SIGSEGV, Segmentation fault.
#0  nj_interp_run ()
#1  vm_invoke ()
#2  nj_call ()
#3  nj_midp_paint ()
#4  nj_midp_frame ()
#5  nj_run ()
#6  main ()
```

HEAD 干净构建上同样复现（退出码 -11），因此**该崩溃与在途的 superinstr / jump threading 改动无关**，是解释器自身的缺陷。

ASan+UBSan 构建（HEAD 源码，`b9f1e639…`）在 2400 帧内**没有**崩溃（堆布局不同，崩溃不复现），但报出一条确定的未定义行为：

```
src/core/nj_lib_string.c:941:15: runtime error: null pointer passed as argument 2, which is declared to never be null
    #0 str_startsWith1     src/core/nj_lib_string.c:941
    #1 nj_invoke_native    src/core/nj_interp.c:173
    #2 interp_invoke       src/core/nj_interp.c:223
    #3 nj_exec             src/core/nj_interp.c:1744
    #4 vm_invoke           src/core/nj_vm.c:1343
    #5 nj_call             src/core/nj_vm.c:1509
    #6 canvas_call_abstract src/midp/nj_lib_lcdui.c:4652
    #7 canvas_call         src/midp/nj_lib_lcdui.c:4670
    #8 nj_midp_key         src/midp/nj_lib_lcdui.c:4866
```

即 `String.startsWith(String)` 被传入 `null` 实参，`memcpy` 收到空指针。

### A5 服务端可用性

| 观测 | 结果 |
|---|---|
| `getent hosts uc.ucweb.com` | `28.0.0.163`（`28.0.0.0/30 dev Mihomo src 28.0.0.1`，mihomo/clash TUN 的 fake-IP 段；`verge-mihomo` 进程在跑） |
| `curl http://uc.ucweb.com/` | `HTTP/1.1 200 OK`、`Server: TwistedWeb/2.2.0`、`Content-length: 11`、正文 `hello world` |
| 同一 IP 换 Host | `403 Forbidden`（TwistedWeb）→ 拦截按 Host 进行 |
| 对照域名 | `example.com` → `Server: cloudflare`；`www.baidu.com` → `Server: bfe` → 通用网络正常 |
| DoH 解析 | `uc.ucweb.com` → `168.235.205.6` / `168.235.206.1`（TTL 600）；`ucus.ucweb.com` → `168.235.206.10` / `.11` → 域名仍然公开解析 |
| 第三方取回服务 | `api.allorigins.win` 与 `api.codetabs.com` 代理取 `http://uc.ucweb.com/` 均为 `HTTP/2 522`（连接超时）；`r.jina.ai` 返回 422（内容类型不符）→ 从外部看源站 80 端口不可达 |
| 本机监听 | `127.0.0.1:8000`（一个空 body 的 BaseHTTP 代理）、`127.0.0.1:7897`（clash mixed）、`127.0.0.1:18788`（api.anthropic.com 中继），都不提供 ucweb.com |

说明：TUN 设备会接管全部出站流量，所以 `curl --resolve uc.ucweb.com:80:168.235.205.6` **并不能**绕过本机代理（仍返回同样的 `TwistedWeb / hello world`），源站不可达的独立证据来自第三方取回服务的 522。

## 异常（精确文本）

HTTP 503 路径（[head-h503/runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-h503/runtime.log>)，均为**已抛出并被应用捕获**的异常，不是未捕获异常）：

```
INFO: thrown: javax/microedition/rms/RecordStoreNotFoundException at javax/microedition/rms/RecordStore.openRecordStore(Ljava/lang/String;ZIZ)Ljavax/microedition/rms/RecordStore; pc=0: (no message)
INFO: thrown: java/lang/NullPointerException at javax/microedition/lcdui/Displayable.addCommand(Ljavax/microedition/lcdui/Command;)V pc=0: command is null
INFO: thrown: java/lang/NullPointerException at bi.o()V pc=29: (no message)
INFO: thrown: java/lang/NullPointerException at bi.run()V pc=1635: (no message)
```

真实网络路径（[head-hreal/runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-hreal/runtime.log>)）：

```
INFO: thrown: java/io/EOFException at java/io/DataInputStream.readUTF()Ljava/lang/String; pc=0: (no message)
INFO: thrown: java/io/EOFException at java/io/DataInputStream.readByte()B pc=0: (no message)      （×15）
INFO: thrown: java/lang/ClassCastException at bj.d([B)I pc=0: [B                                   （×4）
```

输入路径（[head-a6-editor/runtime.log](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-regress-20261022/head-a6-editor/runtime.log>)）：

```
INFO: thrown: java/lang/NoSuchMethodError at aa.a(ILjava/lang/String;)I pc=12: [I does not implement java/lang/String.length
INFO: thrown: java/lang/NoSuchMethodError at aa.a(ILjava/lang/String;)I pc=12: [Z does not implement java/lang/String.length
INFO: thrown: java/lang/VerifyError at aa.b(Ljava/lang/String;Ljava/lang/String;)Z pc=28: receiver is not an object reference
INFO: thrown: java/lang/IllegalArgumentException ... unknown key code      （来自 NJFN(d_getKeyName)，非致命）
```

## 兼容状态

| 能力 | 状态 | 依据 |
|---|---|---|
| 启动并显示中文欢迎协议 | works | A1 `01-welcome` |
| F1 进入空白页（含地址/搜索/历史/帮助） | works | A1 `02-blank` |
| F1 打开主菜单、Down 移动选中、F2 关闭 | works | A1 `03/04/05-menu-closed` |
| 焦点可移到地址栏（黄色高亮） | works | `head-a2-focus-walk` 的 `08-up3` |
| 地址栏按 Return 打开 URL 编辑页 | works | `head-a4-fire-address/04-after-fire` |
| 在地址栏/编辑页输入文本 | **blocked-by-app**（UC 自绘控件，未走 MIDP `TextBox`，模拟器原生 IME 桥不参与） | A4 三组对照 |
| 真实网络下渲染首页 | partially works（首页能出，但分配协议解析失败：`EOFException`/`ClassCastException`） | A3 |
| 真实网络下加载实际网页/搜索 | **blocked-by-server**（源站 522 / 占位 `hello world`） | A5 |
| HTTP 503 下的取消流程 | **blocked-by-app**（应用给 `addCommand(null)`，按 MIDP 规范抛 NPE 后初始化中断）+ **blocked-by-server** | A2 |
| 编辑器输入时的解释器稳定性 | **模拟器缺陷**：`paint()` 内 SIGSEGV（HEAD 干净构建同样复现） | A4 gdb |

## 边界与未验证项

- **未验证**：真实 UC 服务上的实际网页浏览、搜索、书签、历史、下载、账号登录。受控负载是本地派生的 41 字节夹具，只满足分配解析器的非空与冒号检查，不是历史真实响应；因此「首页能渲染」不等于「远端资源安装成功」。
- **未验证**：模拟器原生输入法（fcitx5 / Android 拼音）在 UC 地址栏中的可用性。模拟器的 IME 桥只在当前 Displayable 是 MIDP `TextBox` 或焦点在 `Form` 的 `TextField` 上时激活（`src/midp/nj_lib_lcdui.c:8511-8566`），UC 的地址栏不满足该条件，因此这条路径需要应用侧改为标准 MIDP 控件（或模拟器侧另加一条自定义控件的文本注入通道）。
- **未验证**：Android / libretro 端口的同一路径。
- **待修（不在本轮修改源码）**：解释器在 MIDlet `paint()` 期间的 `SIGSEGV`。定位到的可操作事实有三条：
  1. 崩溃发生在 `nj_interp_run()` 内部，由 `nj_midp_paint → nj_call → vm_invoke → nj_interp_run` 调用链进入（gdb 回溯，无符号化行号，因为发布构建无 DWARF）。
  2. 崩溃前应用侧稳定出现 `NoSuchMethodError: [I does not implement java/lang/String.length`（UC 自己的类型混淆：`aa.a(ILjava/lang/String;I` 收到 `int[]`/`boolean[]`），并伴随 `VerifyError: receiver is not an object reference`；也就是说解释器在「方法解析失败后如何从异常处理器恢复」的路径上很可能是罪魁。ASan 构建下同一路径没有崩，说明与堆布局相关，属于未定义行为。
  3. `src/core/nj_lib_string.c:941`（`str_startsWith1`）存在把 `null` 传给 `memcpy` 的未定义行为；`src/midp/nj_lib_lcdui.c` 中 `addCommand` 对 `null` 抛 NPE 是符合 MIDP 规范的**正确**行为，不应改动。
  建议修复方向：让解释器在 `NoSuchMethodError`/`VerifyError` 抛出后按 JVM 规范清理操作数栈并从异常处理器恢复（`src/core/nj_interp.c` 的 invoke/异常路径），并给 `str_startsWith1` 加 `null` 保护。
- **注意**：「地址栏输入」的两条子路径结论不同，且在两个构建上一致：**地址栏直接输入**不再崩溃（`2849376f` 与 HEAD 都是退出码 0，字符不进地址栏、画面切到首页）；**URL 编辑页内输入**仍会崩溃（HEAD 干净构建退出码 -11）。报告结论一律以 HEAD 干净构建为准。
- **补充（HEAD 干净构建）**：`head-a2-focus-walk` / `head-a4-fire-address` / `head-a5-type` / `head-a7-noinput` 四组取证都跑在 HEAD 二进制 `9f6325a7…` 上，退出码全为 0（`head-a6-editor` 除外，它是故意的崩溃复现）；因此"欢迎协议 → 空白页 → 主菜单 → 焦点巡游 → 打开 URL 编辑页 → 输入触发异常"这条完整链路在本轮是**同一个二进制**上验证的。
