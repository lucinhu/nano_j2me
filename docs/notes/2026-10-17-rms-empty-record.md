# RMS 空记录返回值：HighSpeed 3D 启动修复

## 复现与根因

原始游戏：`tests/games/急速狂飙完美破解版_4964.jar`，MIDlet 显示 HighSpeed 3D 1.6.0。

SHA-256：`c0a384c1565f01407767c4083c56dc939505d97384692b98edd70e34d9597841`。

240×320、headless、30 fps、新建独立 RMS 目录运行 360 帧，旧实现停留在灰色加载画面，出现 `uncaught exception in thread 4: java.lang.ArrayIndexOutOfBoundsException @ j.b+463`，另一个游戏线程也出现同类异常。本地 FreeJ2ME-Plus 对照（提交 `baca76a4dda6f30abac67fb4a3db009eed05f91c`，240×320，独立存档）能显示 Sound on? 提示。

反编译原始游戏可见：`l.a(byte[], int)` 用 `addRecord(null, 0, 0)` 补齐记录 ID；`j.b` 读取记录 1，通过返回值是否为 null 决定是否写入默认值。旧 `getRecord(int)` 为已有空记录返回 `byte[0]`，使游戏跳过初始化，然后访问数组第 16 个字节并抛出异常。

[MIDP 2.0 RecordStore 规范](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/rms/RecordStore.html) 明确规定没有数据的记录返回 null。

## 修改与自动回归

`src/midp/nj_lib_rms.c` 的 `rs_getRecord` 在完成打开状态和记录 ID 校验后，对空记录直接返回 null；非空记录仍返回独立数组副本。没有新增依赖、持久状态、配置项或游戏专用判断，仍属于已有 RMS 功能模块。

`tests/test_lib.c` 覆盖：null 数据创建空记录、空数组创建空记录、非空记录清空后读取、记录大小为零、缓冲区重载返回零且不改动缓冲区、非空记录副本语义。

- 构建成功；`test_lib`：578 passed, 0 failed。
- 完整 CTest：80/80，通过，144.38 秒。
- `git diff --check` 通过。
- 本地同一构建配置的 text 段从修复前 2,105,638 字节降至 2,105,574 字节；data 155,848、bss 6,688 字节保持相同。此为本机构建结果，不代表所有工具链。
- 空记录读取省去一次零长度 Java 数组分配。

## 实际游戏验证

所有实验文件位于 `.local-tools/debug-runs/racer-20261017/`，不修改原始 JAR 或用户存档。

| 实验 | 条件与结果 |
| --- | --- |
| `fixed/` | 新建 RMS，360 帧；恢复 Sound on? 提示，没有原来的未捕获数组越界。 |
| `play/` | 新建 RMS，8 MiB 配置，900 帧；按键推进至 QUICK RACE 菜单，30.2 fps，末次报告 heap 2,382,896/3,781,072 字节、4 次 GC。 |
| `reload/` | SDL2 dummy 视频/音频，8 MiB，复用 `play/rms`，360 帧；完成存档重载，无未捕获异常。dummy 测试不证明真实窗口或音频输出质量。 |
| `race/` | 新建 RMS，8 MiB，1500 帧；推进到赛道加载及俄文操作说明，无未捕获异常。 |
| `reload-final/` | headless，8 MiB，复用 `play/rms`，2100 帧；进入比赛 HUD，1800 帧计时 0:00，2100 帧计时 0:09。30.2 fps，末次 heap 5,555,728/6,283,712 字节、10 次 GC；累计 308,977 triangles、27,080 M3G 计数，无未捕获异常。 |

最终实验命令（再次执行时换用新的截图输出目录）：

```sh
./build/nj2me --port headless --heap 8 --fps 30 --run-frames 2100 \
  --keys '300:soft2,480:fire,720:fire,960:fire,1080:fire,1200:fire,1320:fire' \
  --stats --where \
  --rms-dir .local-tools/debug-runs/racer-20261017/play/rms \
  --dump-frames .local-tools/debug-runs/racer-20261017/reload-final/frames/ \
  --dump-every 300 'tests/games/急速狂飙完美破解版_4964.jar'
```

日志仍有已被游戏处理的 NumberFormatException、EOFException，以及 `/prov.png` 缺失后的 IOException/NullPointerException；没有通过吞异常处理这些情况。

## 剩余问题

比赛 HUD 和计时已推进，但 `reload-final/01800.png` 与 `reload-final/02100.png` 中赛道区域仍为白色。M3G 提交数量不能证明像素渲染正确，需继续对照同阶段参考画面，排查相机、裁剪、深度或材质等路径。当前结论限于 RMS 初始化、菜单交互及实验存档重载恢复，不能据此宣称赛车完整可玩。
