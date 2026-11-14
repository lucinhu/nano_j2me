/*
 * nano_j2me web shell - i18n
 *
 * Every user-visible string lives here.  Chinese is the primary language of the
 * project (README.zh-CN.md is the canonical README), English is maintained in
 * parallel.  The dictionary is a flat key -> string map; "{name}" placeholders
 * are interpolated by t().  The chosen language is persisted in localStorage
 * and mirrors onto <html lang>.
 */

const LS_LANG = 'nj2me.lang';

export const STRINGS = {
  zh: {
    /* ---- shell ------------------------------------------------------- */
    'app.title': 'nano_j2me',
    'app.subtitle': '网页版',
    'app.tagline': '把功能机上的 J2ME 游戏搬进浏览器',
    'app.languageEnglish': 'English',
    'app.languageChinese': '中文',
    'common.ok': '好',
    'common.cancel': '取消',
    'common.close': '关闭',
    'common.retry': '重试',
    'common.delete': '删除',
    'common.error': '错误',
    'common.warning': '警告',
    'common.loading': '加载中…',
    'common.unknown': '未知',
    'common.yes': '是',
    'common.no': '否',

    /* ---- library ----------------------------------------------------- */
    'library.search': '搜索游戏…',
    'library.searchLabel': '搜索游戏',
    'library.sort': '排序方式',
    'library.sort.recent': '最近运行',
    'library.sort.imported': '最近导入',
    'library.sort.name': '名称',
    'library.sort.size': '文件大小',
    'library.import': '导入 JAR',
    'library.importLabel': '选择 J2ME .jar 文件导入',
    'library.dropTitle': '把 .jar 文件拖到这里',
    'library.dropActive': '松开鼠标即可导入',
    'library.dropHint': '也可以点「导入 JAR」选择文件，支持一次导入多个。',
    'library.emptyTitle': '游戏库还是空的',
    'library.emptyBody': '导入一个 J2ME 游戏（.jar），它就会一直留在浏览器里。',
    'library.emptyHint': '游戏文件保存在 IndexedDB 中，不会上传到网络。',
    'library.count': '找到 {n} / {m} 个游戏',
    'library.countAll': '共 {m} 个游戏',
    'library.importing': '正在导入「{name}」…',
    'library.importingMany': '正在导入 {done}/{total}：「{name}」',
    'library.imported': '已导入「{title}」',
    'library.importedMany': '已导入 {n} 个游戏',
    'library.importFailed': '「{name}」导入失败：{message}',
    'library.notJar': '不是 .jar 文件',
    'library.notJarFile': '请选择 .jar 文件（J2ME 游戏套件）。',
    'library.deleteTitle': '删除游戏？',
    'library.deleteBody': '「{title}」会从这台浏览器里删除，它的存档也会一起删除。',
    'library.deleted': '已删除「{title}」',
    'library.deleteFailed': '删除失败：{message}',
    'library.playCount': '运行 {n} 次',
    'library.neverPlayed': '尚未运行',
    'library.open': '开始游戏',
    'library.tileMenu': '更多操作',
    'library.settings': '设置',
    'library.settingsTitle': '设置',
    'library.language': '语言',
    'library.screen': '默认屏幕尺寸',
    'library.screenAuto': '自动（240×320）',
    'library.controls': '虚拟按键',
    'library.controlsAuto': '自动（触屏显示）',
    'library.controlsShow': '总是显示',
    'library.controlsHide': '总是隐藏',
    'library.engine': '引擎',
    'library.engineLoading': '正在加载引擎…',
    'library.engineWasm': 'wasm 引擎已就绪',
    'library.engineMock': 'MOCK 模拟模式',
    'library.engineMissing': 'wasm 引擎未构建',
    'library.storageWarn': '此构建没有持久化文件系统，游戏内的存档只在本次会话有效。',
    'library.mockBanner': 'MOCK 模式：wasm 引擎还没有构建，正在用模拟画面演示界面。',
    'library.fallbackBanner': '未找到 wasm 引擎，已回退到 MOCK 模拟模式。',
    'library.mockHint': '运行 tools/build_web.sh 生成 wasm 模块，再把 build/web 用本地 HTTP 服务打开后刷新。',
    'library.servedHint': '用本地 HTTP 服务打开本页（例如 python3 -m http.server 8001 -d web），直接双击 index.html 无法加载 ES 模块。',
    'library.footerHint': '拖入 .jar 或点「导入 JAR」添加游戏 · 所有数据只保存在本机浏览器里',
    'library.signature': 'C99 运行时 · CLDC 1.1 / MIDP 2.0 · WebAssembly',
    'library.searchEmpty': '没有匹配「{q}」的游戏',
    'library.clearSearch': '清除搜索',
    'library.importingShort': '导入中…',
    'library.fileSize': '{size}',
    'library.classes': 'MIDlet 类',
    'library.moreInfo': '详情',
    'library.duplicate': '「{title}」已经在游戏库里，已更新。',

    /* ---- engine / module loading ------------------------------------- */
    'engine.loading': '正在初始化引擎…',
    'engine.loadFailed': '引擎加载失败：{message}',
    'engine.incompatible': 'wasm 引擎接口不兼容（模块 v{got}，页面需要 v{want}）。',
    'engine.deleteFailed': '删除游戏失败：{message}',
    'engine.noStorage': '浏览器不允许持久化存储，游戏库只在本次会话有效。',

    /* ---- game -------------------------------------------------------- */
    'game.loading': '正在启动「{title}」…',
    'game.loadingHint': '首次启动可能需要几秒钟',
    'game.loadingSlow': '还在加载…按「取消」可以回到游戏库。',
    'game.cancel': '取消',
    'game.paused': '已暂停',
    'game.pause': '暂停',
    'game.resume': '继续',
    'game.panel': '游戏面板',
    'game.panelOpen': '打开游戏面板',
    'game.panelClose': '收起游戏面板',
    'game.speed': '游戏速度',
    'game.speedValue': '{n}%',
    'game.mute': '静音',
    'game.volume': '音量',
    'game.saveSlots': '存档',
    'game.slot': '存档 {n}',
    'game.slotEmpty': '空',
    'game.slotFilled': '{time}',
    'game.save': '保存',
    'game.load': '读取',
    'game.saveOk': '已保存到存档 {n}',
    'game.saveFailed': '保存失败：{message}',
    'game.saveUnsupported': '游戏现在不能存档（可能正停在播放器、网络等原生调用里）。',
    'game.saveNoRuntime': '没有正在运行的游戏。',
    'game.loadOk': '已读取存档 {n}',
    'game.loadFailed': '存档不兼容或已损坏。游戏需要重新开始，不能从这个存档继续。',
    'game.loadRestarted': '存档不兼容，游戏已重新开始。',
    'game.screenshot': '截图',
    'game.screenshotOk': '截图已保存',
    'game.screenshotFailed': '截图失败',
    'game.fullscreen': '全屏',
    'game.fullscreenExit': '退出全屏',
    'game.fullscreenFailed': '浏览器拒绝了全屏请求。',
    'game.restart': '重新开始',
    'game.exit': '退出到游戏库',
    'game.exitedTitle': '游戏已结束',
    'game.exitedReason': '{reason}',
    'game.exitedDefault': 'MIDlet 已退出。',
    'game.errorTitle': '游戏出错了',
    'game.errorDefault': '引擎返回了未知错误。',
    'game.back': '返回游戏库',
    'game.retry': '重试',
    'game.status': '状态',
    'game.statusVersion': '引擎 {version}',
    'game.statusFrames': '帧数 {n}',
    'game.statusFps': '帧率 {fps}',
    'game.statusMock': 'MOCK',
    'game.fit': '画面',
    'game.fitInteger': '整数倍',
    'game.fitWindow': '适应窗口',
    'game.showControls': '显示虚拟按键',
    'game.keysHint': '方向键移动 · 回车/空格 确认 · F1/F2 软键 · Backspace 清除 · 0-9 * # · Q/W/E/R 游戏键 A-D',
    'game.textEditing': '正在用键盘输入…',
    'game.textDone': '完成',
    'game.textHint': '输入完成后按回车确认（多媒体文本框可换行）。',
    'game.autoPaused': '页面切到后台，已自动暂停。',
    'game.hiddenNote': '页面在后台，引擎已暂停。',
    'game.mockBanner': 'MOCK 模式：模拟画面',
    'game.mockStopHint': '按 R 键或打开面板点「退出」可以模拟 MIDlet 结束',
    'game.mockTextHint': 'MOCK：按 F1 打开模拟输入框，回车确认',
    'game.runningTime': '已运行 {time}',
    'game.timeFormat': '{minutes} 分 {seconds} 秒',
    'game.unsupportedEnv': '当前浏览器缺少 WebAssembly 支持。',
    'game.jarMissing': '找不到这个游戏的 JAR 数据，请重新导入。',
    'game.startFailed': '启动失败：{message}',
    'game.engineNotReady': '引擎还没准备好，请稍候再试。',
    'game.audioBlocked': '浏览器拦截了音频，点一下游戏画面即可恢复声音。',
    'game.restartConfirm': '重新开始会丢失当前进度（已保存的存档不受影响）。确定吗？',
    'game.exitConfirm': '退出后未保存的进度会丢失。确定退出吗？',
    'game.experimental': '实验性功能',
    'game.controls': '虚拟按键',
    'game.dpad': '方向键',
    'game.keypad': '数字键盘',
    'game.screen': '游戏画面',
    'game.keyUp': '上',
    'game.keyDown': '下',
    'game.keyLeft': '左',
    'game.keyRight': '右',
    'game.fire': '确认',
    'game.softLeft': '左软键',
    'game.softRight': '右软键',
    'game.clear': '清除',

    /* ---- text editor kinds (MIDP TextField constraints) -------------- */
    'text.constraint.any': '文本',
    'text.constraint.email': '邮箱',
    'text.constraint.number': '数字',
    'text.constraint.phone': '电话',
    'text.constraint.url': '网址',
    'text.constraint.decimal': '小数',

    /* ---- dialogs / toasts -------------------------------------------- */
    'dialog.confirm': '确定',
    'dialog.cancel': '取消',
    'toast.close': '关闭提示'
  },

  en: {
    /* ---- shell ------------------------------------------------------- */
    'app.title': 'nano_j2me',
    'app.subtitle': 'Web',
    'app.tagline': 'Feature-phone J2ME games, in the browser',
    'app.languageEnglish': 'English',
    'app.languageChinese': '中文',
    'common.ok': 'OK',
    'common.cancel': 'Cancel',
    'common.close': 'Close',
    'common.retry': 'Retry',
    'common.delete': 'Delete',
    'common.error': 'Error',
    'common.warning': 'Warning',
    'common.loading': 'Loading…',
    'common.unknown': 'Unknown',
    'common.yes': 'Yes',
    'common.no': 'No',

    /* ---- library ----------------------------------------------------- */
    'library.search': 'Search games…',
    'library.searchLabel': 'Search games',
    'library.sort': 'Sort',
    'library.sort.recent': 'Recently played',
    'library.sort.imported': 'Recently imported',
    'library.sort.name': 'Name',
    'library.sort.size': 'File size',
    'library.import': 'Import JAR',
    'library.importLabel': 'Choose J2ME .jar files to import',
    'library.dropTitle': 'Drop .jar files here',
    'library.dropActive': 'Release to import',
    'library.dropHint': 'Or press “Import JAR” to choose files. Multiple files are fine.',
    'library.emptyTitle': 'Your library is empty',
    'library.emptyBody': 'Import a J2ME game (.jar) and it will stay in this browser.',
    'library.emptyHint': 'Games live in IndexedDB; nothing is uploaded.',
    'library.count': '{n} of {m} games',
    'library.countAll': '{m} games',
    'library.importing': 'Importing “{name}”…',
    'library.importingMany': 'Importing {done}/{total}: “{name}”',
    'library.imported': 'Imported “{title}”',
    'library.importedMany': 'Imported {n} games',
    'library.importFailed': 'Could not import “{name}”: {message}',
    'library.notJar': 'not a .jar file',
    'library.notJarFile': 'Pick a .jar file (a J2ME MIDlet suite).',
    'library.deleteTitle': 'Delete game?',
    'library.deleteBody': '“{title}” will be removed from this browser, including its save states.',
    'library.deleted': 'Deleted “{title}”',
    'library.deleteFailed': 'Delete failed: {message}',
    'library.playCount': 'Played {n} times',
    'library.neverPlayed': 'Never played',
    'library.open': 'Play',
    'library.tileMenu': 'More actions',
    'library.settings': 'Settings',
    'library.settingsTitle': 'Settings',
    'library.language': 'Language',
    'library.screen': 'Default screen size',
    'library.screenAuto': 'Auto (240×320)',
    'library.controls': 'On-screen controls',
    'library.controlsAuto': 'Auto (touch devices)',
    'library.controlsShow': 'Always show',
    'library.controlsHide': 'Always hide',
    'library.engine': 'Engine',
    'library.engineLoading': 'Loading engine…',
    'library.engineWasm': 'wasm engine ready',
    'library.engineMock': 'MOCK mode',
    'library.engineMissing': 'wasm engine not built',
    'library.storageWarn': 'This build has no persistent filesystem; in-game saves last only for this session.',
    'library.mockBanner': 'MOCK mode: the wasm engine has not been built yet, so a simulated screen is showing.',
    'library.fallbackBanner': 'wasm engine not found; falling back to MOCK mode.',
    'library.mockHint': 'Run tools/build_web.sh to build the wasm module, serve build/web over local HTTP and reload.',
    'library.servedHint': 'Serve this page over local HTTP (e.g. python3 -m http.server 8001 -d web); opening index.html directly cannot load ES modules.',
    'library.footerHint': 'Drop .jar files or use “Import JAR” · everything stays in this browser',
    'library.signature': 'C99 runtime · CLDC 1.1 / MIDP 2.0 · WebAssembly',
    'library.searchEmpty': 'No games match “{q}”',
    'library.clearSearch': 'Clear search',
    'library.importingShort': 'Importing…',
    'library.fileSize': '{size}',
    'library.classes': 'MIDlet classes',
    'library.moreInfo': 'Details',
    'library.duplicate': '“{title}” is already in the library; updated.',

    /* ---- engine / module loading ------------------------------------- */
    'engine.loading': 'Starting the engine…',
    'engine.loadFailed': 'Engine failed to load: {message}',
    'engine.incompatible': 'wasm engine ABI mismatch (module v{got}, page needs v{want}).',
    'engine.deleteFailed': 'Could not delete game: {message}',
    'engine.noStorage': 'Persistent storage was refused; the library lasts only for this session.',

    /* ---- game -------------------------------------------------------- */
    'game.loading': 'Starting “{title}”…',
    'game.loadingHint': 'The first start can take a few seconds',
    'game.loadingSlow': 'Still loading… press “Cancel” to go back to the library.',
    'game.cancel': 'Cancel',
    'game.paused': 'Paused',
    'game.pause': 'Pause',
    'game.resume': 'Resume',
    'game.panel': 'Game panel',
    'game.panelOpen': 'Open game panel',
    'game.panelClose': 'Close game panel',
    'game.speed': 'Game speed',
    'game.speedValue': '{n}%',
    'game.mute': 'Mute',
    'game.volume': 'Volume',
    'game.saveSlots': 'Save states',
    'game.slot': 'Slot {n}',
    'game.slotEmpty': 'Empty',
    'game.slotFilled': '{time}',
    'game.save': 'Save',
    'game.load': 'Load',
    'game.saveOk': 'Saved to slot {n}',
    'game.saveFailed': 'Save failed: {message}',
    'game.saveUnsupported': 'The game cannot be saved right now (it may be inside a native call such as a player or socket).',
    'game.saveNoRuntime': 'No game is running.',
    'game.loadOk': 'Loaded slot {n}',
    'game.loadFailed': 'This save state is incompatible or corrupt. The game must restart and cannot continue from it.',
    'game.loadRestarted': 'Incompatible save state; the game restarted.',
    'game.screenshot': 'Screenshot',
    'game.screenshotOk': 'Screenshot saved',
    'game.screenshotFailed': 'Screenshot failed',
    'game.fullscreen': 'Fullscreen',
    'game.fullscreenExit': 'Exit fullscreen',
    'game.fullscreenFailed': 'The browser refused the fullscreen request.',
    'game.restart': 'Restart',
    'game.exit': 'Exit to library',
    'game.exitedTitle': 'Game over',
    'game.exitedReason': '{reason}',
    'game.exitedDefault': 'The MIDlet exited.',
    'game.errorTitle': 'The game crashed',
    'game.errorDefault': 'The engine reported an unknown error.',
    'game.back': 'Back to library',
    'game.retry': 'Retry',
    'game.status': 'Status',
    'game.statusVersion': 'Engine {version}',
    'game.statusFrames': '{n} frames',
    'game.statusFps': '{fps} fps',
    'game.statusMock': 'MOCK',
    'game.fit': 'Picture',
    'game.fitInteger': 'Integer scale',
    'game.fitWindow': 'Fit window',
    'game.showControls': 'Show on-screen controls',
    'game.keysHint': 'Arrows move · Enter/Space fire · F1/F2 soft keys · Backspace clear · 0-9 * # · Q/W/E/R game keys A-D',
    'game.textEditing': 'Typing with the keyboard…',
    'game.textDone': 'Done',
    'game.textHint': 'Press Enter to confirm (multiline text boxes accept newlines).',
    'game.autoPaused': 'The page went to the background; auto-paused.',
    'game.hiddenNote': 'Page is hidden; the engine is paused.',
    'game.mockBanner': 'MOCK mode: simulated picture',
    'game.mockStopHint': 'Press R or use the panel → Exit to simulate the MIDlet ending',
    'game.mockTextHint': 'MOCK: press F1 for a simulated text editor, Enter to confirm',
    'game.runningTime': 'Running for {time}',
    'game.timeFormat': '{minutes}m {seconds}s',
    'game.unsupportedEnv': 'This browser has no WebAssembly support.',
    'game.jarMissing': 'The game’s JAR data is missing; please import it again.',
    'game.startFailed': 'Could not start: {message}',
    'game.engineNotReady': 'The engine is not ready yet; try again in a moment.',
    'game.audioBlocked': 'The browser blocked audio. Click the game picture to re-enable sound.',
    'game.restartConfirm': 'Restarting loses unsaved progress (save states are kept). Continue?',
    'game.exitConfirm': 'Unsaved progress will be lost. Exit anyway?',
    'game.experimental': 'experimental',
    'game.controls': 'Touch controls',
    'game.dpad': 'D-pad',
    'game.keypad': 'Number pad',
    'game.screen': 'Game screen',
    'game.keyUp': 'Up',
    'game.keyDown': 'Down',
    'game.keyLeft': 'Left',
    'game.keyRight': 'Right',
    'game.fire': 'Fire',
    'game.softLeft': 'Left soft key',
    'game.softRight': 'Right soft key',
    'game.clear': 'Clear',

    /* ---- text editor kinds ------------------------------------------- */
    'text.constraint.any': 'text',
    'text.constraint.email': 'email',
    'text.constraint.number': 'number',
    'text.constraint.phone': 'phone',
    'text.constraint.url': 'URL',
    'text.constraint.decimal': 'decimal',

    /* ---- dialogs / toasts -------------------------------------------- */
    'dialog.confirm': 'Confirm',
    'dialog.cancel': 'Cancel',
    'toast.close': 'Dismiss'
  }
};

