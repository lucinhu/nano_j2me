# Minecraft LOAD 分段：文件读取、Pal 导入和网格准备

## 结论

在这份原版 Minecraft J2ME 已保存世界的加载窗口内，RMS 查找调用、扫描项和
范围内 CPU 样本均为 **0**。世界通过 FileConnection 读取，日志中的 `read`
主要执行 Java 区块导入和重新编码；`prepare` 主要执行 Java 网格构建和取块。
因此，[RMS 查找优化](2026-11-10-rms-lookup.md)的微基准倍率不能作为本次
Minecraft 加载收益。

这也进一步区分了 [J2ME-Loader 对比](2026-11-10-j2me-loader-comparison.md)
中的两个方向：GPU 渲染能帮助实际绘制阶段，但本窗口的主要工作仍在 Java
代码执行。以下是一次受控诊断，不是优化前后性能对比。

## 来源、运行与采样口径

- 基线：`d7a6cc511d2f7eed0c12a52405b67336f2117aea`，RMS 新候选未混入。
- 原 JAR SHA256：
  `fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`。
- Linux x86_64，Intel Core Ultra 9 275HX，GCC 16.2.1；CPU 0，headless，
  240×320，16 MiB 堆，30 fps，最多 1600 帧。输入世界及 RMS 的四个文件完整
  复制到独立目录，运行前后核对原件集合和每份哈希，原件及 JAR 未改。
- 采用原按键重放：150/154 帧在 (120,125) 按下/释放，270/274 帧在
  (120,155) 按下/释放；没有从 VM 快照启动。
- 从基线重编 runner、RMS、FileConnection、IO 四个对象，确认各自 `.text`
  与冻结静态库逐字相同，再添加本地计数和采样代码。

`ITIMER_PROF` 每 1 ms CPU 时间采样。信号处理器只将 native RIP 和最多
64 层 Java frame 写入预分配数组；不分配、不做 I/O、不符号化。停采后、
销毁 VM 前才输出。native 地址采用符号的实际范围匹配，库外/PLT 保留 unknown。

frame270 交付按键前启采，在原 `MCJ: LOAD read ...` 日志回调处立即停采
（frame431）。CPU 窗口 5370.745 ms，wall 5388.109 ms，共 **5370 样本**，
无容量截断、Java 栈截断或文件路径记录丢失。分类使用完整 Java 调用链：

| 类别 | 样本数 | 定义 |
|---|---:|---|
| read | 2667 | 含 `Storage.load(String)` |
| prepare | 2563 | 含 `MC.startGame(Level,Player)` |
| 菜单/非 LOAD | 139 | 按下到释放之间的非 LOAD 调用链，单列 |
| 跨阶段 | 1 | Player 初始化等边界工作，单列 |

read/prepare 互斥，后两项不进入其分母。Java 顶层表示当时执行的 VM frame；
inclusive 包括子调用且彼此重叠，不能相加。saved Java PC 不是每条 bytecode
的精确热度。原游戏日志 `read 2679 ms / prepare 2569 ms` 包含插桩开销，不能
用来计算优化倍率。

## read：主要在 Java 导入与编码

| Java 顶层方法 | 样本数 | 占 read |
|---|---:|---:|
| Pal.importChunk | 1109 | 41.58% |
| Pal.encode0 | 801 | 30.03% |
| Pal.index | 395 | 14.81% |
| Storage.fixBlocks | 198 | 7.42% |
| Pal.get | 46 | 1.72% |
| Level.calcHeight | 39 | 1.46% |
| FileInputStream.read(byte[],int,int) | 17 | 0.64% |

`Storage.readChunks` inclusive 为 99.96%，`Pal.importChunk` inclusive 为
86.43%。游戏先读文件、解析区块，再导入内部表示，所以 `read` 并不等于纯
操作系统读文件耗时。

native 自身样本中，解释器为 74.80%，常量池解析为 8.36%，常量运算 helper
为 6.75%，数组读取为 4.09%，Java frame push 为 1.99%。这是定位线索，
不能直接当作某个优化可消除的比例。

## prepare：主要在 Java 网格构建

| Java 顶层方法 | 样本数 | 占 prepare |
|---|---:|---:|
| Pal.get | 881 | 34.37% |
| LevelRenderer.cube | 463 | 18.06% |
| Level.getTile | 310 | 12.10% |
| LevelRenderer.mergeCell | 104 | 4.06% |
| LevelRenderer.tessellate | 102 | 3.98% |

`updateDirty` inclusive 为 97.66%。native 自身样本中解释器占 82.05%，
常量运算 helper 占 8.04%，常量池解析占 3.90%，frame push 占 2.15%。

这两段均未采到 `Graphics3D.render` inclusive 调用栈或软件三角形/像素
span。prepare 中少量 M3G 样本属于对象、状态和几何设置。因此本次应优先
研究通用解释器对区块数据访问、编码和网格构建的开销；这不说明游戏进入世界
后的绘制阶段不受 rasterizer 限制。

## RMS 零调用与文件路径的交叉验证

启采窗口内，`rs_find` 调用/扫描项/范围内样本均为 **0/0/0**，
getRecord/getRecordInto/getRecordSize、open/load/save 均为 0。

FileConnection 开输入流三次，分别是独立世界副本的 level.dat、chunks.dat、
entities.dat。[FileInputStream 实现](../../src/midp/nj_lib_io.c)的 host
入口共调用 1797 次，实际返回 **21,043,015 字节**。这个计数包括构造和
skip/available 等零字节探测，不能理解成 1797 次非零读取；字节数也不是
物理磁盘流量，因为有 OS page cache。CPU 采样本身不测量阻塞 I/O 等待。

为排除计数器漏接，使用同一诊断 RMS 对象和 sampler 做独立公开 API 正控制：
创建 store、增加 3 字节，分别调用三个读取入口后关闭。得到 find=3、probes=3、
三个 API 各 1、open=1、save=3，长度和 ID 正确。新建 store 无需加载旧文件，
load=0；断言在 `-UNDEBUG` 下执行。RMS 的零计数具有正控制支持。

## 实际场景与限制

1600 帧正常结束，400 帧处仍为 Preparing，1600 帧已进入岩洞/沙地世界，
HUD、物品栏和触控方向键完整，随后只向私有副本保存。未观察到未捕获异常
导致游戏线程退出；游戏探测 Android/DatagramSocket 的 caught 回退仍在。

本次只有一份世界、一轮有界诊断，没有未插桩配对、手机 ARM 或参考模拟器
同设备测量。并行构建、计数器和采样开销、OS cache 均会影响日志时间；
不能给出 Minecraft 加载/FPS 倍率，也不能把结论扩展到首次生成或所有世界。
本地完整命令、原始样本、四个原对象字节对照、正控制、场景图和身份清单保留在
`.local-tools/debug-runs/mc-load-20261110-profile/`。

后续候选应针对通用的数组、常量池和调用执行成本，用固定输入及完整世界
校验和验证语义，再做前后配对测量。修改原 JAR 的 Pal 算法或按游戏类名
替换实现不属于本次方案。
