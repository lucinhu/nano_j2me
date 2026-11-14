#!/usr/bin/env node
/**
 * Browser end-to-end test for the nano_j2me web shell + WASM engine.
 *
 *   node tools/web_test/web_browser_test.mjs [--evidence DIR] [--timeout MS]
 *
 * What it does
 * ------------
 *   1. Serves build/web/ (the assembled shell: web/ copy + js/nj2me.js +
 *      js/nj2me.wasm) from a throw-away static HTTP server on an ephemeral
 *      127.0.0.1 port.
 *   2. Drives it with headless Firefox through Playwright.  The public shell is
 *      used exactly like a player would: file input -> library tile -> game
 *      canvas, keyboard, tools panel, save slot, exit, reload.
 *   3. Serves a copy of the same site *without* nj2me.js/nj2me.wasm and checks
 *      that the shell degrades to the friendly MOCK fallback instead of
 *      throwing (no uncaught errors, banner tells the user how to build it).
 *   4. Asserts zero browser console errors / page errors / failed requests on
 *      the real shell, and that no request leaves 127.0.0.1 (no CDN/network).
 *
 * Playwright resolution order (first hit wins, no npm install needed):
 *   - node package "playwright" or "playwright-core" if installed;
 *   - the driver bundled with the Python playwright package (this repo's
 *     verification machines use `~/.local/bin/playwright` = playwright-python
 *     1.60, which ships playwright-core 1.60 under its driver/ directory).
 *
 * Evidence (screenshots, browser-shell-test.json, browser-console.log) is
 * written under --evidence (default .local-tools/debug-runs/
 * web-browser-verify-<YYYYMMDD-HHMM>, git-ignored).  Exit codes: 0 pass,
 * 1 test failure, 2 missing build/usage error.
 *
 * This file is invoked by tools/check_web.sh --browser as the "browser page
 * test" suite; it depends only on Node >= 18 and a Firefox build for
 * Playwright.  It never modifies the repository.
 */

import http from 'node:http';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..');
const BUILD_WEB = path.join(REPO_ROOT, 'build', 'web');
const HELLO_JAR = path.join(REPO_ROOT, 'tests', 'games', 'hello.jar');
const SITE_JS = path.join(BUILD_WEB, 'js', 'nj2me.js');
const SITE_WASM = path.join(BUILD_WEB, 'js', 'nj2me.wasm');

const USAGE = `usage: node tools/web_test/web_browser_test.mjs [--evidence DIR] [--timeout MS]

  --evidence DIR  evidence directory (default .local-tools/debug-runs/web-browser-verify-<stamp>)
  --timeout MS    per-wait timeout in milliseconds (default 25000)
  --help          show this help`;

/* ------------------------------------------------------------------ utils */

const startedAt = Date.now();
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function stamp() {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}`;
}

function parseArgs(argv) {
  const opts = { evidence: null, timeout: 25000, help: false };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') {
      opts.help = true;
    } else if (arg === '--evidence') {
      if (i + 1 >= argv.length) { throw new Error('--evidence needs a directory'); }
      opts.evidence = argv[++i];
    } else if (arg.startsWith('--evidence=')) {
      opts.evidence = arg.slice('--evidence='.length);
    } else if (arg === '--timeout') {
      if (i + 1 >= argv.length) { throw new Error('--timeout needs a number'); }
      opts.timeout = Number(argv[++i]);
    } else if (arg.startsWith('--timeout=')) {
      opts.timeout = Number(arg.slice('--timeout='.length));
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  if (!Number.isFinite(opts.timeout) || opts.timeout < 1000) {
    throw new Error('--timeout must be a number >= 1000');
  }
  if (!opts.evidence) {
    opts.evidence = process.env.WEB_EVIDENCE_DIR
      || path.join(REPO_ROOT, '.local-tools', 'debug-runs', `web-browser-verify-${stamp()}`);
  } else if (!path.isAbsolute(opts.evidence)) {
    opts.evidence = path.resolve(process.cwd(), opts.evidence);
  }
  return opts;
}

const MIME_TYPES = new Map(Object.entries({
  '.html': 'text/html; charset=utf-8',
  '.htm': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.md': 'text/markdown; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.wasm': 'application/wasm',
  '.jar': 'application/java-archive',
  '.txt': 'text/plain; charset=utf-8'
}));

/** Minimal read-only static file server with request logging. */
async function startStaticServer(root) {
  const requests = [];
  const server = http.createServer(async (req, res) => {
    const entry = { method: req.method, url: req.url, status: 0 };
    requests.push(entry);
    try {
      const url = new URL(req.url, 'http://127.0.0.1');
      let rel = decodeURIComponent(url.pathname);
      if (rel.endsWith('/')) { rel += 'index.html'; }
      rel = rel.replace(/^\/+/, '');
      if (!rel) { rel = 'index.html'; }
      let file = path.resolve(root, rel);
      if (file !== root && !file.startsWith(root + path.sep)) {
        entry.status = 403;
        res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end('Forbidden');
        return;
      }
      let st = await fsp.stat(file).catch(() => null);
      if (st && st.isDirectory()) {
        file = path.join(file, 'index.html');
        st = await fsp.stat(file).catch(() => null);
      }
      if (!st || !st.isFile()) {
        entry.status = 404;
        res.writeHead(404, {
          'Content-Type': 'text/plain; charset=utf-8',
          'Cache-Control': 'no-store'
        });
        res.end('Not found');
        return;
      }
      entry.status = 200;
      res.writeHead(200, {
        'Content-Type': MIME_TYPES.get(path.extname(file).toLowerCase())
          || 'application/octet-stream',
        'Content-Length': st.size,
        'Cache-Control': 'no-store'
      });
      if (req.method === 'HEAD') { res.end(); return; }
      const stream = fs.createReadStream(file);
      stream.on('error', () => { try { res.destroy(); } catch { /* ignore */ } });
      stream.pipe(res);
    } catch (error) {
      entry.status = 500;
      try {
        res.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end('internal error');
      } catch { /* client went away */ }
    }
  });
  server.on('clientError', (error, socket) => {
    try { socket.destroy(); } catch { /* ignore */ }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const { port } = server.address();
  return {
    root,
    origin: `http://127.0.0.1:${port}`,
    requests,
    close: () => new Promise((resolve) => server.close(() => resolve()))
  };
}