let current = 'zh-CN';

/** Choose the initial language: saved choice first, then navigator.language. */
export function detectLanguage() {
  try {
    const saved = localStorage.getItem(LS_LANG);
    if (saved === 'en' || saved === 'zh-CN' || saved === 'zh') {
      return saved === 'en' ? 'en' : 'zh-CN';
    }
  } catch (e) { /* private mode / storage disabled */ }
  const nav = (navigator.language || navigator.userLanguage || 'zh-CN').toLowerCase();
  return nav.startsWith('zh') ? 'zh-CN' : 'en';
}

export function getLanguage() {
  return current;
}

/** Is the current language Chinese? (zh-CN) */
export function isChinese() {
  return current !== 'en';
}

/** Persist + apply a language.  Returns the applied tag ('zh-CN' | 'en'). */
export function setLanguage(lang) {
  current = lang === 'en' ? 'en' : 'zh-CN';
  try { localStorage.setItem(LS_LANG, current); } catch (e) { /* ignore */ }
  document.documentElement.lang = current;
  return current;
}

/**
 * Translate a key.  Unknown keys fall back to Chinese, then to the key itself,
 * which keeps a typo visible instead of printing "undefined".
 * vars: { name: value } substituted into "{name}".
 */
export function t(key, vars) {
  const table = current === 'en' ? STRINGS.en : STRINGS.zh;
  let value = table[key];
  if (value === undefined) { value = STRINGS.zh[key]; }
  if (value === undefined) { value = key; }
  if (vars) {
    value = value.replace(/\{(\w+)\}/g, (m, name) => (
      Object.prototype.hasOwnProperty.call(vars, name) ? String(vars[name]) : m
    ));
  }
  return value;
}

