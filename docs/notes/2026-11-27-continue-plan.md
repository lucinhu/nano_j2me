# 性能优化续做清单（交接）

当前基线：确定性回放与「Vulkan 只走单包三角路径」的决策已提交
（`00323052` 确定性回放、`379c49bc` journal 诊断、`5de5afba` 单包路径与列表回退）。

## 已完成且已验证

- **确定性回放**：CLI `--deterministic` = 固定 `java.util.Random` 播种 + headless 端口虚拟时钟
  （每帧 +33 ms，`Thread.sleep` 只推进虚拟时间，`Date`/`Calendar` 用 2000-01-01Z 起的固定日历）。
  8× 回放（Doom3 200 帧）帧 60/120/180 = `846057676cfed67a`/`c65baebe4730fcbe`/`dc5cb60109dd03cb`，
  1× 回放（400 帧）帧 100/200/300/400 = `79fb89347746ef64`/`82272d8e4f649f38`/
  `c911305c5e3a0210`/`c911305c5e3a0210`，同命令重复运行逐位一致。墙钟回放在帧 120/180 会分叉
  （游戏逻辑读 currentTimeMillis），旧闸门哈希已作废。
- **上一轮 8×「镜面写漏」结论被推翻**：帧 120 的开关差异只有 478 px、最大 1 LSB，属回放噪声。
- **多包 list 事务被证伪并回退**：cap=1 与逐包一致；cap≥2 输入（包流/纹理池/深度 keys/输入表面
  哈希全部相同）相同却输出不可复现，依赖 ASLR 与 `MALLOC_PERTURB_`（读到未初始化宿主内存），
  画面有彩虹棋盘格/门偏移等伪影，且比逐包更慢（8× 场内 75-92 s vs ~63 s）。Vulkan 端口不再注册
  `gfx_triangles_list`，`gfx_hd_journal` 恒 0，`NJ_VK_HD_JOURNAL` 删除；`tri_draw.inc` 回退到逐包
  参考实现。详见 `2026-11-27-hd-triangle-transfer.md` §十二。
- 新增单测 `batch_equivalence`（tests/test_gfx_vulkan.c，直接调用 renderer 的 list API，验证委托
  与逐包逐字节一致）；修复 `tests/test_jit` 因端口时钟钩子无条件安装导致的段错误。

## 剩余工作

1. **目标②架构方案**：整帧镜面/深度 GPU 驻留 + 2D 合成上 GPU（§四/§五/§八 的方向）。GLES2 的
   驻留会话实验（`2026-11-27-hd-journal-batching.md`）与本次 Vulkan 实验都表明「并集矩形 +
   每帧一次事务」不可行；下一步应设计「整帧镜像常驻 GPU、CPU 只写脏块」的方案，先把路径打通再做
   逐位/容差闸门。
2. FSR1 与呈现路径继续打磨；有 arm64 设备/CI 时补 NEON（深度 key↔float 的 SSE2/NEON 已有
   `depth_import/depth_export`，缺少对照测试）。
3. 若将来重启批处理：先补「[m3glist] 式输入哈希 + 逐包输出比对」的自动闸门，并在有 Vulkan
   validation layer 的环境复现多包事务的未初始化读取；本机
   `/usr/share/vulkan/explicit_layer.d` 不存在，只能靠 ASLR/扰动实验。

## 闸门

- 确定性：同一二进制同一命令重复运行，8× 回放帧 60/120/180 的 sha256 必须等于上面的值；
  1× 回放帧 100/200/300/400 同理。改动渲染路径后重新生成基准并写进 notes。
- 测试：`/tmp/nj-suite-gpu` 236/236（Release + Vulkan + FSR2 + GFX_ACCEL + JIT + RGB565）、
  `/tmp/nj-suite-sw` 222/222（Release + CLI + SDL2）、`tools/check_android_java.sh`、
  `test_gfx_vulkan`（已包含在上面的 ctest 里）。
- 复现命令：8× 闸门用 `--graphics vulkan --render-scale 8 --ssaa 1 --asset-filter pixel-art
  --deterministic --dump-every 60 --run-frames 200 --keys 200:down:down,205:down:up,215:fire:down,
  216:fire:up --fs-dir .local-tools/debug-runs/fsr-quality-round3-3d/doom-8x/fs/
  --rms-dir .local-tools/debug-runs/fsr-quality-round3-3d/doom-8x/rms/ tests/games/毁灭战士3_10598.jar`
  （注意 `--dump-frames` 目录必须先建好，否则不会写盘）。
