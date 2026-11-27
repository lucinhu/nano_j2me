# 性能优化续做清单（交接）

当前基线：`13342ccc`（单命令缓冲列表批处理已逐位验证；高清 journal 开关默认关闭）。

## 已完成且已验证

- Vulkan 列表事务改为单命令缓冲：一次并集 seed、一次渲染过程（每包动态 viewport/scissor）、一次导出、一次提交/fence。1× 确定性回放帧 100/200 与逐包参考实现逐位一致；1× 实测约 −1.2%（12.845 → 12.693 s / 240 帧）。
- 高清 journal 接线已就绪但默认关闭：`nj_port.gfx_hd_journal`、`jr_hd_flush_queued()`、镜面回收前 flush；`NJ_VK_HD_JOURNAL=1` 可开启。
- 8× 确定性回放开关对照：帧 60 一致，帧 120/180 分叉 ⇒ 存在未经过刷新点的宿主写镜面路径。

## 8× 分叉的排查线索（下一步）

1. `nj_midp_hd_target` / `nj_midp_hd_present` 的 2D 合成路径是否只刷新了 present 或 gfx 之一；
2. M3G 背景图/清屏是否直接写 mirror 而未走 `FLUSH_CLEAR`；
3. FSR2 回填直接写 `hd->pixels`，与 journal 交叠时的顺序；
4. LCDUI 借用镜面（`nj_lib_lcdui.c:2242`）归还时是否 flush。

建议做法：给 `nj_render_mirror` 加写入代次（generation），journal 在 import/flush 前比对代次，不一致则先提交已排队批次。

## 闸门

- 逐位：同一二进制开关两种模式下，8× 回放帧 60/120/180 的 sha256 必须一致（当前默认值 `1481221ee168f9ce`/`625259768f4b32c1`/`4c0afc53d206d5cc`）。
- 耗时：8× 回放 240 帧 wall time；AVD `present stats` 的 draw/frame。
- 测试：236/236 + 222/222 + `check_android_java` + `test_gfx_vulkan`。
