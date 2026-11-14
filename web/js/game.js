/*
 * nano_j2me web shell - game view
 *
 * Owns the <canvas>, the pumped frame loop, the tools panel, save states and
 * the transitions in/out of the engine.  Everything it needs from the engine
 * goes through the Runtime facade from module.js; this file never touches a
 * wasm export directly.
 *
 * Frame loop contract (include/nj2me/nj_web.h):
 *   - call frame(dt) once per requestAnimationFrame with real elapsed ms
 *     clamped to 0..250; dt is virtual clock time, so the engine never sees
 *     wall-clock jumps;
 *   - keep calling while the returned mask has NJW_FRAME_RUNNING;
 *   - DREW means a new picture was presented: copy the RGBA framebuffer with
 *     putImageData;
 *   - LOADING means startApp has not reached the first Displayable yet;
 *   - a negative value is fatal: show error() and stop;
 *   - pause is simply "stop calling frame"; restart/exit are the only callers
 *     of stop().
 */

import {
  NJW,
  FS_ROOT,
  FS_JARS,
  FS_SAVES,
  FS_FILES,
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT
} from './module.js';
import {
  $,
  el,
  toast,
  showBanner,
  focusWithoutScroll,
  setButtonActive,
  downloadBlob,
  prefersReducedMotion
} from './ui.js';
import {
  t,
  formatDateTime,
  formatBytes
} from './i18n.js';
import {
  SAVE_SLOTS,
  listStates,
  getState,
  putState,
  markPlayed
} from './saves.js';
import { AudioOutput } from './audio.js';
import { GameInput } from './input.js';

const LS_SETTINGS = 'nj2me.settings';
const DEFAULT_SETTINGS = Object.freeze({
  speedPercent: 100,
  muted: false,
  volume: 0.8,
  showPad: true,
  fit: 'fit'            // 'fit' | 'integer'
});

function loadSettings() {
  const settings = Object.assign({}, DEFAULT_SETTINGS);
  try {
    const raw = localStorage.getItem(LS_SETTINGS);
    if (raw) {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') {
        for (const key of Object.keys(DEFAULT_SETTINGS)) {
          if (parsed[key] !== undefined) { settings[key] = parsed[key]; }
        }
      }
    }
  } catch (error) { /* keep defaults */ }
  settings.speedPercent = [100, 150, 200].includes(Number(settings.speedPercent))
    ? Number(settings.speedPercent)
    : 100;
  settings.showPad = !!settings.showPad;
  settings.muted = !!settings.muted;
  settings.volume = Math.max(0, Math.min(1, Number(settings.volume) || 0));
  if (settings.fit !== 'integer') { settings.fit = 'fit'; }
  return settings;
}

function saveSettings(settings) {
  try { localStorage.setItem(LS_SETTINGS, JSON.stringify(settings)); } catch (error) { /* ignore */ }
}

function clamp(value, min, max) {
  return value < min ? min : (value > max ? max : value);
}

function slugify(text) {
  return String(text || 'game')
    .toLowerCase()
    .replace(/[^a-z0-9\u4e00-\u9fff]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 48) || 'game';
}

export class GameView {
  /**
   * @param {object} opts
   * @param {object} opts.runtime Runtime facade
   * @param {() => void} opts.onExit called after the game view is closed
   */
  constructor(opts) {
    this.runtime = opts.runtime;
    this.onExit = opts.onExit;
    this.settings = loadSettings();

    this.view = $('#game-view');
    this.screen = $('#game-screen');
    this.stage = $('#game-stage');
    this.canvas = $('#game-canvas');
    this.ctx = null;
    this.imageData = null;
    this._fbWidth = 0;
    this._fbHeight = 0;

    this.game = null;
    this._opening = false;
    this._running = false;
    this._paused = false;
    this._pauseReason = null;
    this._panelAutoPaused = false;
    this._lastTs = null;
    this._raf = 0;
    this._loading = true;
    this._stopShown = false;
    this._stopTimer = 0;
    this._restarting = false;
    this._closing = false;

    this._drawn = 0;
    this._fps = 0;
    this._fpsMark = 0;
    this._fpsFrames = 0;
    this._statusAt = 0;
    this._engineVersion = '';
    this._engineApi = 0;

    this._slotRows = [];
    this._slotsToken = 0;
    this._panelOpen = false;

    this.audio = new AudioOutput(this.runtime);
    this.audio.setMuted(this.settings.muted);
    this.audio.setVolume(this.settings.volume);

    this.input = new GameInput({
      runtime: this.runtime,
      canvas: this.canvas,
      controls: $('#game-controls'),
      textarea: $('#text-bridge'),
      textIndicator: $('#text-indicator'),
      onGesture: () => this._onGesture()
    });

    this._bindUi();
    /* One stable callback for requestAnimationFrame. */
    this._loop = this._loop.bind(this);
  }

