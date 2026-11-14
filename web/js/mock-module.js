/*
 * nano_j2me web shell - mock runtime
 *
 * A complete stand-in for the wasm engine, implementing exactly the same
 * Runtime interface as WasmRuntime in module.js.  It exists so the whole shell
 * (library, import, game view, input, panel, save states) can be developed,
 * demonstrated and tested before the Emscripten module is built, and so a
 * missing/broken nj2me.js degrades to a visible, usable page instead of a
 * blank one.
 *
 * What it fakes:
 *   - a 240x320 (or the configured) animated test picture drawn with a 2D
 *     canvas: moving grid/maze, bouncing orb with a trail, "MOCK MODE" badge,
 *     FPS/frame counters, held keys and pointer marker;
 *   - a short loading period (24 frames) so the loading overlay is exercised;
 *   - probe metadata derived from the file name, plus a generated PNG icon;
 *   - save states (a small self-describing JSON blob);
 *   - the text editor bridge (press F1 / soft-left to open a simulated editor,
 *     type, press Enter to commit; soft-right cancels) so the hidden textarea
 *     path can be tested without a MIDlet.
 *
 * What it does not fake: audio (audioAvailable() is always false), real MIDlet
 * execution, RMS persistence.  Pressing R (GAME_D) simulates the MIDlet
 * exiting normally so the stop-reason panel can be exercised.
 */

import {
  NJW,
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT,
  FS_ROOT
} from './module.js';

const MOCK_LOADING_FRAMES = 24;
const MOCK_TEXT_MAX = 40;
const SAVE_MAGIC = 'NJWMOCK1';

/* FNV-1a, for stable colours from a title. */
function hashString(text) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

function clamp(value, min, max) {
  return value < min ? min : (value > max ? max : value);
}

/** Turn "/a/b/My%20Game.jar" into "My Game". */
function titleFromPath(path) {
  let base = String(path || '').split('/').pop() || '';
  try { base = decodeURIComponent(base); } catch (e) { /* keep raw */ }
  base = base.replace(/\.jar$/i, '').trim();
  return base || 'Mock Game';
}

function utf8Bytes(text) {
  return new TextEncoder().encode(text);
}

function asciiAt(bytes, offset, length) {
  let out = '';
  for (let i = 0; i < length; i++) {
    out += String.fromCharCode(bytes[offset + i] || 0);
  }
  return out;
}

export class MockRuntime {
  constructor(options = {}) {
    this.kind = 'mock';
    this.mock = true;
    this.mockReason = options.reason || (options.error ? 'failed' : 'requested');
    this.mockError = options.error || null;
    this.mockGot = options.got;
    this.mockWant = options.want;
    /* Facade parity: the wasm runtime keeps raw engine output here. */
    this.engineLog = [];

    this._w = DEFAULT_WIDTH;
    this._h = DEFAULT_HEIGHT;
    this._speedPercent = 100;
    this._heapBytes = 16 * 1024 * 1024;

    this._running = false;
    this._frames = 0;
    this._vtime = 0;
    this._fps = 0;
    this._lastDt = 16.7;
    this._startedAt = 0;
    this._stopRequested = false;
    this._stopReason = '';

    this._held = new Set();
    this._lastKey = null;
    this._keyLog = [];
    this._pointer = { x: -1, y: -1, down: false };

    this._textActive = false;
    this._text = '';
    this._textCursor = 0;
    this._textMax = MOCK_TEXT_MAX;

    this._meta = {
      title: 'Mock Game',
      vendor: 'nano_j2me',
      version: 'mock',
      classes: ['mock.MockMidlet'],
      icon: null
    };

    this._ctx = null;
    this._fb = null;
    this._imageData = null;
    this._trail = [];
    this._initCanvas();
  }

  /* ------------------------------------------------------------ identity */

  apiVersion() { return NJW.API_VERSION; }
  runtimeVersion() { return 'mock'; }

  /* ------------------------------------------------------------- options */

