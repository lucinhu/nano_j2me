/*
 * nano_j2me web shell - entry point
 *
 * Boots the shell: picks the language, loads the runtime facade (real wasm
 * module when it is next to this file, mock runtime otherwise), wires the
 * library and the game view together and exposes a small debug handle on
 * window.__nj2me for automated checks.
 */

import {
  detectLanguage,
  setLanguage,
  applyTranslations,
  isChinese,
  t
} from './i18n.js';
import { $, toast, showBanner, hideBanner } from './ui.js';
import { loadRuntime, runtimeStorageWarning } from './module.js';
import { LibraryView } from './library.js';
import { GameView } from './game.js';

const state = {
  runtime: null,
  library: null,
  game: null,
  ready: false,
  error: null
};

/* Exposed early so a test script can wait for readiness even while we boot. */
window.__nj2me = state;

/* ------------------------------------------------------------- language */

function updateLanguageButton() {
  const button = $('#lang-toggle');
  if (!button) { return; }
  /* The button always shows the language you can switch TO. */
  button.textContent = isChinese() ? t('app.languageEnglish') : t('app.languageChinese');
  button.title = t('library.language');
  button.setAttribute('aria-label', t('library.language'));
}

function applyLanguage(lang) {
  setLanguage(lang);
  applyTranslations(document);
  updateLanguageButton();
  state.library?.refreshI18n();
  state.game?.refreshI18n();
  if (state.runtime) {
    updateEngineUi(state.runtime);
  }
}

function initLanguage() {
  applyLanguage(detectLanguage());
  $('#lang-toggle')?.addEventListener('click', () => {
    applyLanguage(isChinese() ? 'en' : 'zh-CN');
  });
}

/* ---------------------------------------------------------------- engine */

function updateEngineBadge(runtime) {
  const badge = $('#engine-badge');
  if (!badge) { return; }
  badge.classList.remove('badge--mock', 'badge--wasm');
  if (!runtime) {
    badge.textContent = t('library.engineLoading');
    badge.dataset.kind = 'loading';
    return;
  }
  if (runtime.mock) {
    badge.textContent = t('library.engineMock');
    badge.classList.add('badge--mock');
    badge.dataset.kind = 'mock';
  } else {
    badge.textContent = t('library.engineWasm');
    badge.classList.add('badge--wasm');
    badge.dataset.kind = 'wasm';
  }
}

function updateBanner(runtime) {
  if (!runtime) { return; }
  if (!runtime.mock) {
    hideBanner();
    return;
  }
  let text;
  if (runtime.mockReason === 'requested') {
    text = t('library.mockBanner');
  } else if (runtime.mockReason === 'incompatible') {
    text = t('engine.incompatible', {
      got: runtime.mockGot ?? '?',
      want: 1
    });
  } else {
    text = t('library.fallbackBanner');
  }
  if (location.protocol === 'file:') {
    text += ' ' + t('library.servedHint');
  } else {
    text += ' ' + t('library.mockHint');
  }
  showBanner(text, { kind: 'warn' });
}

function updateEngineUi(runtime) {
  updateEngineBadge(runtime);
  updateBanner(runtime);
  document.body.dataset.engine = runtime && runtime.mock ? 'mock' : 'wasm';
  if (runtimeStorageWarning(runtime) === 'storage') {
    toast(t('library.storageWarn'), { kind: 'warn', timeout: 8000 });
  }
}

/* --------------------------------------------------------------- routing */

async function openGame(record) {
  if (!state.game || state.game.game || state.game._opening) { return; }
  if (!record) { return; }
  if (!state.library) { return; }
  state.library.view.hidden = true;
  try {
    await state.game.open(record);
  } catch (error) {
    /* GameView.open normally handles its own errors; this is the last net. */
    console.warn('[main] openGame failed', error);
    state.library.view.hidden = false;
    document.body.classList.add('in-library');
    document.body.classList.remove('in-game');
    toast(t('game.startFailed', {
      message: error && error.message ? error.message : String(error)
    }), { kind: 'error' });
  }
}

function onGameExit() {
  if (state.library) {
    state.library.view.hidden = false;
    state.library.load();
  }
  document.body.classList.add('in-library');
  document.body.classList.remove('in-game');
}

/* ------------------------------------------------------------ bootstraps */

function bindGlobalErrors() {
  window.addEventListener('unhandledrejection', (event) => {
    const reason = event.reason;
    const message = reason && reason.message ? reason.message : String(reason || '');
    console.warn('[nj2me] unhandled rejection', reason);
    toast(t('common.error') + (message ? ': ' + message : ''), { kind: 'error' });
    if (event.preventDefault) { event.preventDefault(); }
  });
  window.addEventListener('error', (event) => {
    /* Resource errors (missing image/css) have no .error; keep those quiet. */
    if (!event.error) { return; }
    console.warn('[nj2me] uncaught error', event.error);
    toast(t('common.error') + ': ' + (event.error.message || String(event.error)), { kind: 'error' });
  });
}

async function start() {
  initLanguage();
  bindGlobalErrors();
  updateEngineUi(null);

  let runtime = null;
  try {
    runtime = await loadRuntime();
  } catch (error) {
    console.warn('[main] runtime load failed unexpectedly', error);
    state.error = error;
    toast(t('engine.loadFailed', {
      message: error && error.message ? error.message : String(error)
    }), { kind: 'error', timeout: 0 });
    return;
  }

  state.runtime = runtime;
  updateEngineUi(runtime);

  const library = new LibraryView({ runtime, onPlay: openGame });
  const game = new GameView({ runtime, onExit: onGameExit });
  state.library = library;
  state.game = game;

  library.init();

  /* The chrome back arrow is shell-level; GameView owns the transition. */
  $('#game-back')?.addEventListener('click', () => { game.close(); });

  document.body.classList.add('in-library');
  state.ready = true;
  window.dispatchEvent(new CustomEvent('nj2me:ready', { detail: { runtime } }));
  console.info(
    '[nj2me] shell ready · engine=' + runtime.kind +
    (runtime.mock ? ' (' + (runtime.mockReason || 'mock') + ')' : '')
  );
}

start().catch((error) => {
  console.error('[nj2me] fatal boot error', error);
  state.error = error;
  toast(t('engine.loadFailed', {
    message: error && error.message ? error.message : String(error)
  }), { kind: 'error', timeout: 0 });
});
