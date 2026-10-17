# M3G 顶点颜色：HighSpeed 3D 白色赛道修复

## 证据与根因

在 RMS 初始化修复后，原始 `tests/games/急速狂飙完美破解版_4964.jar`（SHA-256 `c0a384c1565f01407767c4083c56dc939505d97384692b98edd70e34d9597841`）已经进入比赛，但赛道区域仍为白色。此前的 2100 帧回放可见 HUD 计时推进至 0:09，因此不能把 M3G 调用计数当作正确渲染的证明。

本地 FreeJ2ME-Plus（提交 `baca76a4dda6f30abac67fb4a3db009eed05f91c`）使用独立存档、240×320、ISO_8859_1 编码回放后，可见车辆、赛道和场景纹理。

`src/midp/nj_lib_m3g.c` 的公共网格管线存在三个颜色错误：

- `jr_colour` 要求至少四个分量，把合法的三分量 RGB 顶点颜色返回为白色。
- 四分量颜色按 ARGB 读取，实际 VertexArray 顺序为 RGBA。
- 无颜色数组时返回白色，没有使用 `VertexBuffer.setDefaultColor` 指定的默认颜色。光照路径也忽略三分量 RGB，并使用固定白色作为默认值。

修复统一 RGB/RGBA 读取方式，以 alpha=255 补齐 RGB，在无颜色数组时读取 VertexBuffer 默认颜色。保持原有材质选择逻辑；没有游戏名称判断、新依赖、配置项或长期分配。默认颜色在每次绘制的局部结构中增加一个 32 位字段，仍在已有 M3G 模块内。

## 自动与实际游戏回归

`tests/test_m3g.c` 新增真实索引三角形绘制的像素检查：默认颜色、RGB 数组覆盖默认颜色、RGBA 的 RGB 通道顺序。每次绘制清除背景与深度，防止前一次图像或深度缓冲使测试产生假结论。238 checks、0 failures；已有负面用例产生的异常日志属于预期。完整 CTest 为 80/80 通过，174.11 秒。`NJ_ENABLE_M3G=0`、关闭测试/CLI/SDL2 的独立静态库构建通过，保留现有模块裁剪能力。

本轮回放使用新的实验 RMS 目录：

```sh
./build/nj2me --port headless --heap 8 --fps 30 --run-frames 2100 \
  --keys '300:soft2,480:fire,720:fire,960:fire,1080:fire,1200:fire,1320:fire' \
  --stats --where \
  --rms-dir .local-tools/debug-runs/racer-render-20261017/fixed/rms \
  --dump-frames .local-tools/debug-runs/racer-render-20261017/fixed/frames/ \
  --dump-every 300 'tests/games/急速狂飙完美破解版_4964.jar'
```

2100 帧截图显示有颜色和纹理的赛道、车辆、树木与路障，HUD 计时推进到 0:09，没有未捕获异常。观测值：

| 指标 | 结果 |
| --- | --- |
| 堆配置 | 8 MiB |
| 实际时长 / 帧率 | 69.618 秒 / 30.2 fps |
| 末次 Java heap | 5,420,848 / 6,284,016 字节 |
| GC | 10 次 |
| triangles / M3G 计数 | 325,691 / 31,094 |
| 本机构建 text 段 | 2,105,734 字节，比本轮修复前增加 160 字节 |
| data / bss | 155,848 / 6,688 字节，未变化 |

SDL2 dummy 驱动以同样输入、8 MiB 堆、独立 RMS 运行 2100 帧，也无未捕获异常：30.0 fps、324,153 triangles、29,094 次 M3G 计数、10 次 GC。该检查验证 SDL2 运行路径；画面结论来自上述 headless 截图。

日志、截图与参考回放保存在 `.local-tools/debug-runs/racer-render-20261017/`；原始 JAR 和用户存档没有改动。

## 结论边界

确认的是共用颜色语义修复和该游戏比赛场景恢复绘制。参考与当前画面仍有渲染质量差异；本轮没有证明完整通关、所有透明混合模式或光照组合的精确等价。RGBA 像素用例检查 RGB 通道，未据此宣称 alpha 混合已完全兼容。