  optionsReset() {
    this._w = DEFAULT_WIDTH;
    this._h = DEFAULT_HEIGHT;
    this._speedPercent = 100;
    this._heapBytes = 16 * 1024 * 1024;
    this._applySize();
  }

  optionInt(option, value) {
    switch (option) {
      case NJW.OPT_SCREEN_WIDTH:
        if (value >= 32 && value <= 2048) { this._w = value | 0; this._applySize(); }
        return true;
      case NJW.OPT_SCREEN_HEIGHT:
        if (value >= 32 && value <= 2048) { this._h = value | 0; this._applySize(); }
        return true;
      case NJW.OPT_HEAP_BYTES:
        this._heapBytes = value | 0;
        return true;
      case NJW.OPT_SPEED_PERCENT:
        this.setSpeed(value);
        return true;
      default:
        return true;
    }
  }

  optionString(option, value) {
    /* RMS_DIR / FS_DIR / DEVICE / MIDLET_CLASS are accepted and ignored. */
    void option;
    void value;
    return true;
  }

  /* ----------------------------------------------------------- lifecycle */

  start(jarPath) {
    this._running = true;
    this._frames = 0;
    this._vtime = 0;
    this._fps = 0;
    this._lastDt = 16.7;
    this._startedAt = performance.now();
    this._stopRequested = false;
    this._stopReason = '';
    this._held.clear();
    this._lastKey = null;
    this._keyLog = [];
    this._trail = [];
    this._pointer = { x: -1, y: -1, down: false };
    this._textActive = false;
    this._text = '';
    this._textCursor = 0;
    const title = this._meta && this._meta.title ? this._meta.title : titleFromPath(jarPath);
    this._title = title;
    this._draw();
    return true;
  }

  frame(dtMs) {
    if (!this._running) { return 0; }
    const clamped = clamp(Number(dtMs) || 0, 0, 250);
    this._frames += 1;
    this._lastDt = clamped;
    if (clamped > 0) {
      this._fps = this._fps === 0 ? (1000 / clamped) : this._fps * 0.9 + (1000 / clamped) * 0.1;
    }
    this._vtime += clamped * (this._speedPercent / 100) / 1000;

    if (this._stopRequested) {
      this._running = false;
      this._stopReason = 'the MIDlet exited normally (MOCK)';
      this._draw();
      return NJW.FRAME_DREW;
    }
    this._draw();
    let flags = NJW.FRAME_RUNNING | NJW.FRAME_DREW;
    if (this._frames <= MOCK_LOADING_FRAMES) {
      flags |= NJW.FRAME_LOADING;
    }
    return flags;
  }

  running() { return this._running; }

  displayableReady() { return !this._running || this._frames > MOCK_LOADING_FRAMES; }

  stop() {
    if (this._running) {
      this._running = false;
      if (!this._stopReason) { this._stopReason = 'stopped by the host (MOCK)'; }
    }
  }

  error() { return ''; }
  stopReason() { return this._stopReason; }
  frames() { return this._frames; }

  /* --------------------------------------------------------- framebuffer */

  width() { return this._w; }
  height() { return this._h; }
  frameEpoch() { return this._frames; }

  framebuffer() {
    return this._fb;
  }

  /* --------------------------------------------------------------- input */

  key(code, down) {
    const numeric = code | 0;
    if (down) {
      this._held.add(numeric);
      this._lastKey = numeric;
      this._keyLog.push({ code: numeric, at: this._vtime });
      if (this._keyLog.length > 4) { this._keyLog.shift(); }
      if (numeric === 18) {
        /* Soft-left opens the simulated editor (see the file header). */
        this._textActive = true;
        if (!this._text) { this._text = 'mock text'; }
        this._textCursor = this._text.length;
      } else if (this._textActive && (numeric === 19 || numeric === 17)) {
        this._textActive = false;
      }
      if (numeric === 24) { this._stopRequested = true; }
    } else {
      this._held.delete(numeric);
    }
  }

