# 完整 ZIP 资源名称与 Image.createImage(String) 兼容性

旧实现会丢失合法资源名称，或读取另一个同前缀条目：ZIP CENTRAL 名称被截为 127 字节后参与查找；Image.createImage(String) 又把 UTF-8 路径放进固定 256 字节数组，却未检查转换器返回的完整所需长度。图片存在于原始 JAR 中，也可能因此抛出 IOException。

## 修复

- ZIP 查找使用完整 CENTRAL 名称和精确长度，包含原有 ASCII 大小写不敏感查找。LOCAL 名称仅用于定位数据，不替代 CENTRAL 名称。公开 nj_zip_entry 的布局及枚举名称前缀保持不变。
- 内部每个条目增加 8 字节名称位置/长度记录，与既有 entries 一起分配；仍为两次 open 分配，并沿用原有释放路径。短名称比较保留原有实现。
- Image 的短名称继续使用 256 字节栈缓冲；需要 256 字节及以上的 UTF-8 名称才额外申请完整宿主缓冲，返回前释放。
- 解码错误消息的所有追加均有边界；String 解码沿用 byte[] 路径的分配失败和 provider 异常处理。无应用、空名称的原有异常行为保留。

这次没有增加 Java builtin 成员、改变快照格式或修改游戏文件，也没有速度提升的测量结论。

## 实际验证

回归直接在 C fixture 内生成一个 12 条目的真实 stored JAR，再通过 nj_app_open 和公开 Image.createImage(String) 加载。没有依赖未提交的外部测试 JAR。

用例覆盖完整长名称、127 字节前缀冲突、253–257 字节边界、跨边界多字节 UTF-8、254/255 字节名称的无效图片，以及直接 ZIP exact/CI 查找、枚举和 CENTRAL/LOCAL 名称不同的情况。原 LCDUI 检查全部保留，每轮新增 49 个检查。

| 相同 fixture | 普通 Release | 严格 sanitizer |
|---|---:|---:|
| 旧运行时 | 11589 通过 / 57 失败 | 11589 通过 / 57 失败 |
| 修复后 | 11646 通过 / 0 失败 | 11646 通过 / 0 失败 |

旧失败均位于新增资源用例，原 11499 个 LCDUI 检查通过。修复后的三个 VM 周期各 3882 个检查，释放宿主块数均为 1020，采样类数均为 309。两种构建还分别通过原 ZIP/inflate/manifest 的 423 个检查及 codec/provider 的 54 个检查。

严格构建启用 AddressSanitizer、UndefinedBehaviorSanitizer、float-cast-overflow、LeakSanitizer 检测、遇错停止、frame pointer 和编译警告作为错误；上述运行没有 sanitizer 诊断。每种库均保留 76 个成员及原顺序，仅 ZIP/LCDUI 两个成员改变，其余 74 个 payload 字节相同。旧新 LCDUI 使用同一个已编译 fixture 对象。

旧 ZIP 查找先挡住了 254/255 字节名称，所以本次没有声称复现旧错误消息的 sanitizer 越界。新实现已让真实长名称到达解码路径，并验证无效图片返回 IOException。新增用例没有注入宿主 OOM 或 provider 异常；这两个分支的实现对齐既有 byte[] 路径，不能据此声称完成故障注入覆盖。

本轮只运行这三个相关测试组的普通和严格构建，没有重跑全套 171 个注册测试、四套正式 benchmark 或 Android 游戏性能。此前记录保留各自版本身份。

私有证据：`.local-tools/debug-runs/image-resource-20261111-source/` 与 `.local-tools/debug-runs/image-resource-20261111-lead/`，包括源码冻结、实际命令/日志、OLD-RED.json、GREEN.json 和成员比较。

## JB1 单次诊断的独立结论

在本次资源修复之前，使用 e32374c 父运行时构建了仅加观察点的私有 CLI，执行一次 120.416 秒原版 JB1。它推进五阶段并显示 Results；最后采样中目标 paint 为 13027/13027、serially 为 23303/23303，均正常返回，队列持续自排队。相同的被 Java 捕获的除零日志仍出现一次。

这次未复现此前普通 CLI 的 cube 停滞，原因仍 OPEN。插桩运行的分数不计入正式成绩，也不证明问题已修复。观察代码没有进入产品源码。证据为 `.local-tools/debug-runs/jb1-state-20261111-diagnostic/` 及 `jb1-state-20261111-lead/`。
