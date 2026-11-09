# Minecraft J2ME：世界生成与跨宿主帧深度缓冲

本次针对 `MinecraftJ2ME_PE_0.6.1_V2.2.jar` 的生成耗时和水面、树叶、云等物体闪烁问题。基线为 `ba887ad12e95ca1eceb579e09da3eff816d90323`。原始 JAR SHA-256：`fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`。未修改游戏，也未读取或覆盖用户世界；所有生成和回放均使用独立 RMS、文件系统目录。

## 世界生成

Java/宿主采样表明，生成阶段主要消耗在解释器执行原版噪声、随机数及方块调色板代码。固定种子 12345 的原版 `Gen.generate(null)` 执行 5,175,712,766 次解释器 dispatch（计数包含融合指令，并非逐条原始 JVM 指令）；生成中没有 GC。宿主采样约 85.6% 在 `nj_interp_run`，约 6.7% 在其常量算术辅助函数。直接优化游戏逻辑或添加游戏专用指令并非必要。

CMake 原来无条件为 `nj_interp.c` 附加 `-O2`，覆盖 Release 的 `-O3`，也覆盖 Debug 和用户工具链选项。现在该文件默认遵循构建类型。新缓存项 `NJ_INTERP_OPT_LEVEL` 可显式指定 `0/1/2/3/s/g`，空值表示不覆盖；非法值在配置时拒绝。

旧的限制来自提交 `2d5691c5557e056d5733725d201aafc950ba8281` 对 GCC 16 在并行构建内存压力下出现 ICE 的记录，没有具体 minor 版本或复现命令。当前 GCC 16.2.1 独立构建成功不能证明所有环境都已消除此问题；遇到同类编译器故障时可显式回退：

```sh
cmake -S . -B build -DNJ_INTERP_OPT_LEVEL=2
cmake --build build -j2
# 恢复遵循构建类型：
cmake -S . -B build -DNJ_INTERP_OPT_LEVEL=
```

独立实验使用 Intel Core Ultra 9 275HX、GCC 16.2.1，将进程固定到 CPU 0，只把解释器从 `-O2` 改为 `-O3`，两者均保留 `-DNDEBUG`；其余 Release 运行库原本已用 `-O3 -DNDEBUG`。JAR、种子、运行库配置一致。固定 12345 预热后做三轮交替测量，另两个种子做 AB/BA；生成数据的 256 个 chunk 校验、方块/元数据聚合校验、出生点和 dispatch 数均逐项相同。CPU 时钟未锁定，时段之间的绝对耗时不混合统计。

| 种子 | 原 -O2 CPU 中位数 | 遵循 Release -O3 | 耗时减少 |
|---|---:|---:|---:|
| 12345 | 11,329.286 ms | 9,128.450 ms | 19.43% |
| 54321 | 11,644.724 ms | 9,365.679 ms | 19.57% |
| -987654321 | 12,207.890 ms | 9,752.317 ms | 20.12% |

这些数字衡量原版生成方法，不包括菜单、保存及渲染准备；不能直接当作用户日志中整个加载流程的耗时，也不代表所有游戏的 FPS 增益。局部内联常量辅助函数的对照只有约 1–2% 且区间重叠，未加入该源码改动。

## 深度缓冲生命周期

原实现把软件深度缓冲的有效性关联到 `vm->frames`。GameCanvas 的一次 bind/render/release 可以跨过多个宿主调度和呈现帧；在同一次绑定的中途检测到宿主帧号变化并清空深度，会让后续的镂空或半透明层丢失前面地形的遮挡关系。

原版游戏、空存档创建世界、连续 4,800 帧的独立追踪中，共 2,220 次绑定，1,722 次跨过宿主帧；捕获四次中途清掉真实非零深度，每次涉及 70,291 个像素。它们发生在同一次绑定已 render 三次之后、第四次 render 之前。日志没有无效存档恢复产生的 chunk/Datagram IO 错误。

现在深度属于当前目标绑定：bind 后或尺寸变化时初始化，后续 render 保留，显式 clear 按 viewport 清除；宿主帧号不再触发清除。沿用既有 `_nj_zframe` 槽作为有效性标记（负数表示需初始化，非负数表示有效），没有改变 builtin 字段、方法数量或顺序。旧快照的非负帧号也可作为有效状态读取。