  pointer(action, x, y) {
    this._pointer.x = x | 0;
    this._pointer.y = y | 0;
    if (action === NJW.POINTER_DOWN) { this._pointer.down = true; }
    if (action === NJW.POINTER_UP) { this._pointer.down = false; }
  }

  /* ------------------------------------------------------- text editing */

  textInput(text) {
    if (!this._textActive) { return; }
    this._text = (this._text + String(text)).slice(0, this._textMax);
    this._textCursor = this._text.length;
  }

  textEdit(command) {
    if (!this._textActive) { return; }
    const c = this._textCursor;
    switch (command) {
      case NJW.TEXT_EDIT.BACKSPACE:
        if (c > 0) { this._text = this._text.slice(0, c - 1) + this._text.slice(c); this._textCursor = c - 1; }
        break;
      case NJW.TEXT_EDIT.DELETE:
        if (c < this._text.length) { this._text = this._text.slice(0, c) + this._text.slice(c + 1); }
        break;
      case NJW.TEXT_EDIT.LEFT: this._textCursor = Math.max(0, c - 1); break;
      case NJW.TEXT_EDIT.RIGHT: this._textCursor = Math.min(this._text.length, c + 1); break;
      case NJW.TEXT_EDIT.HOME: this._textCursor = 0; break;
      case NJW.TEXT_EDIT.END: this._textCursor = this._text.length; break;
      default: break;
    }
  }

  textReplace(text, cursor) {
    if (!this._textActive) { return false; }
    this._text = String(text == null ? '' : text).slice(0, this._textMax);
    this._textCursor = clamp(cursor | 0, 0, this._text.length);
    return true;
  }

  textActive() { return this._textActive; }

  textCurrent() { return this._text; }

  textState() {
    return {
      active: this._textActive,
      multiline: false,
      constraints: 0,
      maxLength: this._textMax,
      length: this._text.length,
      cursor: this._textCursor
    };
  }

  /* --------------------------------------------------------------- audio */

  audioAvailable() { return false; }
  audioRate() { return 22050; }
  audioChannels() { return 1; }
  audioRead() { return null; }
  audioNotePlaying() { /* nothing is playing */ }

  /* -------------------------------------------------------------- probe */

  probe(jarPath) {
    const title = titleFromPath(jarPath);
    const seed = hashString(title);
    this._meta = {
      title,
      vendor: 'nano_j2me MOCK',
      version: '1.0-' + (seed & 0xff).toString(16).padStart(2, '0'),
      classes: ['mock.' + title.replace(/[^A-Za-z0-9]/g, '') + 'Midlet'],
      icon: this._generateIcon(title, seed)
    };
    return true;
  }

  appTitle() { return this._meta.title; }
  appVendor() { return this._meta.vendor; }
  appVersion() { return this._meta.version; }
  midletCount() { return this._meta.classes.length; }
  midletClassAt(index) { return this._meta.classes[index] || ''; }

  appIcon() {
    return this._meta.icon ? { bytes: this._meta.icon, mime: 'image/png' } : null;
  }

  /* --------------------------------------------------------- save states */

  canSave() { return true; }

  saveState() {
    try {
      const json = JSON.stringify({
        title: this._title || this._meta.title,
        frames: this._frames,
        vtime: this._vtime,
        savedAt: Date.now(),
        width: this._w,
        height: this._h
      });
      const body = utf8Bytes(json);
      const bytes = new Uint8Array(SAVE_MAGIC.length + body.length);
      for (let i = 0; i < SAVE_MAGIC.length; i++) { bytes[i] = SAVE_MAGIC.charCodeAt(i); }
      bytes.set(body, SAVE_MAGIC.length);
      return { ok: true, bytes };
    } catch (e) {
      return { ok: false, error: e && e.message ? e.message : String(e) };
    }
  }

