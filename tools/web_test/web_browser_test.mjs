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
 *      canvas, keyboard, library favourites, URL import (via a second CORS
 *      server), tools panel, save slot (with its thumbnail preview),
 *      one-click recovery from an out-of-memory failure, exit, reload.
 *   3. Serves a copy of the same site *without* nj2me.js/nj2me.wasm and checks
 *      that the shell degrades to the friendly MOCK fallback instead of
 *      throwing (no uncaught errors, banner tells the user how to build it).
 *   4. Asserts zero browser console errors / page errors / failed requests on
 *      the real shell, and that no request leaves 127.0.0.1 (no CDN/network).
 *   5. Checks the PWA layer: manifest, service worker control, cached engine,
 *      a real offline reload after the static server is stopped, the shell's
 *      offline-ready label, and an imported game that starts and keeps
 *      drawing while there is no server at all.
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
const URL_IMPORT_JAR = HELLO_JAR;
// Needs more Java heap than the shell default; used by the OOM recovery check.
const HEAVY_JAR = path.join(REPO_ROOT, 'tests', 'games', '愤怒的小鸟_383.jar');
const GAME15_JAR = path.join(REPO_ROOT, 'tests', 'games', 'game15.jar');
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
  let closed = false;
  return {
    root,
    origin: `http://127.0.0.1:${port}`,
    requests,
    close: () => {
      if (closed) { return Promise.resolve(); }
      closed = true;
      return new Promise((resolve) => server.close(() => resolve()));
    }
  };
}

/* -------------------------------------------------------------- Playwright */

/**
 * Resolve Playwright without requiring an npm install:
 *  1. local/global node packages named playwright / playwright-core;
 *  2. the playwright-core bundle inside the Python playwright driver
 *     (playwright-python ships its own node driver and Firefox browser).
 */
/* Minimal second server: one jar plus permissive CORS, so the shell's URL
 * import can be exercised the way a user with a CORS-enabled host sees it. */
