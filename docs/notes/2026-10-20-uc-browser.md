# UC 浏览器时钟与 Integer.shortValue 兼容性修复（2026-10-20）

RTC 修复后，进一步从受控响应中定位并补齐 `Integer.shortValue()S`。同一原 JAR 现在能完整显示中文欢迎页，桌面原生 F1 确认后进入空白页，继续 F1 打开主菜单、Down 移动选中项、F2 关闭菜单。此结果使用本地构造的分配响应；HTTP 503 下仍停在取消流程，实际网页浏览和地址输入尚未通过。

原 [app.jar](</home/lucinhu/Downloads/jar/app.jar>) 为 UC 9.0.0，530861 字节，SHA-256 `38e7c3f7a9d06c8f6d04d4e6f83b33dda787fa7191445e619c4d9d11c3bcb3a2`。所有实验使用独立新 RMS；原 JAR 运行前后哈希一致，未改用户存档。

## 后续：受控分配响应与 Integer.shortValue

仅 HTTP 200 不足以通过网络测试。[空响应对照](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/http200-empty/result.json>) 收到 4 次 POST，每次都在外层 `DataInputStream.readUTF()` 处遇到 EOF，随后进入原来的空命令失败路径。

按原 JAR 字节码构造的 [41 字节响应](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/derived-assignment/assignment.bin>) SHA-256 为 `e4ea0b6c768c4e588aed995aad79811e07739c5801fd979733bfda860d9b4c72`，可由 [生成脚本](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/make_assignment.py>) 重建。外层为 `writeUTF("data")`、大端有符号 short 长度 33、33 字节负载；内层为两个长度前缀 ASCII 字符串 `assign`、`uc-fixture.invalid:80`，以 `[238,185,233,179,129,142,151,167]` 循环 XOR，加上校验值 `0x25` 分别与前两个密钥字节 XOR 的两字节尾部。它满足分配解析器的非空及冒号检查，是本地派生测试数据，未宣称来自旧 UC 服务。

[分配响应基线](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/http200-assignment/result.json>) 将请求减为 2 次，并进入 `n.e()V`，从而越过之前的命令初始化阻断。但内置页面解析在 `al.a(Lan;I[B)Z pc=201` 抛出 `java/lang/NoSuchMethodError: java/lang/Integer.shortValue()S`；[第 240 帧](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/http200-assignment/frames/00240.png>) 只有空白页与地址、历史、帮助控件。

