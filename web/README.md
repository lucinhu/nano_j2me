# nano_j2me · Web 前端（web shell）

浏览器里的 J2ME 运行时前端。纯 ES Module、无框架、无构建步骤、无外部依赖，所有资源都在本地。
中文为主，English notes at the bottom.

```
web/
├── index.html          应用外壳（库 + 游戏两页，面板、对话框、toast 容器）
├── css/app.css         全部样式（深色、响应式、安全区、减少动画）
├── assets/icon.svg     站点图标（内联 SVG，无外部字体/图片）
└── js/
    ├── main.js         入口：语言、引擎加载、库/游戏路由、mock 横幅
    ├── module.js       wasm 引擎门面（Runtime facade）+ IDBFS 挂载
    ├── mock-module.js  没有 wasm 时的模拟引擎（240×320 动画 + 假存档）
    ├── library.js      游戏库：导入/搜索/排序/删除/封面
    ├── game.js         游戏页：rAF 泵、canvas、面板、存档槽、截图、全屏
    ├── input.js        键盘/触屏按键、指针透传、MIDlet 文本框桥
    ├── audio.js        WebAudio 拉取/调度（njw_audio_read）
    ├── saves.js        IndexedDB（游戏 JAR + 元数据 + 3 个存档槽）
    ├── ui.js           toast / banner / 进度条 / 确认框 / DOM 辅助
    └── i18n.js         全部界面文案（zh-CN 默认，en 可切换）
```

## 本地运行

```sh
# 只跑前端壳（没有 wasm 时会自动进入 mock 模式）
python3 -m http.server 8001 -d web
# 打开 http://127.0.0.1:8001/  （或 http://127.0.0.1:8001/?mock=1 强制 mock）
```

必须用本地 HTTP 服务打开：ES Module 不能从 `file://` 加载。

真实 wasm 引擎由仓库根的构建脚本生成：

```sh
tools/build_web.sh                       # 产出 build/web/
python3 -m http.server -d build/web 8000 # 打开 http://127.0.0.1:8000/
```

`build/web/` 的布局是 `web/` 的副本，另外把构建出的 `nj2me.js` + `nj2me.wasm`
放进 `build/web/js/`，也就是和 `module.js` 同目录。`module.js` 会先对
`new URL('./nj2me.js', import.meta.url)` 做一次 HEAD 探测，再 `await import('./nj2me.js')`；
尚未构建、或用 `web/` 直接服务时不会产生控制台的 MIME/404 报错，而是静默回退到
`mock-module.js` 并显示警告横幅。

## 门面（facade）说明

`js/module.js` 是唯一接触 wasm 导出的文件，其余 UI 只用统一门面：

- `loadRuntime()` → `WasmRuntime` 或 `MockRuntime`（永不抛异常）
- 生命周期：`optionsReset`、`optionInt`、`optionString`、`start`、`frame`、`stop`
- 画面：`width`、`height`、`framebuffer`、`frameEpoch`
- 输入：`key(code, down)`、`pointer(action, x, y)`、`text*`
- 音频：`audioAvailable`、`audioRate`、`audioChannels`、`audioRead`、`audioNotePlaying`
- 元数据/存档：`probe`、`appTitle`、`appVendor`、`appVersion`、`appIcon`、`midlet*`、
  `canSave`、`saveState`、`loadState`、`setSpeed`
- 文件系统：`initFileSystem`、`mkdirp`、`writeFile`、`syncFileSystem`

字符串按 `nj_web.h` 的约定经 `lengthBytesUTF8` + `stringToUTF8` 写进 wasm 内存；
音频/存档缓冲用 `njw_alloc`/`njw_free`；`njw_save_state` 的 out 参数用 `HEAPU32`/`HEAP32`。
若 lead 调整门面名称，只需改 `module.js`（以及 `mock-module.js` 的同名方法）。

## Mock 模式