  loadState(bytes) {
    try {
      const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
      if (view.length <= SAVE_MAGIC.length) { return { ok: false, error: 'truncated mock save' }; }
      if (asciiAt(view, 0, SAVE_MAGIC.length) !== SAVE_MAGIC) {
        return { ok: false, error: 'not a mock save state' };
      }
      const json = new TextDecoder('utf-8').decode(view.subarray(SAVE_MAGIC.length));
      const data = JSON.parse(json);
      this._frames = Number(data.frames) || 0;
      this._vtime = Number(data.vtime) || 0;
      if (data.title) { this._title = String(data.title); }
      return { ok: true };
    } catch (e) {
      return { ok: false, error: e && e.message ? e.message : String(e) };
    }
  }

  /* ------------------------------------------------------------- clock */

  setSpeed(percent) {
    this._speedPercent = clamp(percent | 0, 10, 800);
  }

  speed() { return this._speedPercent; }

  /* -------------------------------------------------------- filesystem */

  initFileSystem() { return Promise.resolve(); }
  get fsPersistent() { return true; }
  get fsWarning() { return null; }
  mkdirp() { /* no real FS in mock mode */ }
  exists() { return false; }
  writeFile() { /* no real FS in mock mode */ }
  unlink() { /* no real FS in mock mode */ }
  syncFileSystem() { return Promise.resolve(true); }

  dispose() {
    this._running = false;
    this._ctx = null;
  }

  /* =================================================================== */
  /*  everything below is the mock picture renderer                      */
  /* =================================================================== */

  _initCanvas() {
    try {
      this._canvas = document.createElement('canvas');
      this._ctx = this._canvas.getContext('2d', { alpha: false });
    } catch (e) {
      this._ctx = null;
    }
    this._applySize();
  }

  _applySize() {
    if (!this._ctx || !this._canvas) { return; }
    if (this._canvas.width !== this._w || this._canvas.height !== this._h) {
      this._canvas.width = this._w;
      this._canvas.height = this._h;
    }
    this._imageData = this._ctx.createImageData(this._w, this._h);
    this._fb = new Uint8Array(this._w * this._h * 4);
  }

