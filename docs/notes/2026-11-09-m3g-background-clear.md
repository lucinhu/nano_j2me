# M3G 默认背景清除与 World/Node 绘制边界

基线：`efa71c4bbcef2e6e3a8d56675ee500b21f303a92`。本批补充 Minecraft 透明渲染排查中发现的独立接口缺陷；没有新增游戏专用分支，也没有修改原游戏或用户世界。

## 问题与修复

按 [JSR184 Graphics3D 规范](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Graphics3D.html)：

- `clear(null)` 等价于默认 Background，应清除当前视口内的颜色与深度。旧实现直接返回，保留了旧画面和旧遮挡；现在与默认 Background 共用清除逻辑。
- `clear` 未绑定绘制目标时应抛 `IllegalStateException`。旧版 null 和非 null 背景都可能无异常返回，现在先检查绑定状态。
- `render(World)` 隐式清除其背景，包括背景为 null 的情况。旧版仅在 World 有非 null 背景时清除；现在空 World 也能正确清除。
- `render(Node, Transform)` 把 World 当作普通 Group，不应隐式清除。旧版按对象类型判断，可能擦除前面立即模式绘制的颜色和深度；现在按实际调用的重载区分。

沿用上一批绑定周期内深度有效性的实现，显式清除仍仅影响视口范围。World 清除调用若失败，向调用方传播原状态。没有更改深度比较、深度偏移、三角形顺序、纹理缓存或混合公式。

## 验证

`tests/test_m3g_depth.c` 增加 15 项断言：默认清除颜色与深度、与默认 Background 一致、视口外颜色/深度保留、不可见视口、World 两种背景与两种 render 重载、未绑定目标的异常。

- 相同测试配旧实现：84 项中 11 项失败；修复后 84 项全部通过。
- ASan+UBSan 与泄漏检测：`test_m3g_depth`、`test_m3g`、`test_m3g_targets`、`test_save` 四组全部通过。
- 本次生产文件、测试文件及 `NJ_ENABLE_M3G=0` 测试分支均通过 `-Werror` 编译。
- 主构建及串行完整 CTest：147/147 通过，298.46 秒。包含 FreeJ2ME 帧对照、SDL2 客户端与存档回归；已有可选功能按构建配置执行。
- 核心 API 审计：982 个参考成员全部可解析。M3G 的 59 个 builtin 字段、方法、类表块与基线逐字一致，没有改变相关存档布局。

常规复现：

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -R '^(test_m3g_depth|test_m3g|test_m3g_targets|test_save)$'
ctest --test-dir build --output-on-failure -j1
```

独立 sanitizer 构建可使用 `-DNJ_SANITIZE=ON -DNJ_WERROR=OFF`，配合 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` 运行上述四组测试。本次还单独对改动文件执行严格告警编译；关闭全项目 Werror 是为了避开已有其他文件的编译器告警。

## 原游戏回归和边界

原始 `MinecraftJ2ME_PE_0.6.1_V2.2.jar`，240×320、16 MiB、30 fps，使用复制的完整测试世界文件系统与 RMS，在 X11 SDL2 后端运行 1,200 帧。通过原菜单加载世界，实际窗口截图确认地形、HUD 和操作覆盖层正常；40.245 秒退出并保存，没有未捕获异常或损坏区块读取。日志中 Android/DatagramSocket 的 ClassNotFound 是游戏已有的可选平台探测。回放触摸为 `150:pressed@120,125 154:released@120,125 270:pressed@120,155 274:released@120,155`。

这是已有世界的兼容性回归，受 30 fps 节拍限制，不是速度基准，也不能证明所有透明物体闪烁已解决。上一批已证明并修复跨宿主帧误清深度造成的水面穿透；本批默认背景缺陷由独立接口测试证明，不把它声称为 Minecraft 云层闪烁根因。

并行只读排查确认，Minecraft 使用 BASE_LEVEL/NEAREST，暂未发现 mipmap 或 Image2D 更新缓存错误。另发现顶点/defaultColor alpha 丢失，以及 CompositingMode 深度偏移未参与光栅化；二者有独立像素复现，将分批处理。World-as-Node 的其他相机/灯光语义、Image2D 绘制目标及目标 alpha 支持也不属于本次清除修复。所有原始日志、图像和实验世界均保留于本地实验目录，不纳入提交。