- 导入任意文件都行（标题来自文件名），封面是 JS 生成的占位图。
- 游戏页是 240×320 的动画测试图案：移动网格、迷宫、拖尾圆点、FPS、`MOCK MODE` 标记。
- 可测试键盘/触屏、文本桥（F1 打开模拟输入框）、存档槽、截图、退出流程（按 `R`）。
- 横幅会提示运行 `tools/build_web.sh`。页面在 wasm 缺失时不会抛未捕获异常。

## 持久化

- **IndexedDB** `nano-j2me`：`games`（JAR 字节 + manifest 元数据 + 图标）和
  `states`（每个游戏 3 个存档槽：元数据 + blob）。
- **IDBFS** 挂载在 `/nano_j2me`：`jars/`、`saves/<gameId>/`（RMS）、
  `files/<gameId>/`。RMS 文件在刷新后保留；不支持 IDBFS 时退化为每 5 秒
  `FS.syncfs` + `pagehide`/`visibilitychange` 时同步。
- 存档状态与游戏 ID 绑定；读档失败会提示“存档不兼容，游戏需要重新开始”。

## 浏览器支持

需要 ES Modules、WebAssembly、IndexedDB、Pointer Events；`autoPersist` 不支持时自动退化。
面向近两年的 Firefox / Chrome / Edge / Safari（桌面与移动），布局覆盖 360px 宽到桌面。

## 自测

```sh
# Firefox（playwright 1.60，仅 Firefox）
~/.local/bin/playwright --version

# 1) mock 模式冒烟测试（不需要 wasm）
python3 .local-tools/debug-runs/web-shell/test_mock_shell.py

# 2) 真实 wasm 冒烟测试（先构建，再把 web/ + 模块拷到调试目录，避免写 build/）
tools/build_web.sh
cp -a web/. .local-tools/debug-runs/web-shell/serve/
cp build/web/js/nj2me.{js,wasm} .local-tools/debug-runs/web-shell/serve/js/
python3 -m http.server 8002 -d .local-tools/debug-runs/web-shell/serve &
python3 .local-tools/debug-runs/web-shell/test_wasm_shell.py
```

mock 脚本会：先验证“没有 wasm 时静默回退到 mock” → 加载页面（断言 0 控制台错误）→
中英切换 → 导入 `tests/games/hello.jar` → 搜索/排序 → 启动游戏 → 检查帧动画、
键盘、触屏、指针、文本桥 → 打开面板暂停、截图下载、保存/读档、速度/静音/适应/按键
开关 → 退出 → 刷新验证 IndexedDB 持久化 → 删除并确认 → 390×844 移动端截图。

wasm 脚本会：断言 `WasmRuntime` 已加载（apiVersion=1、runtimeVersion）、真实
`njw_probe` 读到 `Hello World`/`frog-parrot.net`、启动后 240×320 画面持续提交帧、
键盘/指针直达引擎、面板暂停、`njw_save_state`/`njw_load_state` 真实存档往返、
返回游戏库、刷新后 JAR 与存档仍在。Emscripten 的 `print`/`printErr` 由
`module.js` 显式接管：`[nj2me] …` 引擎日志走 `console.info`，原始行保存在
`runtime.engineLog` 供面板/测试读取，浏览器控制台保持零 error。

截图与日志在 `.local-tools/debug-runs/web-shell/`（git-ignored）。

## English

Plain ES modules, no framework, no build step, no external network dependencies.
The only file that touches wasm exports is `js/module.js`; everything else uses its
central facade. Serve with `python3 -m http.server 8001 -d web` for mock mode, or
build with `tools/build_web.sh` and serve `build/web/` for the real engine.
Persistence lives in IndexedDB (JAR + metadata + 3 save slots) and an IDBFS mount
at `/nano_j2me` (RMS files survive reloads). Strings are all in `js/i18n.js`
(zh-CN default, English toggle). A normal MIDlet exit shows its stop reason and
returns to the library after 3 s (or immediately via the exit button). Playwright
tests: `test_mock_shell.py` (mock, no wasm) and `test_wasm_shell.py` (real engine,
staged copy under `.local-tools/debug-runs/web-shell/`).
