# RMS 记录 ID 查找：去掉逐元素的 Java/native 调用

## 问题与实现

J2ME-Loader 的 RMS 使用内存 HashMap 查找记录。本项目也把记录保存在
内存中，但原来的 `rs_find` 对每一个候选 ID 都调用一次
`Vector.elementAt()` 和一次 `Integer.intValue()`；扫描长表时，桥接调用
比整数比较本身昂贵得多。

本次保留线性扫描，对**精确内建 Vector 和 Integer 对象**直接读取
`elementData`、`elementCount`、`value`。所有字段解析在取得数组指针前完成，
扫描期间没有分配或 Java 回调。检查对象种类、精确类身份、引用数组种类、
非负计数、数组长度和数据指针。空 Vector 不访问其数组。

Vector 子类或未知装箱对象仍从索引 0 走原来的虚调用，因此覆盖方法的副作用、
GC 和异常继续生效。找到第一项就返回，不读取后续对象。没有使用二分查找：
旧文件读取器接受乱序和重复 ID，必须保留**首个匹配项**的行为。
大记录 overflow 区恢复时使用同一个查找函数。

没有增加 Java 字段、类、静态字段或缓存，也没有修改磁盘格式、快照 ABI、
记录顺序、记录负载和 GC 引用关系。查找复杂度仍为 O(n)，额外空间为 O(1)。

## 固定工作量测量

基线为 `d7a6cc511d2f7eed0c12a52405b67336f2117aea`。基线与候选使用
同一构建目录的 GCC 16.2.1 Release 编译参数和同一基线静态库；候选仅以新
RMS 对象文件覆盖库中的原实现。基准代码见
[`tools/bench_rms_lookup.c`](../../tools/bench_rms_lookup.c)。

在 CPU 1 上跑一组预热与三组前后交替测量，以下为进程 CPU 时间中位数。
初始化、生成独立测试文件、读取文件和创建 VM 均在计时区外。每次返回长度
均与独立生成的数据核对，复制用例还核对每个字节。所有前后校验和一致，
计时区 GC 次数均为 0。

| 用例 | 查询次数 | 原实现 ms | 新实现 ms | 原/新时间比 |
|---|---:|---:|---:|---:|
| 8 条记录，混合查找长度 | 8,192 | 6.444 | 1.579 | 4.08× |
| 128 条记录，混合查找长度 | 8,192 | 62.639 | 1.967 | 31.84× |
| 1,024 条记录，混合查找长度 | 8,192 | 490.852 | 5.571 | 88.11× |
| 4,096 条记录，混合查找长度 | 4,096 | 997.433 | 10.363 | 96.25× |
| 4,096 条记录，只查第一条 | 8,192 | 3.135 | 1.402 | 2.24× |
| 4,096 条记录，复制到已有数组 | 4,096 | 965.322 | 10.618 | 90.92× |
| 1,024 条记录，稀疏 ID | 8,192 | 516.383 | 5.571 | 92.69× |
| 1,024 条记录，乱序 ID | 8,192 | 483.870 | 5.499 | 87.99× |

4,096 条记录的混合长度用例中，native 调用计数从 16,801,824 降到 8,192，
与去除每个比较的两次桥接调用相符。随机序列、记录数、查询次数和校验过程
前后完全相同。

这些是特定 RMS 内存查找的微基准结果，不代表游戏帧率、创建世界或整段加载
获得同等倍率。Minecraft 的世界文件路径及 LOAD 分段另行采样，不能仅凭
参考模拟器的 RMS 数据结构推断本游戏的主要瓶颈。

## 复现基准

分别构建基线和候选库，保持功能开关、编译器、优化等级一致。使用本次新增的
基准源码，设置 `RMS_BENCH_BUILD` 指向待测 Ninja 构建目录；默认是当前仓库的
`build`。以下脚本复用 `test_game` 的实际编译和链接参数，并把所有输出放在
新临时目录中。CPU 编号应调整为当前机器允许的编号。运行会留下独立的
`/tmp/nj-rms-lookup-*` 测试数据，路径输出在 stderr。

```sh
python3 - <<'PY'
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path.cwd()
build = Path(os.environ.get('RMS_BENCH_BUILD', 'build')).resolve()
out = Path(tempfile.mkdtemp(prefix='nj-rms-bench-'))
commands = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'commands', 'test_game'], text=True).splitlines()
cc = shlex.split(next(c for c in commands
                     if '-c ' in c and '/tests/test_game.c' in c))
cc[cc.index('-c') + 1] = str(root / 'tools/bench_rms_lookup.c')
cc[cc.index('-o') + 1] = str(out / 'bench.o')
for flag in ['-MF', '-MT']:
    if flag in cc:
        i = cc.index(flag)
        del cc[i:i + 2]
cc = [x for x in cc if x != '-MD']
link = shlex.split(commands[-1])
if link[:2] == [':', '&&']:
    link = link[2:-2]
link = [x for x in link if not x.startswith('-Wl,--dependency-file=')]
link[next(i for i, x in enumerate(link)
          if x.endswith('tests/test_game.c.o'))] = str(out / 'bench.o')
link[link.index('-o') + 1] = str(out / 'bench')
for command in [cc, link]:
    subprocess.run(command, cwd=build, check=True)
subprocess.run(['taskset', '-c', '1', str(out / 'bench')], check=True)
PY
```

## 回归验证

新增 `test_rms_lookup` 在旧基线、新实现及 ASan+UBSan 下均为 **787 项、
零失败**。这是优化等价性测试，旧实现应当通过。

- 公共 API：普通、空和删除后不连续的记录；三种读取入口；目标数组边界、
  返回副本独立性；修改、删除、关闭后重开及异常路径。
- 手造 v1 文件：无序、重复及较大 ID；修改首项、删除后暴露第二项；新 VM
  重开后核对原始顺序、字节和 next ID。
- 内部守卫：Vector 子类覆盖 `elementAt`，非 Integer 对象提供 `intValue`，
  回调中的 GC 和异常，以及计数越界时保留旧实现回退。这些是内部形状注入，
  不把所有损坏对象形状列为 MIDP 支持契约。

现有 RMS **1005 项**、实时枚举 **776 项**分别在 normal 和 sanitizer 下
零失败；类库 **1432 项** normal 零失败。新生产对象和新增测试通过
`-Wall -Wextra -Werror`。

独立 Debug/Werror 构建启用 ASan、UBSan、float-cast-overflow 和泄漏检查：
RMS 快照 **70 项**、通用快照 **2521 项**、M3G 快照 **95 项**全部通过。
实际旧版普通/RMS 快照分别 **6/26 项**通过。与基线比较的完整 ABI
**3737 行**逐字相同，覆盖 **296 类、427 静态槽**（含测试的两个 holder 槽）。

原版 Minecraft 在 SDL2 下按 30 fps 运行 1600 帧，使用独立世界副本和
16 MB 堆。已查看地形、手持方块和 HUD；窗口图像与基线 SHA256 完全一致，
没有 uncaught 或无效区块错误。旧快照与新快照各续跑 300 帧并再次保存。
续跑中仍有先前存在、被游戏捕获的 UDP 接收异常，不据此声称修复了网络句柄
快照恢复。原始 JAR 和种子文件哈希未变。

最终正常构建的完整 serial CTest 为 **154/154 通过**，耗时 323.74 秒。
文中的公开基准复现脚本也已直接执行成功。
