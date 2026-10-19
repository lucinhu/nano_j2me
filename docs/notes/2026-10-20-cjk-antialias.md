# 内置中文字库四级抗锯齿

## 动机

内置中文点阵字库（Source Han Sans CN，12×16 与 16×20 字格）只含 1 位墨迹，
放大后笔画边缘有明显阶梯，且细笔画在阈值下丢失。真实游戏菜单与文本区
（`孤岛余生2时间之谜汉化版_7371.jar` 主菜单）受影响最明显。

## 修改

- `tools/gen_cjk_font.py --bpp 2`：在原有 4× 超采样 Lanczos 之后把灰度值量化为
  四级（0/85/170/255），按每像素 2 位、MSB 先行打包进字格。`--bpp 1`（默认）保持
  原 1 位字库逐字节不变；单色笔画回退符号（U+2016、U+FF3F）在两种模式下都保留。
- `src/gfx/nj_font_cjk_aa.h`：新生成的 2 位字库头，仍由 SIL OFL 1.1 约束；小字
  357360 字节、大字 595600 字节，共增加约 407 KiB 只读数据（相对 1 位库约
  +416928 字节，`size`：text 2667069 vs 2250141）。
- `src/gfx/nj_font.c`：`NJ_ENABLE_FONT_CJK_AA` 时包含 2 位头，否则包含原 1 位头；
  `NJ_CJK_COVERAGE` 决定 provider 格式。
- `src/gfx/nj_text.c`：1 位固定拉丁表始终按 1 位读取（`coverage=0`），与 provider
  元数据无关；provider `coverage=2` 时按每行 `(w+3)/4` 字节解码 2 位样本并混合。
- `include/nj2me/nj_gfx.h`：说明 2 位样本布局（0..3 → 0/85/170/255）。
- `include/nj2me/nj_config.h`：默认 0；`CMakeLists.txt` 桌面默认 1 并作为 PUBLIC
  编译定义。嵌入式仍可裁剪回 1 位，无堆分配或新缓存。

## 验证

- `ctest -R 'font|gfx|ttf'`：6/6（含新 2 位像素/裁切/XOR/透明度混合回归）。
- 独立 `NJ_ENABLE_FONT_CJK_AA=0` 构建：`test_font_png`、`test_font_coverage` 通过。
- 生成器两次运行字节一致（`cmp`）；1 位模式重新生成的数组与现有库一致。
- 原版《孤岛余生2》240×320、16 MiB 堆、30 fps、独立 RMS 回放 1500 帧（含菜单
  导航），退出码 0，约 30.2 fps。主菜单前后对照见
  `.local-tools/debug-runs/font-aa-20261020/menu-comparison.png`。

原 JAR、用户 RMS 及其他未提交改动未受影响。
