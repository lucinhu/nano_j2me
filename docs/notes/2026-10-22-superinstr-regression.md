# 解释器快路径（superinstr / jump threading）回归报告与更正（2026-10-22）

## 结论（先读这一段）

**第一版（本文件初稿）说「两个快路径都开就会打挂真实游戏」——这个结论只对当时的
在途中间态成立，对现在的提交尖端不成立，本文件已更正。**

- 报告最初是在**别的会话尚未提交的在途工作区**上量出来的，那个工作区的二进制
  SHA-256 是 `fb63027bee1da9c49ef18b489100ec661f1df6446fdc094f21b0ceef6b266bc1`。
  当时它确实会把《孤岛余生2》卡在启动画面、把《都市赛车6》拖到超时。
- 那些改动随后被那个会话**修好并提交**了（`06bedce perf(interp): replicate the whole
  dispatch table, fuse four hot pairs` 起的一串提交）。
- **本轮在提交尖端 `2a6292e` 上重新实测，两个游戏都正常**，与「快路径关掉」时的行为一致。
  另一个会话也独立复跑并记录了同一结论（提交 `2e0f0b2 docs(perf): resolve the
  third-party superinstr regression claim on the frozen tip`，写入 `docs/PERFORMANCE.md`）。
- 因此：**当前尖端没有需要修的解释器回归**，本文件保留为「当时看到的现象 + 如何复现
  + 如何证伪」的记录，不再作为待修缺陷。

## 更正后的尖端实测（提交 `2a6292e`，干净树）

构建（干净树，避免工作区里别人在途的改动）：

```bash
git archive HEAD | tar -x -C .local-tools/head-tip-20261022
cmake -S .local-tools/head-tip-20261022 -B .local-tools/head-tip-20261022/build \
      -DCMAKE_BUILD_TYPE=Release
cmake --build .local-tools/head-tip-20261022/build -j4 --target nj2me-cli
# 二进制 SHA-256 4d2625ebf7dd6d66444d086baf7f3738c8909bd54e4b0cfd6e6c678f0c0f93d5
```

《孤岛余生2》`2023012708463745568.jar`（240x320，900 帧，headless）：

```bash
.local-tools/head-tip-20261022/build/nj2me --port headless --size 240x320 --fps 30 \
  --heap 16 --rms-dir "$R/rms" --run-frames 900 \
  --keys '210:fire 360:soft2 540:fire 720:fire 810:down' \
  --dump-frames "$R/frames/" --dump-every 60 --stats "$JAR"
```

| 指标 | 在途中间态 `fb63027b…` | 提交尖端 `2a6292e` |
|---|---|---|
| thrown | **1,350** | 9（全是 RMS 的 `RecordStoreNotFoundException` / `InvalidRecordIDException`，应用自己捕获） |
| uncaught | 0 | **0** |
| gfx 调用 | **2,258** | 27,971 |
| natives | 1,214,565 | 317,602 |
| bytecodes | 7,869,287 | 3,946,058 |
| 堆 | **12.89 MB（撑满）** | **2,589,696 B（2.59 MB）** |
| 第 900 帧不同颜色数 | **3**（停在「…」） | **142**（中文主菜单） |

《都市赛车6》`都市赛车6火线追击.jar`（240x320，1200 帧，headless）：尖端
**exit 0、0 次 uncaught、226,524 次 gfx 调用、246,183,660 条字节码**，跑完 1200 帧
用时 23.5 秒（51.2 fps 逻辑帧）。在途中间态则是 120 秒超时（exit 124）、
一帧都没画出来。

证据目录：`.local-tools/debug-runs/tipcheck-20261022/{01-island,02-asphalt6}/`
（`log.txt` 含 `stats:` 行与全部 thrown 文本，`frames/*.ppm` 为逐帧转储；
第 900 帧 `00900.ppm` 用 PIL 取不同颜色数 = 142）。

## 当时看到的现象（历史记录，仍然有效）

在途中间态的二进制（SHA-256 `fb63027bee1da9c49ef18b489100ec661f1df6446fdc094f21b0ceef6b266bc1`，
与当时工作区的 `build/nj2me` 逐字节相同）上：

- 两个开关都开时，《孤岛余生2》900 帧只有 **3 种颜色**、停在启动画面，
  thrown 1,350 次，堆撑满 12.89 MB；
- 《都市赛车6》300 帧时**超时被杀（exit 124）**，日志只有一行
  `running GloftASP6 (Asphalt 6 Adrenaline 1.0.0)`。

关掉任意一个开关（`-DNJ_ENABLE_SUPERINSTR=0` 或 `-DNJ_ENABLE_JUMP_THREADING=0`）
都恢复成与旧 HEAD 一致的行为，说明当时的交互点在「融合后的指令边界」与
「`goto` 链目标计算」之间。

> 这段历史数据只对**那个中间态二进制**成立；换任何别的二进制重跑都可能得不到
> 同样结果——这正是本文件被更正的原因。复现时请先 `sha256sum` 自己的二进制。

## 隔离实验构建方式（仍然可复用）

| 变体 | 构建方式 |
|---|---|
| 干净树 | `git archive HEAD \| tar -x -C <dir>` + `cmake --build <dir>/build -j4` |
| 工作区 | `cmake -S . -B /tmp/nj-wt-build` + `cmake --build /tmp/nj-wt-build -j4` |
| 只关 superinstr | `cmake -S . -B /tmp/nj-si0 -DNJ_ENABLE_SUPERINSTR=0` |
| 只关 jump threading | `cmake -S . -B /tmp/nj-jt0 -DNJ_ENABLE_JUMP_THREADING=0` |

## 边界与未验证项

- 本文件只覆盖《孤岛余生2》与《都市赛车6》两个 JAR 的 headless 回放；
  **其余 27 个 JAR 没有逐一在尖端复跑**，别的会话提交的快路径是否对它们也无影响，
  本轮未做全面回归。
- 两个游戏都是 headless + 固定按键序列的判定，**不等于完整可玩**。
- 报告期间工作区随时在变，任何「二进制哈希对不上」的现象优先怀疑构建产物过期，
  而不是源码状态。
