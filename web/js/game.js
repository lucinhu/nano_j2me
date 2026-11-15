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
  putGame,
  markPlayed
} from './saves.js';
import { AudioOutput } from './audio.js';
import { GameInput } from './input.js';

const LS_SETTINGS = 'nj2me.settings';
const HEAP_CHOICES_MB = [16, 32, 64, 128];

/* Emulated-frame cap. 0 means "pump once per display refresh", which is what
 * the shell did before this setting existed; the numeric choices match the
 * frame rates J2ME titles were written for and the classic 60 Hz panel. */
const FPS_CHOICES = [0, 60, 30, 24, 15];
/* Device screen presets.  'auto' leaves the engine on its 240x320 default;
 * the rest are common J2ME resolutions that titles were drawn for. */
const SCREEN_PRESETS = Object.freeze({
  auto: null,
  '176x208': [176, 208],
  '176x220': [176, 220],
  '128x160': [128, 160],
  '320x240': [320, 240]
});
const DEFAULT_SETTINGS = Object.freeze({
  speedPercent: 100,
  frameCap: 60,         // default cap for games without their own override; 0 = display refresh
  heapMb: 16,           // Java heap for the next start; heavy titles need 64+
  autoResume: false,    // restarting a game continues from its newest save
  autoSave: false,      // leaving a game keeps its newest state for the next launch
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
  settings.frameCap = FPS_CHOICES.includes(Number(settings.frameCap))
    ? Number(settings.frameCap)
    : 60;
  settings.heapMb = HEAP_CHOICES_MB.includes(Number(settings.heapMb))
    ? Number(settings.heapMb)
    : 16;
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
   * @param {() => void} [opts.onStopped] called when the engine stops running
   *   (MIDlet exit or a fatal error), releasing host-side engine ownership
   */
  constructor(opts) {
    this.runtime = opts.runtime;
    this.onExit = opts.onExit;
    this.onStopped = opts.onStopped || null;
    this.settings = loadSettings();
    /* The setting was persisted as JSON: normalise the optional toggle so a
       hand-edited value or a pre-toggle profile still means "off". */
    this.settings.autoResume = this.settings.autoResume === true;
    this.settings.autoSave = this.settings.autoSave === true;

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
    this._diagAt = 0;
    this._diagInsns = 0;
    this._diagInsnsAt = 0;
    this._engineVersion = '';
    this._engineApi = 0;

    this._slotRows = [];
    this._slotsToken = 0;
    this._panelOpen = false;
    this._autoResumeRetried = false;

    this.audio = new AudioOutput(this.runtime);
    this.audio.setMuted(this.settings.muted);
    this.audio.setVolume(this.settings.volume);

    this.input = new GameInput({
      runtime: this.runtime,
      canvas: this.canvas,
      controls: $('#game-controls'),
      textarea: $('#text-bridge'),
      textIndicator: $('#text-indicator'),
      onGesture: () => this._onGesture(),
      onGamepad: (connected) => this._onGamepad(connected)
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

    // Auto-resume is applied when the next game starts, so the checkbox only
    // persists the preference and tells the user when it takes effect.
    const autoResume = $('#auto-resume');
    if (autoResume) {
      autoResume.addEventListener('change', () => {
        const on = autoResume.checked === true;
        if (on === this.settings.autoResume) { return; }
        this.settings.autoResume = on;
        saveSettings(this.settings);
        toast(t('game.autoResumeNextLaunch'), { kind: 'info' });
      });
    }

    // Saving on exit gives the next launch something to continue from; pair it
    // with auto-resume to make "quit and come back" pick up where you stopped.
    const autoSave = $('#auto-save');
    if (autoSave) {
      autoSave.addEventListener('change', () => {
        const on = autoSave.checked === true;
        if (on === this.settings.autoSave) { return; }
        this.settings.autoSave = on;
        saveSettings(this.settings);
        toast(t('game.autoSaveHint'), { kind: 'info' });
      });
    }

    // Volume is the host-side WebAudio gain.  Dragging the slider also unmutes,
    // like the volume keys on a phone; the value is persisted with the rest of
    // the shell settings.
    const volume = $('#volume-slider');
    if (volume) {
      volume.addEventListener('input', () => {
        const percent = clamp(Number(volume.value) || 0, 0, 100);
        this.settings.volume = percent / 100;
        if (this.settings.muted && percent > 0) {
          this.settings.muted = false;
          this.audio.setMuted(false);
        }
        this.audio.setVolume(this.settings.volume);
        saveSettings(this.settings);
        this._updateVolumeUi();
      });
    }

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

    /* Frame cap: applied live by the loop below, so it needs no restart
       (unlike the Java heap, which the engine fixes when the VM is created).
       Titles were written for different rates, so the choice is remembered on
       the game record like the screen size; the shell setting is only the
       default a game starts with. */
    const fps = $('#fps-select');
    if (fps) {
      fps.value = String(this._frameCap());
      fps.addEventListener('change', () => {
        if (!this.game) { return; }
        const value = FPS_CHOICES.includes(Number(fps.value)) ? Number(fps.value) : 60;
        if (this._frameCap() === value) { return; }
        this.game.frameCap = value;
        this._frameAccumMs = 0;
        void putGame(this.game).catch((error) => {
          console.warn('[game] could not remember the frame-rate cap', error);
        });
        toast(t('game.fpsApplied'), { kind: 'info', timeout: 2200 });
      });
    }

    // The Java heap is fixed when the VM is created, so this only takes effect
    // on the next start; the hint tells the user instead of silently ignoring it.
    // The screen preset is a device-profile choice and titles were drawn for
    // different resolutions, so it is remembered per game (unlike the heap,
    // which stays a shell-wide default).  It reaches the runtime on the next
    // launch of this game.
    const screenSize = $('#screen-select');
    if (screenSize) {
      screenSize.addEventListener('change', () => {
        if (!this.game) { return; }
        const value = Object.prototype.hasOwnProperty.call(SCREEN_PRESETS, screenSize.value)
          ? screenSize.value
          : 'auto';
        if (this._screenPreset() === value) { return; }
        if (value === 'auto') {
          delete this.game.screenPreset;
        } else {
          this.game.screenPreset = value;
        }
        void putGame(this.game).catch((error) => {
          console.warn('[game] could not remember the screen preset', error);
        });
        toast(t('game.screenNextLaunch'), { kind: 'info' });
      });
    }

    const heap = $('#heap-select');
    if (heap) {
      heap.value = String(this.settings.heapMb);
      heap.addEventListener('change', () => {
        const value = HEAP_CHOICES_MB.includes(Number(heap.value))
          ? Number(heap.value) : 16;
        if (this.settings.heapMb === value) { return; }
        this.settings.heapMb = value;
        saveSettings(this.settings);
        toast(t('game.memoryNextLaunch'), { kind: 'info' });
      });
    }

    // A JAR can contain several MIDlets; the choice is stored with the game
    // record and applies on the next start, exactly like the heap setting.
    const midlet = $('#midlet-select');
    if (midlet) {
      midlet.addEventListener('change', () => {
        const classes = Array.isArray(this.game?.midletClasses) ? this.game.midletClasses : [];
        const index = Number(midlet.value);
        if (!this.game || !Number.isInteger(index) || index < 0 || index >= classes.length) { return; }
        if (this._midletIndex() === index) { return; }
        this.game.midletIndex = index;
        void putGame(this.game).catch((error) => {
          console.warn('[game] could not persist the MIDlet choice', error);
        });
        toast(t('game.midletNextLaunch'), { kind: 'info' });
      });
    }

    $('#stop-exit')?.addEventListener('click', () => this.close());
    $('#stop-replay')?.addEventListener('click', () => this.restart());
    $('#error-back')?.addEventListener('click', () => this.close());
    $('#error-retry')?.addEventListener('click', () => this.restart());
    // One click out of the commonest fatal error: rebuild the engine with the
    // next heap step instead of making the player back out to the panel first.
    $('#error-heap')?.addEventListener('click', () => {
      const button = $('#error-heap');
      const mb = Number(button?.dataset.mb);
      if (!Number.isFinite(mb) || mb <= 0) { return; }
      this.settings.heapMb = mb;
      saveSettings(this.settings);
      this._applySettingsToUi();
      void this.restart();
    });

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
      /* Optional display name for this slot: it is stored with the save and
       * also becomes the export filename base.  Empty slots keep it disabled. */
      const label = el('input', {
        className: 'slot__label',
        attrs: {
          type: 'text', maxlength: '24', autocomplete: 'off',
          spellcheck: 'false', 'aria-label': ''
        }
      });
      label.addEventListener('change', () => this.saveSlotName(slot));
      label.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') { event.preventDefault(); label.blur(); }
      });
      /* Live preview of the saved screen; hidden until a slot holds a save
       * captured in this browser (imported .njst files carry no pixels). */
      const thumb = el('img', {
        className: 'slot__thumb',
        attrs: { alt: '', width: '48', height: '64', loading: 'lazy' }
      });
      thumb.hidden = true;
      const save = el('button', {
        className: 'btn btn--small slot__save',
        attrs: { type: 'button' }
      });
      const load = el('button', {
        className: 'btn btn--small slot__load',
        attrs: { type: 'button' }
      });
      const exportBtn = el('button', {
        className: 'btn btn--small slot__export',
        attrs: { type: 'button' }
      });
      const importBtn = el('button', {
        className: 'btn btn--small slot__import',
        attrs: { type: 'button' }
      });
      const row = el('div', { className: 'slot', attrs: { 'data-slot': String(slot) } }, [
        thumb,
        el('div', { className: 'slot__info' }, [name, time, label]),
        el('div', { className: 'slot__actions' }, [save, load, exportBtn, importBtn])
      ]);
      save.addEventListener('click', () => this.saveSlot(slot));
      load.addEventListener('click', () => this.loadSlot(slot));
      exportBtn.addEventListener('click', () => this.exportSlot(slot));
      importBtn.addEventListener('click', () => this.importSlot(slot));
      container.append(row);
      this._slotRows.push({
        slot, row, name, time, label, thumb, save, load,
        export: exportBtn, import: importBtn
      });
    }

    /* One extra row for the exit auto-save kept on the game record.  It is not
       written by hand (leaving the game does that when the toggle is on), but
       the player can inspect when it was taken, load it, or drop it. */
    const autoThumb = el('img', {
      className: 'slot__thumb',
      attrs: { alt: '', width: '48', height: '64', loading: 'lazy' }
    });
    autoThumb.hidden = true;
    const autoName = el('span', { className: 'slot__name' });
    const autoTime = el('span', { className: 'slot__time' });
    const autoLoad = el('button', {
      className: 'btn btn--small slot__load',
      attrs: { type: 'button' }
    });
    const autoClear = el('button', {
      className: 'btn btn--small slot__clear',
      attrs: { type: 'button' }
    });
    const autoRow = el('div', { className: 'slot slot--auto', attrs: { 'data-slot': 'auto' } }, [
      autoThumb,
      el('div', { className: 'slot__info' }, [autoName, autoTime]),
      el('div', { className: 'slot__actions' }, [autoLoad, autoClear])
    ]);
    autoLoad.addEventListener('click', () => this.loadAutoSlot());
    autoClear.addEventListener('click', () => this.clearAutoSlot());
    autoRow.hidden = true;
    container.append(autoRow);
    this._autoRow = {
      row: autoRow, name: autoName, time: autoTime, thumb: autoThumb,
      load: autoLoad, clear: autoClear
    };
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
    this._refreshMidletSelect();

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
    /* Opt-in: keep a state on the record so the next launch (with auto-resume
       on) can continue where the player stopped.  Must run before the VM is
       disposed; a MIDlet that exited on its own fails canSave and is skipped. */
    if (this.settings.autoSave) { await this._autoSaveOnExit(); }
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
      runtime.optionInt(NJW.OPT_HEAP_BYTES, this.settings.heapMb * 1024 * 1024);
      /* 'auto' leaves the engine's 240x320 default in place; an explicit
         option value is clamped by the port, so 0 would mean 32x32.  The
         preset lives on the game record, so a 176x220 title keeps its size. */
      const screenPreset = SCREEN_PRESETS[this._screenPreset()] || null;
      if (screenPreset) {
        runtime.optionInt(NJW.OPT_SCREEN_WIDTH, screenPreset[0]);
        runtime.optionInt(NJW.OPT_SCREEN_HEIGHT, screenPreset[1]);
      }
      runtime.optionString(NJW.OPT_RMS_DIR, savesDir);
      runtime.optionString(NJW.OPT_FS_DIR, filesDir);
      const midletClasses = Array.isArray(game.midletClasses) ? game.midletClasses : [];
      const midlet = midletClasses.length
        ? String(midletClasses[this._midletIndex()] || midletClasses[0])
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

    /* Continue from the newest save when the user asked for it.  The engine has
       just started and the frame loop has not begun pumping, which is exactly
       the state a manual "load slot" works from. */
    await this._autoResumeState();

    this._loading = true;
    this._stopShown = false;
    this._drawn = 0;
    this._fps = 0;
    this._fpsFrames = 0;
    this._fpsMark = 0;
    this._lastTs = null;
    this._frameAccumMs = 0;
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
    if (this._paused) { this._lastTs = timestamp; this._frameAccumMs = 0; return; }

    if (this._lastTs === null) {
      this._lastTs = timestamp;
    }
    let elapsed = timestamp - this._lastTs;
    this._lastTs = timestamp;
    if (!Number.isFinite(elapsed) || elapsed < 0) { elapsed = 0; }
    elapsed = clamp(elapsed, 0, 250);

    this.input.pollGamepads();

    /* Frame cap: 0 pumps one frame per display refresh with the real elapsed
       time (a host clock that follows the monitor); a numeric cap pumps
       fixed-length frames only when enough time has accumulated, so the
       emulated clock keeps running at real speed while the title is drawn at
       the rate it was written for (a 120 Hz screen no longer doubles 30 fps
       animations). */
    const cap = this._frameCap();
    if (cap > 0) {
      const frameMs = 1000 / cap;
      this._frameAccumMs = clamp((this._frameAccumMs || 0) + elapsed, 0, frameMs * 3);
      let pumps = 0;
      while (this._frameAccumMs >= frameMs - 0.5 && pumps < 3) {
        this._frameAccumMs -= frameMs;
        pumps += 1;
        if (!this._step(frameMs)) { return; }
      }
      if (pumps === 0) {
        /* No emulated frame this refresh; audio still needs its pull. */
        this.audio.pump();
        return;
      }
    } else {
      this._frameAccumMs = 0;
      if (!this._step(elapsed)) { return; }
    }

    this.audio.pump();
    this.input.pollText();
    this._updateFps(timestamp);
    this._updateStatus(false);
    this._updateDiagnostics();
  }

  /* Run one emulated frame. Returns false when the loop should stop (fatal
   * error or a clean MIDlet exit). */
  _step(dt) {
    let flags = 0;
    try {
      flags = this.runtime.frame(dt);
    } catch (error) {
      console.warn('[game] frame() threw', error);
      this._fatal(error && error.message ? error.message : String(error));
      return false;
    }

    if (flags < 0) {
      this._fatal(this._engineError() || t('game.errorDefault'));
      return false;
    }

    if (flags & NJW.FRAME_DREW) {
      this._paint();
      this._fpsFrames += 1;
    }

    if (this._loading && !(flags & NJW.FRAME_LOADING)) {
      this._loading = false;
      this._showLoading(false);
    }

    if (flags & NJW.FRAME_RUNNING) { return true; }

    /* Not running any more: the MIDlet exited cleanly. */
    this._onExited();
    return false;
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
    this._frameAccumMs = 0;
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
      this._updateDiagnostics();
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

  /** Keep the range input and its percentage label in sync with settings. */
  _updateVolumeUi() {
    const volume = $('#volume-slider');
    const value = $('#volume-value');
    if (!volume || !value) { return; }
    const percent = Math.round(this.settings.volume * 100);
    if (volume.value !== String(percent)) { volume.value = String(percent); }
    volume.setAttribute('aria-valuetext', percent + '%');
    volume.classList.toggle('is-muted', !!this.settings.muted);
    value.textContent = percent + '%';
  }

  /** Selected MIDlet index, clamped to the probed list. */
  _midletIndex() {
    const classes = Array.isArray(this.game?.midletClasses) ? this.game.midletClasses : [];
    const raw = Number(this.game?.midletIndex);
    return Number.isInteger(raw) && raw >= 0 && raw < classes.length ? raw : 0;
  }

  /** Populate the startup-MIDlet selector; hidden for single-MIDlet JARs. */
  _refreshMidletSelect() {
    const field = $('#midlet-field');
    const select = $('#midlet-select');
    if (!field || !select) { return; }
    const classes = Array.isArray(this.game?.midletClasses) ? this.game.midletClasses : [];
    if (classes.length < 2) {
      field.hidden = true;
      select.replaceChildren();
      return;
    }
    field.hidden = false;
    select.replaceChildren();
    for (let i = 0; i < classes.length; i++) {
      const full = String(classes[i]);
      const short = full.slice(full.lastIndexOf('.') + 1) || full;
      const option = el('option', { text: short, attrs: { value: String(i), title: full } });
      select.append(option);
    }
    select.value = String(this._midletIndex());
  }

  _updatePanelUi() {
    if (!this._slotRows.length) { return; }
    let canSave = false;
    try { canSave = !!this.runtime.canSave(); } catch (error) { canSave = false; }
    for (const row of this._slotRows) {
      row.save.textContent = t('game.save');
      row.load.textContent = t('game.load');
      row.export.textContent = t('game.export');
      row.import.textContent = t('game.import');
      row.name.textContent = t('game.slot', { n: row.slot });
      row.label.placeholder = t('game.slotNameHint');
      row.label.disabled = !row.row.dataset.filled;
      row.label.setAttribute('aria-label', t('game.slotName', { n: row.slot }));
      row.save.disabled = !canSave;
      row.load.disabled = !row.row.dataset.filled;
      row.export.disabled = !row.row.dataset.filled;
      row.import.disabled = !canSave;
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
      if (record && record.thumb) {
        row.thumb.src = record.thumb;
        row.thumb.hidden = false;
      } else {
        row.thumb.removeAttribute('src');
        row.thumb.hidden = true;
      }
      row.name.textContent = t('game.slot', { n: row.slot });
      row.label.value = record && typeof record.name === 'string' ? record.name : '';
      row.label.placeholder = t('game.slotNameHint');
      row.label.disabled = !record;
      row.label.setAttribute('aria-label', t('game.slotName', { n: row.slot }));
      row.save.textContent = t('game.save');
      row.load.textContent = t('game.load');
      row.export.textContent = t('game.export');
      row.import.textContent = t('game.import');
      row.save.disabled = !canSave;
      row.load.disabled = !record;
      row.export.disabled = !record;
      row.import.disabled = !canSave;
    }

    const auto = this._autoRow;
    if (auto) {
      const autoState = this.game.autoState;
      const has = !!(autoState && autoState.bytes);
      auto.row.hidden = !has;
      if (has) {
        const frames = Number(autoState.frames) || 0;
        auto.name.textContent = t('game.autoSaveSlot');
        const when = autoState.savedAt ? formatDateTime(autoState.savedAt) + ' · ' : '';
        auto.time.textContent = when + t('game.autoSaveFrames', { n: frames });
        if (autoState.thumb) {
          auto.thumb.src = autoState.thumb;
          auto.thumb.hidden = false;
        } else {
          auto.thumb.removeAttribute('src');
          auto.thumb.hidden = true;
        }
        auto.load.textContent = t('game.load');
        auto.load.disabled = false;
        auto.clear.textContent = t('game.clear');
        auto.clear.disabled = false;
      }
    }
  }

  async loadAutoSlot() {
    if (!this.game || !this.game.autoState) { return; }
    const record = this.game.autoState;
    let result = null;
    try { result = this.runtime.loadState(record.bytes); } catch (error) {
      result = { ok: false, error: error && error.message ? error.message : String(error) };
    }
    if (result && result.ok) {
      toast(t('game.loadOkAuto'), { kind: 'success' });
      await this._renderSlots();
      return;
    }
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

  async clearAutoSlot() {
    if (!this.game || !this.game.autoState) { return; }
    try {
      await putGame(Object.assign({}, this.game, { autoState: undefined }));
    } catch (error) {
      toast(t('game.autoSaveClearFailed'), { kind: 'error' });
      return;
    }
    this.game.autoState = undefined;
    toast(t('game.autoSaveCleared'), { kind: 'info' });
    await this._renderSlots();
  }

  /* Down-scale the live canvas into a data URL for the slot list.  The screen
   * is the visible frame; keeping a 96 px-wide PNG per slot costs a few KB in
   * IndexedDB and lets the player recognise a save at a glance. */
  _captureSlotThumbnail() {
    if (!this.canvas || !this.canvas.width || !this.canvas.height) { return ''; }
    try {
      const maxWidth = 96;
      const scale = Math.min(1, maxWidth / this.canvas.width);
      const width = Math.max(1, Math.round(this.canvas.width * scale));
      const height = Math.max(1, Math.round(this.canvas.height * scale));
      const scratch = document.createElement('canvas');
      scratch.width = width;
      scratch.height = height;
      const ctx = scratch.getContext('2d', { alpha: false });
      if (!ctx) { return ''; }
      ctx.drawImage(this.canvas, 0, 0, width, height);
      const url = scratch.toDataURL('image/png');
      return url.length > 32 ? url : '';
    } catch (error) {
      console.warn('[game] could not capture a save thumbnail', error);
      return '';
    }
  }

  /**
   * Restart a game from its newest save when "auto-resume" is on.
   *
   * A failed njw_load_state leaves the VM unusable (documented in
   * include/nj2me/nj_web.h), so a rejected save is not just ignored: the game
   * is restarted cleanly once, with auto-resume suppressed for that restart.
   */
  /* Store the state the player leaves behind as part of the game record.  The
     blob is the same engine memory a save slot holds; the record keeps it out
     of the three manual slots so a quick quit does not overwrite deliberate
     saves.  Failures are warnings: leaving the game must not be blocked. */
  async _autoSaveOnExit() {
    const game = this.game;
    if (!game || !this.runtime) { return; }
    let canSave = false;
    try { canSave = !!this.runtime.canSave(); } catch (error) { canSave = false; }
    if (!canSave) { return; }
    let result = null;
    try { result = this.runtime.saveState(); } catch (error) {
      result = { ok: false, error: error && error.message ? error.message : String(error) };
    }
    if (!result || !result.ok) {
      console.warn('[game] auto-save on exit failed', result && result.error);
      return;
    }
    const autoState = {
      bytes: result.bytes,
      frames: this.runtime.frames(),
      title: game.title || '',
      engineApi: this._engineApi,
      thumb: this._captureSlotThumbnail(),
      savedAt: Date.now()
    };
    try {
      await putGame(Object.assign({}, game, { autoState }));
    } catch (error) {
      console.warn('[game] could not store the exit auto-save', error);
    }
  }

  async _autoResumeState() {
    if (!this.settings.autoResume || !this.game) { return null; }
    /* The newest state may live in a manual slot or in the exit auto-save on
       the game record; "newest" is the largest frame count, which is what the
       player perceives as the furthest point. */
    const candidates = [];
    let states = null;
    try { states = await listStates(this.game.id); } catch (error) { states = null; }
    if (states) {
      for (const record of states.values()) {
        if (record && record.bytes) {
          candidates.push({ slot: record.slot, frames: record.frames, bytes: record.bytes, auto: false });
        }
      }
    }
    const autoState = this.game.autoState;
    if (autoState && autoState.bytes) {
      candidates.push({ slot: 'auto', frames: autoState.frames, bytes: autoState.bytes, auto: true });
    }
    let newest = null;
    for (const record of candidates) {
      if (!newest || (Number(record.frames) || 0) > (Number(newest.frames) || 0)) {
        newest = record;
      }
    }
    if (!newest) { return null; }
    let result = null;
    try { result = this.runtime.loadState(newest.bytes); } catch (error) {
      result = { ok: false, error: error && error.message ? error.message : String(error) };
    }
    if (result && result.ok) {
      if (newest.auto) {
        toast(t('game.autoResumedAuto', { frames: newest.frames }), { kind: 'success' });
      } else {
        toast(t('game.autoResumed', { n: newest.slot, frames: newest.frames }), { kind: 'success' });
      }
      return newest;
    }
    console.warn('[game] auto-resume failed', result && result.error);
    toast(t('game.autoResumeFailed'), { kind: 'warn' });
    if (!this._autoResumeRetried) {
      this._autoResumeRetried = true;
      this.settings.autoResume = false;
      try { await this.restart(); } finally { this.settings.autoResume = true; }
    }
    return null;
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
    const row = this._slotRows.find((entry) => entry.slot === slot);
    const name = row && row.label ? row.label.value.trim().slice(0, 24) : '';
    try {
      await putState(this.game.id, slot, {
        bytes: result.bytes,
        frames: this.runtime.frames(),
        title: this.game.title || '',
        engineApi: this._engineApi,
        thumb: this._captureSlotThumbnail(),
        name
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

  /**
   * Download a stored slot as a standalone `.njst` file.  The bytes are the
   * engine's own save blob (self-describing: it records the JAR identity and
   * ABI), so the file can be imported into any browser or desktop build that
   * can run the same title.
   */
  async saveSlotName(slot) {
    if (!this.game) { return; }
    const row = this._slotRows.find((entry) => entry.slot === slot);
    if (!row) { return; }
    if (row.label.disabled) { row.label.value = ''; return; }
    const name = row.label.value.trim().slice(0, 24);
    let record = null;
    try { record = await getState(this.game.id, slot); } catch (error) { record = null; }
    if (!record || !record.bytes) { row.label.value = ''; return; }
    if ((record.name || '') === name) { return; }
    try {
      await putState(this.game.id, slot, {
        bytes: record.bytes, frames: record.frames, title: record.title,
        engineApi: record.engineApi, thumb: record.thumb, name
      });
      toast(t('game.slotNameSaved'), { kind: 'success', timeout: 1600 });
    } catch (error) {
      toast(t('game.slotNameFailed') + ': ' + (error && error.message ? error.message : error),
        { kind: 'error' });
    }
    await this._renderSlots();
  }

  async exportSlot(slot) {
    if (!this.game) { return; }
    let record = null;
    try { record = await getState(this.game.id, slot); } catch (error) { record = null; }
    if (!record || !record.bytes || !record.bytes.length) {
      toast(t('game.exportEmpty'), { kind: 'warn' });
      return;
    }
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    /* The player's name for the slot is the friendliest filename; fall back to
       the title when the slot is unnamed. */
    const base = slugify(record.name || this.game.title || this.game.id || 'game');
    const name = base + '-slot' + slot + '-' + stamp + '.njst';
    downloadBlob(new Blob([record.bytes], { type: 'application/octet-stream' }), name);
    toast(t('game.exportOk', { name: name }), { kind: 'success' });
  }

  /**
   * Import a `.njst` save-state file into a slot and apply it immediately.
   * The file is validated by the engine's loader before it replaces the slot,
   * so a corrupt or different-title blob cannot silently overwrite good state.
   */
  importSlot(slot) {
    if (!this.game) { return; }
    if (this._importInput) { this._importInput.remove(); }
    const input = el('input', {
      attrs: {
        type: 'file',
        accept: '.njst,application/octet-stream',
        hidden: true
      }
    });
    this._importInput = input;
    input.addEventListener('change', async () => {
      const file = input.files && input.files[0];
      try {
        if (!file) { return; }
        if (file.size <= 0 || file.size > 256 * 1024 * 1024) {
          throw new Error('unexpected file size ' + file.size);
        }
        const bytes = new Uint8Array(await file.arrayBuffer());
        let result = null;
        try { result = this.runtime.loadState(bytes); } catch (error) {
          result = { ok: false, error: error && error.message ? error.message : String(error) };
        }
        if (!result || !result.ok) {
          toast(t('game.importFailed') + (result && result.error ? ': ' + result.error : ''),
            { kind: 'error' });
          return;
        }
        await putState(this.game.id, slot, {
          bytes: bytes,
          frames: this.runtime.frames(),
          title: this.game.title || '',
          engineApi: this._engineApi,
          /* A .njst blob is engine memory, not pixels: no preview until the
           * restored state is saved again from this browser. */
          thumb: ''
        });
        toast(t('game.importOk', { n: slot }), { kind: 'success' });
        await this._renderSlots();
      } catch (error) {
        toast(t('game.importFailed') + ': ' + (error && error.message ? error.message : error),
          { kind: 'error' });
      } finally {
        input.remove();
        if (this._importInput === input) { this._importInput = null; }
      }
    }, { once: true });
    document.body.append(input);
    input.click();
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
    this._updateVolumeUi();
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

  /*
   * The diagnostics panel reads the engine counters (heap, paints, GC, bytecode
   * rate) that the native --stats line prints too, so a browser bug report can
   * be compared against the command line.  Refreshing at 2.5 Hz keeps the panel
   * cheap; while closed nothing is read at all.
   */
  _updateDiagnostics() {
    if (!this._panelOpen) { return; }
    const now = performance.now();
    if (now - this._diagAt < 400) { return; }
    this._diagAt = now;
    const stats = (() => {
      try { return this.runtime.stats(); } catch (error) { return null; }
    })();
    const heap = $('#diag-heap');
    const paints = $('#diag-paints');
    const gc = $('#diag-gc');
    const insns = $('#diag-insns');
    if (!stats) {
      for (const node of [heap, paints, gc, insns]) { if (node) { node.textContent = '—'; } }
      return;
    }
    if (heap) {
      heap.textContent = this._formatMiB(stats.heapUsed) + ' / ' +
        this._formatMiB(stats.heapSize) + ' MiB';
    }
    if (paints) {
      paints.textContent = this._formatCount(stats.frames) + ' / ' + this._formatCount(stats.paints);
    }
    if (gc) { gc.textContent = this._formatCount(stats.gcCount); }
    if (insns) {
      const seconds = this._diagInsnsAt ? (now - this._diagInsnsAt) / 1000 : 0;
      const delta = this._diagInsnsAt ? Math.max(0, stats.insns - this._diagInsns) : 0;
      insns.textContent = seconds > 0 ? this._formatRate(delta / seconds) : '—';
      this._diagInsns = stats.insns;
      this._diagInsnsAt = now;
    }
  }

  /* Bytecode rate: most MIDlets are far below 1 M bytecodes/s, so scale down
   * instead of printing a meaningless 0.00 M/s. */
  _formatRate(rate) {
    if (!Number.isFinite(rate) || rate <= 0) { return '0 /s'; }
    if (rate >= 1e6) { return (rate / 1e6).toFixed(2) + ' M/s'; }
    if (rate >= 1e3) { return (rate / 1e3).toFixed(1) + ' K/s'; }
    return Math.round(rate) + ' /s';
  }

  _formatMiB(bytes) {
    return (Math.max(0, Number(bytes) || 0) / (1024 * 1024)).toFixed(1);
  }

  _formatCount(value) {
    return String(Math.round(Math.max(0, Number(value) || 0)));
  }

  /* --------------------------------------------------------------- misc */

  _engineError() {
    try { return this.runtime.error(); } catch (error) { return ''; }
  }

  _onExited() {
    if (this.onStopped) { this.onStopped(); }
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
    if (this.onStopped) { this.onStopped(); }
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
    const errorHeap = $('#error-heap');
    if (errorHeap) { errorHeap.hidden = true; }
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
    const detail = message || t('game.errorDefault');
    // The engine appends its last error-level line to fatal errors, so an
    // out-of-memory failure is recognisable; point at the memory selector and
    // offer the one-click restart with the next heap step.
    const outOfMemory = /outofmemory|out of memory|内存/i.test(detail);
    if (messageEl) {
      messageEl.textContent = detail + (outOfMemory ? ' ' + t('game.heapHint') : '');
    }
    this._showHeapRetry(outOfMemory);
    if (overlay) { overlay.hidden = false; }
    const retry = $('#error-retry');
    if (retry) { retry.textContent = t('game.retry'); }
    const back = $('#error-back');
    if (back) { back.textContent = t('game.back'); }
    const title = $('#error-title');
    if (title) { title.textContent = t('game.errorTitle'); }
  }

  /*
   * Out-of-memory failures get an extra overlay action: restarting with the
   * next heap step (16/32 -> 64, 64 -> 128 MB).  It is hidden for every other
   * failure and when the shell is already offering the largest heap.
   */
  _showHeapRetry(outOfMemory) {
    const button = $('#error-heap');
    if (!button) { return; }
    const suggested = outOfMemory ? this._suggestHeapMb() : 0;
    if (!suggested) {
      button.hidden = true;
      delete button.dataset.mb;
      return;
    }
    button.dataset.mb = String(suggested);
    button.textContent = t('game.retryWithHeap', { mb: suggested });
    button.hidden = false;
  }

  /* The screen preset is a per-game device profile: titles were drawn for a
   * fixed resolution, so the choice travels with the game record (and is
   * absent on records imported before this existed → 'auto'). */
  _frameCap() {
    const override = this.game ? Number(this.game.frameCap) : NaN;
    if (FPS_CHOICES.includes(override)) { return override; }
    const fallback = Number(this.settings.frameCap);
    return FPS_CHOICES.includes(fallback) ? fallback : 60;
  }

  _screenPreset() {
    const preset = this.game && typeof this.game.screenPreset === 'string'
      ? this.game.screenPreset
      : 'auto';
    return Object.prototype.hasOwnProperty.call(SCREEN_PRESETS, preset) ? preset : 'auto';
  }

  _suggestHeapMb() {
    const current = Number(this.settings.heapMb) || HEAP_CHOICES_MB[0];
    if (current < 64) { return 64; }
    if (current < 128) { return 128; }
    return 0;
  }

  _onGesture() {
    this.audio.ensureContext();
    if (!this._paused) { this.audio.resume(); }
  }

  _onGamepad(connected) {
    if (!this._running) { return; }
    toast(connected ? t('game.gamepadConnected') : t('game.gamepadDisconnected'), {
      kind: 'info',
      timeout: 2600
    });
  }

  _applySettingsToUi() {
    const speed = $('#speed-select');
    if (speed) { speed.value = String(this.settings.speedPercent); }
    const fps = $('#fps-select');
    if (fps) { fps.value = String(this._frameCap()); }
    const heap = $('#heap-select');
    if (heap) { heap.value = String(this.settings.heapMb); }
    const autoResume = $('#auto-resume');
    if (autoResume) { autoResume.checked = this.settings.autoResume === true; }
    const autoSave = $('#auto-save');
    if (autoSave) { autoSave.checked = this.settings.autoSave === true; }
    const screenSize = $('#screen-select');
    if (screenSize) { screenSize.value = this._screenPreset(); }
    this._updateVolumeUi();
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
      row.label.placeholder = t('game.slotNameHint');
      row.label.setAttribute('aria-label', t('game.slotName', { n: row.slot }));
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
