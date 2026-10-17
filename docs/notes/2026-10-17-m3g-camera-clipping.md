# M3G 相机近远裁剪面

## 发现与边界

继续检查原版《极品飞车之无间风云 FX》教程渲染时，发现 Java 渲染路径 `jr_triangle_v` 将近裁剪距离固定为 0.05，并且没有远裁剪面。`jr_project` 只使用投影的 x、y 和 w，因此相机实际 near/far 参数没有参与三角形裁剪。该问题同时影响透视、平行和自定义投影。

这与此前截图中的整块灰色区域需要区分。GDB 捕获该区域来自游戏主动调用 `Graphics.fillRect(0,75,240,151)`，原色 `0xff4b4b47` 经 RGB565 量化为 `(72,72,64)`，精确对应截图范围。Java 栈为 `p.run → v.a → az.d(pc=1939) → az.c(pc=33) → Graphics.fillRect`。原版字节码显示，此路径位于加载阶段的十次预渲染循环；填充之后才绑定目标、设置相机并渲染 World。因此不能由本次相机裁剪修复宣称该灰块已经解决，也不能用它作为裁剪问题的单独证据。

## 可复现的 API 回归

使用 mutable Image 的 Graphics 作为渲染目标，设置 `Camera.setPerspective(90,1,1,10)`。蓝色合法三角形位于 z=-3；灰色三角形顶点为 `(-1,-1,-2)`、`(1,-1,-2)`、`(0,1,1)`，跨越相机近面。旧实现错误覆盖中心区域，按 near=1 裁剪后中心应保持蓝色，灰色仅保留底部梯形。

进一步检查完全位于近面前 z=-0.5 和远面后 z=-20 的面既不写颜色也不写深度；先画越界面不能阻挡后续合法面。相同投影矩阵的 generic 路径和平行投影亦纳入回归。修复前测试结果为 266 checks / 19 failures，日志位于 `.local-tools/debug-runs/nfs-clip-20261017/test-red.log`。

## 实现和验证

`jr_triangle_v` 在齐次空间按 `clipW + clipZ >= 0` 与 `clipW - clipZ >= 0` 依次裁剪，交点通过原有 `jr_blend` 插值相机坐标、法线、纹理坐标及颜色，再重新投影。两块各五个顶点的栈数组容纳所有结果，不增加堆分配、依赖或常驻状态。原有工作区为 8+3 个顶点，新工作区为 5+5 个顶点。

同时补齐 `cam_proj_fill` 的平行投影矩阵：按 height/aspect 缩放 x/y，并用 near/far 生成 z 行。旧实现的单位矩阵会使合法 z=-2 Sprite 被新的正确裁剪规则拒绝；现有 Sprite 回归验证此次矩阵修复。没有改变正交投影的深度比较算法。

前一轮裁剪主体的历史验证（保留原记录；不是本轮重新执行的全量检查）：

| 项目 | 结果 |
| --- | --- |
| M3G API 回归 | 266 checks / 0 failures |
| 全量 CTest | 80/80，176.75 秒 |
| 关闭 M3G 的构建与 Loader 回归 | 构建成功，82 checks / 0 failures |
| 极品飞车 headless | 1200 帧，30.0 fps，324253 个三角形，12406 次 M3G 操作 |
| 极品飞车 SDL2 dummy | 1200 帧，30.0 fps，323436 个三角形，12375 次 M3G 操作 |
| 急速狂飙 headless | 2100 帧，30.0 fps，331413 个三角形，29400 次 M3G 操作 |

实际游戏均使用 8 MiB Java 堆和独立实验 RMS，无未捕获异常；日志中游戏捕获的缺省存档、资源或空指针异常仍如实保留。极品飞车第 1080 帧可见车辆、道路与刹车教学；第 720 帧灰块仍存在。急速狂飙第 2100 帧可见比赛 HUD、车辆、赛道与护栏。SDL2 本轮只验证日志，没有截图。它们是启动与短时玩法回归，不能据此推断全游戏兼容。

CLI text 从 2108950 增至 2109958 字节（+1008），data/bss 保持 155848/6688 字节。30 fps 回放有帧率上限，这些测量只说明达到目标帧率，不能作为无限速性能对比。

日志与截图位于 `.local-tools/debug-runs/nfs-clip-20261017/`，灰块来源调试位于 `.local-tools/debug-runs/nfs-render-20261017/`。原版游戏 JAR 与用户 RMS 不变。

## 相机投影切换与零裁剪面的收尾验证

本轮在独立 `build-camera` 构建中重新执行，接管时实际结果为 `test_m3g` 288 checks / 2 failures，`test_m3g_file` 97 checks / 0 failures；不是前述历史 266 / 19 的旧裁剪实现。两个失败均为平行投影的零 near/far 被 `cam_proj_fill` 错当成透视相机默认值。

`setPerspective` / `setParallel` 现在清除 `_nj_generic`，使同一 Camera 从 generic 切换后真正使用新投影。`cam_proj_fill` 仅在透视模式下把零 near/far 回退到构造默认值，平行模式保留零或负距离；已使用既有 `perspective` 局部变量，消除 unused 警告。齐次 near/far 裁剪主体保持不变。

回归扩展到七种渲染路径：透视、等价 generic、平行、generic→透视、generic→平行、平行 near=0/far=10，以及平行 near=-10/far=0。除矩阵端点外，零/负裁剪面也实际绘制三角形，检查越界面既不写颜色也不写深度，且合法面仍可渲染。最终 `test_m3g` 为 **302 checks / 0 failures**，`test_m3g_file` 为 **97 checks / 0 failures**，定向 CTest **2/2** 通过；本轮未重跑全量 CTest 或关闭 M3G 构建。

本轮原版极品飞车 SHA-256 为 `d2eebdceab913587043c2ab3209596e5dc64eac66a15ffeeceb98e31c40ffd00`。以 headless、240×320、8 MiB 堆、30 fps、新隔离 RMS，使用 `360:fire,540:soft1,720:fire,900:fire,1050:fire` 回放 1200 帧：304032 个三角形、11548 次 M3G 操作、40.059 秒、30.0 fps；退出状态 0，日志无未捕获异常。第 1050 与 1200 帧均可见车辆、道路、城市建筑及“按向下或8键刹车”的教程 1/7，速度由 71 增至 97；验证到教程行驶阶段，没有验证完成全部教程或全游戏。

最新日志、实验 RMS 和截图位于 `.local-tools/debug-runs/nfs-camera-final-20261017/`，重点截图为 `frames/01050.png` 与 `frames/01200.png`。第 750 帧 `frames/00750.png` 仍存在已记录的灰色填充块，因此本轮仍不把灰块归因于裁剪。原版 JAR 和用户存档未更改，所有后台任务已收集。

## 后续边界

本轮不处理灰块保留原因、正交深度插值、半像素采样或其他 Camera API 参数校验。不能用限帧达到 30 fps 作为无限速性能结论。
