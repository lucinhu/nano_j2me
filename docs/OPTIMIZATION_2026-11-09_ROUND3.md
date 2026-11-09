# 第三轮：变换透明绘图、字符串容量与 Nokia 声音回调

基线：`2622f535aad536f6aff9479d7e6297883221d4b6`。

## 变换透明绘图与遮罩

ARGB8888 的 PAINT 路径改为每行计算一次源起点，使用固定字节步长读取旋转/镜像后的像素，复用原有混合和 RGB565 舍入逻辑。保持 mask、XOR 等路径语义，使用 `memcpy` 处理未对齐内存；整数偏移避免逆向遍历结束后形成缓冲区前方指针。

短遮罩以前仅记录宽度并按变换后目标尺寸读取，2×2 源加 1×1 遮罩即可触发 ASan `stack-buffer-overflow`。现在同时传递实际宽高，将绘制区域与目标局部坐标下的遮罩尺寸求交，遮罩之外不绘制。回归覆盖 8 种变换、不同短遮罩比例、负目标位置、平移、裁剪、SET/PAINT 以及 stride。

固定 Intel Core Ultra 9 275HX 的 CPU 23，GCC 16.2.1；`-std=c99 -O3 -DNDEBUG`，预热后九对交替运行，中位 ns/源像素如下：

| 用例 | 旧实现 | 新实现 | 加速 |
|---|---:|---:|---:|
| 混合透明度 ARGB，37×29 镜像 | 7.1869 | 4.1454 | 1.73× |
| 混合透明度 ARGB，37×29 旋转 90° | 7.2418 | 3.6799 | 1.97× |
| ARGB → RGB565，37×29 旋转 90° | 8.8916 | 2.8384 | 3.13× |
| ARGB → RGB565，21×19 镜像 | 9.2427 | 2.8431 | 3.25× |
| 全不透明 ARGB，37×29 旋转 90° | 2.8434 | 0.3777 | 7.53× |
| 全透明 ARGB，37×29 旋转 90° | 1.8455 | 0.5395 | 3.42× |
| 半透明 ARGB，1×1 镜像 | 19.9782 | 14.0916 | 1.42× |
| 无变换 ARGB → RGB565 控制 | 3.0165 | 3.0168 | 1.00× |

新增 22 项及原有 14 项固定工作量基准的每轮校验和均一致。mask、XOR、无变换和大块 RGB 拷贝控制基本持平；原有三个 RGB/SET 1×1 用例慢约 0.26–0.62 ns/调用（3–6%），ARGB SET 1×4 慢约 0.54 ns/调用（4%），保留这些小尺寸代价的记录。最初新增调用点使 GCC 将 `nj_blend565` 从内联改成独立函数，导致无变换混合慢约 6%；加入 C99 `inline` 提示后，最终对照恢复为上表的 1.00×。

这些是本机热缓存绘图微基准，不能换算为整款游戏 FPS。复测使用同一份受版本管理的基准，仅替换绘图实现：

```sh
mkdir -p .local-tools/debug-runs/round3-repro
git show 2622f535aad536f6aff9479d7e6297883221d4b6:src/gfx/nj_gfx.c \
  > .local-tools/debug-runs/round3-repro/before.c
cc -std=c99 -O3 -DNDEBUG -Iinclude tools/bench_gfx.c \
  .local-tools/debug-runs/round3-repro/before.c tests/stub_font.c -lm \
  -o .local-tools/debug-runs/round3-repro/before
cc -std=c99 -O3 -DNDEBUG -Iinclude tools/bench_gfx.c \
  src/gfx/nj_gfx.c tests/stub_font.c -lm \
  -o .local-tools/debug-runs/round3-repro/after
.local-tools/debug-runs/round3-repro/before --alpha
.local-tools/debug-runs/round3-repro/after --alpha
```

不传 `--alpha` 运行原有 14 项。固定同一 CPU，预热后交替多轮，先检查完整目标缓冲区校验和再比较中位数。

新增合成矩阵在旧代码的正常输入上 16,826 次断言全部通过，包含每个 alpha 值 0–255、8 种变换、三种目标格式、SET/PAINT、mask/XOR、裁剪、未对齐缓冲区和单行/列。旧代码在短遮罩 ASan 复现中越界；最终新代码在 Release 与 ASan/UBSan（含泄漏检查）中均为 **17,338 次断言 / 0 失败**。