规范依据：[JSR-184 Graphics3D](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Graphics3D.html)。绑定后的初始深度未指定，release 后可丢弃；普通 render 不隐式清除深度。此次修复不改透明排序、纹理采样或 alpha 混合公式。

## 验证与边界

配置选项检查覆盖：Release 默认只有 `-O3`；Debug 不再附加优化；显式 `2` 在命令末尾覆盖为 `-O2`；非法 `9` 配置失败。

- 正常 `cmake --build build -j4` 成功；完整串行 CTest **147/147 通过**，323.18 秒，包括参考画面对照、SDL2 和保存状态回归。
- 新 `test_m3g_depth`：旧实现 69 项中 33 项失败，修复后普通与 ASan+UBSan 均 69/0。覆盖远处不透明/镂空/半透明层遮挡，宿主帧号正负边界及回绕，深度数组完整比较，局部 clear、只清颜色、禁用 clear、重新绑定及尺寸变化。独立 sanitizer 邻测：M3G 933/0、target 1,711,777 通过、save 2,521/0，无 sanitizer 诊断。
- API audit 的 982 个核心参考成员全部可解析；59 个 M3G builtin 方法/字段/类声明块逐字不变。
- 独立重链接 O2/O3/内联实验三种解释器均通过 HelloWorld 三轮、CondCore 三轮、arrays/class hardening/resolution；CondCore 的 27 项 digest 与桌面 JVM 逐项相同，覆盖整数回绕/除零/移位、long/float、字段/数组、融合栈等边界。
- 同一复制世界、静止相机、两个全新 96 MiB headless 进程各 2,400 帧（不限速）：旧版 1,191 次 bind / 896 次跨帧 / 12 次清空非零深度；新版 1,079 次 bind / 810 次跨帧，14 次遇到原来的触发条件但 **0 次隐式清空**。受影响的第四次 draw 实际均为 ALPHA，depth test 开启、depth write 关闭。两组进程的调度次数不同，不能把相同宿主帧号当成相同游戏帧。
- 第一对 draw 的树干区域 `x=164..239, y=168..242` 绘制前像素完全相同，绘制后 3,833 像素不同：旧版水面透过前景树干，新版被树干遮挡。全帧绘制前有 8,036 像素差异（树叶/天空等独立变化），此结论严格限定在相同输入的树干区域，不声称全帧一致。后续其他 draw 对也没有当作相同输入比较。
- 连续已完成游戏帧的稳定窗口 1,300..2,399：旧版捕获 1,088 帧，树干区域出现 4 次切换、每次 3,823 像素；新版捕获 1,027 帧、0 次切换。这证明所复现的水面穿透及闪烁被消除，不代表所有场景中的水、树叶、云动画都已验证。
- 主构建 SDL2/X11 原版 JAR 从复制存档正常加载并运行 1,200 帧；实际窗口截图确认地形和 HUD，未出现未捕获异常、chunk 或 Datagram 读取错误。仍有游戏自己捕获的可选 Android/DatagramSocket 类探测异常。平均 29.7 FPS 是限速回放，不用作性能结论。

可用保留工具独立复核原版生成方法，不需要修改 JAR：

```sh
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude -Isrc/core \
  tools/bench_minecraft_gen.c build/libnj2me.a -lz -lm \
  $(pkg-config --libs libcurl sdl2) -o /tmp/bench_minecraft_gen
/tmp/bench_minecraft_gen tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar 12345
```

链接的可选库须与本地构建配置一致。工具使用全新 64 MiB headless VM、survival 模式、空 progress callback，只给 `Gen.generate(null)` 计时，随后输出出生点、256 个 chunk 的方块/元数据校验和及聚合校验和；不调用 MIDlet 启动或保存世界。固定 12345 的预期聚合值为 blocks `a893a062`、data `c77a1b93`，出生点 `161,64,111`，dispatch `5175712766`，natives `1616679`，生成中 GC `0`。编译后的独立工具以同一对象分别链接 O2/O3 运行库，所有 512 个 chunk 校验（方块和元数据各 256）一致，方块校验也逐项匹配原 JAR 的 WorldSum。

历史旧世界快照在没有匹配文件系统的环境中恢复，会出现 chunk 读取和 Datagram 错误，未用作正确性证据。一次菜单输入固定种子的尝试停留在创建界面，没有生成世界，也未用作性能证据。性能测试使用直接调用原版生成方法的确定性工具；实际游戏回放单独验证。