async function startJarServer(jarPath) {
  const data = fs.readFileSync(jarPath);
  const server = http.createServer((req, res) => {
    const urlPath = (req.url || '/').split('?')[0];
    const headers = {
      'Content-Type': 'application/java-archive',
      'Content-Length': data.length,
      'Access-Control-Allow-Origin': '*',
      'Cache-Control': 'no-store'
    };
    if (urlPath !== '/hello.jar') {
      res.writeHead(404, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
      res.end('not found');
      return;
    }
    if (req.method === 'HEAD') { res.writeHead(200, headers); res.end(); return; }
    if (req.method !== 'GET') { res.writeHead(405, headers); res.end(); return; }
    res.writeHead(200, headers);
    res.end(data);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  server.origin = `http://127.0.0.1:${server.address().port}`;
  return server;
}

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
        : 0,
      screenPreset: game.screenPreset || null,
      speedPercent: Number.isFinite(Number(game.speedPercent)) ? Number(game.speedPercent) : null,
      keyMap: game.keyMap && typeof game.keyMap === 'object' ? { ...game.keyMap } : null,
      frameCap: Number.isFinite(Number(game.frameCap)) ? Number(game.frameCap) : null,
      autoFrames: game.autoState && Number.isFinite(Number(game.autoState.frames))
        ? Number(game.autoState.frames)
        : null
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

  await check('gamepad buttons reach the engine and release', async () => {
    await realPage.evaluate(() => {
      window.__fakePad = {
        connected: true,
        id: 'Test Pad (STANDARD GAMEPAD)',
        index: 0,
        mapping: 'standard',
        buttons: Array.from({ length: 17 }, () => ({ pressed: false, value: 0 })),
        axes: [0, 0]
      };
      window.__realGetGamepads = navigator.getGamepads ? navigator.getGamepads.bind(navigator) : null;
      Object.defineProperty(navigator, 'getGamepads', {
        configurable: true,
        value: () => [window.__fakePad]
      });
      window.dispatchEvent(new Event('gamepadconnected'));
      const runtime = window.__nj2me.runtime;
      const original = runtime.key.bind(runtime);
      window.__padKeyLog = [];
      runtime.key = (code, down) => {
        window.__padKeyLog.push([code, !!down]);
        return original(code, down);
      };
    });
    const waitForPadKey = async (code, down) => {
      await realPage.waitForFunction(
        ([wanted, wantDown]) => (window.__padKeyLog || [])
          .some((entry) => entry[0] === wanted && entry[1] === wantDown),
        [code, down],
        { timeout: 5000, polling: 50 }
      );
    };
    /* A -> FIRE, left stick right -> RIGHT, B -> GAME_A. */
    await realPage.evaluate(() => { window.__fakePad.buttons[0].pressed = true; });
    await waitForPadKey(17, true);
    await realPage.evaluate(() => { window.__fakePad.buttons[0].pressed = false; });
    await waitForPadKey(17, false);
    await realPage.evaluate(() => { window.__fakePad.axes[0] = 0.9; });
    await waitForPadKey(16, true);
    await realPage.evaluate(() => { window.__fakePad.axes[0] = 0; window.__fakePad.buttons[1].pressed = true; });
    await waitForPadKey(21, true);
    await realPage.evaluate(() => {
      window.__fakePad.buttons[1].pressed = false;
      window.dispatchEvent(new Event('gamepaddisconnected'));
      if (window.__realGetGamepads) {
        Object.defineProperty(navigator, 'getGamepads', { configurable: true, value: window.__realGetGamepads });
      }
      delete window.__fakePad;
    });
    await waitForPadKey(21, false);
    const downs = await realPage.evaluate(() => window.__padKeyLog
      .filter((entry) => entry[1]).map((entry) => entry[0]));
    for (const expected of [17, 16, 21]) {
      assert(downs.includes(expected), `missing pad key ${expected} in ${JSON.stringify(downs)}`);
    }
    const released = await realPage.evaluate(() => window.__padKeyLog
      .filter((entry) => !entry[1]).map((entry) => entry[0]));
    for (const expected of [17, 16, 21]) {
      assert(released.includes(expected), `missing pad release ${expected} in ${JSON.stringify(released)}`);
    }
    return `pad key downs ${JSON.stringify(downs)}, releases ${JSON.stringify(released)}`;
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

  await check('memory selector remembers the Java heap for the next launch', async () => {
    const panelOpen = await realPage.evaluate(() => document.querySelector('#game-panel').classList.contains('is-open'));
    if (!panelOpen) {
      await realPage.click('#panel-handle');
      await realPage.waitForSelector('#game-panel.is-open', { timeout: 5000 });
    }
    const before = await realPage.evaluate(() => {
      const raw = JSON.parse(localStorage.getItem('nj2me.settings') || '{}');
      return Number(raw.heapMb) || 16;
    });
    await realPage.selectOption('#heap-select', '64');
    await waitToast(realPage, '内存');
    const saved = await realPage.evaluate(() => {
      const raw = JSON.parse(localStorage.getItem('nj2me.settings') || '{}');
      return Number(raw.heapMb);
    });
    assert(saved === 64, `heapMb should persist as 64, got ${saved}`);
    assert((await realPage.inputValue('#heap-select')) === '64', 'selector keeps 64 MB');
    /* Back to the test default so the following checks start identically. */
    await realPage.selectOption('#heap-select', String(before));
    return `heapMb ${before} -> 64 with a toast, then back to ${before}`;
  });

  await check('diagnostics panel shows live engine counters', async () => {
    const panelOpen = await realPage.evaluate(
      () => document.querySelector('#game-panel').classList.contains('is-open'));
    if (!panelOpen) {
      await realPage.click('#panel-handle');
      await realPage.waitForSelector('#game-panel.is-open', { timeout: 5000 });
    }
    const read = () => realPage.evaluate(() => ({
      heap: (document.querySelector('#diag-heap') || {}).textContent || '',
      paints: (document.querySelector('#diag-paints') || {}).textContent || '',
      gc: (document.querySelector('#diag-gc') || {}).textContent || '',
      insns: (document.querySelector('#diag-insns') || {}).textContent || ''
    }));
    await realPage.waitForFunction(() => {
      const heap = document.querySelector('#diag-heap');
      return !!heap && /\d+\.\d \/ \d+\.\d MiB/.test(heap.textContent || '');
    }, undefined, { timeout: 8000, polling: 100 });
    /* The bytecode rate needs two samples 400 ms apart. */
    await realPage.waitForTimeout(700);
    const first = await read();
    assert(/^\d+\.\d \/ \d+\.\d MiB$/.test(first.heap.trim()),
      `heap text looks wrong: ${JSON.stringify(first.heap)}`);
    assert(/^\d+ \/ \d+$/.test(first.paints.trim()),
      `frames/paints text looks wrong: ${JSON.stringify(first.paints)}`);
    assert(/^\d+$/.test(first.gc.trim()), `gc text looks wrong: ${JSON.stringify(first.gc)}`);
    assert(/^\d+(\.\d+)? [MK]?\/s$/.test(first.insns.trim()),
      `bytecode rate text looks wrong: ${JSON.stringify(first.insns)}`);
    await realPage.waitForTimeout(700);
    const second = await read();
    const frameCount = (text) => Number((text.split('/')[0] || '').trim()) || 0;
    assert(frameCount(second.paints) > frameCount(first.paints),
      `panel frame counter did not advance (${first.paints} -> ${second.paints})`);
    return `heap ${first.heap.trim()}, ${second.paints.trim()} frames/paints, ` +
      `rate ${second.insns.trim()}`;
  });

  await check('screen size preset restarts the canvas at the chosen device size', async () => {
    /* Own context: the setting persists in localStorage, and a failed restore
       must not leak a smaller canvas into the PWA/offline checks. */
    const ctx = await browser.newContext();
    const page = await ctx.newPage();
    try {
      attachDiagnostics(page, 'screen');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined, { timeout: timeoutMs, polling: 100 });
      await page.click('.game-tile');
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 6, timeoutMs);

      const initial = await page.evaluate(() => {
        const canvas = document.querySelector('#game-canvas');
        return canvas ? [canvas.width, canvas.height] : [0, 0];
      });
      assert(initial[0] === 240 && initial[1] === 320,
        `default canvas should be 240x320, got ${initial[0]}x${initial[1]}`);

      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      await page.selectOption('#screen-select', '176x220');
      const library = await readLibrary(page);
      const saved = library.length === 1 ? library[0].screenPreset : null;
      assert(saved === '176x220',
        `screenPreset should be stored on the game record as 176x220, got ${JSON.stringify(saved)}`);

      /* Restarting also closes the panel, which is fine here: the assertion is
         about the canvas the engine hands back with the stored preset. */
      await page.click('#btn-restart');
      await page.waitForFunction(
        () => {
          const canvas = document.querySelector('#game-canvas');
          return !!canvas && canvas.width === 176 && canvas.height === 220;
        },
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 6, timeoutMs);

      /* Another title must keep the engine default: the preset lives on the
         game record, not in the shell-wide settings. */
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined, { timeout: timeoutMs });
      await page.setInputFiles('#file-input', GAME15_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 2,
        undefined, { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').filter({ hasNotText: 'Hello World' }).first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 4, timeoutMs);
      const other = await page.evaluate(() => {
        const canvas = document.querySelector('#game-canvas');
        return canvas ? [canvas.width, canvas.height] : [0, 0];
      });
      assert(other[0] === 240 && other[1] === 320,
        `the other title should stay 240x320, got ${other[0]}x${other[1]}`);

      /* Reopening the first title must restore its own preset. */
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined, { timeout: timeoutMs });
      await page.locator('.game-tile').filter({ hasText: 'Hello World' }).first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 4, timeoutMs);
      const again = await page.evaluate(() => {
        const canvas = document.querySelector('#game-canvas');
        return canvas ? [canvas.width, canvas.height] : [0, 0];
      });
      assert(again[0] === 176 && again[1] === 220,
        `the first title should start again at 176x220, got ${again[0]}x${again[1]}`);

      return `240x320 default, 176x220 for this title (record ${JSON.stringify(saved)}), other title 240x320, reopened 176x220`;
    } finally {
      await ctx.close().catch(() => {});
    }
  });

  await check('auto-resume continues from the newest save on the next launch', async () => {
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'resume');
      await page.goto(realServer.origin + '/');
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 90, timeoutMs);
      const gameId = (await readLibrary(page))[0].id;

      await page.click('#panel-handle');
      if (!(await page.evaluate(() => window.__nj2me.game._paused === true))) {
        await page.click('#btn-pause');
        await waitPaused(page, true);
      }
      await page.click('.slot[data-slot="2"] .slot__save');
      await waitToast(page, await t(page, 'game.saveOk', { n: 2 }));
      const states = await readStates(page, gameId);
      const slot = states.find((entry) => entry.slot === 2);
      assert(slot, `slot 2 missing: ${JSON.stringify(states)}`);
      const savedFrames = Number(slot.frames) || 0;
      assert(savedFrames > 10, `saved frame counter looks wrong: ${savedFrames}`);

      await page.check('#auto-resume');
      const stored = await page.evaluate(() => JSON.parse(localStorage.getItem('nj2me.settings') || '{}'));
      assert(stored.autoResume === true,
        `autoResume should persist as true, got ${JSON.stringify(stored.autoResume)}`);

      /* The panel scrim covers the chrome; close it before exiting. */
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: timeoutMs });
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      await page.reload();
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      /* The resume toast carries the slot and frame count the engine restored;
         its presence is proof that njw_load_state accepted the blob (a failed
         load has its own warning toast). */
      await waitToast(page, await t(page, 'game.autoResumed', { n: 2, frames: savedFrames }));
      await waitFrames(page, 24, timeoutMs);
      const resumed = await page.evaluate(() => {
        const canvas = document.querySelector('#game-canvas');
        return canvas ? [canvas.width, canvas.height] : [0, 0];
      });
      assert(resumed[0] === 240 && resumed[1] === 320,
        `unexpected canvas size after resume: ${resumed[0]}x${resumed[1]}`);
      return `save slot 2 at frame ${savedFrames}; after a reload the title auto-resumed from it and kept drawing`;
    } finally {
      await ctx.close();
    }
  });

  await check('auto-save on exit gives the next launch something to resume', async () => {
    /* Auto-save happens the moment the player leaves the game; auto-resume then
       picks the newest of the manual slots and this record.  Keeping the pair in
       an isolated context avoids dragging the preference into other checks. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'autosave');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 60, timeoutMs);
      await page.click('#panel-handle');
      await page.check('#auto-save');
      await page.check('#auto-resume');
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: timeoutMs, polling: 50 });
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      /* Leaving the game stores the state asynchronously; poll the record. */
      await page.waitForFunction(async () => {
        const saves = await import('/js/saves.js');
        const games = await saves.listGames();
        const record = games[0];
        return !!(record && record.autoState && (Number(record.autoState.frames) || 0) > 10);
      }, undefined, { timeout: timeoutMs, polling: 150 });
      const [record] = await readLibrary(page);
      assert(record.autoFrames && record.autoFrames > 10,
        `the exit auto-save should be stored on the game record, got ${JSON.stringify(record.autoFrames)}`);
      await page.reload();
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitToast(page, await t(page, 'game.autoResumedAuto', { frames: record.autoFrames }));
      await waitFrames(page, 12, timeoutMs);
      return `exit auto-save stored at frame ${record.autoFrames}; the reloaded launch resumed from it`;
    } finally {
      await ctx.close();
    }
  });

  await check('save slots can be named and the name survives a reload', async () => {
    /* The player can label a slot; the label lives in IndexedDB, drives the
       .njst export filename and must still be there after a page reload. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'slotname');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 20, timeoutMs);
      await page.click('#panel-handle');
      await page.click('.slot[data-slot="1"] .slot__save');
      await waitToast(page, await t(page, 'game.saveOk', { n: 1 }));
      const label = page.locator('.slot[data-slot="1"] .slot__label');
      await label.fill('Boss 战');
      await label.blur();
      await waitToast(page, await t(page, 'game.slotNameSaved'));
      const saved = await page.evaluate(async () => {
        const saves = await import('/js/saves.js');
        const games = await saves.listGames();
        const record = await saves.getState(games[0].id, 1);
        return record ? record.name : null;
      });
      assert(saved === 'Boss 战',
        `the state record should keep the slot name, got ${JSON.stringify(saved)}`);
      const downloads = path.join(evidenceDir, 'downloads');
      fs.mkdirSync(downloads, { recursive: true });
      const [download] = await Promise.all([
        page.waitForEvent('download', { timeout: 10000 }),
        page.click('.slot[data-slot="1"] .slot__export')
      ]);
      const filename = download.suggestedFilename();
      assert(/boss/.test(filename) && /slot1/.test(filename) && /\.njst$/.test(filename),
        `the export filename should use the slot name, got ${filename}`);
      await download.saveAs(path.join(downloads, filename));
      /* Reload the page: the name lives in IndexedDB, not in the DOM. */
      await page.reload({ waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 10, timeoutMs);
      await page.click('#panel-handle');
      await page.waitForFunction(
        () => !document.querySelector('.slot[data-slot="1"] .slot__label').disabled,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      const restored = await page.inputValue('.slot[data-slot="1"] .slot__label');
      assert(restored === 'Boss 战',
        `the slot name should come back after a reload, got ${JSON.stringify(restored)}`);
      return `slot name ${JSON.stringify(saved)} persisted; export used ${filename}`;
    } finally {
      await ctx.close();
    }
  });

  await check('the exit auto-save is visible in the panel and can be loaded or cleared', async () => {
    /* Save on exit writes a snapshot on the game record; the panel resurfaces
       it as a read-only row next to the manual slots so the player can see when
       it was taken, load it on purpose, or drop it. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'autoslot');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 30, timeoutMs);
      await page.click('#panel-handle');
      await page.check('#auto-save');
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: timeoutMs, polling: 50 });
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      /* Start the title again: the panel now shows the snapshot the exit wrote. */
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 12, timeoutMs);
      await page.click('#panel-handle');
      await page.waitForFunction(
        () => !document.querySelector('.slot--auto').hidden,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      const meta = await page.textContent('.slot--auto .slot__time');
      assert(/帧|frame/.test(meta || ''),
        `auto-save row should show its frame count, got ${JSON.stringify(meta)}`);
      await page.click('.slot--auto .slot__load');
      await waitToast(page, await t(page, 'game.loadOkAuto'));
      await page.click('.slot--auto .slot__clear');
      await waitToast(page, await t(page, 'game.autoSaveCleared'));
      await page.waitForFunction(
        () => document.querySelector('.slot--auto').hidden,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      const after = await page.evaluate(async () => {
        const saves = await import('/js/saves.js');
        const games = await saves.listGames();
        return games[0] && games[0].autoState ? 'still-there' : 'gone';
      });
      assert(after === 'gone',
        `clearing should drop the auto-save from the game record, got ${after}`);
      return `auto-save row showed ${JSON.stringify(meta)}; load and clear both worked, record entry removed`;
    } finally {
      await ctx.close();
    }
  });

  await check('library badges known titles from the compatibility report', async () => {
    /* web/data/compat.json ships the results of the 403-title audit: an
       audited title gets a "verified" badge, a known-tricky one gets a warning
       with the reason in its tooltip. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'compat');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', [HELLO_JAR, HEAVY_JAR]);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 2,
        undefined,
        { timeout: 120000, polling: 100 });
      const hello = page.locator('.game-tile').filter({ hasText: 'Hello World' });
      const okBadge = hello.locator('.game-tile__compat');
      assert(await okBadge.count() === 1, 'hello.jar should carry one compatibility badge');
      assert(await okBadge.evaluate((node) => node.classList.contains('is-ok')),
        'the audited title should be marked as verified');
      const okText = (await okBadge.textContent()) || '';
      assert(/已验证|Verified/.test(okText), `unexpected verified badge text: ${okText}`);
      const angry = page.locator('.game-tile').filter({ hasText: 'Angry Birds' });
      const warnBadge = angry.locator('.game-tile__compat');
      assert(await warnBadge.count() === 1, 'Angry Birds should carry one compatibility badge');
      assert(await warnBadge.evaluate((node) => node.classList.contains('is-warn')),
        'the failing title should be marked with a warning');
      const warnText = (await warnBadge.textContent()) || '';
      assert(/需更大内存|Needs more heap/.test(warnText),
        `unexpected warning badge text: ${warnText}`);
      const note = await warnBadge.getAttribute('title');
      assert(/内存|heap/.test(note || ''),
        `the badge tooltip should explain the heap issue, got ${JSON.stringify(note)}`);
      /* The warning badge is a button for touch screens: tapping it explains
         the audit result without leaving the library. */
      await warnBadge.click();
      await page.waitForFunction(
        () => {
          const toast = document.querySelector('.toast');
          return Boolean(toast && /内存|heap/i.test(toast.textContent || ''));
        },
        undefined,
        { timeout: 5000, polling: 100 });
      assert(await page.locator('#library-view').isVisible(),
        'tapping the badge should keep the library visible');
      return 'verified badge on hello.jar, heap warning on 愤怒的小鸟_383.jar, tap explains it';
    } finally {
      await ctx.close();
    }
  });

  await check('library offers a continue card for the last played game', async () => {
    /* The card is the quick way back into the newest game; it appears only on
       the unfiltered library and its meta line reflects the exit auto-save. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'continue');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 30, timeoutMs);
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      await page.waitForFunction(
        () => !document.querySelector('#continue-card').hidden,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      const label = await page.textContent('#continue-title');
      assert(/Hello World/.test(label || ''),
        `continue card should name the last game, got ${JSON.stringify(label)}`);
      await page.fill('#search-input', 'zzz');
      await page.waitForFunction(
        () => document.querySelector('#continue-card').hidden,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.fill('#search-input', '');
      await page.waitForFunction(
        () => !document.querySelector('#continue-card').hidden,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.click('#continue-card');
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 12, timeoutMs);
      return `continue card named ${JSON.stringify(label)} after play; hidden by search; clicking it started the title again`;
    } finally {
      await ctx.close();
    }
  });

  await check('a second tab cannot run the engine while another tab owns it', async () => {
    /* Two pages in one context share IndexedDB and the BroadcastChannel, so
       this is exactly the user situation: a second tab opened while a game is
       already running somewhere else in the same profile. */
    const ctx = await browser.newContext();
    const first = await ctx.newPage();
    const second = await ctx.newPage();
    try {
      attachDiagnostics(first, 'tabs-first');
      attachDiagnostics(second, 'tabs-second');

      await first.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(first);
      await first.setInputFiles('#file-input', HELLO_JAR);
      await first.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined, { timeout: timeoutMs, polling: 100 });
      await first.click('.game-tile');
      await first.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(first, 6, timeoutMs);

      await second.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(second);
      await second.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined, { timeout: timeoutMs, polling: 100 });
      await second.click('.game-tile');
      await second.waitForFunction(
        () => Array.from(document.querySelectorAll('#toast-container .toast'))
          .some((node) => /另一个标签页|Another tab/.test(node.textContent || '')),
        undefined, { timeout: 8000, polling: 100 });
      const blocked = await second.evaluate(() => ({
        library: !document.querySelector('#library-view').hidden,
        game: !!(window.__nj2me.game && window.__nj2me.game.game),
      }));
      assert(blocked.library && !blocked.game,
        `second tab started a game anyway: ${JSON.stringify(blocked)}`);

      /* Exiting the first tab frees the lease and the second tab may start. */
      await first.click('#game-back');
      await first.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined, { timeout: timeoutMs, polling: 100 });
      await second.click('.game-tile');
      await second.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined, { timeout: timeoutMs, polling: 100 });
      await waitFrames(second, 6, timeoutMs);
      return 'tab 2 blocked while tab 1 played; allowed after tab 1 exited';
    } finally {
      await ctx.close();
    }
  });

  await check('volume slider drives the WebAudio gain and persists it', async () => {
    const panelOpen = await realPage.evaluate(() => document.querySelector('#game-panel').classList.contains('is-open'));
    if (!panelOpen) {
      await realPage.click('#panel-handle');
      await realPage.waitForSelector('#game-panel.is-open', { timeout: 5000 });
    }
    const reader = () => realPage.evaluate(() => ({
      stored: Number(JSON.parse(localStorage.getItem('nj2me.settings') || '{}').volume),
      output: (document.querySelector('#volume-value') || {}).textContent || '',
      gain: (window.__nj2me.game && window.__nj2me.game.audio) ? window.__nj2me.game.audio.volume : -1,
      muted: !!(window.__nj2me.game && window.__nj2me.game.settings.muted),
      slider: document.querySelector('#volume-slider') ? document.querySelector('#volume-slider').value : '',
    }));
    const before = await reader();
    const setSlider = (value) => realPage.evaluate((percent) => {
      const slider = document.querySelector('#volume-slider');
      slider.value = String(percent);
      slider.dispatchEvent(new Event('input', { bubbles: true }));
    }, value);

    await setSlider(40);
    await realPage.waitForFunction(
      () => Math.abs(Number(JSON.parse(localStorage.getItem('nj2me.settings') || '{}').volume) - 0.4) < 0.001,
      undefined, { timeout: 5000, polling: 100 });
    const at40 = await reader();
    assert(at40.output.trim() === '40%', `volume label is ${JSON.stringify(at40.output)}`);
    assert(Math.abs(at40.gain - 0.4) < 0.001, `WebAudio gain is ${at40.gain}`);
    assert(at40.slider === '40', `slider value is ${at40.slider}`);

    /* Muting then touching the slider unmutes, like a phone volume key. */
    await realPage.click('#btn-mute');
    await realPage.waitForFunction(
      () => !!(window.__nj2me.game && window.__nj2me.game.settings.muted),
      undefined, { timeout: 5000, polling: 100 });
    await setSlider(55);
    const at55 = await reader();
    assert(at55.muted === false, 'dragging the volume slider should unmute');
    assert(at55.output.trim() === '55%', `volume label after unmute is ${JSON.stringify(at55.output)}`);
    assert(Math.abs(at55.gain - 0.55) < 0.001, `gain after unmute is ${at55.gain}`);

    /* Put the test default back so later checks start identically. */
    await setSlider(Math.round((before.stored || 0.8) * 100));
    return `volume ${before.slider}% -> 40% (gain 0.4) -> 55% after mute-touch, restored`;
  });

  await check('frame-rate cap follows the chosen rate and persists', async () => {
    /* The shell pumps one emulated frame per display refresh by default; the cap
       keeps a title at the rate it was written for even on 120 Hz panels.  An
       isolated context keeps the preference away from the other checks. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'fps');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await waitFrames(page, 20, timeoutMs);
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      /* Opening the tools panel auto-pauses the shell; resume before sampling. */
      if (await page.evaluate(() => window.__nj2me.game._paused === true)) {
        await page.click('#btn-pause');
        await waitPaused(page, false);
      }

      const sample = async (ms) => {
        const from = await frames(page);
        const startedAt = Date.now();
        await page.waitForTimeout(ms);
        const to = await frames(page);
        const seconds = (Date.now() - startedAt) / 1000;
        return { fps: (to - from) / seconds, pumped: to - from };
      };

      /* 30 fps is the default: the frame rate a J2ME-era handset serviced (a
         title written for 15/20/30 fps must not run at the browser's 60 Hz). */
      const defaultValue = await page.inputValue('#fps-select');
      assert(defaultValue === '30', `default frame cap is ${defaultValue}`);
      const at30 = await sample(1500);
      assert(at30.fps > 20 && at30.fps < 45, `30 fps cap pumped ${at30.fps.toFixed(1)} fps`);

      await page.selectOption('#fps-select', '60');
      await waitToast(page, await t(page, 'game.fpsApplied'));
      const at60 = await sample(1200);
      assert(at60.fps > 35 && at60.fps < 90, `60 fps cap pumped ${at60.fps.toFixed(1)} fps`);

      await page.selectOption('#fps-select', '20');
      await waitToast(page, await t(page, 'game.fpsApplied'));
      const at20 = await sample(1500);
      assert(at20.fps > 12 && at20.fps < 30, `20 fps cap pumped ${at20.fps.toFixed(1)} fps`);

      await page.selectOption('#fps-select', '15');
      const at15 = await sample(1500);
      assert(at15.fps > 8 && at15.fps < 24, `15 fps cap pumped ${at15.fps.toFixed(1)} fps`);

      await page.selectOption('#fps-select', '0');
      const atDisplay = await sample(1200);
      /* Headless Firefox can throttle requestAnimationFrame, so "uncapped" only
         has to be clearly above the lowest cap, not a fixed 60. */
      assert(atDisplay.fps > 15 && atDisplay.fps > at15.fps * 1.3,
        `uncapped mode pumped ${atDisplay.fps.toFixed(1)} fps (15 fps cap: ${at15.fps.toFixed(1)})`);

      /* The cap belongs to the game record: pick 30 fps again, leave, reload
         and start the same title — the select and the pump rate both remember. */
      await page.selectOption('#fps-select', '30');
      await waitToast(page, await t(page, 'game.fpsApplied'));
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: timeoutMs, polling: 50 });
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      const [record] = await readLibrary(page);
      assert(record.frameCap === 30, `game record frame cap is ${JSON.stringify(record.frameCap)}`);

      await page.reload();
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      if (await page.evaluate(() => window.__nj2me.game._paused === true)) {
        await page.click('#btn-pause');
        await waitPaused(page, false);
      }
      const remembered = await page.inputValue('#fps-select');
      assert(remembered === '30', `remembered frame cap is ${remembered}`);
      const atReload = await sample(1500);
      assert(atReload.fps > 20 && atReload.fps < 45,
        `remembered 30 fps cap pumped ${atReload.fps.toFixed(1)} fps`);
      return `default 30 -> ${at30.fps.toFixed(0)}, 60 -> ${at60.fps.toFixed(0)}, 20 -> ${at20.fps.toFixed(0)}, 15 -> ${at15.fps.toFixed(0)}, display -> ${atDisplay.fps.toFixed(0)}; per-game 30 -> ${atReload.fps.toFixed(0)} after reload`;
    } finally {
      await ctx.close();
    }
  });

  await check('game speed is remembered per title', async () => {
    /* Speed is a per-title flavour like the frame cap: the shell setting only
       seeds new titles, while an existing game keeps the speed it was given. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'speed');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });

      const engineSpeed = () => page.evaluate(() => window.__nj2me.game.runtime.speed());
      const initial = await page.inputValue('#speed-select');
      assert(initial === '100', `default game speed is ${initial}`);
      assert((await engineSpeed()) === 100, 'engine starts at 100 %');

      await page.selectOption('#speed-select', '200');
      await waitToast(page, await t(page, 'game.speedApplied'));
      assert((await engineSpeed()) === 200, `engine speed after 200 % is ${await engineSpeed()}`);

      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: timeoutMs, polling: 50 });
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      const [record] = await readLibrary(page);
      assert(record.speedPercent === 200, `game record speed is ${JSON.stringify(record.speedPercent)}`);

      await page.reload();
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      const remembered = await page.inputValue('#speed-select');
      assert(remembered === '200', `remembered game speed is ${remembered}`);
      assert((await engineSpeed()) === 200, 'engine speed after reload is not 200 %');
      return '100 % -> 200 % for hello.jar; the reloaded launch reports 200 %';
    } finally {
      await ctx.close();
    }
  });

  await check('custom key bindings take effect and are saved per game', async () => {
    /* The tools panel can rebind D-pad / fire / soft keys / clear per game.
       Capture key presses by wrapping the runtime key sink, so the assertion
       covers the shell -> input -> engine path rather than the dialog only. */
    const ctx = await browser.newContext({ viewport: { width: 960, height: 840 }, locale: 'zh-CN' });
    try {
      const page = await ctx.newPage();
      attachDiagnostics(page, 'keymap');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HELLO_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });

      const installKeyProbe = () => page.evaluate(() => {
        const view = window.__nj2me.game;
        if (view.__keyProbeInstalled) { view.__keyLog = []; return; }
        view.__keyProbeInstalled = true;
        view.__keyLog = [];
        const original = view.runtime.key.bind(view.runtime);
        view.runtime.key = (key, down) => {
          view.__keyLog.push([key, down]);
          return original(key, down);
        };
      });
      const clearKeyLog = () => page.evaluate(() => { window.__nj2me.game.__keyLog = []; });
      const keyLog = () => page.evaluate(() => window.__nj2me.game.__keyLog);
      await installKeyProbe();

      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      await page.click('#open-keymap');
      await page.waitForSelector('#keymap-dialog[open]', { timeout: 5000 });
      // The dialog lists the actions in KEY_ACTIONS order: fire is the 5th.
      await page.click('#keymap-list .keymap__row:nth-child(5) .keymap__key');
      await page.keyboard.press('KeyZ');
      const fireLabel = await page.evaluate(
        () => document.querySelector('#keymap-list .keymap__row:nth-child(5) .keymap__key').textContent.trim());
      assert(fireLabel === 'Z', `fire row shows ${JSON.stringify(fireLabel)} after rebinding to Z`);
      await page.click('#keymap-done');
      await page.waitForFunction(
        () => !document.querySelector('#keymap-dialog').open,
        undefined,
        { timeout: 5000 });
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: 5000, polling: 50 });

      await clearKeyLog();
      await page.keyboard.press('KeyZ');
      let entries = await keyLog();
      assert(entries.some(([key, down]) => key === 17 && down === true), `Z presses fire: ${JSON.stringify(entries)}`);
      assert(entries.some(([key, down]) => key === 17 && down === false), `Z releases fire: ${JSON.stringify(entries)}`);
      await clearKeyLog();
      await page.keyboard.press('Space');
      entries = await keyLog();
      assert(!entries.some(([key]) => key === 17), `Space no longer fires: ${JSON.stringify(entries)}`);

      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        undefined,
        { timeout: timeoutMs });
      let [record] = await readLibrary(page);
      assert(record.keyMap && record.keyMap.fire === 'KeyZ',
        `game record keyMap is ${JSON.stringify(record.keyMap)}`);

      await page.reload();
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length >= 1,
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await page.locator('.game-tile').first().click();
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 });
      await installKeyProbe();
      await clearKeyLog();
      await page.keyboard.press('KeyZ');
      entries = await keyLog();
      assert(entries.some(([key, down]) => key === 17 && down === true),
        `reloaded launch still fires with Z: ${JSON.stringify(entries)}`);

      // Reset through the same dialog and make sure the default comes back.
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });
      await page.click('#open-keymap');
      await page.waitForSelector('#keymap-dialog[open]', { timeout: 5000 });
      await page.click('#keymap-reset');
      await page.waitForSelector('#keymap-dialog[open]', { timeout: 5000 });
      await page.click('#keymap-done');
      await page.waitForFunction(
        () => !document.querySelector('#keymap-dialog').open,
        undefined,
        { timeout: 5000 });
      await page.click('#panel-close');
      await page.waitForFunction(
        () => document.querySelector('#game-panel').getAttribute('aria-hidden') === 'true',
        undefined,
        { timeout: 5000, polling: 50 });
      await clearKeyLog();
      await page.keyboard.press('Space');
      entries = await keyLog();
      assert(entries.some(([key, down]) => key === 17 && down === true),
        `Space fires again after resetting the bindings: ${JSON.stringify(entries)}`);
      return 'Z fires after rebinding, Space does not, the record keeps keyMap.fire, reload keeps it, reset restores Space';
    } finally {
      await ctx.close();
    }
  });

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

  await check('save slot shows a thumbnail preview', async () => {
    const probe = () => realPage.evaluate(() => {
      const img = document.querySelector('.slot[data-slot="1"] .slot__thumb');
      if (!img) { return null; }
      return {
        src: img.getAttribute('src') || '',
        hidden: img.hidden === true,
        complete: img.complete,
        width: img.naturalWidth,
        height: img.naturalHeight
      };
    });
    const shot = await probe();
    assert(shot, 'slot 1 has no .slot__thumb element');
    assert(!shot.hidden, 'saved slot 1 hides its thumbnail');
    assert(shot.src.startsWith('data:image/png;base64,') || shot.src.startsWith('data:image/jpeg;base64,'),
      `thumbnail is not an embedded image: ${JSON.stringify(String(shot.src).slice(0, 24))}`);
    assert(shot.src.length > 500, `thumbnail data URL is only ${shot.src.length} chars`);
    await realPage.waitForFunction(() => {
      const img = document.querySelector('.slot[data-slot="1"] .slot__thumb');
      return !!img && img.complete && img.naturalWidth > 0;
    }, undefined, { timeout: 5000, polling: 100 });
    const size = await probe();
    /* The capture down-scales the 240x320 canvas to 96x128 (same 3:4 aspect). */
    assert(size.width === 96 && size.height === 128,
      `thumbnail decodes as ${size.width}x${size.height}, expected 96x128`);
    const empty = await realPage.evaluate(() => {
      const img = document.querySelector('.slot[data-slot="2"] .slot__thumb');
      return { present: !!img, hidden: !!img && img.hidden === true };
    });
    assert(empty.present && empty.hidden, 'empty slot 2 shows a thumbnail');
    return `96x128 PNG preview (${shot.src.length} chars), empty slot hidden`;
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

  await check('save states export to .njst and import back into another slot', async () => {
    const before = await readStates(realPage, gameId);
    const slot1 = before.find((entry) => entry.slot === 1);
    assert(slot1 && slot1.size > 1000, `slot 1 missing before export: ${JSON.stringify(before)}`);
    const downloads = path.join(evidenceDir, 'downloads');
    fs.mkdirSync(downloads, { recursive: true });
    const [download] = await Promise.all([
      realPage.waitForEvent('download', { timeout: 10000 }),
      realPage.click('.slot[data-slot="1"] .slot__export')
    ]);
    const suggested = download.suggestedFilename();
    assert(/\.njst$/.test(suggested), `unexpected export filename: ${suggested}`);
    const savedPath = path.join(downloads, suggested);
    await download.saveAs(savedPath);
    const bytes = fs.readFileSync(savedPath);
    assert(bytes.length === slot1.size,
      `exported ${bytes.length} B but the slot holds ${slot1.size} B`);
    assert(bytes.subarray(0, 4).toString('latin1') === 'NJSS', 'export does not start with the save-state magic');
    if (!(await realPage.evaluate(() => document.querySelector('#game-panel').classList.contains('is-open')))) {
      await realPage.click('#panel-handle');
      await realPage.waitForSelector('#game-panel.is-open', { timeout: 5000 });
    }
    const [chooser] = await Promise.all([
      realPage.waitForEvent('filechooser', { timeout: 10000 }),
      realPage.click('.slot[data-slot="2"] .slot__import')
    ]);
    await chooser.setFiles(savedPath);
    const importToast = await t(realPage, 'game.importOk', { n: 2 });
    await waitToast(realPage, importToast);
    await realPage.waitForFunction(
      () => {
        const row = document.querySelector('.slot[data-slot="2"]');
        return !!row && row.dataset.filled === '1';
      },
      undefined,
      { timeout: 8000, polling: 100 }
    );
    const after = await readStates(realPage, gameId);
    const slot2 = after.find((entry) => entry.slot === 2);
    assert(slot2, 'slot 2 did not receive the imported state');
    assert(slot2.size === slot1.size,
      `imported ${slot2.size} B, expected ${slot1.size} B`);
    return `exported ${suggested} (${bytes.length} B), imported into slot 2`;
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

  await check('multi-MIDlet JAR: startup selector switches the running MIDlet', async () => {
    /* Isolated context: fresh IndexedDB, so the single-game expectations of
       the checks around this one are not disturbed. */
    const book6 = path.join(REPO_ROOT, 'tests', 'games', 'book6.jar');
    const ctx = await browser.newContext();
    const page = await ctx.newPage();
    try {
      attachDiagnostics(page, 'midlet');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', book6);
      await page.waitForFunction(
        () => [...document.querySelectorAll('.game-tile__title')]
          .some((node) => node.textContent.trim() === 'Dungeon'),
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      await page.click('.game-tile:has-text("Dungeon")');
      await page.waitForFunction(
        () => !!(window.__nj2me.game && window.__nj2me.game.game),
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      await waitFrames(page, 12, timeoutMs);
      await page.click('#panel-handle');
      await page.waitForSelector('#game-panel.is-open', { timeout: 5000 });

      const selector = await page.evaluate(() => {
        const field = document.querySelector('#midlet-field');
        const select = document.querySelector('#midlet-select');
        return {
          visible: !!field && !field.hidden,
          options: select ? [...select.options].map((option) => option.textContent.trim()) : [],
          value: select ? select.value : '',
        };
      });
      assert(selector.visible, 'MIDlet selector is hidden for a 3-MIDlet JAR');
      assert(selector.options.join(',') === 'Dungeon,Checkers,Messenger',
        `probed MIDlet labels: ${JSON.stringify(selector.options)}`);

      const digest = () => page.evaluate(() => {
        const canvas = document.querySelector('#game-canvas');
        const data = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
        let hash = 2166136261 >>> 0;
        for (let i = 0; i < data.length; i += 97) {
          hash ^= data[i];
          hash = Math.imul(hash, 16777619) >>> 0;
        }
        return hash;
      });

      const before = await digest();
      await page.selectOption('#midlet-select', '1');
      await waitToast(page, 'MIDlet');
      await page.click('#btn-restart');
      await waitFrames(page, 12, timeoutMs);
      await page.waitForFunction(
        () => document.querySelector('#loading-overlay').hidden === true,
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      const after = await digest();
      assert(after !== before, `switching the MIDlet did not change the picture (0x${after.toString(16)})`);

      const stored = await page.evaluate(async () => {
        const saves = await import('/js/saves.js');
        const games = await saves.listGames();
        return games.map((game) => ({ title: game.title, midletIndex: game.midletIndex }));
      });
      assert(stored.length === 1 && stored[0].midletIndex === 1,
        `MIDlet choice not persisted: ${JSON.stringify(stored)}`);
      return `Dungeon 0x${before.toString(16)} -> Checkers 0x${after.toString(16)}, persisted`;
    } finally {
      await ctx.close();
    }
  });

  await check('out-of-memory error offers a bigger heap and recovers', async () => {
    const ctx = await browser.newContext();
    const page = await ctx.newPage();
    try {
      attachDiagnostics(page, 'oom');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', HEAVY_JAR);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined,
        { timeout: 120000, polling: 100 }
      );
      await page.click('.game-tile');
      await page.waitForSelector('#error-overlay:not([hidden])', { timeout: timeoutMs });
      const failure = await page.evaluate(() => ({
        message: (document.querySelector('#error-message') || {}).textContent || '',
        heapHidden: document.querySelector('#error-heap').hidden,
        heapLabel: (document.querySelector('#error-heap') || {}).textContent || '',
        heapMb: Number(document.querySelector('#heap-select').value) || 0,
      }));
      assert(/OutOfMemory|out of memory|内存/i.test(failure.message),
        `expected an out-of-memory failure, got ${JSON.stringify(failure.message.slice(0, 200))}`);
      assert(!failure.heapHidden, 'the larger-heap restart button is hidden after an out-of-memory failure');
      assert(/64 MB/.test(failure.heapLabel), `suggested heap label is ${JSON.stringify(failure.heapLabel)}`);
      assert(failure.heapMb === 16, `default heap before the retry is ${failure.heapMb}`);
      const before = await frames(page);
      await page.click('#error-heap');
      await page.waitForFunction(
        () => {
          const game = window.__nj2me && window.__nj2me.game;
          return !!game && !!game.game && !game._paused
            && document.querySelector('#error-overlay').hidden === true;
        },
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      await waitFrames(page, Math.max(12, before + 12), timeoutMs);
      const recovered = await page.evaluate(() => ({
        heapMb: Number((window.__nj2me.game.settings || {}).heapMb) || 0,
        storedHeapMb: Number(JSON.parse(localStorage.getItem('nj2me.settings') || '{}').heapMb) || 0,
      }));
      assert(recovered.heapMb === 64, `heap after the retry is ${recovered.heapMb}`);
      assert(recovered.storedHeapMb === 64, `persisted heap after the retry is ${recovered.storedHeapMb}`);
      return `out of memory at 16 MB, retried at ${recovered.heapMb} MB and kept drawing`;
    } finally {
      await ctx.close();
    }
  });

  await check('favourites can be starred, filtered and survive a reload', async () => {
    const ctx = await browser.newContext();
    const page = await ctx.newPage();
    try {
      attachDiagnostics(page, 'favourites');
      await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.setInputFiles('#file-input', [HELLO_JAR, GAME15_JAR]);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 2,
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      await page.click('.game-tile:has-text("Hello World") .game-tile__star');
      const starred = await page.evaluate(() => {
        const tiles = [...document.querySelectorAll('.game-tile')];
        const tile = tiles.find((node) => (node.querySelector('.game-tile__title') || {}).textContent?.trim() === 'Hello World');
        const star = tile ? tile.querySelector('.game-tile__star') : null;
        return {
          favourite: !!tile && tile.classList.contains('is-favourite'),
          pressed: star ? star.getAttribute('aria-pressed') : null,
          glyph: star ? star.textContent.trim() : null,
        };
      });
      assert(starred.favourite && starred.pressed === 'true',
        `the star did not stick: ${JSON.stringify(starred)}`);
      await page.click('#favourites-toggle');
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      const filtered = await page.evaluate(() => ({
        title: (document.querySelector('.game-tile__title') || {}).textContent.trim(),
        pressed: document.querySelector('#favourites-toggle').getAttribute('aria-pressed'),
        count: (document.querySelector('#library-count') || {}).textContent.trim(),
      }));
      assert(filtered.title === 'Hello World' && filtered.pressed === 'true',
        `filtered view: ${JSON.stringify(filtered)}`);
      assert(/1/.test(filtered.count) && /2/.test(filtered.count),
        `filtered count does not mention 1 of 2: ${JSON.stringify(filtered.count)}`);
      await page.reload({ waitUntil: 'load', timeout: timeoutMs });
      await waitReady(page);
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 2,
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      const afterReload = await page.evaluate(() => {
        const tiles = [...document.querySelectorAll('.game-tile')];
        const tile = tiles.find((node) => (node.querySelector('.game-tile__title') || {}).textContent?.trim() === 'Hello World');
        return { favourite: !!tile && tile.classList.contains('is-favourite'), total: tiles.length };
      });
      assert(afterReload.favourite && afterReload.total === 2,
        `the star did not survive a reload: ${JSON.stringify(afterReload)}`);
      await page.click('#favourites-toggle');
      await page.waitForFunction(
        () => document.querySelectorAll('.game-tile').length === 1,
        undefined,
        { timeout: timeoutMs, polling: 100 }
      );
      await page.click('.game-tile .game-tile__star');
      await page.waitForSelector('#library-empty:not([hidden])', { timeout: timeoutMs });
      const emptyText = await page.evaluate(() =>
        (document.querySelector('#library-empty-title') || {}).textContent.trim());
      assert(/收藏|favourite/i.test(emptyText),
        `unexpected empty state: ${JSON.stringify(emptyText)}`);
      return `starred, filtered ${JSON.stringify(filtered.count)}, survived reload, empty state ${JSON.stringify(emptyText)}`;
    } finally {
      await ctx.close();
    }
  });

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

/* --------------------------------------------------------------- PWA/offline */

async function runPwaChecks() {
  const page = await context.newPage();
  attachDiagnostics(page, 'pwa');

  await check('PWA manifest is linked and parseable', async () => {
    await page.goto(realServer.origin + '/', { waitUntil: 'load', timeout: timeoutMs });
    await waitReady(page);
    const info = await page.evaluate(async () => {
      const link = document.querySelector('link[rel="manifest"]');
      const href = link ? link.href : '';
      const response = href ? await fetch(href, { cache: 'no-store' }) : null;
      const manifest = response && response.ok ? await response.json() : null;
      return { href, ok: !!(response && response.ok), manifest };
    });
    assert(info.href && /\/manifest\.json$/.test(info.href),
      `manifest link looks wrong: ${JSON.stringify(info.href)}`);
    assert(info.ok && info.manifest, 'manifest.json could not be fetched/parsed');
    const manifest = info.manifest;
    assert(manifest.display === 'standalone',
      `manifest display expected "standalone", got ${JSON.stringify(manifest.display)}`);
    const icons = Array.isArray(manifest.icons) ? manifest.icons : [];
    assert(icons.length >= 3, `manifest icons expected >= 3, got ${icons.length}`);
    assert(icons.some((icon) => /maskable/.test(icon.purpose || '')),
      'manifest has no maskable icon');
    return `display=${manifest.display}, name=${JSON.stringify(manifest.name)}, icons=${icons.length}`;
  });

  await check('service worker controls the page and caches the engine', async () => {
    await page.waitForFunction(
      () => !!(navigator.serviceWorker && navigator.serviceWorker.controller),
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    const info = await page.evaluate(async () => {
      const registration = await navigator.serviceWorker.ready;
      const keys = await caches.keys();
      const cacheName = keys.find((key) => key.startsWith('nj2me-shell-'));
      const cached = [];
      if (cacheName) {
        const cache = await caches.open(cacheName);
        cached.push(...(await cache.keys()).map((request) => request.url));
      }
      return {
        controller: navigator.serviceWorker.controller
          ? navigator.serviceWorker.controller.scriptURL : '',
        registrationActive: !!(registration && registration.active),
        cacheName: cacheName || '',
        cachedCount: cached.length,
        hasModule: cached.some((url) => /\/js\/nj2me\.js$/.test(url)),
        hasWasm: cached.some((url) => /\/js\/nj2me\.wasm$/.test(url))
      };
    });
    assert(/\/sw\.js$/.test(info.controller),
      `page is not controlled by sw.js: ${JSON.stringify(info.controller)}`);
    assert(info.registrationActive, 'service worker registration is not active');
    assert(info.cacheName, 'no nj2me-shell-* cache exists');
    assert(info.hasModule && info.hasWasm,
      `engine missing from the cache: ${JSON.stringify(info)}`);
    return `${info.cacheName}, ${info.cachedCount} entries, js+wasm cached`;
  });

  await check('library footer reports the shell as offline-ready', async () => {
    /* main.js probes the service-worker cache after registration and sets
     * document.body[data-offline]; the library footer repeats it so a player
     * can tell the page will still open without a network. */
    await page.waitForFunction(
      () => document.body.dataset.offline === 'ready',
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    const info = await page.evaluate(() => {
      const el = document.querySelector('#library-engine');
      return {
        text: el ? el.textContent.trim() : '',
        offline: el ? el.dataset.offline || '' : '',
        body: document.body.dataset.offline || ''
      };
    });
    assert(info.body === 'ready', `body[data-offline] expected ready, got ${JSON.stringify(info.body)}`);
    assert(info.offline === '1', `#library-engine[data-offline] expected 1, got ${JSON.stringify(info.offline)}`);
    assert(/离线可用|offline ready/i.test(info.text),
      `library footer does not mention offline readiness: ${JSON.stringify(info.text)}`);
    return info.text;
  });

  await check('offline reload boots the shell with the WASM engine', async () => {
    /* Playwright's Firefox offline emulation is not service-worker aware, so
     * true offline is simulated by stopping the static server instead. */
    await realServer.close();
    await page.reload({ waitUntil: 'load', timeout: timeoutMs });
    await waitReady(page);
    const info = await page.evaluate(() => ({
      kind: window.__nj2me.runtime.kind,
      mock: window.__nj2me.runtime.mock,
      engine: document.body.dataset.engine
    }));
    assert(info.kind === 'wasm' && info.mock === false,
      `offline reload fell back: ${JSON.stringify(info)}`);
    assert(info.engine === 'wasm', `body[data-engine] expected wasm, got ${info.engine}`);
    await screenshot(page, 'browser-09-offline.png');
    return 'server stopped, shell + engine served from the service-worker cache';
  }, { fatal: true });

  await check('offline: an imported game starts and keeps drawing', async () => {
    /* The library, the engine and the service worker cache are all local, so
     * after the server is stopped a tile from the earlier import must still
     * open and produce frames. */
    const tiles = await page.$$('.game-tile');
    assert(tiles.length >= 1, 'library is empty after the offline reload');
    await page.click('.game-tile');
    await page.waitForFunction(
      () => !!(window.__nj2me.game && window.__nj2me.game.game),
      undefined,
      { timeout: timeoutMs, polling: 100 }
    );
    await waitFrames(page, 12);
    const stats = await canvasStats(page);
    assert(stats.width === 240 && stats.height === 320,
      `unexpected canvas size: ${stats.width}x${stats.height}`);
    assert(stats.nonBlack > 1000,
      `canvas looks blank offline: ${stats.nonBlack} non-black pixels`);
    const before = await frames(page);
    await page.waitForTimeout(400);
    const after = await frames(page);
    assert(after > before, `frames did not advance offline: ${before} -> ${after}`);
    await screenshot(page, 'browser-10-offline-game.png');
    return `${stats.nonBlack} non-black pixels, ${before} -> ${after} frames offline`;
  });

  await page.close();
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

async function runUrlImportChecks() {
  const jarServer = await startJarServer(URL_IMPORT_JAR);
  results.servers.urlJar = jarServer.origin;
  const jarUrl = `${jarServer.origin}/hello.jar`;
  const ctx = await browser.newContext({
    viewport: { width: 960, height: 840 },
    locale: 'zh-CN',
    serviceWorkers: 'block'
  });
  const page = await ctx.newPage();
  attachDiagnostics(page, 'url');
  try {
    await page.goto(realServer.origin + '/', { waitUntil: 'domcontentloaded' });
    await waitReady(page);

    await check('URL import downloads a JAR from an http link', async () => {
      await page.click('#url-import-btn');
      await page.waitForSelector('#url-dialog:not([hidden])');
      await page.fill('#url-input', jarUrl);
      await page.click('#url-submit');
      await page.waitForSelector('#url-dialog', { state: 'hidden', timeout: timeoutMs });
      await page.waitForSelector('.game-tile', { timeout: timeoutMs });
      const title = ((await page.textContent('.game-tile__title')) || '').trim();
      assert(/Hello World/.test(title), `imported tile title: ${title}`);
      await page.click('.game-tile');
      await page.waitForFunction(
        () => Boolean(window.__nj2me && window.__nj2me.game && window.__nj2me.game.game),
        null, { timeout: timeoutMs });
      await waitFrames(page, 12);
      await page.click('#game-back');
      await page.waitForFunction(
        () => window.getComputedStyle(document.querySelector('#library-view')).display !== 'none',
        null, { timeout: timeoutMs });
      return `tile “${title}” from ${jarUrl}`;
    });

    await check('URL import reports a failing link inside the dialog', async () => {
      await page.click('#url-import-btn');
      await page.fill('#url-input', `${jarServer.origin}/missing.jar`);
      await page.click('#url-submit');
      await page.waitForSelector('#url-error:not([hidden])', { timeout: timeoutMs });
      const message = ((await page.textContent('#url-error')) || '').trim();
      assert(/HTTP 404/.test(message), `dialog error: ${message}`);
      await page.click('#url-cancel');
      await page.waitForSelector('#url-dialog', { state: 'hidden', timeout: timeoutMs });
      return `dialog error “${message.slice(0, 60)}”`;
    });

    const errors = results.diagnostics.url.consoleErrors;
    assert(errors.length === 0, `url-import console errors: ${JSON.stringify(errors)}`);
  } finally {
    await ctx.close().catch(() => {});
    await new Promise((resolve) => jarServer.close(resolve));
  }
}

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
    await runUrlImportChecks();
    await runPwaChecks();
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
