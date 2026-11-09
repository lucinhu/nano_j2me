# 字符复制提速、RMS 生命周期与 game API 修复

本轮基线为 `05c889029b478a110d4de7d5c2b24b8bc71f4d7d`。重点是减少每帧字符复制的分配、修复 RMS 的 GC 生命周期及边界错误、纠正 Sprite 帧序列和空裁剪行为。内建类的方法、字段及绑定表不变，不引入快照布局变更。

## 字符复制性能

[字符串实现](../src/core/nj_lib_string.c)中的 `StringBuffer.getChars` 直接复制请求的 UTF-16 区间，去掉 `toString()` 的整段复制、临时 Java String 和额外虚拟调用；StringBuilder 的转发路径同样受益。`String.getChars` 和新的直接复制路径用减法检查目标区间，避免 `Integer.MAX_VALUE` 参数引起有符号溢出。

[固定工作量基准](../tools/bench_string.c)每个用例调用 100000 次，预热 256 次后先 GC，再测量耗时与 Java 对象分配数，并校验完整目标数组。Intel Core Ultra 9 275HX，固定 CPU 23，GCC 16.2.1，`-std=c99 -O3 -DNDEBUG`，预热后交替运行九对，中位 ns/调用如下：

| 用例 | 修改前 | 修改后 | 加速 |
|---|---:|---:|---:|
| StringBuffer 长度 32，复制 16 | 237.600 | 41.108 | 5.78× |
| StringBuffer 长度 4096，复制 16 | 772.944 | 41.529 | 18.61× |
| StringBuffer 长度 4096，复制 4096 | 924.425 | 173.230 | 5.34× |
| StringBuffer 长度 4096，复制 0 | 773.884 | 38.914 | 19.89× |
| StringBuilder 长度 32，复制 16 | 305.962 | 110.011 | 2.78× |

每个用例的 Java 对象分配从 100000 个降至 0；九对的校验和全部一致。这是接口微基准，不能据此声称整个游戏提升同样倍数。

POSIX 环境复现当前版本：

```sh
cmake --build build -j4
mkdir -p .local-tools/debug-runs/string-bench
cc -std=c99 -O3 -DNDEBUG -Iinclude -Isrc/core tools/bench_string.c \
  build/libnj2me.a -lm -lz -lcurl \
  -o .local-tools/debug-runs/string-bench/after
.local-tools/debug-runs/string-bench/after
```

对照时用同一运行库，只替换字符串实现的目标文件，使两版输入、编译选项和调用次数一致：

```sh
git show 05c889029b478a110d4de7d5c2b24b8bc71f4d7d:src/core/nj_lib_string.c \
  > .local-tools/debug-runs/string-bench/nj_lib_string-before.c
cc -std=c99 -O3 -DNDEBUG -Iinclude -Isrc/core -c \
  .local-tools/debug-runs/string-bench/nj_lib_string-before.c \
  -o .local-tools/debug-runs/string-bench/before.o
cc -std=c99 -O3 -DNDEBUG -Iinclude -Isrc/core tools/bench_string.c \
  .local-tools/debug-runs/string-bench/before.o build/libnj2me.a -lm -lz -lcurl \
  -o .local-tools/debug-runs/string-bench/before
```

固定同一个 CPU，预热后交替运行 before/after，先检查校验和与分配数再比较中位时间。绝对耗时取决于编译器、CPU、缓存与电源策略。

## RMS 稳定性与接口行为

[RecordStore 实现](../src/midp/nj_lib_rms.c)修复：

- open、add、load 中尚未挂到对象上的 Vector、记录副本和 Integer 在后续分配或 Java 调用期间显式保活。
- 枚举的 ID 数组在 filter/comparator 回调及结果对象分配期间保活；比较的第一个 byte[] 在读取第二个记录时保活。所有成功、失败路径释放临时根。
- 新增的根注册检查根数量确实增长。宿主根表分配失败时，在下一次分配前退出；load 安全中止并保留完整记录前缀。
- 先抓取 ID 快照，再执行 filter，防止 filter 追加记录导致固定长度数组越界。
- 关闭后的查询、读取、setMode 和枚举取记录检查 store 状态；关闭后的枚举 hasNext/hasPrevious 返回 false。
- destroy 清除枚举持有的记录 ID 和 store，后续使用抛 IllegalStateException。
- add/set/getInto 的范围检查改用减法，避免带符号加法溢出，保留 null 空记录行为。

