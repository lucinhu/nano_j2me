# Nokia 像素接口兼容性修复（第四批）

本轮基于 `417a17a7d39cd2361ec964894f3a184acb1beb03`。重点是修复 Nokia DirectGraphics 的 int/short 像素接口和 DirectUtils 图像语义；性能收益需有单独测量，本轮不以限帧游戏运行时间推算加速比。

## 规范与根因

依据 [Nokia DirectGraphics 文档](https://nikita36078.github.io/J2ME_Docs/docs/Nokia_UI_API_1_1/com/nokia/mid/ui/DirectGraphics.html) 和 [DirectUtils 文档](https://nikita36078.github.io/J2ME_Docs/docs/Nokia_UI_API_1_1/com/nokia/mid/ui/DirectUtils.html)。本地 FreeJ2ME-Plus 的 DirectGraphics 常量也与 SDK 一致。

旧实现把大多数像素格式编号写成连续整数；真实 SDK 使用 `565`、`888`、`8888` 等数值。Java 编译桩与 C 测试重复了同一错误，因此以前的测试通过不能证明真实游戏调用正确。原始 `魂斗罗_5652.jar` 的 `e` 类字节码在四处直接使用 `sipush 8888` 调用 `drawPixels(int[],...)`，旧实现会将其按 RGB565 解析。

修正运行时常量和编译桩，测试及 Nokia Java 演示使用真实 SDK 常量。保留现有内建类、字段、方法的数量及顺序。

## 接口改动

- int 像素支持 `888` / `8888`；short 支持 `444` / `555` / `565` / `4444` / `1555`，分别编码和解码。
- drawPixels 检查 transparency，按逆时针旋转后翻转处理 manipulation，遵守 Graphics 的裁剪与平移；操作不改变当前绘图颜色。
- getPixels 遵守平移、不受 clip 限制，支持负 scanlength，按目标格式返回像素。getNativePixelFormat 根据实际目标表面返回支持的格式。
- 数组索引使用 64 位范围计算，在绘制或写回前校验完整范围，避免整数溢出和部分写入。非法格式、尺寸、manipulation 按规范抛异常。
- DirectUtils.createImage(width,height,argb) 保留原始 ARGB（包含透明度）；encoded overload 解码后返回可变图像，能够继续 getGraphics 绘制；创建过程中保护临时 GC 引用。

## 验证

- 初版新增 DirectGraphics 回归配旧实现：3,783 通过 / 264 失败，UBSan 报告 int/short 两条路径共八处索引、坐标有符号溢出。最终回归补了超大但仅少量可见的矩形等用例，这部分没有对旧代码运行其可能极长的循环。
- 最终 DirectGraphics 独立 ASan+UBSan：4,343 通过 / 0 失败；相邻 LCDUI 3,459/0、当前 rasterizer + stub-font 17,338/0。7 种格式、16 种操作、正负/零/重叠行跨度、无部分写入和三种屏幕格式均有覆盖。
- 更新后的 Nokia Java 演示在旧版上 5 项失败，在整合版上全部通过，涵盖真实 SDK 常量、565 往返、初始 ARGB 和可变 PNG。
- 内建字段、方法、类声明块逐字比较不变：LCDUI 21 块，Nokia 9 块；982 个核心参考成员仍全部可解析。既有 drawImage 的旋转/锚点实现保持原样。

- DirectUtils：普通构建和 ASan+UBSan+泄漏检测均 1,943 通过 / 0 失败；相同测试配旧实现 1,656 通过 / 278 失败，旧版另被 UBSan 捕获 `offset + length` 溢出。旧版不可变图像会跳过后续绘制断言，因此前后总数不同。覆盖全部 256 个 alpha、PNG 的 0/1/127/128/254/255 alpha、实际绘制、创建中 GC、根表定点 OOM、非法维度和超大尺寸。
- 更新常量生成资产后，完整 CTest 142 项为零失败，74.28 秒。其中 tempo 测试因并发生成桩文件失败而跳过，随后单独运行通过（0.76 秒）。首次全量曾因旧常量对照文件失败；重新运行 `make_const_jar.sh` 后相关 8 组回归通过，再执行了上述完整套件。包含三个 FreeJ2ME 参考画面回归、SDL2 客户端及快照测试。

### 原始游戏回放

原始 `tests/games/魂斗罗_5652.jar` SHA-256 为 `a3f5b603d43166a47cc373e28a336ec4b5793376920e13232ec5c19ac0a01380`。前后各跑两条 600 帧回放，均为 headless、240×320、16 MiB Java heap、`--fps 30`，RMS/FS 使用相互隔离的临时目录，`--dump-every 100`。

```text
教程：100:soft1 180:fire 260:soft1 340:fire 420:fire 480:right:down 540:right:up 560:fire
商店：100:soft1 180:fire 260:soft1 340:soft2 400:soft1 450:fire 480:soft2 500:5 520:right:down 580:right:up
```

两条路径各 6 张样本均逐像素一致，肉眼检查了教程覆盖层和商店菜单。前后均有同一条由游戏捕获的 `NullPointerException at c.C()V pc=17`，无未捕获异常，均正常完成预算并清理。游戏有忙循环，实际约 42 秒/600 帧；这只能证明这些场景没有新增回归，不能证明 DirectGraphics 分支一定在这些帧执行，也不能作为性能基准。

### 复现命令

需要已安装 JDK 和项目依赖，并已生成其余测试资产：

```sh
cmake -S . -B build -G Ninja
./tools/make_nokiademo_jar.sh
./tools/make_const_jar.sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -j4
```

两个直接 C 回归是 `build/test_directgraphics`、`build/test_directutils`；Java 检查可通过 `NJ_BIN="$PWD/build/nj2me" ./tools/check_nokiademo.sh` 单独执行。ASan+UBSan 独立验证使用 GCC `-g -fsanitize=address,undefined -fno-omit-frame-pointer`（DirectGraphics `-O2`，DirectUtils `-O1 -fno-sanitize-recover=all`），检测包括实际创建中 GC、宿主 root table 定点分配失败和非法数组范围。若并发生成桩导致 tempo 测试跳过，单独执行 `ctest --test-dir build --output-on-failure -R '^check_tempo_control$'`。

## 本轮边界

byte 打包灰度、vertical 位排列与 transparencyMask 尚需后续修复。setARGBColor、三角形/多边形颜色和偶奇填充规则的既有问题另行处理。MIDP Image 的无效编码异常类型与规范存在差异；本轮仅在 DirectUtils 边界规范化为 IllegalArgumentException，保留 OutOfMemoryError 等错误。

本轮还保存了共享运行时的待办：当 Java heap 完全被活对象占满时，新异常对象本身也可能分配失败，导致回退异常类型与预期不同。这是共享异常分配路径的问题，本轮没有扩大修改范围。

getPixels 对传入负 x/y 按文档抛 IllegalArgumentException。文档未明确平移后的区域超出实际表面的行为；本实现选择在写目标数组前抛 IllegalArgumentException。没有宣称全游戏通关或全面厂商 API 兼容。