/* -------------------------------------------------------------- Playwright */

/**
 * Resolve Playwright without requiring an npm install:
 *  1. local/global node packages named playwright / playwright-core;
 *  2. the playwright-core bundle inside the Python playwright driver
 *     (playwright-python ships its own node driver and Firefox browser).
 */
async function loadPlaywright() {
  const attempts = [];
  for (const id of ['playwright', 'playwright-core']) {
    try {
      const mod = await import(id);
      if (mod && mod.firefox) {
        return { playwright: mod, origin: `node package '${id}'` };
      }
      attempts.push(`${id}: module has no firefox export`);
    } catch (error) {
      attempts.push(`${id}: ${error && error.code ? error.code : error.message}`);
    }
  }

  const python = process.env.PYTHON || 'python3';
  try {
    const driverIndex = execFileSync(python, ['-c',
      'import playwright, os; ' +
      'print(os.path.join(os.path.dirname(playwright.__file__), "driver", "package", "index.mjs"))'
    ], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
    if (driverIndex && fs.existsSync(driverIndex)) {
      const mod = await import(pathToFileURL(driverIndex).href);
      if (mod && mod.firefox) {
        return { playwright: mod, origin: `python playwright driver (${driverIndex})` };
      }
      attempts.push('python playwright driver: no firefox export');
    } else {
      attempts.push('python playwright driver: index.mjs not found');
    }
  } catch (error) {
    const message = error && error.message ? error.message.split('\n')[0] : String(error);
    attempts.push(`python playwright: ${message}`);
  }

  throw new Error(
    'Playwright for Firefox is required but was not found.\n' +
    '  tried: ' + attempts.join(' | ') + '\n' +
    '  install one of: npm i -D playwright (then npx playwright install firefox)\n' +
    '                  pip install playwright && playwright install firefox'
  );
}

/* --------------------------------------------------------------- test rig */

const results = {
  ok: false,
  startedAt: new Date(startedAt).toISOString(),
  evidence: '',
  checks: [],
  failures: 0,
  fatalError: null,
  screenshots: [],
  build: { dir: BUILD_WEB, module: SITE_JS, wasm: SITE_WASM },
  playwright: null,
  servers: {},
  diagnostics: {},
  externalRequests: [],
  notes: []
};

let evidenceDir = '';
let timeoutMs = 25000;
let browser = null;
let context = null;
let realServer = null;
let missingServer = null;
let missingDir = '';
let realPage = null;
let missingPage = null;

function log(line) {
  console.log(line);
}

async function check(name, fn, { fatal = false } = {}) {
  const t0 = Date.now();
  try {
    const detail = await fn();
    const ms = Date.now() - t0;
    results.checks.push({ name, ok: true, detail: detail === undefined ? null : detail, ms });
    log(`[PASS] ${name}${detail !== undefined && detail !== null ? ` — ${detail}` : ''}`);
    return detail;
  } catch (error) {
    results.failures++;
    const message = error && error.message ? error.message : String(error);
    results.checks.push({ name, ok: false, error: message, ms: Date.now() - t0 });
    log(`[FAIL] ${name}: ${message}`);
    if (fatal) { throw new Error(`${name}: ${message}`); }
    return null;
  }
}

function assert(condition, message) {
  if (!condition) { throw new Error(message); }
}

async function screenshot(page, name) {
  const file = path.join(evidenceDir, name);
  await page.screenshot({ path: file });
  results.screenshots.push(file);
  log(`[shot] ${file}`);
  return file;
}

function attachDiagnostics(page, label) {
  const diag = {
    label,
    url: page.url(),
    consoleErrors: [],
    consoleLog: [],
    pageErrors: [],
    failedRequests: [],
    badResponses: [],
    requests: []
  };
  page.on('console', (msg) => {
    const rec = { type: msg.type(), text: msg.text() };
    diag.consoleLog.push(rec);
    if (msg.type() === 'error') { diag.consoleErrors.push(rec); }
  });
  page.on('pageerror', (error) => {
    diag.pageErrors.push(String(error && error.stack ? error.stack : error));
  });
  page.on('request', (request) => { diag.requests.push(request.url()); });
  page.on('requestfailed', (request) => {
    diag.failedRequests.push({
      url: request.url(),
      error: request.failure() ? request.failure().errorText : ''
    });
  });
  page.on('response', (response) => {
    if (response.status() >= 400) {
      diag.badResponses.push({ url: response.url(), status: response.status() });
    }
  });
  results.diagnostics[label] = diag;
  return diag;
}

function isAbortLike(errorText) {
  return /NS_BINDING_ABORTED|NS_ERROR_|ERR_ABORTED|cancelled|interrupted/i.test(errorText || '');
}

function isFavicon(url) {
  return /\/favicon\.ico(?:$|\?)/.test(url);
}

/* --------------------------------------------------------- page utilities */

async function t(page, key, vars) {
  return page.evaluate(async ({ key, vars }) => {
    const { t: translate } = await import('/js/i18n.js');
    return translate(key, vars);
  }, { key, vars: vars || {} });
}

async function frames(page) {
  return page.evaluate(() => {
    const state = window.__nj2me;
    if (!state || !state.runtime) { return -1; }
    try { return state.runtime.frames(); } catch { return -1; }
  });
}

async function waitFrames(page, minimum, timeout = timeoutMs) {
  await page.waitForFunction(
    (min) => {
      const state = window.__nj2me;
      if (!state || !state.runtime) { return false; }
      try { return state.runtime.frames() >= min; } catch { return false; }
    },
    minimum,
    { timeout, polling: 100 }
  );
}

async function waitPaused(page, paused, timeout = 5000) {
  await page.waitForFunction(
    (want) => !!(window.__nj2me && window.__nj2me.game && window.__nj2me.game._paused === want),
    paused,
    { timeout, polling: 50 }
  );
}

async function waitReady(page, timeout = timeoutMs) {
  await page.waitForFunction(
    () => !!(window.__nj2me && window.__nj2me.ready === true),
    undefined,
    { timeout, polling: 100 }
  );
}

async function waitToast(page, text, timeout = 8000) {
  await page.waitForFunction(
    (needle) => Array.from(document.querySelectorAll('#toast-container .toast'))
      .some((node) => (node.textContent || '').includes(needle)),
    text,
    { timeout, polling: 100 }
  );
}

async function canvasStats(page) {
  return page.evaluate(() => {
    const canvas = document.querySelector('#game-canvas');
    if (!canvas) { throw new Error('no #game-canvas'); }
    const ctx = canvas.getContext('2d');
    const { width, height } = canvas;
    const data = ctx.getImageData(0, 0, width, height).data;
    const colors = new Set();
    let nonBlack = 0;
    for (let i = 0; i < data.length; i += 4) {
      const r = data[i];
      const g = data[i + 1];
      const b = data[i + 2];
      if (r !== 0 || g !== 0 || b !== 0) { nonBlack++; }
      colors.add((r << 16) | (g << 8) | b);
    }
    return { width, height, colorCount: colors.size, nonBlack, totalPixels: width * height };
  });
}

async function readLibrary(page) {
  return page.evaluate(async () => {
    const saves = await import('/js/saves.js');
    const games = await saves.listGames();
    return games.map((game) => ({
      id: game.id,
      title: game.title,
      vendor: game.vendor,
      version: game.version,
      probeSource: game.probeSource,
      fileName: game.fileName,
      jarBytes: game.jar
        ? (game.jar.byteLength !== undefined ? game.jar.byteLength : game.jar.length)
        : 0
    }));
  });
}

async function readStates(page, gameId) {
  return page.evaluate(async (id) => {
    const saves = await import('/js/saves.js');
    const states = await saves.listStates(id);
    return Array.from(states.entries()).map(([slot, record]) => ({
      slot: Number(slot),
      size: record.bytes
        ? (record.bytes.byteLength !== undefined ? record.bytes.byteLength : record.bytes.length)
        : 0,
      frames: record.frames || 0,
      at: record.at || 0
    }));
  }, gameId);
}

/* ------------------------------------------------------------------- flow */

async function runRealShellChecks() {
  realPage = await context.newPage();
  const diag = attachDiagnostics(realPage, 'real');

  await check('real shell page loads with the WASM engine', async () => {
    await realPage.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
    await waitReady(realPage);
    const info = await realPage.evaluate(() => ({
      kind: window.__nj2me.runtime.kind,
      mock: window.__nj2me.runtime.mock,
      apiVersion: window.__nj2me.runtime.apiVersion(),
      runtimeVersion: window.__nj2me.runtime.runtimeVersion(),
      engine: document.body.dataset.engine,
      badge: (document.querySelector('#engine-badge') || {}).textContent || ''
    }));
    assert(info.kind === 'wasm' && info.mock === false, `expected wasm runtime, got ${JSON.stringify(info)}`);
    assert(info.apiVersion === 1, `expected njw_api_version 1, got ${info.apiVersion}`);
    assert(typeof info.runtimeVersion === 'string' && info.runtimeVersion.length > 0,
      'runtime version is empty');
    assert(info.engine === 'wasm', `body[data-engine] expected wasm, got ${info.engine}`);
    return `api ${info.apiVersion}, runtime ${info.runtimeVersion}, badge ${JSON.stringify(info.badge)}`;
  }, { fatal: true });

  await check('library renders and language is zh-CN', async () => {
    await realPage.waitForSelector('#library-view', { state: 'visible' });
    const htmlLang = await realPage.evaluate(() => document.documentElement.lang);
    assert(htmlLang === 'zh-CN', `expected <html lang="zh-CN">, got ${htmlLang}`);
    return `html lang ${htmlLang}`;
  });

  await check('no banner / mock fallback in the real shell', async () => {
    const state = await realPage.evaluate(() => ({
      bannerHidden: document.querySelector('#app-banner').hidden,
      engineMock: window.__nj2me.runtime.mock
    }));
    assert(state.engineMock === false, 'runtime reports mock in the real shell');
    assert(state.bannerHidden === true, 'mock/fallback banner is visible in the real shell');
  });

  await screenshot(realPage, 'browser-01-library-empty.png');

  await check('import hello.jar through the file input', async () => {
    await realPage.setInputFiles('#file-input', HELLO_JAR);
    await realPage.waitForFunction(
      () => document.querySelectorAll('.game-tile').length === 1,
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    const tile = await realPage.textContent('.game-tile__title');
    assert(tile && tile.trim() === 'Hello World', `tile title expected "Hello World", got ${JSON.stringify(tile)}`);
    const count = await realPage.textContent('#library-count');
    return `tile ${JSON.stringify(tile.trim())}, count ${JSON.stringify((count || '').trim())}`;
  }, { fatal: true });

  await check('probed metadata is persisted in IndexedDB', async () => {
    const games = await readLibrary(realPage);
    assert(games.length === 1, `expected 1 stored game, got ${games.length}`);
    const game = games[0];
    assert(game.title === 'Hello World', `title ${JSON.stringify(game.title)}`);
    assert(game.vendor === 'frog-parrot.net', `vendor ${JSON.stringify(game.vendor)}`);
    assert(game.version === '2.0', `version ${JSON.stringify(game.version)}`);
    assert(game.probeSource === 'wasm', `probe source ${JSON.stringify(game.probeSource)}`);
    assert(game.jarBytes === fs.statSync(HELLO_JAR).size, `jar bytes ${game.jarBytes}`);
    results.notes.push(`gameId=${game.id}`);
    return `${game.id} ${game.title} / ${game.vendor} ${game.version}, ${game.jarBytes} B`;
  });

  await screenshot(realPage, 'browser-02-library-imported.png');

  const gameId = (await readLibrary(realPage))[0].id;

  await check('start the game: canvas is 240x320 and drawn', async () => {
    await realPage.click('.game-tile', { timeout: timeoutMs });
    await realPage.waitForFunction(
      () => !!(window.__nj2me.game && window.__nj2me.game.game),
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    await waitFrames(realPage, 12, timeoutMs);
    await realPage.waitForFunction(
      () => document.querySelector('#loading-overlay').hidden === true,
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    const stats = await canvasStats(realPage);
    assert(stats.width === 240 && stats.height === 320,
      `canvas is ${stats.width}x${stats.height}, expected 240x320`);
    assert(stats.colorCount >= 2, `canvas is uniform (colors=${stats.colorCount})`);
    assert(stats.nonBlack > 100, `canvas is blank (nonBlack=${stats.nonBlack})`);
    return `${stats.width}x${stats.height}, ${stats.colorCount} colors, ${stats.nonBlack} non-black pixels`;
  }, { fatal: true });

  await screenshot(realPage, 'browser-03-game.png');

  await check('keyboard reaches the engine with nj_key codes', async () => {
    await realPage.evaluate(() => {
      const runtime = window.__nj2me.runtime;
      const original = runtime.key.bind(runtime);
      window.__keyLog = [];
      runtime.key = (code, down) => {
        window.__keyLog.push([code, !!down]);
        return original(code, down);
      };
    });
    for (const key of ['ArrowLeft', 'ArrowRight', 'Enter', '5']) {
      await realPage.keyboard.press(key);
    }
    await realPage.waitForFunction(
      () => window.__keyLog && window.__keyLog.length >= 8,
      undefined,
      { timeout: 5000, polling: 50 }
    );
    const log = await realPage.evaluate(() => window.__keyLog);
    const downs = log.filter((entry) => entry[1]).map((entry) => entry[0]);
    const ups = new Set(log.filter((entry) => !entry[1]).map((entry) => entry[0]));
    for (const expected of [15, 16, 17, 6]) {
      assert(downs.includes(expected), `missing key down ${expected} in ${JSON.stringify(downs)}`);
      assert(ups.has(expected), `missing key up ${expected}`);
    }
    const before = await frames(realPage);
    await realPage.waitForTimeout(800);
    const after = await frames(realPage);
    assert(after > before, `frames did not advance during input (${before} -> ${after})`);
    return `downs ${JSON.stringify(downs)}; frames ${before}->${after}`;
  });

  await check('tools panel pauses, frames freeze, and resume works', async () => {
    await realPage.click('#panel-handle');
    await realPage.waitForSelector('#game-panel.is-open', { timeout: 5000 });
    await waitPaused(realPage, true);
    const pausedFrom = await frames(realPage);
    await realPage.waitForTimeout(600);
    const pausedTo = await frames(realPage);
    assert(pausedFrom === pausedTo, `frames advanced while paused (${pausedFrom} -> ${pausedTo})`);
    const indicator = await realPage.isVisible('#pause-indicator');
    assert(indicator, '#pause-indicator is not visible while paused');
    const status = await realPage.evaluate(() => ({
      version: (document.querySelector('#status-version') || {}).textContent || '',
      frames: (document.querySelector('#status-frames') || {}).textContent || ''
    }));
    assert(/[0-9]/.test(status.version), `status version looks empty: ${JSON.stringify(status.version)}`);
    await screenshot(realPage, 'browser-04-panel-paused.png');
    await realPage.click('#btn-pause');
    await waitPaused(realPage, false);
    await waitFrames(realPage, pausedTo + 5, 8000);
    return `frozen at frame ${pausedFrom}, resumed, status version ${JSON.stringify(status.version.trim())}`;
  }, { fatal: true });

  await check('save slot 1 writes a real njw_save_state blob', async () => {
    /* Pause again so the save is taken at a stable frame. */
    if (!(await realPage.evaluate(() => window.__nj2me.game._paused === true))) {
      await realPage.click('#btn-pause');
      await waitPaused(realPage, true);
    }
    await realPage.click('.slot[data-slot="1"] .slot__save');
    const saveToast = await t(realPage, 'game.saveOk', { n: 1 });
    await waitToast(realPage, saveToast);
    await realPage.waitForFunction(
      () => {
        const button = document.querySelector('.slot[data-slot="1"] .slot__load');
        return !!button && button.disabled === false;
      },
      undefined,
      { timeout: 8000, polling: 100 }
    );
    const states = await readStates(realPage, gameId);
    assert(states.length >= 1, `expected a stored slot, got ${JSON.stringify(states)}`);
    const slot = states.find((entry) => entry.slot === 1);
    assert(slot, `slot 1 missing: ${JSON.stringify(states)}`);
    assert(slot.size > 1000, `saved blob is only ${slot.size} bytes`);
    return `slot 1: ${slot.size} B at frame ${slot.frames}`;
  });

  await check('load slot 1 round-trips without restarting', async () => {
    const states = await readStates(realPage, gameId);
    const slotFrames = states.find((entry) => entry.slot === 1).frames;
    await realPage.click('.slot[data-slot="1"] .slot__load');
    const loadToast = await t(realPage, 'game.loadOk', { n: 1 });
    await waitToast(realPage, loadToast);
    const dialogOpen = await realPage.evaluate(
      () => !!document.querySelector('#confirm-dialog[open]')
    );
    assert(!dialogOpen, 'load reported the incompatible-state dialog for a valid slot');
    const errorVisible = await realPage.isVisible('#error-overlay');
    assert(!errorVisible, '#error-overlay appeared while loading a valid slot');
    const paused = await realPage.evaluate(() => window.__nj2me.game._paused === true);
    assert(paused, 'game should stay paused after loading a state');
    const stillOpen = await realPage.evaluate(() => !!(window.__nj2me.game && window.__nj2me.game.game));
    assert(stillOpen, 'game was torn down by a successful state load');
    return `loaded slot saved at frame ${slotFrames}`;
  });

  await screenshot(realPage, 'browser-05-panel-loaded.png');

  await check('exit returns to the library', async () => {
    await realPage.click('#btn-pause');
    await waitPaused(realPage, false);
    await realPage.click('#panel-close');
    await realPage.waitForFunction(
      () => document.querySelector('#game-panel').classList.contains('is-open') === false,
      undefined,
      { timeout: 5000, polling: 100 }
    );
    await realPage.click('#game-back');
    await realPage.waitForFunction(
      () => {
        const state = window.__nj2me;
        return !!(state && state.game && state.game.game === null)
          && window.getComputedStyle(document.querySelector('#library-view')).display !== 'none';
      },
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    assert(await realPage.isVisible('#library-view'), '#library-view is not visible after exit');
    const errors = results.diagnostics.real.consoleErrors.length;
    assert(errors === 0, `${errors} console error(s) during the game session`);
    return 'back on the library page';
  }, { fatal: true });

  await screenshot(realPage, 'browser-06-library-after-exit.png');

  await check('reload keeps the library and the save slot', async () => {
    await realPage.reload({ waitUntil: 'load', timeout: timeoutMs });
    await waitReady(realPage);
    await realPage.waitForFunction(
      () => document.querySelectorAll('.game-tile').length === 1,
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    const games = await readLibrary(realPage);
    assert(games.length === 1, `after reload expected 1 game, got ${games.length}`);
    assert(games[0].title === 'Hello World', `after reload title is ${JSON.stringify(games[0].title)}`);
    assert(games[0].jarBytes === fs.statSync(HELLO_JAR).size,
      `after reload jarBytes ${games[0].jarBytes}`);
    const states = await readStates(realPage, games[0].id);
    assert(states.some((entry) => entry.slot === 1 && entry.size > 1000),
      `save slot did not survive reload: ${JSON.stringify(states)}`);
    return `1 game (${games[0].jarBytes} B), slot 1 ${states.find((s) => s.slot === 1).size} B`;
  }, { fatal: true });

  await screenshot(realPage, 'browser-07-library-reloaded.png');
}

async function runMissingWasmChecks() {
  missingPage = await context.newPage();
  const diag = attachDiagnostics(missingPage, 'missing');

  await check('missing wasm falls back to MOCK with a friendly hint', async () => {
    await missingPage.goto(missingServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
    await waitReady(missingPage);
    const state = await missingPage.evaluate(() => ({
      mock: window.__nj2me.runtime.mock === true,
      reason: window.__nj2me.runtime.mockReason,
      engine: document.body.dataset.engine,
      bannerHidden: document.querySelector('#app-banner').hidden,
      banner: (document.querySelector('#app-banner') || {}).textContent || '',
      body: document.body.textContent || ''
    }));
    assert(state.mock, 'runtime did not fall back to mock without nj2me.js');
    assert(state.reason === 'failed', `mockReason expected "failed", got ${JSON.stringify(state.reason)}`);
    assert(state.engine === 'mock', `body[data-engine] expected mock, got ${JSON.stringify(state.engine)}`);
    assert(state.bannerHidden === false, 'fallback banner is not visible');
    assert(/build_web\.sh/.test(state.banner) || /build_web\.sh/.test(state.body),
      'friendly hint mentioning tools/build_web.sh is missing from the banner/page');
    return `mockReason=${state.reason}, banner visible`;
  }, { fatal: true });

  await check('missing wasm does not raise uncaught errors', async () => {
    await sleep(400);
    const d = results.diagnostics.missing;
    assert(d.pageErrors.length === 0, `page errors: ${JSON.stringify(d.pageErrors)}`);
    assert(d.consoleErrors.length === 0, `console errors: ${JSON.stringify(d.consoleErrors)}`);
    return 'zero page errors, zero console errors';
  });

  await screenshot(missingPage, 'browser-08-missing-wasm-fallback.png');
}

async function checkNetworkHygiene() {
  const allowedOrigins = new Set([realServer.origin, missingServer.origin]);
  const external = new Set();
  for (const key of ['real', 'missing']) {
    const diag = results.diagnostics[key];
    for (const url of diag.requests) {
      if (url.startsWith('data:') || url.startsWith('blob:')) { continue; }
      let origin = '';
      try { origin = new URL(url).origin; } catch { origin = url; }
      if (!allowedOrigins.has(origin)) { external.add(url); }
    }
  }
  results.externalRequests = Array.from(external);

  await check('all requests stayed on the local test servers', async () => {
    assert(results.externalRequests.length === 0,
      `external requests: ${JSON.stringify(results.externalRequests)}`);
    const total = results.diagnostics.real.requests.length
      + results.diagnostics.missing.requests.length;
    return `${total} requests, all 127.0.0.1`;
  });

  await check('real shell has zero console errors/page errors', async () => {
    const d = results.diagnostics.real;
    assert(d.consoleErrors.length === 0, `console errors: ${JSON.stringify(d.consoleErrors)}`);
    assert(d.pageErrors.length === 0, `page errors: ${JSON.stringify(d.pageErrors)}`);
    return `${d.consoleLog.length} console messages, 0 errors`;
  });

  await check('real shell has no failed requests or 4xx/5xx responses', async () => {
    const d = results.diagnostics.real;
    const failed = d.failedRequests.filter((entry) => !isAbortLike(entry.error));
    const bad = d.badResponses.filter((entry) => !isFavicon(entry.url));
    assert(failed.length === 0, `failed requests: ${JSON.stringify(failed)}`);
    assert(bad.length === 0, `bad responses: ${JSON.stringify(bad)}`);
    return `failed=0, bad=0`;
  });

  await check('only expected 404 for the intentionally missing module', async () => {
    const d = results.diagnostics.missing;
    const bad = d.badResponses.filter((entry) => !isFavicon(entry.url));
    const unexpected = bad.filter((entry) => !/\/js\/nj2me\.(js|wasm)$/.test(entry.url));
    assert(unexpected.length === 0, `unexpected bad responses: ${JSON.stringify(unexpected)}`);
    return bad.length ? `expected probe miss: ${JSON.stringify(bad.map((e) => e.url))}` : 'none';
  });
}

/* --------------------------------------------------------------- evidence */

async function writeEvidence() {
  results.ok = results.failures === 0 && !results.fatalError;
  results.finishedAt = new Date().toISOString();
  results.elapsedMs = Date.now() - startedAt;
  results.evidence = evidenceDir;
  if (!evidenceDir) { return; }
  try {
    const summaryFile = path.join(evidenceDir, 'browser-shell-test.json');
    await fsp.writeFile(summaryFile, JSON.stringify(results, null, 2) + '\n');

    const lines = [];
    for (const [label, diag] of Object.entries(results.diagnostics)) {
      lines.push(`== ${label} (${diag.url})`);
      for (const entry of diag.consoleLog) {
        lines.push(`[${entry.type}] ${entry.text}`);
      }
      for (const error of diag.pageErrors) {
        lines.push(`[pageerror] ${error}`);
      }
      for (const request of diag.failedRequests) {
        lines.push(`[requestfailed] ${request.url} ${request.error}`);
      }
      for (const response of diag.badResponses) {
        lines.push(`[http ${response.status}] ${response.url}`);
      }
      lines.push('');
    }
    await fsp.writeFile(path.join(evidenceDir, 'browser-console.log'), lines.join('\n'));

    const summary = [
      `browser page test — ${results.ok ? 'PASS' : 'FAIL'}`,
      `evidence:   ${evidenceDir}`,
      `playwright: ${results.playwright || 'unknown'}`,
      `checks:     ${results.checks.filter((c) => c.ok).length} passed, ` +
        `${results.checks.filter((c) => !c.ok).length} failed`,
      `screenshots:${results.screenshots.length}`,
      `elapsed:    ${results.elapsedMs} ms`
    ].join('\n');
    await fsp.writeFile(path.join(evidenceDir, 'browser-summary.txt'), summary + '\n');
  } catch (error) {
    console.error(`warning: could not write evidence: ${error && error.message}`);
  }
}

/* ------------------------------------------------------------------- main */

async function main() {
  let opts;
  try {
    opts = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(`web_browser_test: ${error.message}\n${USAGE}`);
    process.exit(2);
  }
  if (opts.help) {
    console.log(USAGE);
    process.exit(0);
  }
  evidenceDir = opts.evidence;
  timeoutMs = opts.timeout;
  await fsp.mkdir(evidenceDir, { recursive: true });

  log('== browser page test (Playwright Firefox, real WASM shell + mock fallback)');
  log(`   repository: ${REPO_ROOT}`);
  log(`   evidence:   ${evidenceDir}`);

  const missingBuild = [];
  if (!fs.existsSync(path.join(BUILD_WEB, 'index.html'))) { missingBuild.push('index.html'); }
  if (!fs.existsSync(SITE_JS)) { missingBuild.push('js/nj2me.js'); }
  if (!fs.existsSync(SITE_WASM)) { missingBuild.push('js/nj2me.wasm'); }
  if (!fs.existsSync(HELLO_JAR)) { missingBuild.push('tests/games/hello.jar'); }
  if (missingBuild.length) {
    console.error('web_browser_test: missing artifacts: ' + missingBuild.join(', '));
    console.error('  run tools/build_web.sh first (or tools/check_web.sh --build --browser)');
    results.fatalError = 'missing build artifacts: ' + missingBuild.join(', ');
    await writeEvidence();
    process.exit(2);
  }

  try {
    realServer = await startStaticServer(BUILD_WEB);
    results.servers.real = realServer.origin;
    log(`   serve build/web at ${realServer.origin}/`);

    missingDir = path.join(evidenceDir, 'missing-wasm-site');
    await fsp.rm(missingDir, { recursive: true, force: true }).catch(() => {});
    await fsp.cp(BUILD_WEB, missingDir, { recursive: true });
    await fsp.rm(path.join(missingDir, 'js', 'nj2me.js'), { force: true });
    await fsp.rm(path.join(missingDir, 'js', 'nj2me.wasm'), { force: true });
    missingServer = await startStaticServer(missingDir);
    results.servers.missing = missingServer.origin;
    log(`   serve no-wasm copy at ${missingServer.origin}/`);

    const loaded = await loadPlaywright();
    results.playwright = loaded.origin;
    log(`   playwright: ${loaded.origin}`);

    browser = await loaded.playwright.firefox.launch({ headless: true });
    try {
      context = await browser.newContext({
        viewport: { width: 960, height: 840 },
        locale: 'zh-CN',
        acceptDownloads: true
      });
    } catch (error) {
      throw new Error(`could not create Firefox context (is the browser installed? ` +
        `try 'playwright install firefox'): ${error.message}`);
    }

    await runRealShellChecks();
    await runMissingWasmChecks();
    await checkNetworkHygiene();
  } catch (error) {
    results.fatalError = error && error.message ? error.message : String(error);
    log(`[FATAL] ${results.fatalError}`);
  } finally {
    if (context) { await context.close().catch(() => {}); }
    if (browser) { await browser.close().catch(() => {}); }
    if (realServer) { await realServer.close().catch(() => {}); }
    if (missingServer) { await missingServer.close().catch(() => {}); }
    if (missingDir) { fs.rmSync(missingDir, { recursive: true, force: true }); }
    results.servers.realRequests = realServer ? realServer.requests.length : 0;
    results.servers.missingRequests = missingServer ? missingServer.requests.length : 0;
    results.diagnostics.real && (results.diagnostics.real.url = realServer ? realServer.origin + '/' : '');
    results.diagnostics.missing && (results.diagnostics.missing.url = missingServer ? missingServer.origin + '/' : '');
    await writeEvidence();
  }

  const passed = results.checks.filter((entry) => entry.ok).length;
  const failed = results.checks.filter((entry) => !entry.ok).length;
  log('');
  log('== browser page test summary');
  log(`   result:     ${results.ok ? 'PASS' : 'FAIL'}`);
  log(`   checks:     ${passed} passed, ${failed} failed`);
  log(`   screenshots:${results.screenshots.length}`);
  log(`   evidence:   ${evidenceDir}`);
  if (results.fatalError) { log(`   fatal:      ${results.fatalError}`); }
  log(`   elapsed:    ${results.elapsedMs ?? (Date.now() - startedAt)} ms`);
  process.exit(results.ok ? 0 : 1);
}

main().catch((error) => {
  console.error(`web_browser_test: unexpected failure: ${error && error.stack ? error.stack : error}`);
  process.exit(1);
});
