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

## 历史判断修正与未完成项

本地 FreeJ2ME-Plus `baca76a4` 曾在同 JAR 到达测试网络，取消后同样未进主菜单。对历史提交的重建均复现过动态数据停滞，支持这是长期兼容性缺口；2026-10-17 共享构建曾发出 HTTP，但该二进制未保存，来源无法重建。撤回先前“确定来自某个未提交改动”的强推断，不能据此认定或排除特定提交回归。

UC 取消后不回主菜单仍待查。已有只读线索指向空 Command 回调路径，但尚无完成验证的修复；下一步应结合应用字节码和 MIDP 命令分发定位。未验证主菜单、地址编辑、真实网页、账号服务；本轮没有 Android 真机运行或新 APK 交付。
