# Linux 高分屏、图像解码与解释器回归修复（第六批）

基线：`7542864171d576e8701e30541a9311590b2eb70e`。本批优先处理 Linux 高分屏界面过小的问题，同时完成前一批已确认的图像解码错误和失效的解释器回归脚本。

## Linux DPI 与界面缩放

- SDL2 启动器与游戏窗口启用 `SDL_WINDOW_ALLOW_HIGHDPI`。启动器保留逻辑画布，按实际渲染缓冲区放大；鼠标从窗口坐标转换到逻辑画布一次。游戏窗口以渲染缓冲区布局，鼠标先转换到该坐标系，再按游戏画面的整数倍率转换成 Java 坐标。
- 新设置「界面缩放」提供自动及 100/125/150/175/200/250/300%。配置 `ui_scale = 0` 为自动，100..300 为百分比；旧配置默认自动。设置页立即保存，恢复默认回到自动，游戏的 `game_scale` 保持独立。
- 优先级为 `NJ2ME_UI_SCALE` 因子（如 `1.5`）> 保存的百分比 > 桌面自动识别。自动依次使用 GDK 缩放、Qt 缩放、X11 `Xft.dpi`、SDL 显示 DPI，并以渲染缓冲区/窗口比例为下限。物理界面倍率除以该比例得到窗口倍率，避免 Wayland 已放大缓冲区时再重复放大。
- X11 DPI 查询动态加载现有 X11 库并使用 SDL 连接，不新增强制链接依赖。窗口保存逻辑尺寸，初始窗口受可用桌面区域限制。
- 使用方法：`NJ2ME_UI_SCALE=1.5 build/nj2me`，或在设置页修改「界面缩放」。

## Image 解码

按 [MIDP Image 规范](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/Image.html) 区分字节数组与流入口：非法字节编码报告 `IllegalArgumentException`，非法流编码报告 `IOException`。已有流读取异常及真正的分配失败保留，不能被统一改写成格式异常。

- 字节切片采用减法边界检查，修复 `offset + length` 和四参数扩展默认长度的有符号溢出。保留四参数兼容入口中 `length == 0` 表示剩余字节的旧行为。
- `img_alloc` 中检查两次临时根登记，避免宿主根表扩容失败后继续使用未保护的引用。流缓冲区初建及增长也检查临时根登记结果。
- 解码器请求使用局部分配适配器记录真实宿主内存失败，不依靠错误字符串猜测 OOM。图像解码成功后如已有 Java 异常，先释放解码结果并保留异常。
- 不修改内置类、方法、字段表的数量或顺序。

## 恢复解释器真实回归

`check_threaded_dispatch.sh` 的手工源码列表缺少 CLI 现在需要的三个 client 文件，导致链接失败被宽泛地当作不支持 computed goto 而跳过；两个配置探针此前仅编译而没有执行。

现在执行配置探针，独立检查编译器语言扩展，再构建并实际运行 switch 和 threaded 两个解释器。项目编译、链接、运行错误必须失败；仅缺编译器、缺 fixture、编译器确实不支持 computed goto 时跳过。两个解释器都必须完成 60 帧、执行相同的正数字节码数，且不能出现未捕获异常或 `VerifyError`。

独立验证两者均为 60 帧 / 1,042 条字节码，交替各重复三次结果一致。真实缺符号注入使两个构建路径均返回 FAIL；配置探针的链接失败和“编译成功但默认值错误”也返回 FAIL。共 8 类正反控制。该 fixture 极短，9.23–12.25 ms 进程时间、8–9 ms 运行时间受噪声影响，不作为性能提升数据；未声称覆盖长时运行、多线程或所有字节码。

## 验证