/**
 * Apply translations to a DOM subtree: [data-i18n] for textContent,
 * [data-i18n-title] for title/aria-label, [data-i18n-placeholder].
 */
export function applyTranslations(root = document) {
  if (!root || !root.querySelectorAll) { return; }
  for (const el of root.querySelectorAll('[data-i18n]')) {
    const key = el.getAttribute('data-i18n');
    if (key) { el.textContent = t(key); }
  }
  for (const el of root.querySelectorAll('[data-i18n-title]')) {
    const key = el.getAttribute('data-i18n-title');
    if (key) { el.title = t(key); el.setAttribute('aria-label', t(key)); }
  }
  for (const el of root.querySelectorAll('[data-i18n-placeholder]')) {
    const key = el.getAttribute('data-i18n-placeholder');
    if (key) { el.placeholder = t(key); }
  }
  for (const el of root.querySelectorAll('[data-i18n-aria]')) {
    const key = el.getAttribute('data-i18n-aria');
    if (key) { el.setAttribute('aria-label', t(key)); }
  }
  document.title = t('app.title') + ' · ' + t('app.subtitle');
}

/** Locale-aware date + time, used for save-slot timestamps. */
export function formatDateTime(ms) {
  if (!ms) { return ''; }
  try {
    return new Intl.DateTimeFormat(current === 'en' ? 'en' : 'zh-CN', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit'
    }).format(new Date(ms));
  } catch (e) {
    return new Date(ms).toLocaleString();
  }
}