## StringBuffer / StringBuilder

修复共用实现的容量和失败语义：

- 无参数构造器容量为 16；整数构造器精确保留请求（包括 0），负数仍抛出 `NegativeArraySizeException`。
- 字符串构造器容量为字符串 UTF-16 长度加 16，保留原始内容。
- 扩容使用 `max(请求值, 旧容量 * 2 + 2)`，计算在有符号 Java 长度上限处饱和；不再使用 `2n+8` 的重复扩容。
- `ensureCapacity` 在分配失败时返回错误状态并保留 `OutOfMemoryError`；旧缓冲区的容量、长度、内容保持可用。
- `append(char[], offset, length)` 使用减法范围检查，避免 `offset + length` 的 C 有符号溢出。

依据：[CLDC 1.1 StringBuffer](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/lang/StringBuffer.html)。同一组回归在旧实现中为 **833 项 / 32 失败**，UBSan 报告 `2147483647 + 1` 有符号溢出；修复后普通和 ASan/UBSan 运行均为 **833 项 / 0 失败**。覆盖指定/默认/字符串构造器、负数、零容量、增长规则、带 NUL 的 UTF-16 内容保留、极端 append 范围和大容量分配失败。此处是语义与稳定性修复，没有把容量变化宣称为整机速度提升。

## Nokia SoundListener

`soundStateChanged(Sound sound, int event)` 原先把状态整数写入第一个参数，第二个参数未初始化。现传入发生变化的 `Sound` 实例和新状态。修复不改变类、字段、方法表，也不改变现有回调调度或异常处理方式。

测试监听器验证 play、stop、resume 的对象身份和状态，在回调内主动 GC 后查询 `getState()`。旧实现 22 通过 / 4 失败；修复后 32 通过 / 0 失败（旧代码无法进入有效 Sound 的后续检查，因此断言总数不同）。

规范与实现依据：

- [Nokia SoundListener API](https://nikita36078.github.io/J2ME_Docs/docs/nokiaapi2/com/nokia/mid/sound/SoundListener.html)。
- [Symbian/Nokia Sound 源码](https://raw.githubusercontent.com/SymbianSource/oss.FCL.sf.app.JRT/0822c2dcfb807a245ec84ab06006b59df7aedab6/javauis/nokiasound/javasrc/com/nokia/mid/sound/Sound.java)。原始实现使用独立通知线程；本模拟器仍同步通知，该差异尚未修复。

## 集成与游戏验证

- 默认 Release 全目标构建成功，完整 CTest **141/141 通过，73.77 秒**，包括 FreeJ2ME 三组对照画面、SDL2 客户端与快照检查。
- `python3 tools/api_audit.py`：CLDC/MIDP 参考的 982 个成员全部解析（其中 20 个在其他类中声明）；成员可解析不等同于所有语义已兼容。
- 图形、字符串和 Nokia 声音回归分别通过上文的 Release / ASan+UBSan 验证。
- 无生产 builtin 字段/方法表变动，未改变快照布局。主构建中未增加依赖或修改功能开关。

原版《3D版田园物语》JAR SHA-256：`8ad0920c1ebf685a1272bfa5f7fc1e0c4415189bcb82d0e28bd82917fca54e63`。修改前后均使用 240×320、16 MiB 堆、headless 30 FPS、480 帧和独立全新 RMS/FS 目录，按键：

```text
100:soft1 140:fire 180:soft1 230:fire 330:fire 370:fire 410:fire
```

两版都到达菜单和剧情页，正常结束。每 60 帧采样，8 张中 6 张逐像素相同；第 240、300 帧菜单飘叶动画相位不同，最终剧情页相同，已查看菜单及剧情图像。两版均有相同的五次被游戏捕获的空输入流 `NullPointerException`，没有未捕获异常。此验证限于启动、菜单和进入剧情，不覆盖完整游戏，也不用于计算速度提升。

本机复现、逐次计时、校验和与日志保存在 `.local-tools/debug-runs/opt-round3-{gfx,string,vendor}/`，不纳入提交。原游戏、用户存档和编译产物不纳入提交。

## 尚待单独处理的兼容性问题

厂商接口只读审查发现 DirectGraphics 的若干格式常量、`getNativePixelFormat()` 返回值、透明度/变换处理及像素范围检查需要进一步修复。该接口涉及多个像素布局，留待独立复现、逐格式回归后处理；本轮没有宣称已经补齐。