- Image 新测试配旧实现为 213 通过 / 15 失败，并捕获两处 UBSan 整数溢出；修复后 Release、ASan+UBSan 均为 228 通过 / 0 失败。覆盖 3×2 RGBA 的 0/1/127/128/254/255 alpha、切片、数组极值、流扩容、异常身份、不关闭调用者的流，以及宿主/根表/Java 堆分配失败和创建期间实际 GC。
- 邻近 DirectUtils 在 Release 和 ASan+UBSan 下均为 1,943 通过 / 0 失败。两处修改对象独立通过 `-Werror`；LCDUI 的 49 组 builtin 表声明与基线逐字节相同，核心 API 审计仍为 982 项全部可解析。
- 客户端配置 133 项、设置页 557 项独立测试全部通过。

- DPI：dummy、真实 X11、真实 Wayland 后端各 194 检查 / 0 失败；独立 Release `-Werror` 构建通过。真实 renderer 截图中，启动器 100%=720×540、150%=1080×810、200%=1440×1080，设置页即时切换 125%=900×675；鼠标和归一化触摸命中同一按钮。游戏 overlay 的按钮、字形、速度 badge 随倍率变化，Java 分辨率和整数逆映射保持一致。
- 修正 SDL2-compat 软件 renderer 在 resize 后报告旧 drawable 尺寸的行为：取 DPI 前刷新 backbuffer，所有调用均发生于完整重绘之前。移动/resize 和 drawable 变化会重新同步；IME 输入矩形转换回窗口坐标。小窗口 300% 时整体收缩 overlay 保持键可达。明确指定 101% 或环境变量 1.02 不被自动 DPI 的近似归整覆盖。
- 独立只读审查另有 667 检查 / 0 失败，生产文件哈希在编译及运行前后相同。同一 360×240 窗口的受控 ratio=1/2 观测中，自动 Up 键 21/42 个渲染像素对应同样的 21 个窗口单位；逐键边界、点击、Java 中心坐标和 IME 均通过。**本机真实 X11/Wayland drawable/window 比都是 1；ratio=2 是受控探针，原生 2× compositor 仍待实机验证。**
- 原版 `3D版田园物语_6744.jar` 在修复前后各运行 480 帧（240×320、16 MiB、30 fps、隔离 RMS/文件系统），启动、菜单和故事场景可达。8 个截图采样中 6 个完全一致，240/300 帧菜单叶子动画相位不同；480 帧故事画面一致并人工查看。两边都有相同 5 条被游戏捕获的空流 NPE，无未捕获异常。限速回放不作为 FPS 提升证据。

主构建通过；完整串行 CTest **146/146 通过，310.75 秒**，包括新 Image、DPI、客户端配置和 UI 测试、SDL client/port/savestate 回归及三组 FreeJ2ME 参考帧。恢复后的 `check_threaded_dispatch` 本次实际编译运行，耗时 23.60 秒，不再因缺符号链接而跳过。未启用的可选 engine 等子检查仍服从原有配置跳过策略。所有临时日志、截图和独立构建位于 `.local-tools/debug-runs/opt-round6-*`，不提交构建产物、游戏或用户存档。

常规回归命令：

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -j1
# 仅运行新的真实 SDL dummy 事件测试：
ctest --test-dir build -R '^test_sdl2_dpi$' --output-on-failure
# 已有 X11/Wayland 显示时验证各实际后端：
NJ_DPI_TEST_VIDEO=x11 build/test_sdl2_dpi
NJ_DPI_TEST_VIDEO=wayland build/test_sdl2_dpi
```

串行执行完整 CTest，避免多个历史 Java fixture 脚本同时重写同一 stub 目录。Image 隔离 sanitizer 构建使用 `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DNJ_SANITIZE=ON -DNJ_WERROR=OFF`，目标为 `test_image_decode test_directutils`；`NJ_WERROR=OFF` 仅绕过既有 GCC 16 socket 警告，修改对象另行通过严格编译。DPI 测试使用自己创建的窗口和临时配置，不改用户配置；截图可由 `NJ_DPI_TEST_SHOTS` 指定一个已存在目录保存实际 renderer 输出。

边界：本批不扩展 `createImage(String)` 的独立 resource-name 解码路径；第三方 codec 若绕过请求分配器，仍须自行报告 Java OOM。流读取的逐字节实现和保留旧增长缓冲区行为未在本批优化。
