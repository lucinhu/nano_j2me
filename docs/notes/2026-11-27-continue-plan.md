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

## 2026-11-27 深夜进展（下一轮起点）

- **更正**：多包事务「读取未初始化宿主内存」的结论是错的，根因是 M3G journal 的纹理池扩容后没
  有搬走已排队三角形里的纹理指针。修复后多包事务、批处理 list、8× HD 会话全部确定（见
  [hd-triangle-transfer.md §十三](2026-11-27-hd-triangle-transfer.md)）。
- **Vulkan 持久会话（M1）已完成**：`nj_vk_renderer_triangles_begin/end/abort`，会话期间附件常驻，
  每批只开一次 render pass（动态 viewport/scissor），颜色/深度各导入导出一次；`NJ_VK_SESSION=1`
  opt-in，默认路径不变；`tests/test_gfx_vulkan.c` 的 `session_equivalence` 覆盖逐包/一次 list 提交、
  abort、越界 decline 与 grouped 事务可复现。
- **确定性**：8× 闸门（帧 60/120/180）与单包路径逐位一致；1× 400 帧 cap=1/2/4 与基线逐帧一致，
  默认 cap 只差 2–5 px（最大通道差 8，深度量化语义差）；8× 场内 400 帧会话两次运行逐位相同。
- **耗时**：8× 场内 400 帧，会话把 38,162 次提交收成 619 次，但 M3GPROF GPU 段 32.4 s vs 单包
  30.2 s，release 墙钟无可测收益。原因：619 个会话里 369 个是整屏矩形，一次导入+导出约 39 MB，
  与逐包 ROI 之和相当 ⇒ 不默认开启（保持 opt-in）。
- **修正后的下一步**：物理镜面/深度加 CPU 写入代次与脏矩形；`m3g_clear` 改 GPU 侧清屏；会话只
  导入「自上次导出以来 CPU 写过」的矩形；导出推迟到 present/createImage/软件回退/FSR2 观察点并
  只导必要矩形；之后再评估 2D 上 GPU。旧的「多包事务不可复现、以后不要再碰」的说法作废，但批处理
  本身仍无收益，不要重启。
- 验证：236/236、222/222、check_android_java 33/33、no_statics OK。
- **AVD 证据（目标②收尾）**：新增 `debug.nj2me.vk_session` 属性开关（模拟器无法传环境变量），
  用 `--gles2 --vulkan` APK 在 Medium_Phone_API_36.1（-gpu host）上采 8× present stats：
  JBenchmark3D 场景 session 关/开 draw ≈4.0 ms、swap 6.1–7.6 vs 8.1–8.5 ms、frame 28.7–47.9 vs
  28.8–38.1 ms ⇒ 无可测收益，与桌面结论一致；session 保持 opt-in。日志见
  `.local-tools/debug-runs/avd-session-20261127/`，细节见 gpu-resident-hd-plan.md §九。

### 2026-11-28 上午（目标③第一步）

- contract v2 保留会话（dirty/retain/depth_retain）落地并通过单测与 1×/8× 确定性闸门；
  默认仍 opt-in，journal 侧已接线但真实场景 `depth_retain` 恒为 0（每帧 CPU 深度 clear），
  且 dirty 仍是整屏，因此保留路径未命中。
- 8× 时间去向已量化：journal 关墙钟 40.1 s / user 25.8 s；会话墙钟 40.9 s / user 28.1 s；
  `[vkprof]` import 8.9 s + export 6.1 s + render 3.2 s。结论：8× 主要成本是 CPU 侧高清 2D
  合成（~70 ms/帧 vs 1× 的 ~5 ms/帧），import/export 优化天花板低于 20% 目标。
- 下一轮起点：方案 B（2D 合成上 GPU、present 直接消费 GPU 图像；镜面/深度常驻），以及
  GPU 侧深度清屏 + 诚实脏矩形作为会话路线的前置。

### 2026-11-28 上午后半（目标③方案 B）

- gprof（-pg，8× 400 帧）定位 CPU 侧：逐包 ROI 拷贝（copy_words 7.7 s）、逐包校验
  （7.7 s）、M3G 背景图 8× 缩放（5.1 s）、视频 overlay（1.45 s）、2D 物理 span（1.24 s）、
  区域 blit（~1.3 s）、Java 解释器（1.4 s）。
