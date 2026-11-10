# 解释器 CP 缓存碰撞路径：完整内联查找及测量边界

本轮接续 [J2ME-Loader 对比](2026-11-10-j2me-loader-comparison.md)和
[Minecraft LOAD 分阶段采样](2026-11-10-minecraft-load-profile.md)。修改适用于通用
Java 字段、静态字段和方法调用，不识别 Minecraft 类名，也不修改游戏 JAR。

## 问题与修改

常量池缓存由 `(class, CP index)` 定位，发生 hash 碰撞时使用线性探测。
原来的 `nj_cp_hit` 只查看首槽。已缓存的字段或方法若位于后续槽，解释器仍需进入
`nj_cp_resolve`，重新计算 hash、再次扫描前缀，然后找到同一个缓存项。

[内部头文件](../../src/core/nj_internal.h)现在使用与
[resolver 的 `cp_cache_slot`](../../src/core/nj_class.c)相同的 hash、mask、探测顺序和
空槽终止规则。循环最多检查缓存容量个槽，碰撞命中也能直接返回。

这消除了重复查找，不代表原来的 Java 解析结果错误：旧实现能通过 resolver 回退取得
正确结果。命中仍只增加一次 `cp_cache_hits`，miss 不增加 hit 计数；真实解析、类初始化、
receiver 校验、虚调用缓存以及错误翻译保持原路径。

helper 不分配、不调用 Java、不扩容，也不保存跨 GC、rehash 或恢复快照的缓存指针。
VM、缓存项和类的布局、静态字段数、快照格式均不变。

## 为什么检查这条路径

冻结的原版 Minecraft LOAD 采样窗口共 5,370 个样本，其中读取阶段 2,667 个、准备阶段
2,563 个，其余为菜单和过渡。在 resolver 的 323 个样本中：

| 原生代码区域 | read | prepare |
| --- | ---: | ---: |
| 入口 / hash | 32 | 33 |
| 探测循环 | 184 | 55 |
| 命中返回专属路径 | 7 | 12 |
| 真正解析主体 | 0 | 0 |

其中碰撞推进专属指令有 read 7、prepare 8 个样本，已包含在探测循环内。
这些是 SIGPROF 样本，不能当调用次数、probe 长度、硬件 cache miss 或 CPI。
解析主体零样本也不能证明没有冷 miss。

原静态库与被采样 binary 的六个 CP 函数已核对：除链接重定位字节外相同。
因此这里针对的是缓存查询路径，而不是凭函数名认定“类解析很慢”。
`ldc String` 另有直接调用 resolver 的路径，本轮不改它，也不把所有 resolver 样本
都算成可以消除的开销。

## 隔离候选与实际测量

基线是 `cb330a3b057a349ec67e74f70bf259dec1b6cea6`。
从 Ninja 提取现有 Release 的编译、链接参数，复制解释器和头文件，仅替换
`nj_cp_hit`，再用单个解释器 object 覆盖同一份静态库。
重新编译的 before 解释器 `.text` 与库中 object 逐字一致。
候选 helper 与最终源码除注释外一致，没有附带其他优化。

本机为 GCC 16.2.1、Release `-O3`、threaded dispatch、JIT 关闭。
解释器 object `.text` 为 **83,713 → 82,669 字节，减少 1,044 字节**；
`.rodata` 均为 1,657 字节。该大小变化只代表本机这组编译参数。

### 固定工作量 GEN

使用公开的 [生成基准](../../tools/bench_minecraft_gen.c)，种子 12345。
最初单对筛选 CPU 时间为 9,006.337 → 8,882.539 ms，不能据此宣称稳定收益。
随后 CPU0 三对交替复测如下，顺序为 before/after、after/before、before/after：

| 对次 | before CPU ms | after CPU ms |
| --- | ---: | ---: |
| 1 | 9,276.002 | 9,412.204 |
| 2 | 9,495.679 | 9,362.503 |
| 3 | 9,476.941 | 9,418.912 |
| 中位数 | **9,476.941** | **9,412.204** |

中位数仅减少 **0.68%**，一对反而更慢；本轮不宣称稳定 GEN 加速。
每次均逐项核对 256 个 chunk 的 block/data/WORLD 校验结果、spawn `161,64,111`、
5,175,712,766 次融合 dispatch、1,616,679 次 native 调用、GC 0、heap/peak 6,510,256 字节。
校验的是实际工作与输出一致，dispatch 不是原始 JVM bytecode 数量。

### 原版游戏 LOAD