修复在 [Integer 实现](</home/lucinhu/workspace/c/nano_j2me/src/core/nj_lib_boxing.c#L42>) 同时补上实例方法声明和 native 绑定，返回低 16 位的有符号值，保留原始 Integer。实现不分配内存，不引入依赖。新增 [13 组边界测试](</home/lucinhu/workspace/c/nano_j2me/tests/test_lib.c#L3018>) 覆盖正负边界、回绕、高位混入与 int 极值，并检查调用状态、异常和原值；旧实现为 [752 passed, 26 failed](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-red-test.log>)，补齐后 [778 passed, 0 failed](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-green-test.log>)。当前全套 [CTest 85/85 通过](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-ctest.log>)。

在同一个 `25ef93f` 诊断基线中只覆盖该 boxing 文件后，二进制 SHA-256 为 `76d40a1bd2eef214a3e7c5fd6c7c3e2515f841931bf7c7afd35b78725d57055e`。同一原 JAR、同一响应、全新 RMS、240×320、16 MiB、30fps，已显示 [完整中文欢迎协议](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/http200-assignment-shortvalue-v2/frames/00240.png>)。[条件及结果](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/http200-assignment-shortvalue-v2/result.json>) 记录 2 次 POST、241 帧和 8.031 秒；第 240 帧的 soft2 此时对应“退出”，应用调用 `notifyDestroyed()` 正常退出，不能把缺少第 450 帧误判为崩溃。第一次命名为 `http200-assignment-shortvalue` 的隔离构建没有实际带入补丁，其结果已标记排除，采用核对源文件后的 `v2` 重跑证据。

### 桌面原生按键验证

当前 [桌面构建](</home/lucinhu/workspace/c/nano_j2me/build/nj2me>) SHA-256 `1f784eef59da9b5e5f9dede637d0541afd3272dc2c716aa320bbd1ab964f1fef`，来源为 `de6d5c8` 加 boxing 修复。新 RMS，原 JAR 不变，240×320、16 MiB、30fps；所有 HTTP 请求由 loopback fixture 直接回答，不转发。按 PID 与标题共同定位 SDL 窗口，采集实际渲染缓冲与按键 down/up。

| 操作 | 已检查的结果 |
|---|---|
| 启动 | [中文欢迎协议](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/01-welcome.png>) |
| F1 确定 | [进入含地址、搜索、历史和帮助的空白页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/02-after-confirm.png>) |
| 再按 F1 | [主菜单打开，选中“1.打开”](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/03-menu.png>) |
| Down | [选中项移到“2.窗口”](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/04-menu-down.png>) |
| F2 关闭菜单，再按 Return | [仍显示空白页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/06-after-return.png>)；未打开地址编辑器 |
| 后续 F2 | [底部“切换”焦点变化](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/07-followup.png>)；不作为输入法通过证据 |

[完整命令、哈希、动作和截图帧号](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/manifest.json>)、[SDL 输入及运行日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/runtime.log>)、[2 次 POST](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-sdl/proxy-requests.json>) 保留。运行 518 帧、17.618 秒、29.4fps，随后受控结束；JAR 前后 SHA 一致。日志中不再有 `Integer.shortValue` 的缺失方法异常，但仍有资源解析相关的已捕获异常；最后 F2 后记录 `NullPointerException at l.a(Lan;I)V pc=23`，因此不把本轮菜单操作扩大为所有交互通过。

### 冷启动读取已保存状态

用当前 HEAD `ac24cd7` 的纯净源码（`git archive`）在隔离目录构建，二进制 SHA-256 `a821b5977760ff2f9de6fb9f8cce3bded06c242444000ce7afde3676642e5c33`；RMS 直接沿用上方 `1f784eef` 运行写出的存档副本，其余条件相同。再次启动时不再显示欢迎协议，[直接进入空白页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-relaunch-clean/01-relaunch.png>)；F1 打开[选中“1.打开”的主菜单](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-relaunch-clean/02-menu.png>)，Down 移到[“2.窗口”](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-relaunch-clean/03-menu-down.png>)，F2 关闭后[回到同一空白页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-relaunch-clean/04-closed.png>)。共 2 次 POST，受控结束，JAR 哈希不变。

该 RMS 目录实际写出 `B.rms`、`coreA.rms`、`G2.rms`、`V.rms`，说明“已接受协议”这类状态确实落盘并在新进程中被读回。这只能证明本次会话状态延续，不能推导历史记录、书签或网页缓存已完整实现。

### 当前边界

分配响应被 fixture 同样用于第二次 `usquery.php` 请求，它并不是有效的下游资源协议响应。`bi.v()V` 调用 `bd.a(Hashtable)Z` 后丢弃返回值，并在方法返回时标记该阶段成功；不能以此证明远端资源安装成功。欢迎页来自 JAR 内置资源，实际网页、搜索、地址编辑、输入法、冷启动状态和 Android 回归仍待验证。

同一当前二进制另跑 [HTTP 503 对照](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-http503/result.json>)：4 次 POST，300 帧、9.979 秒，soft2 后 [仍停在取消操作中](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-response-round15-20261020/shortvalue-http503/frames/00300.png>)；日志仍有 `Displayable.addCommand` 的 `command is null`。该取消停滞没有被本轮方法补齐修复，远端服务可用性也未据此判定。

## 历史：时钟根因与修复

UC 初始化路径存在当前时间条件。模拟器原先将端口启动后的单调计数作为 Java 时间，桌面 `System.currentTimeMillis()` 因而落在 1970 年附近；`new Date()` 与新建 Calendar 又分别默认到零。加载器在进度 3/99 之后没有进入 HTTP worker，只有进度线程 `ah.run()` 按 300ms 睡眠轮询。

核心现在在创建 VM 时读取可选 `port.now`，校验年月日、闰年、时分秒、毫秒及 UTC 偏移，将当地 RTC 转成 UTC epoch，作为虚拟时钟起点。随后仍用单调毫秒累计时间，主机校时不会突然触发已有线程或 Timer 的截止时间；倍速继续作用于经过的时间。缺少 RTC 或 RTC 无效时保留原计数回退行为。

POSIX headless、SDL2 端口接入共用 RTC 实现；`Date()`、Calendar 两个构造路径改为捕获同一 `nj_now_ms()`。快照格式不变，恢复时继续采用已保存的虚拟时钟，不能由新端口 RTC 覆盖。

## 实际 A/B 与后续输入

基线二进制 SHA-256 `b10490233a306ecfeba0547e1ad5db9a2b0963d2ae1d54dabdc7539c5524d807`（历史固定构建，HEAD `159e4d3`）；修复二进制 `bde0bab93c5794a726a9c4f9eec1319b666ceca3b4ccdb48e16f759f04e0f806`（`e4a7f35` 加本轮时钟补丁，隔离排除在途 SAX 改动）。固定 240×320、16 MiB、30fps、450 帧，各自新 RMS。对照覆盖离线与受控本地 HTTP 503 代理；代理仅记录方法、目标和头名，不记录请求负载。

| 组 | 画面与网络观察 | 后续输入 |
|---|---|---|
| 基线离线 / 代理 | 均停在动态数据；代理零请求 | soft2 改为取消操作中；后续 down/fire/soft1 未推进 |
| 修复离线 | 进入测试网络；关闭网络时连接按配置失败 | soft2 进入取消操作中；后续输入未进入菜单 |
| 修复代理 | 进入测试网络；4 次 POST，目标为 UC 端点与其 WAP 代理候选 | soft2 进入取消操作中；后续输入未进入菜单 |

可核查的 [基线命令及结果](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/baseline-proxy503/result.json>)、[修复命令及结果](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/fixed-proxy503/result.json>)、[4 次请求](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/fixed-proxy503/proxy-requests.json>) 保留。

另用 SDL2 真窗口、按 PID 和标题同时定位窗口，注入 Escape、Down、Return、F1 的 down/up。由 SDL 渲染缓冲采集的 [输入前网络测试页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/fixed-sdl-proxy503-buffer/01-restored.png>) 与 [Escape 后取消页](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/fixed-sdl-proxy503-buffer/02-after-Escape.png>) 已人工检查；后续按键仍停在取消页。完整 [SDL 命令、动作与截图帧号](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-clock-20261020/fixed-sdl-proxy503-buffer/manifest.json>) 保留。这里是受控失败响应验证，不能推导旧 UC 服务仍可用或已能上网。

## 回归与兼容性边界

隔离源码构建全套 CTest **84/84 通过**；ASan/UBSan 的时钟、Timer、快照 **3/3 通过**。覆盖 1969/1970、2000 闰日、2038、2100 非闰年、UTC 正负偏移、无效 RTC、主机校时不跳变、倍速，以及有/无 RTC 互相恢复的 4 种组合 × 3 个端口计数起点 × 2 种倍速。Timer 用 Date 计算相对延迟的边界一并验证；这不声称新增了 Timer 的 Date 参数重载。

同一修复二进制恢复原《孤岛余生 2》N97 室内快照：持续 Up 后角色上移，F2 打开暂停，Return 返回场景，随后 Escape/Return 再次暂停/返回。已检查 [移动后](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/island-n97-followup-20261019/clock-bounded3-20261020/02-after-Up.png>)、[暂停](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/island-n97-followup-20261019/clock-bounded3-20261020/03-after-F2.png>)、[恢复](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/island-n97-followup-20261019/clock-bounded3-20261020/04-after-Return.png>)，并有 [SDL 实际按键日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/island-n97-followup-20261019/clock-bounded3-20261020/runtime.log>)。暂停菜单中央白底仍存在。前两次窗口采集失败不计入通过证据；重跑在快照恢复后重新查询窗口，以免使用重建前的旧窗口 ID。

## 取消停滞的调用链诊断

在已提交的 RTC 修复上另建隔离诊断构建，仅为 `nj_throw` 增加完整帧栈输出；SHA-256 `a7f438f5b40dcabfe0269b937ab20f49df80ec796751602db436d36bb5554622`。原 JAR、全新 RMS、240×320、16 MiB、30fps、450 帧，脚本为 `240:soft2 300:down 330:fire 390:soft1`，本地代理返回 HTTP 503。该诊断没有成为生产代码修改。[运行条件](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/diagnostic-proxy503/manifest.json>)、[完整异常栈](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/diagnostic-proxy503/runtime.log>) 与 [后续输入后的取消画面](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/diagnostic-proxy503/frames/00450.png>) 可回查。

首个相关异常为：

```text
java/lang/NullPointerException at javax/microedition/lcdui/Displayable.addCommand(Ljavax/microedition/lcdui/Command;)V pc=0: command is null
```

保存的调用链为 `aa.a(Display,String,Displayable,int,int,Command)V pc=129` → `aa.a(Display,String,Displayable,int)V pc=11` → `aa.a(Display,String,Displayable)V pc=7` → `n.a(String,Displayable)V pc=23` → `n.p()V pc=11` → `bi.a(BZZ)I pc=1082` → `bi.v()V pc=48` → `bi.run()V pc=1302`。这里的调用者 PC 是调用后的保存位置；`aa` 中实际 `Alert.addCommand` 指令位于 126，不能将 `bi.o()V pc=29` 当成原始 addCommand 调用点。

[aa 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/aa.javap.txt>) 显示三参数重载传入默认正超时 3000，因此此路径选择 `n.k`；超时非正时才选择 `n.j`。可选的额外 Command 有空值检查，但这两个默认命令共用的 addCommand 调用没有检查。[n 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/n.javap.txt>) 显示构造器未赋值这些字段，`n.e()V` 才建立命令；[g 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/g.javap.txt>) 中 `g.a(ZZ)V pc=139` 调用它。截至这次异常栈诊断，运行时的空值与字节码只共同定位了失败字段，尚未解释命令初始化为何没有完成；后续顺序复核见下节。不能据此宣布 UC 本身有缺陷或外部服务已失效。

随后 `bi.o()V pc=29` 和 `bi.run()V pc=1635` 的空引用发生在页面清理与异常恢复路径；前者调用未建立页面上的方法，后者访问空页面字段。它们是首个命令异常之后的次级失败，不能与正常命令回调混为一谈。450 帧结束时 `ah.run()V pc=22` 仍在睡眠轮询，后续输入没有推进菜单；日志没有 `uncaught exception`，这也不证明取消流程成功。MIDP 对 `addCommand(null)` 的异常检查保留，本轮不通过吞掉异常改变应用控制流。

## 首次启动的初始化顺序复核

在 `25ef93f` 的隔离副本中，仅给方法调用和异常加诊断，未改生产源码。诊断二进制 SHA-256 `eb6bd2dc39288244ab077c6bc73efafc7ed0becc02a87bc61cca9f7d138a3573`；原 JAR、全新 RMS、240×320、16 MiB、30fps、450 帧和相同后续输入，受控代理返回 HTTP 503。运行 15.064 秒、达到帧预算，JAR 前后哈希一致。[完整条件与结果](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-init-round15-20261020/trace-proxy503/result.json>)、[调用及异常日志](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-init-round15-20261020/trace-proxy503/runtime.log>) 和 [4 次请求](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-init-round15-20261020/trace-proxy503/proxy-requests.json>) 保留。检查末帧确认 soft2 后仍是[取消操作中](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-init-round15-20261020/trace-proxy503/frames/00450.png>)，后续 down/fire/soft1 未进菜单。

运行日志先记录 `g.a()V`，再记录 `g.a(ZZ)Z(1,1)` 启动 worker，随后 `bi.run()V → bi.v()V`，接着就在 `bi.a(BZZ)I` 内触发上述空命令异常。在该异常之前没有 `bi.b(Z)V`、`g.c(Z)V`、`g.a(ZZ)V` 或 `n.e()V` 调用。必须区分返回 boolean 的 `g.a(ZZ)Z` 与返回 void 的 `g.a(ZZ)V`。

与字节码交叉核对，首次启动的 `n.a(ZZ)V` 在调用 `g.a(ZZ)Z` 后返回；命令初始化在 worker 的后半段：`bi.v()V pc=492 → bi.b(Z)V → g.c(Z)V → g.a(ZZ)V pc=139 → n.e()V`。网络测试调用位于 `bi.v()V pc=45`，早于该初始化，且不在此方法局部异常区间 `[113,149)`、`[156,404)` 内。`bi.a(BZZ)I` 的重试失败分支在 pc=1079 调用 `n.p()V` 构造错误提示；此时 `n.k` 尚未建立，提示自身抛出的异常直接跳过后半段初始化，再进入已有的清理空引用路径。

因此，本次受控失败条件下“默认命令为何为空”的上游顺序已经确认。MIDP 对 `addCommand(null)` 的拒绝符合接口约定，当前证据不支持修改它或异常展开规则。此结论仅覆盖 HTTP 503 失败路径；仍需要可核验的成功协议响应，才能区分服务返回数据与后续解析、界面初始化是否另有兼容性缺口。不能用这次失败代理推断远端服务已关闭，也不能声称主菜单已修复。

## 参考实现与未完成项

本地 FreeJ2ME-Plus `baca76a4dda6f30abac67fb4a3db009eed05f91c` 使用同一原 JAR，等待 7 秒后发 soft2，随后 down/fire/soft1，最终同样停在 [取消画面](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/followup.png>)。[参考运行条件](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/manifest.json>) 记录了代理配置，但 [代理请求清单](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/proxy-requests.json>) 为空。

这是参考实现的能力边界：[HttpConnectionImpl](</home/lucinhu/workspace/c/nano_j2me/.local-tools/freej2me-plus/src/javax/microedition/io/HttpConnectionImpl.java>) 的 `connect()` 仅记录日志，`getResponseCode()` 固定返回 200，普通 MIDP 的输入流和输出流返回 null（DoJa 分支才返回空输入流）。因此这次参考运行没有实际经过 HTTP 503 代理，不能作为真实网络行为或相同服务失败条件的独立对照。相似取消画面也不能证明两个模拟器失败原因相同。

对历史提交的重建均复现过动态数据停滞，支持这是长期兼容性缺口；2026-10-17 共享构建曾发出 HTTP，但该二进制未保存，来源无法重建。撤回先前“确定来自某个未提交改动”的强推断，不能据此认定或排除特定提交回归。

UC 取消后不回主菜单仍未通过；首次启动的失败提示早于命令初始化，已由调用日志与字节码交叉确认。下一步需要可核验的成功协议响应，才能继续检查资源解析与正常界面初始化。未验证主菜单、地址编辑、真实网页、账号服务；本轮没有 Android 真机运行或新 APK 交付。
