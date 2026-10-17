# M3G Loader：极品飞车启动根对象修复

## 证据与根因

原始 `tests/games/极品飞车之无间风云FX中文版_4518.jar`，SHA-256 `d2eebdceab913587043c2ab3209596e5dc64eac66a15ffeeceb98e31c40ffd00`，在 240×320、headless、30 fps 的独立存档启动中触发：

```text
thrown: java/lang/ClassCastException at bc.a(II)Ljava/lang/Object; pc=264: javax/microedition/m3g/Node
uncaught exception in thread 2: java.lang.ClassCastException: javax/microedition/m3g/Node @ bc.a+263
```

`bc.a(II)` 在字节码 248 调用资源加载，260 读取返回数组第零项，261 执行 `checkcast Node`；异常发生在 `duplicate()` 之前。异常消息中的 Node 是目标类型。

对原始 JAR 中三份资源直接执行加载，对照本地 FreeJ2ME-Plus `baca76a4dda6f30abac67fb4a3db009eed05f91c`：

| 资源 | 修复前 | 修复后及参考实现 |
| --- | --- | --- |
| `/f` | 16 个根，首项 KeyframeSequence | 唯一 Group |
| `/g` | 34 个根，首项 KeyframeSequence | 唯一 Group |
| `/bl` | VertexBuffer、Group | 唯一 Group |

根因有三项：公共 Object3D 头被假设为固定 12 字节，遗漏动画列表与用户参数造成的变长部分；AnimationTrack 的 KeyframeSequence/AnimationController 引用未解析；Mesh 已解析的 VertexBuffer 引用没有参与根标记。被引用对象因而误留在返回数组中。

## 实现与资源开销

解析公共头时校验轨道数量、参数数量和参数字节长度，按实际头长定位类型载荷。动画轨道引用独立于各类型引用槽保存，仅有轨道的对象分配列表；扫描失败及正常结束均释放。AnimationTrack 的两个依赖、溢出引用和 Mesh 的 VertexBuffer 一并参与根标记。

不依赖游戏名称或将 Node 排到数组首位。没有新增依赖、配置项或常驻动画表；扫描结构增加一个指针和一个计数，仅在加载期间使用。当前 64 位构建中每个扫描对象增加 16 字节，现有 4096 项临时表因此增加 64 KiB，轨道索引另按实际数量分配。CLI text 从 2105734 增至 2108950 字节（+3216）；data/bss 保持 155848/6688 字节。现有 M3G 功能开关继续可用。

## 验证

合成 Loader 用例在旧实现得到 86 checks / 1 failure；加入修复及边界用例后为 97 checks / 0 failures。测试覆盖唯一 Group、Mesh 子对象与 VertexBuffer 链接、带动画和用户参数的变长公共头、畸形长度，以及 17 个子对象超出内联引用槽时的根过滤。五项 M3G CTest 回归全部通过；ASan、UBSan 与泄漏检查通过（97/0），测试自身的 headless 端口补齐 shutdown。关闭 M3G 的构建回归为 82 checks / 0 failures，静态库构建通过。完整 CTest 为 80/80 通过，耗时 180.02 秒。

原版游戏 headless 回放 1200 帧，30.0 fps，324757 个三角形、12448 次 M3G 操作，进入教程并推进至刹车教学；无未捕获异常。SDL2 dummy 视频/音频端口同流程也运行 1200 帧、30.0 fps，进入 `tutorial.0`，无未捕获异常。SDL2 此轮以日志验证，没有屏幕截图。启动中仍有被游戏捕获的 RMS 缺省记录和空指针异常，未通过吞异常改变控制流。

headless 第 720 帧有大块遮挡，第 1080 帧可见车辆、街道和刹车提示；渲染质量问题仍保留。验证结果仅覆盖启动和教程推进。

实际游戏使用原版 JAR、8 MiB Java 堆、独立实验 RMS：

```sh
./build/nj2me --port headless --heap 8 --fps 30 --run-frames 1200 \
  --keys '360:fire,540:soft1,720:fire,900:fire,1050:fire' --stats \
  --rms-dir .local-tools/debug-runs/nfs-root-20261017/fixed2/rms \
  --dump-frames .local-tools/debug-runs/nfs-root-20261017/fixed2/frames/ \
  --dump-every 180 'tests/games/极品飞车之无间风云FX中文版_4518.jar'
```

本地参考回放使用相同尺寸、独立 RMS，12 秒后 fire、再等 6 秒 soft1、再等 6 秒 fire，已进入车辆与赛道教程。它为场景阶段对照，不是严格帧级比较。

详细日志、原始资源分析和截图在 `.local-tools/debug-runs/nfs-cast-20261017/` 与 `.local-tools/debug-runs/nfs-root-20261017/`。`fixed/` 第一次实际回放启动时 CLI 尚未重新链接，因此它仍复现旧异常；正式修复验证使用确认重新链接后的 `fixed2/`。原始 JAR 与用户存档保持不变。

## 结论边界

本轮目标是恢复 Loader 根对象语义及游戏启动控制流。动画播放、duplicate 的完整图复制和渲染质量仍需实际场景逐步验证，不能由根数组正确推断全游戏兼容。