- 新增 `test_gfx_vulkan --bench-resident`：1920×2560 全屏颜色+深度一次往返
  min 18.40 / median 20.60 ms ⇒ 常驻模型每帧只导出一次颜色（约 5-7 ms）后，帧预算有
  希望从 ~100 ms 降到 ~20-30 ms。
- 方案与里程碑写入 docs/notes/2026-11-28-gpu-2d-composition-plan.md；下一轮从 M1
  （Vulkan 常驻镜面 + 最小 2D 操作 + 单测/基准）开始。

### 2026-11-28 上午末（常驻镜面 M1）

- Vulkan 常驻镜面对象与 clear/fill/export API 落地，单测逐字节通过；
  1920×2560 每帧 clear+8 fills+export 基准 min 8.32 / median 16.46 ms。
- 下一步：M2 纹理 quad（背景图/overlay/drawImage）+ 文字接入、M3G HD 路径改画进常驻镜面，
  逐位闸门改为截图/容差对照；全量测试与 Android present 验证后合入。

### 2026-11-28 上午末（常驻镜面 M2a）

- 常驻镜面加深常驻深度；新增纹理 quad API 与单测（nearest 逐像素一致、alpha 混合 0 outliers）。
- 基准（1920×2560，load≈8.7）：clear+8 fills+全屏纹理 quad+export min 4.70 / median 6.38 ms；
  逐包全屏颜色+深度往返 21.39/24.40 ms。
- 下一步 M2b：nj_gfx 2D 调用点（背景图/overlay/drawImage/文字）与 M3G HD 三角形接入常驻镜面，
  截图/容差对照 + 8× 场内 A/B。

### 2026-11-28 上午（Round 6）

- 常驻镜面三角形路径已实现并自测通过：`nj_vk_renderer_mirror_triangles`（一次导入、
  单 render pass 画多包、一次颜色导出；深度常驻不回读）。单测批量 vs 逐包逐字节一致；
  基准 62 包 / 1920×2560：15.76 ms vs 921.58 ms（min）。
- 下一步：把 M3G HD 路径与 `nj_gfx` 2D 图元接到常驻镜面，验证 8× 画面（截图/容差）与
  性能 A/B；目标 ≥20% 才默认开启。

## 2026-11-28 Round 6/7 进展：常驻镜面接线（M2b wiring）

- 后端/端口：`hd_mirror_sync/clear/image/drop` 能力 + `nj_vk_resident` 常驻镜面；M3G 清屏、
  背景图缩放、三角形全部留在 GPU，present 前一次颜色导出；HD 条目释放时丢弃 GPU 副本
  （防地址复用继承旧内容）。
- 开关 `NJ_VK_RESIDENT=1`（Android `debug.nj2me.vk_resident`），默认关闭。
- 数据：Doom3 8× 400 帧稳定段 23–26 s/100 帧 → 10–11 s/100 帧（2.0–2.5×）；8× 确定性
  回放两次逐位相同；与基线差集中在星空单像素与几何边缘（最大 39/22，无块状伪影）。
- AVD 对照已完成：x86_64 --gles2 --vulkan APK + `debug.nj2me.vk_resident`（logcat 确认
  `vulkan: resident HD mirror enabled`），JBenchmark3D 温暖后 on/off present stats 无可测
  差异（frame 24.0/29.8 vs 23.35/28.73 ms）⇒ AVD 瓶颈在 GLES2 present/上传，需 M4。
- 剩余：M3 脏矩形导出、M4 present 接 GPU 图像、多 title 回归；通过后考虑默认开启。

### 2026-11-28 下午（Round 8，常驻路径校验优化）

- 发现常驻路径 CPU 的 73% 是重复契约校验（journal 入队 + commit + 后端各一次，含深度 keys
  扫描）。加端口 `gfx_triangles_list_trusted` 与 `nj_gfx_triangles_validate_fast`（仅内部
  journal 包使用）后：8× 场内 400 帧 24.9 s → 14.3 s（−42%），gprof 采样 13.08 s → 3.48 s，
  确定性逐位不变。
- 剩余热点：`hd_art_span`（CPU 2D 高清图像 blit）与 `jr_draw_walk`（逻辑面软件走查）。
