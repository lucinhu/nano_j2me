# ImageItem 的 null alternate text

MIDP 的 [ImageItem API](https://nikita36078.github.io/J2ME_Docs/docs/midp-2.0/javax/microedition/lcdui/ImageItem.html) 明确允许 alternate text 为 `null`。四参数构造器、五参数构造器及 `setAltText(null)` 都应接受它；`getAltText()` 在没有替代文本时返回 `null`。空字符串仍是一个存在的字符串，不等于 `null`。

原实现会在这三个入口抛出 `NullPointerException: altText is null`。这也会让五参数构造器在 alternate text 为 null、appearance mode 非法时先抛 NPE，掩盖应有的 `IllegalArgumentException`。

[修复](../../src/midp/nj_lib_lcdui.c) 只删除两处构造器和一处 setter 的 null 拒绝分支，共三块、十一行；已有字段写入和 getter 本来就能保存、返回 null。layout 及显式 appearance mode 的有效性检查继续执行。没有新增 builtin、方法行或字段，builtin 表完整后缀逐字节保持一致。

[回归测试](../../tests/test_imageitem_alttext.c) 在旧库执行前冻结为同一份源码；两个构建配置各自的同一个测试对象同时链接旧库和新库：

| 配置 | 旧库 | 新库 |
| --- | --- | --- |
| normal，原 Release 参数、`-O3 -DNDEBUG` | 348 checks，16 failed，exit 1 | 778 checks，0 failed |
| strict，ASan/UBSan/float-cast-overflow、LSAN | 348 checks，16 failed，exit 1 | 778 checks，0 failed |

旧行为的十六个失败分别是四个 null 构造用例、八个 null setter 用例，以及四个 null text 搭配非法 appearance mode 的异常类型用例。旧构造器失败后会跳过该对象后续的 getter/GC 检查，因此新旧执行的 checks 总数不同。非 null、非法 layout 和非 null 搭配非法 appearance 的对照在旧库也通过。

新测试覆盖：

- 两种构造器的 null/non-null 参数；五参数构造器的三种合法 appearance mode。
- getter、非 null setter、清空、重复清空、空字符串与 null 的区别，以及清空后的再次赋值。
- 只有 ImageItem 自身保留 host root 后真实执行 GC，再检查 label、immutable image 和 alternate text 的可达性与内容。
- 两种构造器及 setter 的非法 layout，五参数构造器的非法 appearance；异常必须是 IAE，非法 `setLayout` 不改变已有布局。

normal、strict 都额外执行了受影响的既有路径：

| 路径 | 两种配置各自的结果 |
| --- | --- |
| [LCDUI](../../tests/test_lcdui.c) | 8826 passed / 0 failed，三次创建/运行/销毁及一致清理 |
| [Form 滚动](../../tests/test_form_scroll.c) | overflow pixels、换行文本、chrome、pointer focus、Gauge 及三次一致清理通过 |
| [Form snapshot](../../tests/test_save_form_scroll.c)，默认 | 752 checks，实际 GC、listener、paint、key、pointer 及 roundtrip 通过 |
| 同一 snapshot fixture，`--old-2a` | 585 checks，旧 Screen 格式在安装前被拒绝，同 VM 随后可正常新启动；不是旧存档恢复成功 |

strict 使用 `-Wall -Wextra -Werror -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer`，执行时启用 LSAN 和 halt-on-error；未发现 sanitizer 错误。旧 strict 库已有完整 TU 的相同严格参数记录，本次只替换 LCDUI TU。

验证在新的私有 checkpoint 进行。normal 原库含 76 个成员（包括两个 SDL 成员），strict 含 74 个成员；新库都以原名 `nj_lib_lcdui.c.o` 替换且保留完整成员清单和顺序，normal 其余 75 个、strict 其余 73 个成员逐字节相同。原库、原始游戏 JAR、原存档和主 build 保持原身份。记录保留了一个私有成员计数校验失败，以及 LCDUI 首次缺相对 fixture 路径、进入断言前 exit 2 的设置失败；后者使用官方目录参数和字节一致的 fixture 副本恢复，已成功项没有重复运行。

测试源码 SHA256 为 `a22e1bfabac93d99815874011ca3bdc50626232625a11033882e73e6a65897a2`，修复后的 LCDUI 源码 SHA256 为 `143b4fe1dd2268c849d9dae11c02a26c17cd8831e7373dcffbe238bf7e9ce145`。实际逐条 argv、日志、产物 SHA 和逐成员身份保存在本次私有证据报告中。

本轮没有重跑无关完整套件，也没有重新测量或重标之前 5bb6/1733/诊断版本的 JBenchmark 分数。mutable Image 的 snapshot、四参数构造器默认 appearance 的既有策略、绘制布局和设备 capabilities 均不在本次修复范围；测试使用 immutable image，避免混入这些独立语义。
