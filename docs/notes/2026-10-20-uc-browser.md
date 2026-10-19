# UC浏览器初始化停滞排查（2026-10-20）

原JAR `/home/lucinhu/Downloads/jar/app.jar`（UC浏览器 9.0.0），SHA-256 `38e7c3f7a9d06c8f6d04d4e6f83b33dda787fa7191445e619c4d9d11c3bcb3a2`，530861字节。固定二进制 `b10490233a306ecfeba0547e1ad5db9a2b0963d2ae1d54dabdc7539c5524d807`（HEAD `159e4d3`）。全部实验使用headless 240×320、16 MiB、30 fps、隔离RMS副本，原始JAR与用户存档不变。证据目录 `.local-tools/debug-runs/uc-followup-20261020/` 与 `.local-tools/debug-runs/uc-lead-20261020/`。

## 实际运行结果

| 组 | 配置 | 结果 |
|---|---|---|
| 离线观察 | `--run-frames 360` | 停留在“初始化:动态数据”，无uncaught |
| 联网观察 | `--network` 900帧/30秒 | 同样停在“动态数据”，日志中无任何HTTP尝试 |
| 取消操作 | soft2后继续输入 | 文案变“取消操作中…”，后续方向/软键/fire均不推进 |
| Nokia设备配置 | `--device nokia6230` | 同样停在“动态数据” |
| RMS重启 | 复制首轮RMS新进程 | 仍停在同一阶段；仅新RMS初始化的RecordStoreNotFound与厂商类Class.forName探测异常（caught） |

对照本地 FreeJ2ME-Plus（commit `baca76a4`，同JAR、同尺寸）：9秒即可推进到“初始化:测试网络[第1次]”，soft2后进入“取消操作中…”，同样未能进入主菜单。**nano停滞点比FreeJ2ME更早，属于本地兼容性问题，不能用远端服务器关闭解释**；而对照两者取消后都无法进入主菜单，外部服务不可用仍是后续联网验证的现实限制。

另需注意：2026-10-17基线（当时旧二进制）日志中出现过反复的 `this build has no HTTP support` 抛出，说明旧构建在初始化时至少尝试过HTTP；当前固定二进制连HTTP尝试都没有发生。这一差异可能来自两轮之间的核心改动，也可能是路径分支差异，**需要新旧二进制A/B后才能定性**，本报告不将其认定为回归。

## 线程与字节码证据

- 结束时 `--where` 只有两个线程：主线程 `running depth=0` 空闲，以及进度屏线程 `ah.run()` 在 `pc=22` 睡眠轮询（300ms周期）。
- 私有调试构建（`NJ_ENABLE_DEBUG=1`）下 `NJ_TRACE=run` 覆盖200帧：**除 `ah.run` 外没有任何其他 `run()` 方法执行**，HTTP线程类 `bi extends Thread` 从未运行，`--network` 下也没有任何连接动作。带 `--network` 再跑600帧（约20秒）的同类追踪结果一致：608次 `run` 记录全部属于 `ah`，联网开启也没有启动任何工作线程。
- 进度字段 `ah.d` 停在 `3`；`ah.run` 等待 `d==99` 才切换下一屏。`ah.f(int)`/`ah.g(int)` 负责推进进度，调用方为 `g`（加载器）、`n`、`bi`（HttpConnection线程）与 `WebClient` 自身。
- `WebClient` 启动序列：创建进度屏 `ah` → `setCurrent` → `ah.f(1)` → `n.f()` 初始化主界面，然后由 `g.a(boolean,boolean)`（synchronized 加载器，`g.javap` 5272行起）驱动后续阶段；该路径推进到 `d=3` 后即不再前进，HTTP worker 从未启动。

## 结论与下一步

当前可确认：**UC浏览器在“动态数据”阶段（进度3/99）停滞，加载器线程控制流未推进到网络测试阶段，且没有任何HTTP调用发生**。这与服务器可用性无关，是模拟器侧的兼容性问题，候选方向是加载器等待的线程/条件在nano中未满足（例如依赖某线程启动、某返回值或被吞掉的分支）。

尚未完成：`g.a(boolean,boolean)` 在 `d=3` 之后的具体等待条件未逐条追完；方向/菜单/编辑/实际HTTP均未验证；未访问任何账号或付费服务。后续应继续沿加载器字节码定位停滞条件，修复后按 `nano-j2me-debug` 流程用真实截图与后续输入重新验证本报告中的全部阶段。
