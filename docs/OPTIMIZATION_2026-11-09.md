# 二维绘图提速与 CLDC 接口修复（2026-11-09）

本轮基线为 `6c2f638fc932f9f60bb93cf5e6ea94c685dda6b1`，重点是有测试证据的二维绘图热点、数据流接口语义和 Hashtable 低内存问题。没有增加内建类的方法或字段，快照布局不变。

## 速度

[二维绘图实现](../src/gfx/nj_gfx.c)的旋转、镜像同格式拷贝，从逐像素逆变换与可变长度 memcpy，改成每行计算源起点、按固定步长读取，并分别拷贝 2/3/4 字节。1×1 区域直接拷贝。适用 RGB565、RGB888 的 SET/PAINT，以及 ARGB8888 的 SET；透明混合仍使用原有合成路径。

固定 Intel Core Ultra 9 275HX 的 CPU 23，GCC 16.2.1，`-std=c99 -O3 -DNDEBUG`。相同输入、调用次数和编译选项，预热后交替运行九对，比较中位 ns/像素：

| 用例 | 修改前 | 修改后 | 加速 |
|---|---:|---:|---:|
| RGB565 镜像 21×19 | 1.1174 | 0.3282 | 3.40× |
| RGB565 旋转 90° 120×320 | 1.1293 | 0.2646 | 4.27× |
| RGB565 旋转 180° 240×320 | 1.8020 | 0.2675 | 6.74× |
| RGB888 旋转 270° 120×320 | 1.7967 | 0.3021 | 5.95× |
| ARGB8888 镜像 240×320 SET | 2.1347 | 0.2677 | 7.97× |

三种格式的 1×1 用例提升 1.11–1.25 倍；无变换拷贝、透明混合、填充控制组持平。14 项基准所有轮次的输出校验和一致。这是热缓存绘图微基准，不代表整机游戏 FPS 的提升倍数。

[固定工作量基准](../tools/bench_gfx.c)输出用例名、秒数、ns/源像素、完整目标缓冲区 FNV-1a 校验和、调用次数。POSIX 环境可用下列命令复现当前版本：

```sh
mkdir -p .local-tools/debug-runs/gfx-bench
cc -std=c99 -O3 -DNDEBUG -Iinclude tools/bench_gfx.c \
  src/gfx/nj_gfx.c tests/stub_font.c -lm \
  -o .local-tools/debug-runs/gfx-bench/after
.local-tools/debug-runs/gfx-bench/after
```

对照版本使用相同基准和选项，仅替换绘图源：

```sh
git show 6c2f638fc932f9f60bb93cf5e6ea94c685dda6b1:src/gfx/nj_gfx.c \
  > .local-tools/debug-runs/gfx-bench/nj_gfx-before.c
cc -std=c99 -O3 -DNDEBUG -Iinclude tools/bench_gfx.c \
  .local-tools/debug-runs/gfx-bench/nj_gfx-before.c tests/stub_font.c -lm \
  -o .local-tools/debug-runs/gfx-bench/before
```

测量时固定同一个 CPU，先预热再交替执行 before/after，检查校验和后比较多轮中位数。微基准绝对数值会随编译器、CPU、缓存和电源策略改变。

## 接口与兼容性

[IO 实现](../src/midp/nj_lib_io.c)修复：

- `DataInputStream.readFully` 通过底层 `read(byte[],off,len)` 批量读，正确处理短读、EOF 和异常，支持只覆盖批量读取的流；异常流返回零时尝试单字节读取，避免死循环。
- 实例和静态 `readUTF` 先读完声明的字段体，再检查 modified UTF-8。非法尾部不能吃掉下一字段；物理截断抛出 EOF，非法编码抛出 UTFDataFormatException。
- `DataOutputStream.write(byte[],off,len)` 直接转发原数组和区间，消除临时数组复制；单字节写入走 `write(int)`。成功写入更新现有 `written` 字段，`size()` 不再固定为零，达到 INT_MAX 后饱和。
- `writeUTF` 先验证编码长度，再一次性写出头部与正文，减少临时数组和虚拟调用。
- 输入、输出包装流的 `close()` 转发至底层流；输出先调用自身虚拟 `flush()`，支持子类覆盖。flush 失败仍尝试 close；close 成功时保留 flush 异常，两者都失败时传播 close 异常。
- 多处数组区间检查用减法比较，避免 `off+len` 的有符号溢出。

[集合实现](../src/midp/nj_lib_util.c)修复：

- Hashtable 构造、扩容的临时数组和枚举快照在分配期间显式保活，避免 GC 回收尚未挂到 Java 对象上的数据。四个内部数组全部分配成功才发布，分配失败不丢旧表。
- 扩容按缓存的 hash 直接寻找空槽，避免对已存在的不同键再次调用 Java `equals()`，减少碰撞时的调用开销，防止重排中发生用户异常或 GC。

另修复图像区域裁剪的 `sx+sw` / `sy+sh` 溢出；旧实现已由新用例和 UBSan 复现。

运行 `python3 tools/api_audit.py`，CLDC 1.1 / MIDP 2.0 参考的 982 个成员均能解析。本轮主要修复已存在接口的错误行为；成员可解析不等同于所有可选 API 或游戏场景完全兼容。

## 验证

基线完整 CTest：140/140 通过；修改后完整 CTest：141/141 通过，包括 FreeJ2ME 对照画面检查、SDL2 客户端及快照测试。最终 close 子类边界修正后，重新构建全部目标，并重跑 IO、类库、ZIP、快照、RMS、集合、图形、原生绑定、生命周期九组测试：9/9 通过。新增 [集合回归](../tests/test_util.c)，扩充 [图形回归](../tests/test_gfx.c)及 [IO 回归](../tests/test_ioapi.c)。

最终 IO Release 与 ASan+UBSan 均为 400 通过、零失败；独立 ASan+UBSan 还验证类库 1432 项、快照 2521 项、ZIP 423 项及集合 GC 用例，均通过。独立 sanitizer 构建因现有 socket 的 GCC 16 stringop-overread 警告关闭了该构建的 Werror，未修改 socket 源码；默认 Release 主构建正常通过。

图形 Release 和 ASan+UBSan 均为 4920 次断言、零失败。新增矩阵包含 2240 次正向参考绘制比较，覆盖八种变换、三种像素格式、SET/PAINT、裁剪平移、单行单列、未对齐缓冲区及非紧凑 stride；极值测试另覆盖 56 次绘制。旧实现的新 IO 用例为 327 通过、54 失败，并复现 readFully 参数溢出；Hashtable 的碰撞扩容、数组 GC 存活、枚举快照 GC 存活三个新场景也均在旧代码上复现失败。

实际游戏回放使用《3D版田园物语》，240×320、16 MiB、30 FPS、480 帧，修改前后分别使用新的隔离 RMS/FS 目录和相同按键：

```text
100:soft1 140:fire 180:soft1 230:fire 330:fire 370:fire 410:fire
```

两者均到达菜单及剧情页，480 帧正常结束。八个采样画面中六个逐像素相同，两个菜单画面的飘叶动画相位不同；剧情页逐像素相同。日志中两版均出现相同的五次资源读取空流 NullPointerException，游戏捕获后继续，未出现未捕获异常或崩溃。这次回放不覆盖完整游戏流程，也不是整机性能基准。

实验日志、原始计时、校验和和画面保存在本机 `.local-tools/debug-runs/opt-20261109*`，原游戏包与用户存档未修改。