  _generateIcon(title, seed) {
    try {
      const canvas = document.createElement('canvas');
      canvas.width = 48;
      canvas.height = 48;
      const ctx = canvas.getContext('2d');
      if (!ctx) { return null; }
      const hue = seed % 360;
      const gradient = ctx.createLinearGradient(0, 0, 48, 48);
      gradient.addColorStop(0, 'hsl(' + hue + ', 65%, 48%)');
      gradient.addColorStop(1, 'hsl(' + ((hue + 45) % 360) + ', 70%, 26%)');
      ctx.fillStyle = gradient;
      ctx.fillRect(0, 0, 48, 48);
      ctx.fillStyle = 'rgba(255,255,255,0.16)';
      ctx.fillRect(0, 34, 48, 4);
      ctx.fillRect(0, 42, 48, 2);
      const glyph = Array.from(title)[0] || '?';
      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 26px ui-monospace, monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(glyph, 24, 26);
      const dataUrl = canvas.toDataURL('image/png');
      const base64 = dataUrl.split(',')[1] || '';
      const binary = atob(base64);
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i++) { bytes[i] = binary.charCodeAt(i); }
      return bytes;
    } catch (e) {
      return null;
    }
  }

  _draw() {
    const ctx = this._ctx;
    if (!ctx) { return; }
    const w = this._w;
    const h = this._h;
    const t = this._vtime;
    const loading = this._frames <= MOCK_LOADING_FRAMES;
    const pad = Math.max(6, Math.round(w * 0.03));

    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.imageSmoothingEnabled = false;
    ctx.globalAlpha = 1;

    /* background */
    ctx.fillStyle = '#070b16';
    ctx.fillRect(0, 0, w, h);
    const glow = ctx.createRadialGradient(w / 2, h * 0.45, w * 0.05, w / 2, h * 0.45, h * 0.7);
    glow.addColorStop(0, '#0d1830');
    glow.addColorStop(1, '#05070e');
    ctx.fillStyle = glow;
    ctx.fillRect(0, 0, w, h);

    /* scrolling grid */
    const step = Math.max(12, Math.round(w / 15));
    const offset = (t * 18) % step;
    ctx.strokeStyle = 'rgba(70, 130, 255, 0.18)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = -step + offset; x < w; x += step) {
      ctx.moveTo(Math.round(x) + 0.5, 0);
      ctx.lineTo(Math.round(x) + 0.5, h);
    }
    for (let y = -step + offset; y < h; y += step) {
      ctx.moveTo(0, Math.round(y) + 0.5);
      ctx.lineTo(w, Math.round(y) + 0.5);
    }
    ctx.stroke();

    /* a slowly shifting maze on top of the grid */
    const cell = Math.max(18, Math.round(w / 10));
    const phase = Math.floor(t / 1.5);
    ctx.strokeStyle = 'rgba(80, 200, 255, 0.22)';
    ctx.beginPath();
    for (let cy = 0; cy * cell < h; cy++) {
      for (let cx = 0; cx * cell < w; cx++) {
        const seed = hashString(cx + ':' + cy + ':' + phase);
        if ((seed & 7) > 2) { continue; }
        const x = cx * cell + 3;
        const y = cy * cell + 3;
        const s = cell - 6;
        ctx.moveTo(x, y);
        ctx.lineTo(x + s, y);
        if (seed & 1) { ctx.lineTo(x + s, y + s); }
      }
    }
    ctx.stroke();

    /* moving diagonal light band */
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, w, h);
    ctx.clip();
    ctx.translate(w / 2, h / 2);
    ctx.rotate(-0.65);
    const band = ((t * 60) % (w * 2.2)) - w * 1.1;
    ctx.fillStyle = 'rgba(60, 235, 180, 0.06)';
    ctx.fillRect(band, -h, w * 0.22, h * 2);
    ctx.restore();

    /* bouncing orb + trail (the trail is what proves the loop is running) */
    const orbX = w / 2 + Math.sin(t * 1.3) * (w * 0.28) + Math.sin(t * 2.7) * (w * 0.06);
    const orbY = h * 0.42 + Math.cos(t * 1.7) * (h * 0.12);
    this._trail.push({ x: orbX, y: orbY });
    if (this._trail.length > 22) { this._trail.shift(); }
    for (let i = 0; i < this._trail.length; i++) {
      const point = this._trail[i];
      const alpha = (i + 1) / this._trail.length;
      ctx.fillStyle = 'rgba(255, 170, 60, ' + (0.06 + alpha * 0.35).toFixed(3) + ')';
      const size = 2 + alpha * 5;
      ctx.fillRect(Math.round(point.x - size / 2), Math.round(point.y - size / 2), Math.round(size), Math.round(size));
    }
    ctx.fillStyle = '#ffc45e';
    ctx.fillRect(Math.round(orbX) - 3, Math.round(orbY) - 3, 6, 6);
    ctx.fillStyle = '#fff3d0';
    ctx.fillRect(Math.round(orbX) - 1, Math.round(orbY) - 1, 2, 2);

    /* status bar */
    const statusH = Math.max(14, Math.round(h * 0.05));
    ctx.fillStyle = 'rgba(8, 14, 28, 0.92)';
    ctx.fillRect(0, 0, w, statusH);
    ctx.fillStyle = 'rgba(120, 170, 255, 0.35)';
    ctx.fillRect(0, statusH - 1, w, 1);
    ctx.font = '9px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    ctx.fillStyle = '#9dc0ff';
    const title = (this._title || this._meta.title || 'Mock Game');
    ctx.fillText(this._truncate(ctx, title, w - 70), pad, statusH / 2 + 1);
    ctx.textAlign = 'right';
    ctx.fillStyle = '#5f7fa8';
    ctx.fillText('F ' + this._frames, w - pad, statusH / 2 + 1);

    /* FPS / speed readout */
    ctx.textAlign = 'left';
    ctx.font = '8px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(150, 200, 255, 0.75)';
    const fps = this._fps ? this._fps.toFixed(1) : '–';
    ctx.fillText('fps ' + fps + '   speed ' + this._speedPercent + '%', pad, statusH + 8);

    if (loading) {
      this._drawLoading(ctx, w, h, pad);
    } else {
      this._drawMockBadge(ctx, w, h, t);
      if (this._textActive) { this._drawTextEditor(ctx, w, h, t, pad); }
    }

    /* pointer crosshair */
    if (this._pointer.down && this._pointer.x >= 0) {
      ctx.strokeStyle = 'rgba(255, 90, 160, 0.9)';
      ctx.lineWidth = 1;
      const px = this._pointer.x;
      const py = this._pointer.y;
      ctx.beginPath();
      ctx.moveTo(px - 6, py); ctx.lineTo(px + 6, py);
      ctx.moveTo(px, py - 6); ctx.lineTo(px, py + 6);
      ctx.stroke();
    }

    /* held keys chips */
    const held = Array.from(this._held).sort((a, b) => a - b);
    if (held.length) {
      ctx.font = '8px ui-monospace, SFMono-Regular, Menlo, monospace';
      let x = pad;
      const y = h - Math.round(h * 0.085) - 12;
      for (const code of held.slice(0, 6)) {
        const label = String(code);
        const width = ctx.measureText(label).width + 8;
        ctx.fillStyle = 'rgba(60, 120, 220, 0.55)';
        ctx.fillRect(x, y, width, 10);
        ctx.fillStyle = '#e8f2ff';
        ctx.textAlign = 'center';
        ctx.fillText(label, x + width / 2, y + 5);
        ctx.textAlign = 'left';
        x += width + 3;
      }
    }

    /* soft key bar */
    const barH = Math.max(18, Math.round(h * 0.065));
    const barY = h - barH;
    ctx.fillStyle = 'rgba(6, 10, 20, 0.95)';
    ctx.fillRect(0, barY, w, barH);
    ctx.fillStyle = 'rgba(120, 170, 255, 0.3)';
    ctx.fillRect(0, barY, w, 1);
    ctx.font = '9px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    ctx.fillStyle = this._held.has(18) ? '#7ef0c8' : '#7192ba';
    ctx.fillText('MENU', pad, barY + barH / 2 + 1);
    ctx.textAlign = 'right';
    ctx.fillStyle = this._held.has(19) ? '#7ef0c8' : '#7192ba';
    ctx.fillText('BACK', w - pad, barY + barH / 2 + 1);
    /* fire button pill */
    const fireW = Math.round(w * 0.24);
    const fireH = Math.max(10, barH - 8);
    const fireX = Math.round((w - fireW) / 2);
    const fireY = barY + Math.round((barH - fireH) / 2);
    ctx.fillStyle = this._held.has(17) ? '#ffbf4d' : 'rgba(255, 170, 60, 0.25)';
    ctx.fillRect(fireX, fireY, fireW, fireH);
    ctx.strokeStyle = 'rgba(255, 190, 90, 0.7)';
    ctx.strokeRect(fireX + 0.5, fireY + 0.5, fireW - 1, fireH - 1);
    ctx.fillStyle = this._held.has(17) ? '#1a1206' : '#ffcf80';
    ctx.textAlign = 'center';
    ctx.fillText('FIRE', fireX + fireW / 2, fireY + fireH / 2 + 1);

    /* tiny mode note above the bar */
    ctx.textAlign = 'center';
    ctx.font = '7px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(140, 170, 210, 0.55)';
    ctx.fillText('R = exit MIDlet (mock)', w / 2, barY - 4);

    /* present */
    try {
      const image = ctx.getImageData(0, 0, w, h);
      this._fb.set(image.data);
    } catch (e) {
      /* getImageData can fail only if the canvas is tainted - it never is */
    }
  }

  _drawLoading(ctx, w, h, pad) {
    const progress = Math.min(1, this._frames / (MOCK_LOADING_FRAMES + 1));
    const cx = w / 2;
    const cy = h * 0.5;
    const radius = Math.max(10, Math.round(w * 0.06));
    ctx.strokeStyle = 'rgba(120, 170, 255, 0.2)';
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.stroke();
    ctx.strokeStyle = '#4ee6b8';
    ctx.beginPath();
    ctx.arc(cx, cy, radius, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * progress);
    ctx.stroke();
    ctx.textAlign = 'center';
    ctx.fillStyle = '#b9d2f5';
    ctx.font = 'bold 10px ui-monospace, monospace';
    ctx.fillText('LOADING', cx, cy + radius + 14);
    ctx.fillStyle = 'rgba(150, 180, 220, 0.6)';
    ctx.font = '8px ui-monospace, monospace';
    ctx.fillText(Math.round(progress * 100) + '%', cx, cy + radius + 26);
  }

  _drawMockBadge(ctx, w, h, t) {
    const text = 'MOCK MODE';
    ctx.font = 'bold 20px ui-monospace, SFMono-Regular, Menlo, monospace';
    const metrics = ctx.measureText(text);
    const boxW = metrics.width + 24;
    const boxH = 30;
    const boxX = Math.round((w - boxW) / 2);
    const boxY = Math.round(h * 0.31 + Math.sin(t * 2) * 3);
    ctx.fillStyle = 'rgba(6, 16, 24, 0.82)';
    ctx.fillRect(boxX, boxY, boxW, boxH);
    ctx.strokeStyle = 'rgba(78, 230, 184, 0.85)';
    ctx.lineWidth = 1;
    ctx.strokeRect(boxX + 0.5, boxY + 0.5, boxW - 1, boxH - 1);
    ctx.fillStyle = '#4ee6b8';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, w / 2, boxY + boxH / 2 + 1);
    ctx.font = '8px ui-monospace, monospace';
    ctx.fillStyle = '#8fb0d8';
    ctx.fillText('wasm engine not built - simulated picture', w / 2, boxY + boxH + 12);
    ctx.fillStyle = 'rgba(140, 170, 210, 0.65)';
    ctx.fillText('tools/build_web.sh builds the real module', w / 2, boxY + boxH + 24);
  }

  _drawTextEditor(ctx, w, h, t, pad) {
    const boxW = w - pad * 2;
    const boxH = 46;
    const boxX = pad;
    const boxY = Math.round(h * 0.62);
    ctx.fillStyle = 'rgba(8, 14, 26, 0.95)';
    ctx.fillRect(boxX, boxY, boxW, boxH);
    ctx.strokeStyle = '#ffb84d';
    ctx.lineWidth = 1;
    ctx.strokeRect(boxX + 0.5, boxY + 0.5, boxW - 1, boxH - 1);
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.font = '8px ui-monospace, monospace';
    ctx.fillStyle = '#ffb84d';
    ctx.fillText('MOCK TEXT INPUT (F1) - ENTER COMMITS', boxX + 5, boxY + 4);
    ctx.font = '11px ui-monospace, monospace';
    ctx.fillStyle = '#f2f7ff';
    const shown = this._truncate(ctx, this._text || '', boxW - 16);
    ctx.fillText(shown || '…', boxX + 5, boxY + 16);
    /* caret */
    if (Math.floor(t * 2) % 2 === 0) {
      const before = this._text.slice(0, this._textCursor);
      const caretX = boxX + 5 + ctx.measureText(this._truncate(ctx, before, boxW - 16)).width;
      ctx.fillStyle = '#ffb84d';
      ctx.fillRect(Math.round(caretX), boxY + 16, 1, 12);
    }
    ctx.font = '7px ui-monospace, monospace';
    ctx.fillStyle = 'rgba(200, 215, 235, 0.65)';
    ctx.fillText('soft-right / fire closes', boxX + 5, boxY + boxH - 9);
  }

  _truncate(ctx, text, maxWidth) {
    if (ctx.measureText(text).width <= maxWidth) { return text; }
    let value = text;
    while (value.length > 1 && ctx.measureText(value + '…').width > maxWidth) {
      value = value.slice(0, -1);
    }
    return value + '…';
  }
}

/** Factory used by module.js loadRuntime(); mirrors the emcc factory shape. */
export function createMockRuntime(options = {}) {
  return new MockRuntime(options);
}
