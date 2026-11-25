# 超分画质复核（FSR 文字发虚）

来自用户反馈：FSR 超分下文字模糊、画面观感差。本轮用 StaticDemo（8px 文字、细网格、斜线）
在宿主 GLES2 路径做同帧对比：`--upscale-scale 3`（240x320 → 720x960），分别输出 nearest 与 fsr（EASU+RCAS）。

## 观察

- **nearest**：文字与网格边缘是硬像素，清晰但有锯齿（`docs/images/ui/upscale/fsr-compare-nearest.png`）。
- **fsr**：文字出现加粗/重影，网格线两侧出现亮暗光晕（`docs/images/ui/upscale/fsr-compare-fsr.png`）。
- 数值上 FSR 输出的梯度能量反而更高（文本带 mean |∇| 31.8 vs 20.8），说明不是单纯"变糊"，
  而是 EASU 在硬边缘上构造了渐变、RCAS 又把边缘过冲放大成光晕（ringing）。

## 原因

1. FSR1 的 EASU 假设输入是"连续图像"（3D 渲染的软边），对像素画的硬边/小字号会主动平滑，
   再用 RCAS 增强，结果就是文字变粗 + 光晕。
2. 源码 240x320、8~12px 字号，放大 3~5 倍后文字信息本身就很少，任何插值算法都无法恢复笔画细节；
   只有 nearest/sharp-bilinear（整数倍时等于 nearest）能保持字形。
3. 我们的 RCAS 锐度是 AMD 参考默认值（`sharp = exp2(-0.2)`），并非误调；问题在算法适用场景。

## 建议

- **2D/文字类**：优先 nearest（整数倍）或 sharp-bilinear（非整数倍），它们保持字形清晰。
- **3D/M3G 类**：FSR1 更合适；当前默认 `--upscale-scale 2`，可尝试 2x 并降低 RCAS 强度以减少光晕。
- **改进方向**（后续实现）：
  1. 新增 `bicubic`（Catmull-Rom / Lanczos2）模式：非整数倍时比 bilinear 锐，光晕小于 FSR；
  2. 暴露 RCAS 强度参数（默认 0.2，允许 0–0.4），文字场景可调低；
  3. Android 选择器按用途标注（文字/像素游戏 → nearest；3D → FSR），并按显示分辨率自动建议倍数；
  4. 若追求像素画放大质量，可评估 xBR/MMPX 类算法（仅整数倍、只对 2D 有意义）。

## 本轮结论

桌面端 FSR 在像素画/文字上是"算法不匹配"而非参数错误；先给用户可选模式与说明，再实现 bicubic/Lanczos
与 RCAS 强度调节，并用同一 StaticDemo 对比图验收。
