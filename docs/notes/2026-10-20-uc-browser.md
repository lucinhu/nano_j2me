# UC 浏览器时钟兼容性修复（2026-10-20）

本轮已修复 UC 停在“初始化:动态数据”的本地阻断。原 JAR 现能进入“初始化:测试网络[第1次]”，受控代理收到 4 次 POST。取消后仍停在“取消操作中…”，主菜单、网页浏览及输入尚未验证。

原 [app.jar](</home/lucinhu/Downloads/jar/app.jar>) 为 UC 9.0.0，530861 字节，SHA-256 `38e7c3f7a9d06c8f6d04d4e6f83b33dda787fa7191445e619c4d9d11c3bcb3a2`。所有实验使用独立新 RMS；原 JAR 运行前后哈希一致，未改用户存档。

## 根因与修复

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

[aa 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/aa.javap.txt>) 显示三参数重载传入默认正超时 3000，因此此路径选择 `n.k`；超时非正时才选择 `n.j`。可选的额外 Command 有空值检查，但这两个默认命令共用的 addCommand 调用没有检查。[n 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/n.javap.txt>) 显示构造器未赋值这些字段，`n.e()V` 才建立命令；[g 字节码](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/bytecode/g.javap.txt>) 中 `g.a(ZZ)V pc=139` 调用它。运行时的空值与字节码共同定位了失败字段，但尚未解释命令初始化为何没有完成，不能据此宣布 UC 本身有缺陷或外部服务已失效。

随后 `bi.o()V pc=29` 和 `bi.run()V pc=1635` 的空引用发生在页面清理与异常恢复路径；前者调用未建立页面上的方法，后者访问空页面字段。它们是首个命令异常之后的次级失败，不能与正常命令回调混为一谈。450 帧结束时 `ah.run()V pc=22` 仍在睡眠轮询，后续输入没有推进菜单；日志没有 `uncaught exception`，这也不证明取消流程成功。MIDP 对 `addCommand(null)` 的异常检查保留，本轮不通过吞掉异常改变应用控制流。

## 参考实现与未完成项

本地 FreeJ2ME-Plus `baca76a4dda6f30abac67fb4a3db009eed05f91c` 使用同一原 JAR，等待 7 秒后发 soft2，随后 down/fire/soft1，最终同样停在 [取消画面](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/followup.png>)。[参考运行条件](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/manifest.json>) 记录了代理配置，但 [代理请求清单](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/uc-command-round14-20261020/reference-proxy503/proxy-requests.json>) 为空。

这是参考实现的能力边界：[HttpConnectionImpl](</home/lucinhu/workspace/c/nano_j2me/.local-tools/freej2me-plus/src/javax/microedition/io/HttpConnectionImpl.java>) 的 `connect()` 仅记录日志，`getResponseCode()` 固定返回 200，普通 MIDP 的输入流和输出流返回 null（DoJa 分支才返回空输入流）。因此这次参考运行没有实际经过 HTTP 503 代理，不能作为真实网络行为或相同服务失败条件的独立对照。相似取消画面也不能证明两个模拟器失败原因相同。

对历史提交的重建均复现过动态数据停滞，支持这是长期兼容性缺口；2026-10-17 共享构建曾发出 HTTP，但该二进制未保存，来源无法重建。撤回先前“确定来自某个未提交改动”的强推断，不能据此认定或排除特定提交回归。

UC 取消后不回主菜单仍待修复；下一步需要定位 `n.e()V` 初始化未完成的上游分支，并以可核验的资源和网络响应重放。未验证主菜单、地址编辑、真实网页、账号服务；本轮没有 Android 真机运行或新 APK 交付。
