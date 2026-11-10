# Android M3G GPU 后端：实际目标与接入边界

日期：2026-11-10。基于 nano_j2me `78114ba5493dce28776e6fd0beae26d8a96ac63a`
和固定 J2ME-Loader `afffdde4e677b3d5ef516e713732abc488f19fc0` 的源码审查。
本轮没有实现 GPU、构建或运行参考 APK，也没有 GPU 性能测量。

可行的接入方向是在 M3G native draw 内使用离屏 EGL context/FBO，复用现有
对象验证、场景展开和材质处理，再把结果同步到原 CPU surface。当前 Android
端口没有可直接启用的 M3G/GLES 开关。原版 Minecraft 需要的能力是
**离屏 Image、启用深度、多次绘制保持遮挡，以及与 CPU 和快照交换颜色/深度**。
只优化最终显示层，无法覆盖它的地形光栅。

## 原版 Minecraft 实际画到哪里

静态检查原始 `MinecraftJ2ME_PE_0.6.1_V2.2.jar`，SHA-256：

`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`

`javap -c -p MC` 与此前保存的原版反汇编逐字节一致，输出 SHA-256 为
`998a9c002d96884b041393d240a0e4264b8849cfb0369d9dde0cbc38b5578e3a`。
`MC` 继承 GameCanvas，运行线程取得 `getGraphics()` 后传给 `bindWorld`。
两条 `bindTarget(Object, boolean, int)` 调用的 boolean 都来自 `iconst_1`，
包括 hints 触发 IllegalArgumentException 后的重试，因此都开启深度。

