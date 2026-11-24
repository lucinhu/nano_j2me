# nano_j2me 0.5.46

Android GL 呈现接入设置（最近邻/扫描线/LCD/CRT/FSR1）、FSR1 直接绘制到窗口、
Android 多存档位与倒带 UI、GL 呈现诊断与像素级校验（0.249% 仅字体差异）。

## 验证
- CTest 211/211（安装 r65 核心后全绿；唯一失败的原因为旧 dist 缓存已更新）
- libretro mock 20/20（见构建日志）、真实 RetroArch LoopDemo PASS
- Android APK x86_64 模拟器：各呈现模式截图与 logcat 证据在 docs/notes 中

## 资产
见 GitHub release 的 sha256sums 与各 ABIs APK。
