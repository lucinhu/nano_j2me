# nano_j2me · Web 前端（web shell）

浏览器里的 J2ME 运行时前端。纯 ES Module、无框架、无构建步骤、无外部依赖，所有资源都在本地。
中文为主，English notes at the bottom.

```
web/
├── index.html          应用外壳（库 + 游戏两页，面板、对话框、toast 容器）
├── manifest.json       PWA 清单（standalone、主题色、192/512/maskable 图标）
├── sw.js               service worker：预缓存外壳 + 引擎，支持离线打开
├── css/app.css         全部样式（深色、响应式、安全区、减少动画）
├── assets/icon.svg     站点图标（内联 SVG，无外部字体/图片）
├── assets/icon-*.png   由 icon.svg 生成的 192/512/maskable 图标
└── js/
    ├── main.js         入口：语言、引擎加载、库/游戏路由、mock 横幅
    ├── module.js       wasm 引擎门面（Runtime facade）+ IDBFS 挂载
    ├── mock-module.js  没有 wasm 时的模拟引擎（240×320 动画 + 假存档）
    ├── library.js      游戏库：导入/搜索/排序/删除/封面
    ├── game.js         游戏页：rAF 泵、canvas、面板、存档槽（缩略图 + .njst 导出/导入）、截图、全屏
    ├── input.js        键盘/触屏按键、手柄（Gamepad API）、指针透传、文本框桥
    ├── audio.js        WebAudio 拉取/调度（njw_audio_read）
    ├── saves.js        IndexedDB（游戏 JAR + 元数据 + 3 个存档槽含缩略图）
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
- 存档状态与游戏 ID 绑定；读档失败会提示“存档不兼容，游戏需要重新开始”。每个槽支持
  **导出** 为 `<标题>-slot<N>-<时间>.njst` 文件、以及从文件 **导入** 到任意槽（导入先经引擎
  校验 JAR/ABI，失败不会覆盖原有存档）。

## 安装与离线（PWA）

- `manifest.json` 声明了 `standalone` 显示模式、主题色和 192/512/maskable 图标；
  桌面版 Chrome/Edge 可用地址栏的安装按钮，Firefox/移动端可“添加到主屏幕”。
- `sw.js` 在安装时预缓存整个外壳和引擎（`js/nj2me.js` + `js/nj2me.wasm`），
  之后离线重载也能打开并直接运行游戏；页面探测引擎用的 HEAD 请求由缓存合成响应。
- 引擎进缓存后，游戏库页脚会显示“离线可用”（`#library-engine[data-offline=1]`，
  同时 `document.body[data-offline=ready]`）：此时游戏库（IndexedDB）和引擎都在本地，
  断网启动已导入的游戏也能正常跑帧。
- service worker 需要安全上下文（`https://` 或 `localhost`/`127.0.0.1`）。
  本地开发时用 `?nosw=1` 跳过注册并注销已有 worker，避免缓存旧页面。
- **改动 `web/` 下的外壳文件后，记得同时改 `sw.js` 里的 `CACHE` 版本号**，
  否则旧缓存会继续返回上一版页面（缓存优先策略）。
- `tools/check_web.sh --browser` 的浏览器套件包含 PWA 检查：manifest、service worker
  接管、引擎已缓存、库页脚“离线可用”，以及停掉静态服务器后的真实离线重载 +
  从库启动已导入游戏（画面非黑、帧号继续推进；Firefox 的 Playwright 离线模拟对
  service worker 不生效，所以测试用停服务器来模拟断网）。

## 浏览器支持

需要 ES Modules、WebAssembly、IndexedDB、Pointer Events；`autoPersist` 不支持时自动退化。
离线打开需要 service worker（`https://` 或 `localhost`）。
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

官方浏览器套件（`node tools/web_test/web_browser_test.mjs`，34 项）在真实 Firefox
里跑完整流程（含库收藏的星标与「只看收藏」筛选），并额外验证 PWA：manifest 可解析（4 个图标、standalone）、
service worker 接管页面、引擎已进缓存、库页脚显示“离线可用”、停止静态服务器后
离线重载仍是 wasm 引擎，以及断网状态从库启动已导入的游戏（12 帧内出画面且帧号继续推进）；
游戏面板的「游戏内存」选择（16/32/64/128 MiB）、「屏幕尺寸」预设（自动/176×208/176×220/128×160/320×240）都会写入 `nj2me.settings` 并在下次启动生效；内存不足（OOM）时错误浮层会出现「以 64 MB 重启」按钮，一键改用更大堆重启并持久化设置；
工具面板的「诊断」区显示 Java 堆用量/上限、帧/绘制计数、GC 次数与字节码速率（来自 `njw_stats()`，与原生 `--stats` 同源）。
游戏页每帧轮询 `navigator.getGamepads()`，标准手柄布局按 A=Fire、B/X/Y/LB=GAME_A..D、
RB=Clear、LT/Start=左软键、RT/Back=右软键、十字键/左摇杆（0.45 死区）映射，连接/断开有 toast。
多 MIDlet 的 JAR（例如 frog-parrot 的 book5/6/7）会在工具面板出现「启动 MIDlet」选择框，
选择写入游戏记录并在下次启动生效；套件用 book6.jar 验证切换后画面变化且选择已持久化。
工具面板的音量滑块直接写 WebAudio gain（0–100%），拖动时自动取消静音并持久化到设置。
同一浏览器 profile 一次只允许一个标签页运行引擎：标签页之间用 `BroadcastChannel`
租约协调（`web/js/engine-owner.js`），后开的标签页会提示「另一个标签页正在运行游戏」，
对方退出后即可启动；无 `BroadcastChannel` 的浏览器保持旧行为。

全语料审计（403 个 JAR，每款 90 帧）现已自动化：

```sh
tools/check_web.sh --sweep   # 或 node tools/web_test/web_sweep_test.mjs --evidence DIR
tools/check_web.sh --perf    # 或 node tools/web_test/web_perf_test.mjs --evidence DIR
```

`--perf` 对 4 个轻/中量标题各跑 600 帧（固定 1000/60 ms 步长），下限取参考机
数字的约十分之一，用来抓数量级回退而不在慢机器上误报；实测 fps/耗时写入
`perf-summary.{txt,json}`。

每标题独立子进程 + 25 s 超时，输出 `sweep-403.jsonl` 与 `sweep-summary.txt`；
`black`（采样帧全黑）只作警告，已知的构造器拒绝/堆不足/无效 JAR 固定为预期项。

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
