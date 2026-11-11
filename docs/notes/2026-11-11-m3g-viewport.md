# M3G setViewport 尺寸校验与失败原子性

`Graphics3D.setViewport(x, y, width, height)` 原先直接写入四个视口字段，接受零、负数及超过已公布上限的尺寸，也会覆盖此前合法的视口状态。调用方因而无法依靠异常判断参数是否有效。

[JSR 184 Graphics3D 规范](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Graphics3D.html) 要求宽、高非正或分别超过 `maxViewportWidth` / `maxViewportHeight` 时抛出 `IllegalArgumentException`；`x`、`y` 可以是任意 Java int。项目此前已公布两个尺寸上限均为 4096。

修复在任何字段写入前检查 `1 <= width <= 4096`、`1 <= height <= 4096`。非法调用抛异常并完整保留原有 x、y、宽、高；后续合法调用仍可更新视口。校验不计算 `x + width` / `y + height`，也不增加 target 绑定或 target 大小限制。

## 回归范围

扩展既有 `test_m3g`，保留原 935 项检查，并在独立 512 KiB VM 中通过真实 `getInstance`、`setViewport`、四个 getter 和 `getTarget` 验证：

- 1×1、4096×1、1×4096、4096×4096 均成功。
- `x/y = INT_MIN/INT_MAX` 及反向组合配合合法尺寸均成功。
- 15 组非法尺寸覆盖单独宽/高的 0、-1、-7、INT_MIN、INT_MAX、4097，以及两尺寸同时非法。每组先设置四字段互异的合法状态，再分别检查异常及四字段保持。
- 全程无绑定 target；最大尺寸只设置状态，不创建大图或渲染。非法调用后合法设置恢复正常，roots 与异常状态平衡。

normal 与 strict 均使用相同最终 fixture：旧库各 **1127 checks / 75 failures**，修复后各 **1127 checks / 0 failures**。旧失败恰为 15 组非法调用的异常检查及四字段保持检查，原 935 项均通过。两档相关 `test_vm` 均 exit 0，保留原三次 create/run/destroy、相同清理结果，并通过 System 属性与 M3G 发现检查。VM 只打印第一轮的 298 classes、host blocks 1106 / peak 1118，内部比较三轮后明确报告 identical teardown PASS。

strict 使用 ASan、UBSan、float-cast-overflow、LeakSanitizer，开启 Werror、no-recover 和 frame-pointer；未报告 sanitizer 错误。两档各复用上轮 76 成员库，仅替换 M3G 一个成员，其他 75 个成员及顺序逐字节保持。同档旧、新运行共用同一个冻结 fixture object。

本轮不修改查询能力、渲染器、内置字段/方法表或存档格式。验证范围为上述 API 状态与既有 M3G/VM 回归；没有新增游戏、基准、完整 170 项测试或 Android 实测，也不据此声明完整 M3G 一致性或性能收益。