关闭与销毁语义参照 [MIDP RecordEnumeration 规范](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/rms/RecordEnumeration.html)。本轮没有实现 keepUpdated 的变更订阅、rebuild 的重新过滤排序、RecordListener、真实版本计数和同名打开对象共享；这些仍是已有兼容性缺口。

## Sprite 与裁剪

[game 实现](../src/midp/nj_lib_game.c)修复：

- `Sprite.setImage` 按自定义帧序列长度检查当前序列索引。重复序列长于原始帧数时，合法索引不再被错误重置。
- Sprite/TiledLayer 绘制始终应用 Graphics 裁剪，空裁剪不再恢复为整图。
- LayerManager 始终将原裁剪与视窗求交，保持空裁剪和平移语义。

行为依据：[Sprite.setImage](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/game/Sprite.html#setImage(javax.microedition.lcdui.Image,%20int,%20int))、[LayerManager.paint](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/game/LayerManager.html#paint(javax.microedition.lcdui.Graphics,%20int,%20int))。

## 验证结果

默认 Release 主构建成功；完整 CTest **141/141** 通过（74.28 秒），包含已有 FreeJ2ME 画面对照、SDL2 和快照回归。最终 RMS 故障注入用例冻结后，重建并重跑字符串、game、RMS、绘图、裁剪、快照、类库、IO、集合九组：**9/9** 通过。API 审计仍是 982 个参考成员全部解析；这不表示所有接口行为均完整。

- [字符串回归](../tests/test_string.c)：最终 ASan+UBSan 为 701 项检查、零失败，覆盖 String/StringBuffer/StringBuilder 的所有小区间、目标哨兵、NUL、中文、代理项及极值参数。Buffer/Builder 复制无新增 Java 对象。旧实现复现不必要分配和带符号溢出。
- [RMS 回归](../tests/test_rms.c)：最终 ASan+UBSan 为 822 通过、零失败。GC 在 load/add/filter/comparator 中触发；另有 9 个根表扩容失败注入点（open 3、load 2、add 2、枚举 2），检查退出状态、根数恢复和数据完整性。旧实现复现范围溢出、关闭/销毁错误、GC 失效和回调追加越界；仅缺少 checked-root 的中间版本在同一最终测试下有 11 个失败。
- [game 回归](../tests/test_game.c)：Release 和 ASan+UBSan 均为 2352 条断言、零失败；同一新测试在旧实现为 90 个失败。包含帧序列换图、绘制像素、nextFrame 回绕，以及 48 个空/负尺寸/无交集/单像素/平移/恢复裁剪场景。三轮 VM 各释放 985 个宿主内存块。
- 独立 sanitizer 邻测：类库 1432/0、IO 400/0、快照、ZIP、绘图 4920/0、裁剪 15/0 均通过，无 ASan/UBSan 或泄漏诊断。sanitizer 构建沿用 `NJ_WERROR=OFF`，避开已有 socket 的 GCC 16 警告；主 Release 配置未改。

实际游戏使用原始《合金弹头4（完美汉化版）》JAR，两版分别使用新的隔离 RMS/FS。240×320、16 MiB、30 FPS、1200 帧，每 120 帧采样，按键相同：

```text
100:soft1 160:fire 220:soft1 280:fire 340:fire 400:fire
450:right:down 500:right:up 510:fire 550:fire 700:fire
800:fire 900:fire 1000:fire 1050:right:down 1150:right:up
```

两版均进入第一关，10 个采样画面全部逐像素相同；均为 978 次 paint、189228 次 gfx、9 次 GC。日志中两版各有两次新建存档前的 RecordStoreNotFoundException，游戏捕获后继续，无未捕获异常或崩溃。新版本 native 调用从 408081 降至 402929。回放固定 30 FPS，不用其总耗时推算吞吐提升，且不覆盖完整通关。

原始计时、校验和、源文件快照、故障复现和画面保留在本机 `.local-tools/debug-runs/opt-round2-{string,rms,gfx}/`。实验文件、游戏、用户存档和编译产物均不纳入提交。