/** Compact relative time ("刚刚", "3 分钟前"), used in the library. */
export function formatRelativeTime(ms) {
  if (!ms) { return ''; }
  const delta = Date.now() - ms;
  if (delta < 60 * 1000) { return isChinese() ? '刚刚' : 'just now'; }
  if (delta < 60 * 60 * 1000) {
    const n = Math.floor(delta / 60000);
    return isChinese() ? n + ' 分钟前' : n + ' min ago';
  }
  if (delta < 24 * 60 * 60 * 1000) {
    const n = Math.floor(delta / 3600000);
    return isChinese() ? n + ' 小时前' : n + ' h ago';
  }
  if (delta < 7 * 24 * 60 * 60 * 1000) {
    const n = Math.floor(delta / 86400000);
    return isChinese() ? n + ' 天前' : n + ' d ago';
  }
  return formatDateTime(ms);
}

/** Human file size.  Binary units, decimal precision only where useful. */
export function formatBytes(bytes) {
  if (!bytes || bytes < 0) { return '0 B'; }
  if (bytes < 1024) { return bytes + ' B'; }
  if (bytes < 1024 * 1024) { return (bytes / 1024).toFixed(0) + ' KB'; }
  if (bytes < 1024 * 1024 * 1024) { return (bytes / (1024 * 1024)).toFixed(1) + ' MB'; }
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

/** Localised constraint name for the text editor bridge. */
export function textConstraintName(constraints) {
  const kind = (constraints || 0) & 0xffff;
  switch (kind) {
    case 1: return t('text.constraint.email');
    case 2: return t('text.constraint.number');
    case 3: return t('text.constraint.phone');
    case 4: return t('text.constraint.url');
    case 5: return t('text.constraint.decimal');
    default: return t('text.constraint.any');
  }
}