原始 JAR SHA256：
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`。
每次新建私有 fs/RMS 副本，固定同一世界和菜单输入，240×320、heap 16 MiB、
headless、30 fps、600 frames，CPU1。复制和运行前后均检查原始输入未改变。

| 对次 | before read 墙钟 ms | after read 墙钟 ms |
| --- | ---: | ---: |
| 1 | 2,453 | 2,383 |
| 2 | 2,569 | 2,390 |
| 3 | 2,419 | 2,462 |
| 中位数 | **2,453** | **2,390** |

read 中位数减少 **2.57%**，但第三对更慢。这是本机三对测量的描述，不是跨机器、
Android 或整个游戏帧率的稳定收益证明。最初单对 2,479 → 2,371 ms 的 4.36% 不作为最终结果。

原 `MC.loadWorld` 的 read 计时包含 `Storage.load`。后续 prepare 中
`updateDirty(..., 60)` 受时间预算控制，每次剩余 mesh 数量不同；虽然 prepare 中位数
为 2,381 → 2,308 ms，本轮不把它作为固定工作量加速，也不与 read 相加计算“总加载提升”。
30 fps 的整段运行耗时也不用于计算吞吐收益。

保留修改的依据是直接消除已缓存碰撞项的重复 hash/函数调用、代码变小以及定向语义验证；
上述整机计时只支持小幅、受噪声影响的结果，不据此宣传新的 Minecraft 加速倍率。

## 边界和复测方法

真实 miss 仍由 resolver 验证 index 并解析。helper 和 resolver 可能各扫描一次相同前缀；
极端满 8,192 槽 absent-key 最多合计检查 16,384 槽，而旧 inline 只检查首槽再回退。
这是冷 miss 的性能代价，循环保持有界。本轮没有测出所有缓存尺寸、碰撞分布或 Android
设备的成本，暖命中结果不能代替这些结论。

复测时应保留原 JAR 和世界数据，分别构建上述基线与当前源码，使用相同 Release 参数。
固定生成种子、逐项比较完整 chunk 输出，并按三对交替顺序计 CPU 时间。
LOAD 每次从同一 fs/RMS 原始副本启动，使用相同菜单输入和帧限制，分别记录 read 与 prepare；
不要用菜单等待时间或受时间预算影响的 prepare 冒充固定工作量。现有资料中的基准命令和
[构建与运行说明](../../README.zh-CN.md)适用于本地检查，原始游戏不随提交分发。

## 回归与实际场景

最终主构建完整重编头文件依赖，**155/155** 项 CTest 通过（312.74 秒）。
从最终静态库提取解释器 object，其 `.text` 与上述被测候选逐字一致，避免把临时副本的
性能数据误记到不同的最终实现。

新增 [CP 测试](../../tests/test_cp_cache.c)覆盖同 hash 冲突链、所有起始槽的回绕、
空槽终止、满表有界 miss、容量 0/1、不同 class 的同 index、六种缓存 payload、
恰好一次 hit 计数和查找前后整表不变。真实 resolver 控制项覆盖字段、方法、String、
多次 growth/rehash、三次 GC、malformed index/tag 及缺失成员的原有 Java 错误。
旧版完整运行 **2,161 checks，1,767 passed，394 failed**；其中 197 项指针断言和
197 项对应的计数断言只验证新增碰撞 fast-hit 能力，原 resolver 正确性控制项仍通过。

独立正常版和 sanitizer 版均重新编译完整 runtime，并实际开启 `NJ_ENABLE_JIT=1`。
两版新增 CP 测试各 **2,161/0**，lookup cache 各 **15/0**，class library 各 **1,432/0**；
resolution、String intern 和 JIT 执行测试也均通过，六个选定测试各 **6/6**。
Sanitizer 的全部 76 个 runtime 编译单元和六个测试均核对 ASan、UBSan、显式
`float-cast-overflow`、禁止错误恢复和 frame pointer，未报告诊断。
新 fixture 单独以 `-Wall -Wextra -Werror` 编译、链接各自的新 runtime 再执行也都通过。
JIT runtime 沿用项目原 warning 策略：全库 `-Werror` 会被既有两个未使用静态函数警告阻断，
本轮没有为此修改无关源码。没有将关闭 JIT 的跳过结果记成 JIT 验证。

独立快照检查比较了公共 ABI 89 行、全部类 ABI 3,737 行及 host VM/cache 布局，均逐字一致。
用真实 `cb330a3` writer 保存普通及 RMS 状态，再加载至已运行 FieldDemo 的新 sanitizer VM：
预热时 `used=32, hits=24, misses=32`，恢复后 512 槽中的 used 和 hit/miss 计数全部清零；
重新解析 32 个真实 key 后 `used=32, hits=64, misses=32`，GC 后查询和程序续跑均通过。
两种恢复分别 **154/0、177/0**；同时检查旧 snapshot 输入字节不变。
同一完整依赖 Debug sanitizer 库的 general save **2,521/0**、M3G save **95/0**、
RMS save **70/0**，三个选定 CTest 全部通过；此组显式启用 leak 检查，未报告问题。

原版 Minecraft 经 SDL2、240×320、heap 16 MiB、30 fps、1,600 帧启动并加载同一世界，
实际检查地形、手持方块和 HUD；退出前保存新快照。窗口像素与此前相同场景一致，
没有未捕获异常或 invalid chunk。该段运行 53.713 秒、29.8 fps（人为限帧），不作为帧率提升。
前候选版本保存的快照与本版新快照分别续跑 300 帧，都继续至原世界并再次保存；
抽样帧逐字一致。恢复后仍有旧版本已存在的、被捕获的 DatagramConnection 接收异常日志，
本轮不宣称修复网络句柄恢复，也不代表全游戏兼容验证。

定向检查可运行：

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -R 'cp_cache|resolution|lookup_cache|string_intern|save'
ctest --test-dir build --output-on-failure -j1
```
