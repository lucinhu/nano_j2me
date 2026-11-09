# J2ME-Loader 对照：Minecraft 的执行、绘制和存储成本

日期：2026-11-10。参考官方 J2ME-Loader commit
[`afffdde4e677b3d5ef516e713732abc488f19fc0`](https://github.com/nikita36078/J2ME-Loader/commit/afffdde4e677b3d5ef516e713732abc488f19fc0)，
版本声明 1.8.2 / code 101。以下是固定源码的对照和 nano 的独立测量，
没有运行参考 APK 或在同一部手机上测过两者，不能据此给出速度差的倍数。

## 已确认的架构差异

| 路径 | J2ME-Loader | nano_j2me 当前路径 | 可借鉴的方向 |
| --- | --- | --- | --- |
| 游戏执行 | 安装时 class/JAR → DEX，DexClassLoader 交给 Android runtime | C 字节码解释器，已有 computed goto、融合指令和调用缓存 | 减少通用解释执行成本；更大粒度的编译及衔接 |
| M3G 几何 | Java 对象 handle → JNI → native M3G → GLES 1.1 | C 中变换、裁剪和 CPU 光栅，framebuffer 交端口显示 | 批量提交、GPU 资源驻留与后端接口 |
| RMS 热读取 | 已打开 store 的 HashMap 查 id，再复制记录 | 已加载内存记录，线性扫描 id Vector，再复制记录 | 测量 id 查找，建立可随 GC/快照恢复的索引 |
| RMS 更新 | 同步保存 header 和变更 record 文件 | 同步序列化整个 store | 单独设计降低写放大的方案及恢复语义 |
| 显示和限帧 | 多种显示模式、pending repaint 合并和可选 cap | 端口 present、VM/游戏限帧与线程调度 | 比较相同逻辑工作量、绘制和实际呈现 |

### 执行引擎与 GEN

[安装器][install]调用 dx，使用 `--no-optimize`、`--core-library` 生成 DEX。
`--no-optimize` 是转换器参数，不表示 Android runtime 不能优化。
[AppClassLoader][loader]继承 Android `DexClassLoader`，由
[MicroLoader][midlet]加载游戏入口。Android runtime 可以解释、AOT 或 JIT；
具体受 Android 版本、热度、profile 和设备配置影响。本研究没有取得参考设备的
compiled-method 状态，不能断言 Minecraft 所有方法都已编译。

这项执行模型差异能够解释纯 Java 热循环的性能潜力。GPU 不参与本项目的纯
`Gen.generate(NULL)` 基准，不能用 GLES 的存在解释这段 GEN 的主要成本。
ART 专用 runner 需要重做对象、native bridge、线程、类加载和快照的衔接，
不是替换一个解释器选项即可完成的移植。

### M3G 批次和同步

[Java render][render]一次传入 VB/IB/Appearance handle 和 transform，经
[JNI][jni]进入 native M3G。[index buffer 提交][draw]直接调用
`glDrawElements(buf->glPrimitive, buf->indexCount, buf->glType, buf->indices)`。
JNI 粒度是一次 render/submesh，不是每个三角形；这也不意味着所有 immediate
render API 调用已经跨调用合批。GLES 是可使用 GPU 的后端，实际设备是否走硬件
驱动仍须运行时确认。

参考也有同步和复制：[releaseTarget][finish]调用 `glFinish`，Android memory
目标经 [glReadPixels][readback]回写 Bitmap；临时缓冲是
`max(width * 4, 16384)`，按若干行处理，不是所有块都小于 16 KiB。
显示端 [GLRenderer][display]又用 `GLUtils.texImage2D` 上传 Bitmap。
因此不能把这条链称作零复制，或承诺原样接到 SDL 就更快。

nano 已有整个 VB/IB 的 native 入口、strip 顶点复用、纹理缓存和快速 span。
后端实验应先规定 GPU target、2D overlay、getRGB/Image2D、截图和快照的可见性，
分别测 upload、draw、readback 和 present。保留软件后端作为嵌入式实现和像素 oracle。

### 存储、加载和 FPS 的口径

参考 [RecordStoreImpl][records]以 `HashMap<Integer, byte[]>` 保存打开的记录，
热读复制数据；[saveToDisk][save]写 header 和变更的 record 文件。
nano 已有打开后的内存记录，增加一份笼统的“RMS 内存缓存”不会消除现有 id 扫描。
普通 [FileConnection][file]使用 FileInputStream/DataInputStream 或
RandomAccessFile，没有在该路径发现 Minecraft 专用的世界读取器。

LOAD 必须拆开首次文件读、热读、Java 解码、建几何、更新纹理与首帧。
增量写策略不直接解释只读取世界的 LOAD；分 record 文件也可能增加冷启动 open/stat。
不能仅从日志字段 `LOAD` 就认定磁盘是主要瓶颈。

[Canvas][canvas]合并重复 repaint；异步 graphicsMode 1/2 的
[setLimitFps][fps-zero]会把 fpsLimit 0 改为 1000，不能把所有模式的 0
都理解成完全无限帧。
[immediate event mode][events]明确违反事件串行化，不能作为等语义的默认加速。
应同时记录逻辑 ticks、paint、triangles、present、分辨率、相机、cap 与 sleep。

## nano 的新 GEN 采样

使用 v0.5.5 commit `f56ca2327b78e1b4c956e52d27bd453bc5f1661e` 的冻结库，
原版 `MinecraftJ2ME_PE_0.6.1_V2.2.jar`，SHA-256
`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`。
工作量来自 [`bench_minecraft_gen.c`](../../tools/bench_minecraft_gen.c)：
seed 12345、type 0，调用原游戏算法，输出 spawn 和 256 个 chunk 的内容校验。
Linux x86_64、GCC 16.2.1、Release `-O3`，threaded dispatch 和融合开启，JIT 关闭，
固定 CPU0，以 1 kHz SIGPROF 预分配采样缓冲记录 native PC 和当前 Java 方法。
采样 handler 不分配、不做文件 IO，结束后才符号化。

单次带采样运行 CPU 9762.402 ms、wall 9808.419 ms，5,175,712,766 次融合后
解释分发、1,616,679 次 native 调用、0 次 GC；spawn 为 `(161, 64, 111)`。
这不是无采样的吞吐基准，更不是与 J2ME-Loader 的 A/B 测速。

| Native 样本位置 | 9,763 个样本中的数量 | 占比 |
| --- | ---: | ---: |
| `nj_interp_run` | 8,327 | 85.29% |
| `nj_interp_const_arith` | 603 | 6.18% |
| `nj_cp_const` | 266 | 2.72% |
| `nj_frame_push` | 193 | 1.98% |
| `nj_array_get_int` | 184 | 1.88% |

Java 方法归属：`Gen$INoise.add/grad` 合计 28.12%，`Gen$Rnd.next` 10.24%，
`Pal.get/encode0/decode` 合计 21.24%。保存的 Java PC 只在解释器安全边界更新，
不能据此把样本定位到某一条精确字节码。这些百分比说明调查优先级，不是可消除的
加速比例；0 次 GC 也只适用于这次固定 GEN 工作量。

已有 [JIT 实验](2026-11-02-jit-x86_64.md)记录平均 block 只有约 10.5 条字节码，
频繁返回 C，且该路径关闭解释器融合；固定工作量 microbenchmark 曾从
0.177 s 回退到 0.279 s。因此本轮没有把开启现有 JIT 当作 ART 的替代方案。

## 一个未合入的通用执行候选

根据 `nj_interp_const_arith` 的样本，做了只强制内联该整数算术 helper 的
隔离试验，未改算法或浮点逻辑。基线重编译的 `.text` 与冻结库原成员逐字节一致。
候选的解释器 `.text` 从 83,713 B 增为 84,763 B（+1,050 B）。

CPU0 首对 seed 12345：基线 CPU 9272.278 ms，候选 9015.174 ms，约 -2.77%。
256 个 chunk 的 block/data、WORLD hash、spawn、分发数、native 数、GC 和
heap/peak 完全一致。它没有达到试验前设定的 3% 重复验证门槛，因此停止该候选，
没有改生产解释器。单对结果不足以确认稳定收益，也没有 Android 的对应测量，
不能据此宣称 Minecraft 已提速 2.8%。这项试验只说明该局部候选暂不进入合入验证。

## 适配顺序和验收

1. 先按新样本实验通用的算术、数组、字段和调用序列融合。保持异常、预算、安全点、
   Java 浮点和整数溢出语义，固定 seed 校验所有 chunk/spawn；不按 Minecraft 类名
   替换游戏算法。每项以交替运行的 CPU 时间判断，分别说明桌面和 Android 证据。
2. LOAD 单独分段采样。RMS 先确认 id lookup、读取和写放大的占比；索引需覆盖删除
   孔洞、同名重复打开、重新打开、枚举、GC 和快照重建。磁盘格式与失败恢复另立设计。
3. 绘制后端先建立资源身份/revision、顺序和 CPU/GPU 所有权，再做可回退的离屏
   GPU 实验。M3G 的 alpha、fog、相等深度及 selection pass 保留现有像素测试。
4. 若继续 JIT，优先保留解释器融合、增加 block 粒度和 chaining，分别计编译、
   入口/出口、guard 和代码内存。没有固定工作量收益前不默认打开。

本轮同时完成的 [RMS 生命周期修复](2026-11-10-rms-enumeration.md)已提交为
`4addd88`：实时枚举、回调 GC、共享打开计数和旧状态迁移经过 153/153 全回归。
它保持现有 RMS 磁盘格式，尚未改变 id 线性查找和整个 store 的持久化粒度，
也没有据此声称 Minecraft 的 GEN 或 FPS 已获得新的倍数提升。

本次按 `Minecraft|Mojang|Gen\$INoise` 检索固定参考 checkout 未发现专用分支；
这不覆盖其他 fork、APK 或外部 profile。参考存在通用 ASM 兼容改写和可选关闭
lighting、放宽 alpha threshold 的行为，它们不属于本项目的等像素优化。
根项目 Apache-2.0 与 Nokia M3G 的 EPL-1.0 分开处理；本轮仅研究架构，未拷入代码。

[install]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/ru/woesss/j2me/installer/AppInstaller.java#L290-L302
[loader]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/shell/AppClassLoader.java#L38-L55
[midlet]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/shell/MicroLoader.java#L177-L208
[render]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/m3g/Graphics3D.java#L299-L325
[jni]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/jni/graphics3d.inl#L314-L352
[draw]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/src/m3g_indexbuffer.c#L67-L76
[finish]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/src/m3g_rendercontext.c#L1409-L1432
[readback]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/src/m3g_rendercontext.inl#L1011-L1095
[display]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/lcdui/Canvas.java#L785-L827
[records]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/rms/impl/RecordStoreImpl.java#L335-L364
[save]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/rms/impl/AndroidRecordStoreManager.java#L233-L251
[file]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/org/microemu/cldc/file/FileSystemFileConnection.java#L363-L429
[canvas]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/lcdui/Canvas.java#L608-L668
[fps-zero]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/lcdui/Canvas.java#L219-L224
[events]: https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/lcdui/event/EventQueue.java#L38-L99