对应到本项目，[GameCanvas.getGraphics](../../src/midp/nj_lib_game.c#L2610)
创建指向离屏缓冲 Image 的 Graphics。[M3G target helper](../../src/midp/nj_lib_m3g.c#L3961)
将它解析为 Java Image 的 CPU 像素；目标也可能是逻辑屏幕 surface。
helper 接受 RGB565、RGB888、ARGB8888 并检查 stride，不能假设所有目标都与
Android window 格式相同。当前 helper 没有 M3G Image2D 目标分支。

Minecraft 在释放 M3G target 后继续绘制 HUD，最终由
[GameCanvas flush](../../src/midp/nj_lib_game.c#L2703) 读取、复制或转换离屏
Image 到屏幕。之后的 [Android present](../../src/port/android/nj_port_android.c#L1064)
才执行 ANativeWindow_lock、CPU 缩放/转换和 unlockAndPost。
把 SurfaceView 改成 GL 显示，仍然会留下前面的 CPU M3G 绘制。

## 颜色可见性和深度必须一起设计

[bindTarget](../../src/midp/nj_lib_m3g.c#L4013) 捕获 Graphics 的 translate/clip，
设置 viewport、depth/hints，并使深度缓存在本次绑定开始时失效。
[releaseTarget](../../src/midp/nj_lib_m3g.c#L4061) 当前只解除绑定。
[clear](../../src/midp/nj_lib_m3g.c#L4176) 支持分别保留颜色或深度，并遵守
有效区域。不能在每次 render、host present、换材质或回退时无条件清深度。

[sw_depth](../../src/midp/nj_lib_m3g.c#L10984) 的 `_nj_zbuf` 是 Java `int[]`，
尺寸和有效标记也在 Java 字段，深度有效期属于一次 bind。当前
[量化](../../src/midp/nj_lib_m3g.c#L12520) 使用窗口 Z、depthRange 和 polygon offset；
有限正常值的键为 `1 + round((1-z)*16777215)`，有效范围 `1..16777216`，
`0` 表示清空，大键更近，相等允许通过。
[饱和处理](../../src/midp/nj_lib_m3g.c#L11624) 还处理极端输入。
普通 GL depth attachment 不会自动成为这份可序列化的整数数组。

[Image.getRGB](../../src/midp/nj_lib_lcdui.c#L2591)、2D 图像/文字、旋转复制、
GameCanvas flush 和截图目前直接依赖 CPU buffer，没有统一 GPU dirty barrier。
如果首版在每次 native render 返回前回读颜色，就可保持这些调用的可见性。
回读需处理 Y 翻转、RGBA/ARGB、目标格式和 stride；先写临时缓冲，成功后才提交
CPU 副本。`glFlush` 本身不能保证 CPU 已拿到结果。

保存也不能等到 release：[nj_vm_can_save](../../src/core/nj_save.c#L551) 拒绝
正在执行的 C native frame，但不会拒绝仍然绑定的 Graphics3D。
[Android 保存请求](../../src/port/android/nj_port_android.c#L1440) 在 VM 线程
input poll 处理，[序列化](../../src/core/nj_save.c#L1120) 包括 Java arena 中的
Image 和深度数组。协作调度可以停在同一次 bind 的两次 render 之间。
GPU 中尚未同步的状态不会自行进入快照。

| 设计阶段 | 必须覆盖 | 可以得出的结论 |
| --- | --- | --- |
| 无深度原型 | `depth=false`；离屏 Image 和屏幕；每 native 调用同步回读；格式、坐标、clip、2D/getRGB 可见性；失败回 CPU | 验证后端接线，尚未覆盖默认 Minecraft 地形 |
| Minecraft 所需后端 | `depth=true`；跨 draw 遮挡；部分 clear；depthRange/offset；alpha/blend/depth-write；CPU/GPU 深度互通 | 通过相应测试后，才具备默认地形路径的功能覆盖 |
| 可用性与性能验证 | 绑定中途保存/恢复；CPU 回退；上下文丢失；多 VM/GC/销毁；固定世界的真机配对测量 | 才能判断能否启用及实际收益 |

启用深度需要另行验证 canonical depth 的导入/导出，包括零清空标记、量化、
正交/通用投影、反向/相等 depthRange、polygon offset、alpha discard 和
透明层关闭 depth write。不能假定所有 GLES2 设备都提供所需 depth 读回能力。
另一种方案是跨调用驻留 GPU 数据并在所有 CPU 访问、保存和回退前同步，
但这需要完整的 barrier 和上下文丢失恢复设计。仅回读颜色后继续 CPU 光栅会丢遮挡。

## 后端接入口和所有权

[直接 VB render](../../src/midp/nj_lib_m3g.c#L14382) 和
[场景 render](../../src/midp/nj_lib_m3g.c#L4703) 已有 native 入口。
可在验证成功、进入逐三角形软件光栅前构造同步 draw 描述，复用 CPU 变换、
光照、裁剪和纹理解码，批量提交支持的组合。若使用已裁剪顶点，必须保留
clip-W 和透视插值信息。现有每三角形 mip 选择也不等同于 GPU 自动 LOD。
不能用逐 span 的 GL 调用或隐式改变过滤、wrap、blend 来替代这些语义。

[nj_port](../../include/nj2me/nj_port.h#L256) 的 display 接口只接收 framebuffer
和呈现区域，没有几何或深度参数。可选 M3G backend 需要另批接口设计；
新增 host 结构字段也是接口变化，不能混称无 ABI 变化。
[当前 MIDP 状态约束](../../src/midp/nj_midp.h#L10) 继续适用：Java 字段保存
逻辑状态，GL context/FBO/buffer 是每 VM/port 可丢弃的派生资源。
首版可先不缓存跨调用的 Java 对象；借用 arena 指针期间禁止会使其失效的分配或回调。

GL owner 应是实际运行 M3G 的
[android_main_thread](../../src/port/android/nj_port_android.c#L4896) pthread。
外层 [nativeRun](../../src/port/android/nj_port_android.c#L5456) 创建并等待它，
Java `nj2me-vm` 线程和 UI surface callback 都不能据名称被当成这个 owner。
离屏 context 可使用独立 pbuffer/FBO，保持现有 window 呈现路径。
暂停、重建和销毁通过 owner 处理；surface callback 不能直接销毁它的 GL 资源。
上下文丢失后需保留 CPU 权威副本，确保可以继续或回退。

[Android 链接脚本](../../tools/build_android.sh#L234) 当前没有 EGL/GLES 依赖。
后续需要可选构建源和库、设备能力检测及明确的 unsupported/failure 回退。
先完整预检受支持 draw，再修改目标；失败时不得在已部分更新的 CPU 结果上重复绘制。

## J2ME-Loader 的参考价值和收益范围

固定参考确实采用 GPU M3G，但它的
[releaseTarget](https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/src/m3g_rendercontext.c#L1409)
包含 `glFinish`，内存目标经
[glReadPixels](https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/cpp/m3g/src/m3g_rendercontext.inl#L1065)
回写 Bitmap，显示端又上传 Bitmap，不能称为零复制。
其 [Platform.executeInUIThread](https://github.com/nikita36078/J2ME-Loader/blob/afffdde4e677b3d5ef516e713732abc488f19fc0/app/src/main/java/javax/microedition/m3g/Platform.java#L45)
实际直接调用 `run()`，方法名不构成线程保证。完整引擎有自己的对象和资源模型，
不能仅移入几段 JNI 当作本项目的后端。

已有 [J2ME-Loader 对照](2026-11-10-j2me-loader-comparison.md) 和
[LOAD 分段采样](2026-11-10-minecraft-load-profile.md) 表明，当前纯 GEN 和已测
LOAD 解码/mesh 准备阶段主要耗在 Java 执行，采样窗口没有 M3G render/span。
GPU 潜在收益在开始绘制后的光栅阶段，仍可能被 upload、同步回读和 CPU 场景处理抵消。
以后应分别记录 setup、upload、draw、readback、present，使用原始 JAR、相同世界、
分辨率、相机和限帧配置，在真机上确认实际命中 GPU draw。
本轮源码审查不能给出 Android FPS、GEN 或 LOAD 的加速数字。