  /* ------------------------------------------------------------- wiring */

  _bindUi() {
    const screen = this.screen;

    $('#panel-handle')?.addEventListener('click', () => this.togglePanel());
    $('#panel-close')?.addEventListener('click', () => this.togglePanel(false));
    $('#panel-scrim')?.addEventListener('click', () => this.togglePanel(false));

    $('#btn-pause')?.addEventListener('click', () => this.togglePause());
    $('#btn-restart')?.addEventListener('click', () => this.restart());
    $('#btn-exit')?.addEventListener('click', () => this.close());
    $('#btn-screenshot')?.addEventListener('click', () => this.screenshot());
    $('#btn-fullscreen')?.addEventListener('click', () => this.toggleFullscreen());
    $('#btn-fit')?.addEventListener('click', () => this.toggleFit());
    $('#btn-pad')?.addEventListener('click', () => this.togglePad());
    $('#btn-mute')?.addEventListener('click', () => this.toggleMute());

    const speed = $('#speed-select');
    if (speed) {
      speed.value = String(this.settings.speedPercent);
      speed.addEventListener('change', () => {
        const value = clamp(Number(speed.value) || 100, 10, 800);
        this.settings.speedPercent = value;
        saveSettings(this.settings);
        try { this.runtime.setSpeed(value); } catch (error) { /* mock/engine */ }
        this._updateSpeedChip();
        this._updateStatus(true);
      });
    }

    $('#stop-exit')?.addEventListener('click', () => this.close());
    $('#stop-replay')?.addEventListener('click', () => this.restart());
    $('#error-back')?.addEventListener('click', () => this.close());
    $('#error-retry')?.addEventListener('click', () => this.restart());

    this._buildSlotRows();

    /* Panel keyboard: Escape closes, Enter/Space activate the focused button. */
    window.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape') { return; }
      if (!this.view || this.view.hidden) { return; }
      if (this._panelOpen) {
        event.preventDefault();
        event.stopPropagation();
        this.togglePanel(false);
      }
    });

    document.addEventListener('visibilitychange', () => {
      if (document.hidden) { this._onVisibilityHidden(); }
      else { this._onVisibilityVisible(); }
    });

    document.addEventListener('fullscreenchange', () => this._updateFullscreenButton());

    if (typeof ResizeObserver === 'function') {
      this._resizeObserver = new ResizeObserver(() => this.layout());
      this._resizeObserver.observe(this.stage);
    } else {
      window.addEventListener('resize', () => this.layout());
    }
    window.addEventListener('resize', () => this.layout());

    this.input.attach();
  }

  _buildSlotRows() {
    const container = $('#save-slots');
    if (!container) { return; }
    container.textContent = '';
    this._slotRows = [];
    for (let slot = 1; slot <= SAVE_SLOTS; slot++) {
      const name = el('span', { className: 'slot__name' });
      const time = el('span', { className: 'slot__time' });
      const save = el('button', {
        className: 'btn btn--small slot__save',
        attrs: { type: 'button' }
      });
      const load = el('button', {
        className: 'btn btn--small slot__load',
        attrs: { type: 'button' }
      });
      const row = el('div', { className: 'slot', attrs: { 'data-slot': String(slot) } }, [
        el('div', { className: 'slot__info' }, [name, time]),
        el('div', { className: 'slot__actions' }, [save, load])
      ]);
      save.addEventListener('click', () => this.saveSlot(slot));
      load.addEventListener('click', () => this.loadSlot(slot));
      container.append(row);
      this._slotRows.push({ slot, row, name, time, save, load });
    }
  }

  /* ------------------------------------------------------- open / close */

  /**
   * Open a game record from the library.
   * @param {object} game record from saves.js (contains jar bytes + metadata)
   */
  async open(game) {
    if (this._opening || this.game) { return; }
    this._opening = true;
    this.game = game;
    this._resetOverlays();
    this._showLoading(true, game.title || game.fileName || '');
    this._setTitle(game.title || game.fileName || '');
    this.settings = loadSettings();

    /* Must be inside the click gesture for autoplay policies. */
    this.audio.ensureContext();
    this.audio.setMuted(this.settings.muted);
    this.audio.setVolume(this.settings.volume);

    this.view.hidden = false;
    document.body.classList.add('in-game');
    document.body.classList.remove('in-library');
    this._applySettingsToUi();

    this._engineVersion = '';
    try { this._engineVersion = this.runtime.runtimeVersion() || ''; } catch (error) { this._engineVersion = ''; }
    try { this._engineApi = this.runtime.apiVersion() || 0; } catch (error) { this._engineApi = 0; }

    try {
      await this._prepare();
    } catch (error) {
      console.warn('[game] prepare failed', error);
      this._showError(error && error.message ? error.message : String(error));
    } finally {
      this._opening = false;
    }
  }

  /** Tear the engine down and go back to the library. */
  async close() {
    if (!this.game || this._closing) { return; }
    this._closing = true;
    this._clearStopTimer();
    this._stopLoop();
    this._running = false;
    this._paused = false;
    this.togglePanel(false, { silent: true });
    this.input.setEnabled(false);
    this.input.releaseAll();
    this.audio.suspend();
    try { this.runtime.stop(); } catch (error) { /* already stopped */ }
    if (document.fullscreenElement && document.exitFullscreen) {
      try { await document.exitFullscreen(); } catch (error) { /* ignore */ }
    }
    this.game = null;
    this.view.hidden = true;
    document.body.classList.remove('in-game');
    document.body.classList.add('in-library');
    this._closing = false;
    if (this.onExit) { this.onExit(); }
  }

  /** Restart the same game from scratch (no state resume). */
  async restart() {
    if (!this.game || this._restarting) { return; }
    this._restarting = true;
    this._clearStopTimer();
    this._stopLoop();
    this._running = false;
    this._paused = false;
    this.input.setEnabled(false);
    this.input.releaseAll();
    this.audio.suspend();
    try { this.runtime.stop(); } catch (error) { /* already stopped */ }
    this.togglePanel(false, { silent: true });
    this._resetOverlays();
    this._showLoading(true, this.game.title || '');
    try {
      await this._prepare();
    } catch (error) {
      this._showError(error && error.message ? error.message : String(error));
    } finally {
      this._restarting = false;
    }
  }

  _jarPath(game) {
    if (this.runtime.mock) {
      const name = encodeURIComponent(game.fileName || game.title || 'game.jar');
      return FS_JARS + '/' + name;
    }
    return FS_JARS + '/' + game.id + '.jar';
  }

  async _prepare() {
    const runtime = this.runtime;
    const game = this.game;
    if (!game) { return; }

    try {
      if (typeof runtime.initFileSystem === 'function') {
        await runtime.initFileSystem();
      }
    } catch (error) {
      console.warn('[game] initFileSystem failed', error);
    }

    const savesDir = FS_SAVES + '/' + game.id;
    const filesDir = FS_FILES + '/' + game.id;

    try {
      runtime.mkdirp(FS_ROOT);
      runtime.mkdirp(FS_JARS);
      runtime.mkdirp(FS_SAVES);
      runtime.mkdirp(savesDir);
      runtime.mkdirp(filesDir);

      const jarPath = this._jarPath(game);
      if (!runtime.exists(jarPath)) {
        if (!game.jar || !game.jar.length) {
          throw new Error(t('game.jarMissing'));
        }
        runtime.writeFile(jarPath, game.jar);
      }

      runtime.optionsReset();
      runtime.optionInt(NJW.OPT_SPEED_PERCENT, this.settings.speedPercent);
      runtime.optionString(NJW.OPT_RMS_DIR, savesDir);
      runtime.optionString(NJW.OPT_FS_DIR, filesDir);
      const midlet = Array.isArray(game.midletClasses) && game.midletClasses.length
        ? String(game.midletClasses[0])
        : '';
      if (midlet) { runtime.optionString(NJW.OPT_MIDLET_CLASS, midlet); }

      const started = runtime.start(jarPath);
      if (!started) {
        throw new Error(runtime.error() || t('game.startFailed', { message: t('common.unknown') }));
      }
    } catch (error) {
      console.warn('[game] start failed', error);
      this._showError(error && error.message ? error.message : String(error));
      return;
    }

    /* The engine may choose a device screen size; canvas follows it. */
    let width = DEFAULT_WIDTH;
    let height = DEFAULT_HEIGHT;
    try {
      width = runtime.width() || width;
      height = runtime.height() || height;
    } catch (error) { /* keep defaults */ }
    this._initCanvas(width, height);

    try { runtime.setSpeed(this.settings.speedPercent); } catch (error) { /* optional */ }
    void markPlayed(game.id).catch(() => {});

    this._loading = true;
    this._stopShown = false;
    this._drawn = 0;
    this._fps = 0;
    this._fpsFrames = 0;
    this._fpsMark = 0;
    this._lastTs = null;
    this._paused = false;
    this._pauseReason = null;
    this._running = true;
    this.input.setEnabled(true);
    this.input.resetGesture();
    this.audio.resume();
    this._updateStatus(true);
    this._startLoop();
  }

  /* ----------------------------------------------------------- frame loop */

  _startLoop() {
    this._stopLoop();
    this._fpsMark = 0;
    this._fpsFrames = 0;
    this._raf = requestAnimationFrame(this._loop);
  }

  _stopLoop() {
    if (this._raf) {
      cancelAnimationFrame(this._raf);
      this._raf = 0;
    }
  }

  _loop(timestamp) {
    /* Bound once in the constructor via arrow function assignment. */
    if (!this._running) { return; }
    this._raf = requestAnimationFrame(this._loop);
    if (this._paused) { this._lastTs = timestamp; return; }

    if (this._lastTs === null) {
      this._lastTs = timestamp;
    }
    let dt = timestamp - this._lastTs;
    this._lastTs = timestamp;
    if (!Number.isFinite(dt) || dt < 0) { dt = 0; }
    dt = clamp(dt, 0, 250);

    let flags = 0;
    try {
      flags = this.runtime.frame(dt);
    } catch (error) {
      console.warn('[game] frame() threw', error);
      this._fatal(error && error.message ? error.message : String(error));
      return;
    }

    if (flags < 0) {
      this._fatal(this._engineError() || t('game.errorDefault'));
      return;
    }

    if (flags & NJW.FRAME_DREW) {
      this._paint();
      this._fpsFrames += 1;
    }

    if (this._loading && !(flags & NJW.FRAME_LOADING)) {
      this._loading = false;
      this._showLoading(false);
    }

    if (flags & NJW.FRAME_RUNNING) {
      this.audio.pump();
      this.input.pollText();
      this._updateFps(timestamp);
      this._updateStatus(false);
      return;
    }

    /* Not running any more: the MIDlet exited cleanly. */
    this._onExited();
  }

  _paint() {
    const fb = this.runtime.framebuffer();
    if (!fb || !fb.length) { return; }
    const width = this.runtime.width();
    const height = this.runtime.height();
    if (width !== this._fbWidth || height !== this._fbHeight) {
      this._initCanvas(width, height);
    }
    const image = this.imageData;
    if (!image) { return; }
    if (fb.length !== image.data.length) {
      /* Defensive: a frame in flight after an option change. */
      return;
    }
    image.data.set(fb);
    this.ctx.putImageData(image, 0, 0);
  }

  _initCanvas(width, height) {
    const w = Math.max(1, width | 0);
    const h = Math.max(1, height | 0);
    this.canvas.width = w;
    this.canvas.height = h;
    this.ctx = this.canvas.getContext('2d', { alpha: false });
    if (this.ctx) {
      this.ctx.imageSmoothingEnabled = false;
      this._imageData = this.ctx.createImageData(w, h);
      this.imageData = this._imageData;
      this.ctx.fillStyle = '#000000';
      this.ctx.fillRect(0, 0, w, h);
    }
    this._fbWidth = w;
    this._fbHeight = h;
    this.layout();
  }

  /**
   * Fit the fixed-resolution framebuffer into the stage with letterboxing.
   * "fit" prefers a whole-number scale when that stays close to the largest
   * fit (crisp pixels without leaving huge black bars); otherwise it uses the
   * exact fit, which the browser then smooths.  "integer" is always whole.
   */
  layout() {
    if (!this.stage || !this.canvas || !this._fbWidth) { return; }
    const availW = this.stage.clientWidth;
    const availH = this.stage.clientHeight;
    if (!availW || !availH) { return; }
    const fitScale = Math.min(availW / this._fbWidth, availH / this._fbHeight);
    let scale = fitScale;
    if (this.settings.fit === 'integer') {
      scale = Math.max(1, Math.floor(fitScale));
    } else {
      const integer = Math.floor(fitScale);
      if (integer >= 1 && integer / fitScale >= 0.85) { scale = integer; }
    }
    const cssW = Math.max(1, Math.round(this._fbWidth * scale));
    const cssH = Math.max(1, Math.round(this._fbHeight * scale));
    this.canvas.style.width = cssW + 'px';
    this.canvas.style.height = cssH + 'px';
  }

  /* -------------------------------------------------------------- pause */

  togglePause() {
    if (!this._running) { return; }
    if (this._paused) { this.resume(); } else { this.pause(); }
  }

  pause() {
    if (!this._running || this._paused) { return; }
    this._applyPause(true, 'user');
  }

  resume() {
    if (!this._running || !this._paused) { return; }
    this._panelAutoPaused = false;
    this._applyPause(false, null);
  }

  _applyPause(paused, reason) {
    this._paused = paused;
    this._pauseReason = paused ? reason : null;
    this._lastTs = null;
    if (paused) {
      this.input.setEnabled(false);
      this.input.releaseAll();
      this.audio.suspend();
    } else {
      this.input.setEnabled(true);
      this.audio.ensureContext();
      this.audio.resume();
    }
    this._updatePauseUi();
  }

  _updatePauseUi() {
    const paused = this._paused;
    const button = $('#btn-pause');
    if (button) {
      button.textContent = paused ? t('game.resume') : t('game.pause');
      setButtonActive(button, paused);
    }
    const indicator = $('#pause-indicator');
    if (indicator) { indicator.hidden = !paused || !this._running; }
    if (this.screen) { this.screen.classList.toggle('is-paused', paused); }
  }

  _onVisibilityHidden() {
    if (!this._running || this._paused) { return; }
    this._applyPause(true, 'visibility');
    toast(t('game.autoPaused'), { kind: 'info', timeout: 2500 });
  }

  _onVisibilityVisible() {
    /* Stay paused: resuming audio without a gesture is not reliable. */
    this._updatePauseUi();
  }

  /* ---------------------------------------------------------------- panel */

  togglePanel(open, opts = {}) {
    const next = open === undefined ? !this._panelOpen : !!open;
    if (next === this._panelOpen) { return; }
    this._panelOpen = next;
    const panel = $('#game-panel');
    const scrim = $('#panel-scrim');
    const handle = $('#panel-handle');
    if (!panel) { return; }
    if (next) {
      this._renderSlots();
      this._updateStatus(true);
      this._updatePanelUi();
      if (this._running && !this._paused) {
        this._panelAutoPaused = true;
        this._applyPause(true, 'panel');
      }
      panel.classList.add('is-open');
      panel.setAttribute('aria-hidden', 'false');
      if (scrim) { scrim.hidden = false; }
      if (handle) { handle.hidden = true; }
      this.view.classList.add('panel-open');
      const closeBtn = $('#panel-close');
      if (closeBtn) { setTimeout(() => focusWithoutScroll(closeBtn), 40); }
    } else {
      panel.classList.remove('is-open');
      panel.setAttribute('aria-hidden', 'true');
      if (scrim) { scrim.hidden = true; }
      if (handle) { handle.hidden = false; }
      this.view.classList.remove('panel-open');
      if (this._panelAutoPaused) {
        this._panelAutoPaused = false;
        if (this._paused && this._pauseReason === 'panel') {
          this._applyPause(false, null);
        }
      }
    }
    void opts;
  }

  _updatePanelUi() {
    if (!this._slotRows.length) { return; }
    let canSave = false;
    try { canSave = !!this.runtime.canSave(); } catch (error) { canSave = false; }
    for (const row of this._slotRows) {
      row.save.textContent = t('game.save');
      row.load.textContent = t('game.load');
      row.name.textContent = t('game.slot', { n: row.slot });
      row.save.disabled = !canSave;
      row.load.disabled = !row.row.dataset.filled;
    }
    const fit = $('#btn-fit');
    if (fit) {
      fit.textContent = this.settings.fit === 'integer' ? t('game.fitInteger') : t('game.fitWindow');
      setButtonActive(fit, this.settings.fit === 'integer');
    }
    const pad = $('#btn-pad');
    if (pad) {
      pad.textContent = t('game.showControls');
      setButtonActive(pad, this.settings.showPad);
    }
    const mute = $('#btn-mute');
    if (mute) {
      mute.textContent = t('game.mute');
      setButtonActive(mute, this.settings.muted);
    }
    const fullscreen = $('#btn-fullscreen');
    if (fullscreen) { fullscreen.textContent = t('game.fullscreen'); }
  }

  async _renderSlots() {
    if (!this.game) { return; }
    const token = ++this._slotsToken;
    let states = new Map();
    try { states = await listStates(this.game.id); } catch (error) { states = new Map(); }
    if (token !== this._slotsToken || !this.game) { return; }
    let canSave = false;
    try { canSave = !!this.runtime.canSave(); } catch (error) { canSave = false; }
    for (const row of this._slotRows) {
      const record = states.get(row.slot);
      if (record) {
        row.row.dataset.filled = '1';
        row.time.textContent = formatDateTime(record.at) + ' · ' + formatBytes(record.size || 0);
      } else {
        delete row.row.dataset.filled;
        row.time.textContent = t('game.slotEmpty');
      }
      row.name.textContent = t('game.slot', { n: row.slot });
      row.save.textContent = t('game.save');
      row.load.textContent = t('game.load');
      row.save.disabled = !canSave;
      row.load.disabled = !record;
    }
  }

  async saveSlot(slot) {
    if (!this.game) { return; }
    let canSave = false;
    try { canSave = !!this.runtime.canSave(); } catch (error) { canSave = false; }
    if (!canSave) {
      toast(t('game.saveUnsupported'), { kind: 'warn' });
      return;
    }
    let result = null;
    try { result = this.runtime.saveState(); } catch (error) {
      result = { ok: false, error: error && error.message ? error.message : String(error) };
    }
    if (!result || !result.ok) {
      toast(t('game.saveFailed') + (result && result.error ? ': ' + result.error : ''), { kind: 'error' });
      return;
    }
    try {
      await putState(this.game.id, slot, {
        bytes: result.bytes,
        frames: this.runtime.frames(),
        title: this.game.title || '',
        engineApi: this._engineApi
      });
      toast(t('game.saveOk', { n: slot }), { kind: 'success' });
    } catch (error) {
      toast(t('game.saveFailed') + ': ' + (error && error.message ? error.message : error), { kind: 'error' });
    }
    await this._renderSlots();
  }

  async loadSlot(slot) {
    if (!this.game) { return; }
    let record = null;
    try { record = await getState(this.game.id, slot); } catch (error) { record = null; }
    if (!record) { return; }
    let result = null;
    try { result = this.runtime.loadState(record.bytes); } catch (error) {
      result = { ok: false, error: error && error.message ? error.message : String(error) };
    }
    if (result && result.ok) {
      toast(t('game.loadOk', { n: slot }), { kind: 'success' });
      await this._renderSlots();
      return;
    }
    /* The VM is unusable after a failed load: warn, then restart clean. */
    const restart = await confirmDialogSafe({
      title: t('game.load'),
      message: t('game.loadFailed'),
      confirmLabel: t('game.restart'),
      danger: true
    });
    if (restart) {
      this._resetOverlays();
      await this.restart();
      if (!this._stopShown) {
        toast(t('game.loadRestarted'), { kind: 'warn' });
      }
    }
  }

  /* ---------------------------------------------------------- panel actions */

  toggleFit() {
    this.settings.fit = this.settings.fit === 'integer' ? 'fit' : 'integer';
    saveSettings(this.settings);
    this.layout();
    this._updatePanelUi();
  }

  togglePad() {
    this.settings.showPad = !this.settings.showPad;
    saveSettings(this.settings);
    this._applySettingsToUi();
    this._updatePanelUi();
  }

  toggleMute() {
    this.settings.muted = !this.settings.muted;
    this.audio.setMuted(this.settings.muted);
    saveSettings(this.settings);
    this._updatePanelUi();
  }

  async screenshot() {
    if (!this.canvas) { return; }
    try {
      const blob = await new Promise((resolve) => this.canvas.toBlob(resolve, 'image/png'));
      if (!blob) { throw new Error('toBlob returned null'); }
      const stamp = new Date().toISOString().replace(/[:.]/g, '-');
      downloadBlob(blob, slugify(this.game ? this.game.title : 'game') + '-' + stamp + '.png');
      toast(t('game.screenshotOk'), { kind: 'success' });
    } catch (error) {
      toast(t('game.screenshotFailed') + ': ' + (error && error.message ? error.message : error), { kind: 'error' });
    }
  }

  async toggleFullscreen() {
    try {
      if (document.fullscreenElement) {
        await document.exitFullscreen();
      } else if (this.view.requestFullscreen) {
        await this.view.requestFullscreen();
      }
      this._updateFullscreenButton();
    } catch (error) {
      toast(t('game.fullscreenFailed') + ': ' + (error && error.message ? error.message : error), { kind: 'warn' });
    }
  }

  _updateFullscreenButton() {
    const button = $('#btn-fullscreen');
    if (!button) { return; }
    button.textContent = document.fullscreenElement ? t('game.fullscreenExit') : t('game.fullscreen');
    setTimeout(() => this.layout(), 50);
  }

  /* -------------------------------------------------------------- status */

  _updateFps(timestamp) {
    if (!this._fpsMark) { this._fpsMark = timestamp; this._fpsFrames = 0; return; }
    const elapsed = timestamp - this._fpsMark;
    if (elapsed >= 600) {
      this._fps = this._fpsFrames * 1000 / elapsed;
      this._fpsMark = timestamp;
      this._fpsFrames = 0;
    }
  }

  _updateStatus(force) {
    const now = performance.now();
    if (!force && now - this._statusAt < 300) { return; }
    this._statusAt = now;
    const version = $('#status-version');
    const frames = $('#status-frames');
    const fps = $('#status-fps');
    if (version) {
      const name = this._engineVersion || (this.runtime.mock ? t('game.statusMock') : '');
      version.textContent = name ? t('game.statusVersion', { version: name }) : '';
    }
    if (frames) {
      let count = 0;
      try { count = this.runtime.frames() | 0; } catch (error) { count = 0; }
      frames.textContent = t('game.statusFrames', { n: count });
    }
    if (fps) {
      const value = this._paused ? 0 : this._fps;
      fps.textContent = t('game.statusFps', { fps: value ? value.toFixed(1) : '0' });
    }
  }

  /* --------------------------------------------------------------- misc */

  _engineError() {
    try { return this.runtime.error(); } catch (error) { return ''; }
  }

  _onExited() {
    this._stopLoop();
    this._running = false;
    this.input.setEnabled(false);
    this.audio.suspend();
    const reason = (() => {
      try { return this.runtime.stopReason(); } catch (error) { return ''; }
    })() || t('game.exitedDefault');
    this._showStop(reason);
    /*
     * The MIDlet is gone for good (the runtime is single-shot): show why, then
     * go back to the library on our own.  A click on "exit" returns sooner and
     * "retry" restarts the game (both clear the timer through close/restart).
     */
    this._clearStopTimer();
    this._stopTimer = setTimeout(() => {
      this._stopTimer = 0;
      if (this._stopShown && !this._closing && !this._restarting) { this.close(); }
    }, 3000);
  }

  _clearStopTimer() {
    if (this._stopTimer) {
      clearTimeout(this._stopTimer);
      this._stopTimer = 0;
    }
  }

  _fatal(message) {
    this._stopLoop();
    this._running = false;
    this.input.setEnabled(false);
    this.input.releaseAll();
    this.audio.suspend();
    try { this.runtime.stop(); } catch (error) { /* ignore */ }
    this._showError(message || t('game.errorDefault'));
  }

  _setTitle(title) {
    const elTitle = $('#game-title');
    if (elTitle) { elTitle.textContent = title || ''; }
    const loading = $('#loading-title');
    if (loading) { loading.textContent = title || ''; }
    this._updateSpeedChip();
  }

  _updateSpeedChip() {
    const chip = $('#game-speed-chip');
    if (!chip) { return; }
    const percent = Number(this.settings.speedPercent) || 100;
    chip.hidden = percent === 100;
    chip.textContent = t('game.speedValue', { n: percent });
  }

  _showLoading(show, title) {
    const overlay = $('#loading-overlay');
    if (overlay) { overlay.hidden = !show; }
    if (show) {
      this._loading = true;
      if (title) { this._setTitle(title); }
    }
  }

  _resetOverlays() {
    this._clearStopTimer();
    this._stopShown = false;
    const stop = $('#stop-overlay');
    if (stop) { stop.hidden = true; }
    const error = $('#error-overlay');
    if (error) { error.hidden = true; }
    const pause = $('#pause-indicator');
    if (pause) { pause.hidden = true; }
    this._showLoading(true);
    if (this.screen) { this.screen.classList.remove('is-paused'); }
  }

  _showStop(reason) {
    this._stopShown = true;
    this._showLoading(false);
    const overlay = $('#stop-overlay');
    const title = $('#stop-title');
    const message = $('#stop-message');
    if (title) { title.textContent = t('game.exitedTitle'); }
    if (message) { message.textContent = reason || t('game.exitedDefault'); }
    if (overlay) { overlay.hidden = false; }
    const exit = $('#stop-exit');
    if (exit) { exit.textContent = t('game.exit'); setTimeout(() => focusWithoutScroll(exit), 40); }
    const replay = $('#stop-replay');
    if (replay) { replay.textContent = t('game.retry'); }
    this._updatePauseUi();
  }

  _showError(message) {
    this._clearStopTimer();
    this._showLoading(false);
    const overlay = $('#error-overlay');
    const messageEl = $('#error-message');
    if (messageEl) { messageEl.textContent = message || t('game.errorDefault'); }
    if (overlay) { overlay.hidden = false; }
    const retry = $('#error-retry');
    if (retry) { retry.textContent = t('game.retry'); }
    const back = $('#error-back');
    if (back) { back.textContent = t('game.back'); }
    const title = $('#error-title');
    if (title) { title.textContent = t('game.errorTitle'); }
  }

  _onGesture() {
    this.audio.ensureContext();
    if (!this._paused) { this.audio.resume(); }
  }

  _applySettingsToUi() {
    const speed = $('#speed-select');
    if (speed) { speed.value = String(this.settings.speedPercent); }
    this.view.classList.toggle('controls-hidden', !this.settings.showPad);
    this._updateSpeedChip();
    this._updatePanelUi();
  }

  /* ------------------------------------------------------------ i18n hook */

  refreshI18n() {
    this._updatePauseUi();
    this._updatePanelUi();
    this._updateSpeedChip();
    this._updateStatus(true);
    if (this._stopShown) {
      const message = $('#stop-message');
      const exit = $('#stop-exit');
      const replay = $('#stop-replay');
      if (exit) { exit.textContent = t('game.exit'); }
      if (replay) { replay.textContent = t('game.retry'); }
      if (message && this.runtime) {
        try { message.textContent = this.runtime.stopReason() || t('game.exitedDefault'); } catch (error) { /* ignore */ }
      }
    }
    for (const row of this._slotRows) {
      row.name.textContent = t('game.slot', { n: row.slot });
      row.save.textContent = t('game.save');
      row.load.textContent = t('game.load');
    }
  }

  dispose() {
    this._clearStopTimer();
    this._stopLoop();
    this.input.detach();
    this.audio.dispose();
    try { this.runtime.stop(); } catch (error) { /* ignore */ }
    if (this._resizeObserver) { this._resizeObserver.disconnect(); }
  }
}

/* confirmDialog import is lazy to keep the module import graph simple. */
async function confirmDialogSafe(opts) {
  try {
    const mod = await import('./ui.js');
    return await mod.confirmDialog(opts);
  } catch (error) {
    return false;
  }
}
