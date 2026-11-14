/*
 * nano_j2me web shell - runtime facade
 *
 * This is the ONLY file that touches the Emscripten module.  Everything else
 * (library, game loop, input, audio, panel) talks to the Runtime object
 * defined here, which mirrors include/nj2me/nj_web.h one-to-one so the C
 * contract stays visible:
 *
 *   lifecycle   optionsReset() / optionInt() / optionString() / start() /
 *               frame() / running() / displayableReady() / stop() / error() /
 *               stopReason()
 *   framebuffer width() / height() / framebuffer() / frameEpoch()
 *   input       key() / pointer() / textInput() / textEdit() / textReplace() /
 *               textActive() / textCurrent() / textState()
 *   audio       audioAvailable() / audioRate() / audioChannels() /
 *               audioRead() / audioNotePlaying()
 *   library     probe() / appTitle() / appVendor() / appVersion() /
 *               midletCount() / midletClassAt() / appIcon()
 *   save state  canSave() / saveState() / loadState()
 *   clock       setSpeed() / speed()
 *   filesystem  initFileSystem() / writeFile() / mkdirp() / exists() /
 *               unlink() / syncFileSystem()
 *
 * Loading: import('./nj2me.js') (the emcc module built next to this file with
 * -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createNj2meModule) and call its
 * factory.  When that import fails (wasm not built yet, page opened with
 * ?mock=1, or the engine is incompatible) loadRuntime() resolves to
 * web/js/mock-module.js instead, which implements exactly the same interface.
 * The page therefore never depends on wasm being present to be usable.
 *
 * Frame flags (include/nj2me/nj_web.h): NJW_FRAME_RUNNING=1, DREW=2, LOADING=4;
 * negative return means a fatal error, readable through error().
 */

export const NJW = Object.freeze({
  API_VERSION: 1,

  FRAME_RUNNING: 1,
  FRAME_DREW: 2,
  FRAME_LOADING: 4,

  OPT_SCREEN_WIDTH: 1,
  OPT_SCREEN_HEIGHT: 2,
  OPT_HEAP_BYTES: 3,
  OPT_SPEED_PERCENT: 4,
  OPT_RMS_DIR: 5,
  OPT_FS_DIR: 6,
  OPT_DEVICE: 7,
  OPT_MIDLET_CLASS: 8,
  OPT_LOAD_STATE_PATH: 9,

  POINTER_DOWN: 0,
  POINTER_UP: 1,
  POINTER_MOVE: 2,

  TEXT_EDIT: Object.freeze({
    BACKSPACE: 1,
    DELETE: 2,
    LEFT: 3,
    RIGHT: 4,
    HOME: 5,
    END: 6
  })
});

/** nj_key enum, for the keyboard map and on-screen pad. */
export const NJ_KEYS = Object.freeze({
  NONE: 0,
  NUM_0: 1,
  NUM_1: 2,
  NUM_2: 3,
  NUM_3: 4,
  NUM_4: 5,
  NUM_5: 6,
  NUM_6: 7,
  NUM_7: 8,
  NUM_8: 9,
  NUM_9: 10,
  STAR: 11,
  POUND: 12,
  UP: 13,
  DOWN: 14,
  LEFT: 15,
  RIGHT: 16,
  FIRE: 17,
  SOFT_LEFT: 18,
  SOFT_RIGHT: 19,
  CLEAR: 20,
  GAME_A: 21,
  GAME_B: 22,
  GAME_C: 23,
  GAME_D: 24,
  VOLUP: 25,
  VOLDOWN: 26,
  POWER: 27
});

export const DEFAULT_WIDTH = 240;
export const DEFAULT_HEIGHT = 320;
export const DEFAULT_HEAP_BYTES = 16 * 1024 * 1024;
export const LARGE_HEAP_BYTES = 32 * 1024 * 1024;
export const FS_ROOT = '/nano_j2me';
export const FS_JARS = FS_ROOT + '/jars';
export const FS_SAVES = FS_ROOT + '/saves';
export const FS_FILES = FS_ROOT + '/files';
export const RUNTIME_MODULE_URL = './nj2me.js';

const FS_SYNC_INTERVAL_MS = 5000;

/* ===================================================================== */
/*  Emscripten backed runtime                                            */
/* ===================================================================== */

export class WasmRuntime {
  /**
   * @param {object} moduleInstance resolved Emscripten Module object
   */
  constructor(moduleInstance) {
    this.kind = 'wasm';
    this.mock = false;
    this.mockReason = null;
    this.mockError = null;
    this._m = moduleInstance;
    this._fns = new Map();
    this._scratch = 0;
    this._scratchBytes = 0;
    this._fsInit = null;
    this._fsPersistent = false;
    this._fsWarning = null;
    this._syncTimer = 0;
    this._syncQueued = false;
    this._syncPromise = null;
    this._pagehide = null;
    this._disposed = false;
    /* Raw engine stdout/stderr lines (see loadRuntime; [nj2me] logs go to info). */
    this.engineLog = [];

    if (!moduleInstance) { throw new Error('WasmRuntime needs a Module instance'); }
    /* Fail early with a clear message instead of a random TypeError later. */
    if (!this._tryFn('njw_api_version')) {
      throw new Error('module does not export njw_api_version()');
    }
  }

  /* ------------------------------------------------------------ exports */

  _tryFn(name) {
    if (this._fns.has(name)) { return this._fns.get(name); }
    const m = this._m;
    const candidates = ['_' + name, name];
    let fn = null;
    for (const candidate of candidates) {
      if (typeof m[candidate] === 'function') { fn = m[candidate]; break; }
    }
    this._fns.set(name, fn);
    return fn;
  }

  _fn(name) {
    const fn = this._tryFn(name);
    if (!fn) { throw new Error('wasm module is missing export ' + name); }
    return fn;
  }

  _u8() {
    const m = this._m;
    const view = m.HEAPU8;
    if (view && view.buffer && view.byteLength) { return view; }
    if (m.wasmMemory && m.wasmMemory.buffer) { return new Uint8Array(m.wasmMemory.buffer); }
    throw new Error('no HEAPU8 view on the wasm module');
  }

  _i32() {
    const m = this._m;
    const view = m.HEAP32;
    if (view && view.buffer && view.byteLength) { return view; }
    return new Int32Array(this._u8().buffer);
  }

  _u32() {
    const m = this._m;
    const view = m.HEAPU32;
    if (view && view.buffer && view.byteLength) { return view; }
    return new Uint32Array(this._u8().buffer);
  }

  _i16() {
    const m = this._m;
    const view = m.HEAP16;
    if (view && view.buffer && view.byteLength) { return view; }
    return new Int16Array(this._u8().buffer);
  }

  _malloc(bytes) {
    return this._fn('malloc')(bytes | 0) >>> 0;
  }

  _free(ptr) {
    if (!ptr) { return; }
    const fn = this._tryFn('free');
    if (fn) { fn(ptr); }
  }

  /** njw_alloc: 16-byte aligned, used for buffers the runtime copies or keeps. */
  _alloc(bytes) {
    const fn = this._tryFn('njw_alloc');
    if (fn) { return fn(bytes | 0) >>> 0; }
    return this._malloc(bytes);
  }

  _freeAlloc(ptr) {
    if (!ptr) { return; }
    const fn = this._tryFn('njw_free');
    if (fn) { fn(ptr); } else { this._free(ptr); }
  }

  /** Encode a JS string as NUL-terminated UTF-8 in the wasm heap. */
  _encodeString(str) {
    const text = String(str ?? '');
    let encoded;
    let needed;
    const lengthFn = this._tryFn('lengthBytesUTF8');
    if (lengthFn) {
      needed = lengthFn(text) >>> 0;
      encoded = null;
    } else {
      encoded = new TextEncoder().encode(text);
      needed = encoded.length;
    }
    const ptr = this._malloc(needed + 1);
    if (!ptr) { throw new Error('out of memory'); }
    const heap = this._u8();
    if (encoded) { heap.set(encoded, ptr); } else {
      const writeFn = this._tryFn('stringToUTF8');
      if (writeFn) {
        writeFn(text, ptr, needed + 1);
      } else {
        heap.set(new TextEncoder().encode(text), ptr);
      }
    }
    heap[ptr + needed] = 0;
    return { ptr, size: needed };
  }

  /** Read a NUL-terminated UTF-8 string from the wasm heap. */
  _decodeString(ptr) {
    if (!ptr) { return ''; }
    const fn = this._tryFn('UTF8ToString');
    if (fn) {
      try { return fn(ptr) || ''; } catch (e) { /* fall through */ }
    }
    const heap = this._u8();
    let end = ptr;
    while (end < heap.length && heap[end] !== 0) { end++; }
    try {
      return new TextDecoder('utf-8').decode(heap.subarray(ptr, end));
    } catch (e) {
      return '';
    }
  }

  /** Call fn(pointerToUtf8String) and free the temporary allocation. */
  _withString(str, fn) {
    const { ptr } = this._encodeString(str);
    try {
      return fn(ptr);
    } finally {
      this._free(ptr);
    }
  }

  /* ----------------------------------------------------------- identity */

  apiVersion() {
    return this._fn('njw_api_version')();
  }

  runtimeVersion() {
    return this._stringResult('njw_runtime_version');
  }

  /* ------------------------------------------------------------ options */

  optionsReset() {
    this._fn('njw_options_reset')();
  }

  optionInt(option, value) {
    return this._fn('njw_option_int')(option | 0, value | 0) === 0;
  }

  optionString(option, value) {
    return this._withString(value, (ptr) =>
      this._fn('njw_option_string')(option | 0, ptr) === 0);
  }

  /* ---------------------------------------------------------- lifecycle */

  /** Returns true on success; error() describes a failure. */
  start(jarPath) {
    return this._withString(jarPath, (ptr) => this._fn('njw_start')(ptr) === 0);
  }

  /** Raw frame flags: non-negative bitmask, negative = fatal error. */
  frame(dtMs) {
    return this._fn('njw_frame')(Number(dtMs) || 0) | 0;
  }

  running() {
    return this._fn('njw_running')() !== 0;
  }

  displayableReady() {
    return this._fn('njw_displayable_ready')() !== 0;
  }

  stop() {
    try { this._fn('njw_stop')(); } catch (e) { /* stop must never throw */ }
  }

  error() {
    const fn = this._tryFn('njw_error');
    if (!fn) { return ''; }
    return this._decodeString(fn());
  }

  stopReason() {
    const fn = this._tryFn('njw_stop_reason');
    if (!fn) { return ''; }
    return this._decodeString(fn());
  }

  frames() {
    return this._fn('njw_frames')() >>> 0;
  }

  /* --------------------------------------------------------- framebuffer */

  width() {
    return this._fn('njw_width')() | 0;
  }

  height() {
    return this._fn('njw_height')() | 0;
  }

  frameEpoch() {
    return this._fn('njw_frame_epoch')() >>> 0;
  }

  /** RGBA8888 view straight into the wasm heap; valid until the next frame. */
  framebuffer() {
    const ptr = this._fn('njw_framebuffer')() >>> 0;
    if (!ptr) { return null; }
    const w = this.width();
    const h = this.height();
    if (w <= 0 || h <= 0) { return null; }
    const bytes = w * h * 4;
    const heap = this._u8();
    if (ptr + bytes > heap.byteLength) { return null; }
    return heap.subarray(ptr, ptr + bytes);
  }

  /* --------------------------------------------------------------- input */

  key(code, down) {
    this._fn('njw_key')(code | 0, down ? 1 : 0);
  }

  pointer(action, x, y) {
    this._fn('njw_pointer')(action | 0, x | 0, y | 0);
  }

  /* ------------------------------------------------------- text editing */

  textInput(text) {
    this._withString(text, (ptr) => this._fn('njw_text_input')(ptr));
  }

  textEdit(command) {
    this._fn('njw_text_edit')(command | 0);
  }

  textReplace(text, cursor) {
    return this._withString(text, (ptr) =>
      this._fn('njw_text_replace')(ptr, cursor | 0) !== 0);
  }

  textActive() {
    return this._fn('njw_text_active')() !== 0;
  }

  /** Full current editor contents; the runtime exposes it as UTF-8. */
  textCurrent() {
    const fn = this._fn('njw_text_current');
    const needed = fn(0, 0) | 0;
    if (needed <= 0) { return ''; }
    const cap = needed + 1;
    const ptr = this._malloc(cap);
    if (!ptr) { return ''; }
    try {
      fn(ptr, cap);
      return this._decodeString(ptr);
    } finally {
      this._free(ptr);
    }
  }

  /**
   * Text editor state.  Returns null when the runtime has nothing to say.
   * constraints: MIDP TextField constraints (low 16 bits = kind: 1 EMAILADDR,
   * 2 NUMERIC, 3 PHONENUMBER, 4 URL, 5 DECIMAL; 0x20000 password is never
   * exposed as a native editor, see nj_lib_lcdui.c).
   */
  textState() {
    const fn = this._fn('njw_text_state');
    const ptr = this._malloc(24);
    if (!ptr) { return null; }
    try {
      const rc = fn(ptr, ptr + 4, ptr + 8, ptr + 12, ptr + 16, ptr + 20) | 0;
      if (rc < 0) { return null; }
      const i32 = this._i32();
      const u32 = this._u32();
      return {
        active: i32[ptr >> 2] !== 0,
        multiline: i32[(ptr + 4) >> 2] !== 0,
        constraints: u32[(ptr + 8) >> 2] >>> 0,
        maxLength: i32[(ptr + 12) >> 2] | 0,
        length: i32[(ptr + 16) >> 2] | 0,
        cursor: i32[(ptr + 20) >> 2] | 0
      };
    } finally {
      this._free(ptr);
    }
  }

  /* --------------------------------------------------------------- audio */

  audioAvailable() {
    return this._fn('njw_audio_available')() !== 0;
  }

  audioRate() {
    const rate = this._fn('njw_audio_rate')() | 0;
    return rate > 0 ? rate : 22050;
  }

  audioChannels() {
    const channels = this._fn('njw_audio_channels')() | 0;
    return channels > 0 ? channels : 1;
  }

  /**
   * Pull up to maxFrames interleaved S16 frames.  Returns an Int16Array copy
   * (safe across wasm memory growth) or null when no audio is pending.
   */
  audioRead(maxFrames) {
    if (!this.audioAvailable()) { return null; }
    const channels = this.audioChannels();
    const frames = Math.max(1, maxFrames | 0);
    const bytes = frames * channels * 2;
    if (bytes > this._scratchBytes) {
      if (this._scratch) { this._freeAlloc(this._scratch); }
      this._scratch = this._alloc(bytes);
      this._scratchBytes = this._scratch ? bytes : 0;
      if (!this._scratch) { return null; }
    }
    const got = this._fn('njw_audio_read')(this._scratch, frames) | 0;
    if (got <= 0) { return null; }
    const samples = got * channels;
    const out = new Int16Array(samples);
    const heap = this._i16();
    const base = this._scratch >> 1;
    out.set(heap.subarray(base, base + samples));
    return out;
  }

  audioNotePlaying(frames) {
    const fn = this._tryFn('njw_audio_note_playing');
    if (fn) { fn(frames | 0); }
  }

  /* -------------------------------------------------------------- probe */

  probe(jarPath) {
    return this._withString(jarPath, (ptr) => this._fn('njw_probe')(ptr) === 0);
  }

  appTitle() { return this._stringResult('njw_app_title'); }
  appVendor() { return this._stringResult('njw_app_vendor'); }
  appVersion() { return this._stringResult('njw_app_version'); }

  midletCount() {
    return this._fn('njw_midlet_count')() | 0;
  }

  midletClassAt(index) {
    if (index < 0) { return ''; }
    return this._stringResult('njw_midlet_class_at', index);
  }

  _stringResult(fnName, arg) {
    const fn = this._fn(fnName);
    const ptr = arg === undefined ? fn() : fn(arg);
    return this._decodeString(ptr);
  }

  /** Probe icon bytes (PNG/JPEG/GIF) copied out of the wasm heap, or null. */
  appIcon() {
    const fn = this._tryFn('njw_app_icon');
    if (!fn) { return null; }
    const lenPtr = this._malloc(4);
    if (!lenPtr) { return null; }
    try {
      const ptr = fn(lenPtr) >>> 0;
      const length = this._i32()[lenPtr >> 2] | 0;
      if (!ptr || length <= 0) { return null; }
      const heap = this._u8();
      if (ptr + length > heap.byteLength) { return null; }
      return { bytes: heap.slice(ptr, ptr + length), mime: sniffImageMime(heap, ptr, length) };
    } finally {
      this._free(lenPtr);
    }
  }

  /* --------------------------------------------------------- save states */

  canSave() {
    return this._fn('njw_can_save')() !== 0;
  }

  /**
   * Save the VM.  The runtime allocates the blob with njw_alloc and we free it
   * after copying.  Returns { ok, bytes?, error? }.
   */
  saveState() {
    const fn = this._fn('njw_save_state');
    const outPtr = this._malloc(8); /* void** blob, int* len */
    if (!outPtr) { return { ok: false, error: 'out of memory' }; }
    try {
      const rc = fn(outPtr, outPtr + 4) | 0;
      if (rc !== 0) {
        return { ok: false, error: this.error() || ('njw_save_state returned ' + rc) };
      }
      const u32 = this._u32();
      const i32 = this._i32();
      const blobPtr = u32[outPtr >> 2] >>> 0;
      const length = i32[(outPtr + 4) >> 2] | 0;
      if (!blobPtr || length <= 0) {
        return { ok: false, error: this.error() || 'empty save state' };
      }
      const bytes = this._u8().slice(blobPtr, blobPtr + length);
      this._freeAlloc(blobPtr);
      return { ok: true, bytes };
    } catch (e) {
      return { ok: false, error: e && e.message ? e.message : String(e) };
    } finally {
      this._free(outPtr);
    }
  }

  /** Load a save-state blob.  On failure the caller must restart the game. */
  loadState(bytes) {
    const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    if (!view.length) { return { ok: false, error: 'empty save state' }; }
    const ptr = this._alloc(view.length);
    if (!ptr) { return { ok: false, error: 'out of memory' }; }
    try {
      this._u8().set(view, ptr);
      const rc = this._fn('njw_load_state')(ptr, view.length) | 0;
      if (rc !== 0) {
        return { ok: false, error: this.error() || ('njw_load_state returned ' + rc) };
      }
      return { ok: true };
    } catch (e) {
      return { ok: false, error: e && e.message ? e.message : String(e) };
    } finally {
      this._freeAlloc(ptr);
    }
  }

  /* ------------------------------------------------------------- clock */

  setSpeed(percent) {
    try { this._fn('njw_set_speed')(percent | 0); } catch (e) { /* ignore */ }
  }

  speed() {
    const fn = this._tryFn('njw_speed');
    return fn ? (fn() | 0) : 100;
  }

  /* -------------------------------------------------------- filesystem */

  /**
   * Mount IDBFS at /nano_j2me and restore whatever the previous session wrote
   * (RecordStore, JSR-75 files, ...).  Idempotent; resolves once the initial
   * sync has finished (or failed gracefully).
   */
  initFileSystem() {
    if (this._fsInit) { return this._fsInit; }
    this._fsInit = this._initFileSystem();
    return this._fsInit;
  }

  async _initFileSystem() {
    const FS = this._m.FS;
    if (!FS) {
      this._fsPersistent = false;
      this._fsWarning = 'no FS';
      return;
    }
    try { FS.mkdir(FS_ROOT); } catch (e) { /* already exists */ }
    const IDBFS = this._m.IDBFS ||
      (FS.filesystems && (FS.filesystems.IDBFS || FS.filesystems['IDBFS']));
    if (IDBFS) {
      try {
        FS.mount(IDBFS, { autoPersist: true }, FS_ROOT);
        this._fsPersistent = true;
      } catch (e) {
        try {
          FS.mount(IDBFS, {}, FS_ROOT);
          this._fsPersistent = true;
        } catch (e2) {
          this._fsPersistent = false;
        }
      }
    }
    if (this._fsPersistent) {
      /* Pull in what IndexedDB has from earlier sessions (best effort). */
      await this._syncFileSystem(true, 10000);
      this._syncTimer = window.setInterval(() => {
        this.syncFileSystem(false);
      }, FS_SYNC_INTERVAL_MS);
      this._pagehide = () => { this.syncFileSystem(false); };
      window.addEventListener('pagehide', this._pagehide);
      document.addEventListener('visibilitychange', this._pagehide);
    } else {
      this._fsWarning = 'IDBFS unavailable';
    }
  }

  get fsPersistent() { return this._fsPersistent; }
  get fsWarning() { return this._fsWarning; }

  async _syncFileSystem(populate, timeoutMs) {
    const FS = this._m.FS;
    if (!FS || typeof FS.syncfs !== 'function') { return false; }
    return new Promise((resolve) => {
      let done = false;
      const finish = (ok) => {
        if (done) { return; }
        done = true;
        resolve(ok);
      };
      const timer = window.setTimeout(() => finish(false), timeoutMs || 8000);
      try {
        FS.syncfs(!!populate, (err) => {
          window.clearTimeout(timer);
          finish(!err);
        });
      } catch (e) {
        window.clearTimeout(timer);
        finish(false);
      }
    });
  }

  /** Persist the emscripten FS to IndexedDB (no-op for non-persistent builds). */
  syncFileSystem(populate = false) {
    if (!this._fsPersistent) { return Promise.resolve(false); }
    if (this._syncQueued && !populate) { return this._syncPromise || Promise.resolve(false); }
    this._syncQueued = true;
    this._syncPromise = this._syncFileSystem(populate, 8000).finally(() => {
      this._syncQueued = false;
    });
    return this._syncPromise;
  }

  _FS() {
    const FS = this._m.FS;
    if (!FS) { throw new Error('module has no Emscripten FS'); }
    return FS;
  }

  /** Recursive mkdir over any absolute path ("/a/b/c"). */
  mkdirp(path) {
    const FS = this._FS();
    const parts = String(path).split('/').filter(Boolean);
    let current = '';
    for (const part of parts) {
      current += '/' + part;
      try {
        FS.mkdir(current);
      } catch (e) {
        let exists = false;
        try { exists = !!FS.analyzePath(current).exists; } catch (e2) { exists = false; }
        if (!exists) { throw e; }
      }
    }
  }

  exists(path) {
    try {
      return !!this._FS().analyzePath(String(path)).exists;
    } catch (e) {
      return false;
    }
  }

  writeFile(path, bytes) {
    const FS = this._FS();
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    FS.writeFile(String(path), data);
    this.syncFileSystem(false);
  }

  unlink(path) {
    try { this._FS().unlink(String(path)); } catch (e) { /* ignore missing */ }
    this.syncFileSystem(false);
  }

  /* ------------------------------------------------------------- cleanup */

  dispose() {
    if (this._disposed) { return; }
    this._disposed = true;
    if (this._syncTimer) { window.clearInterval(this._syncTimer); this._syncTimer = 0; }
    if (this._pagehide) {
      window.removeEventListener('pagehide', this._pagehide);
      document.removeEventListener('visibilitychange', this._pagehide);
      this._pagehide = null;
    }
    if (this._scratch) { this._freeAlloc(this._scratch); this._scratch = 0; this._scratchBytes = 0; }
    try { this._fn('njw_stop')(); } catch (e) { /* ignore */ }
  }
}

/** Identify the image type of the probe icon from its magic bytes. */
function sniffImageMime(heap, ptr, length) {
  if (length >= 8 && heap[ptr] === 0x89 && heap[ptr + 1] === 0x50) { return 'image/png'; }
  if (length >= 3 && heap[ptr] === 0xff && heap[ptr + 1] === 0xd8) { return 'image/jpeg'; }
  if (length >= 6 && heap[ptr] === 0x47 && heap[ptr + 1] === 0x49 && heap[ptr + 2] === 0x46) { return 'image/gif'; }
  return 'image/png';
}

/* ===================================================================== */
/*  Loading                                                              */
/* ===================================================================== */

function wantsMockMode() {
  try {
    const params = new URLSearchParams(window.location.search);
    const value = (params.get('mock') || '').toLowerCase();
    return value === '1' || value === 'true' || value === 'yes' || params.has('mock');
  } catch (e) {
    return false;
  }
}

/**
 * Load the runtime.  Never throws: on any failure it resolves to the mock
 * runtime and records why, so the shell can show a banner instead of a blank
 * page.  Resolved object is either a WasmRuntime or a MockRuntime; both have
 * `kind` ('wasm' | 'mock'), `mock` boolean, `mockReason`, `mockError`.
 */
/**
 * Check whether the Emscripten module is actually served next to this module
 * before importing it.  A dynamic import of a missing ./nj2me.js makes the
 * browser log a network/MIME console error; probing first keeps "wasm not
 * built yet" a silent, intentional fallback to mock mode.
 */
async function probeRuntimeModule() {
  if (typeof fetch !== 'function') { return { ok: true }; }
  try {
    /* fetch() resolves relative URLs against the document, not this module:
     * resolve against import.meta.url exactly like the dynamic import does. */
    const url = new URL(RUNTIME_MODULE_URL, import.meta.url).href;
    const response = await fetch(url, { method: 'HEAD', cache: 'no-store' });
    if (response.status === 405) { return { ok: true }; } /* HEAD not allowed: let import decide */
    if (!response.ok) {
      return { ok: false, error: new Error('wasm engine not found (HTTP ' + response.status + ')') };
    }
    const type = (response.headers.get('content-type') || '').toLowerCase();
    if (type.includes('text/html')) {
      return { ok: false, error: new Error('wasm engine not found (nj2me.js served as HTML)') };
    }
    return { ok: true };
  } catch (error) {
    return { ok: false, error };
  }
}

export async function loadRuntime(options = {}) {
  let createMockRuntime;
  const ensureMock = async (extra = {}) => {
    if (!createMockRuntime) {
      const mod = await import('./mock-module.js');
      createMockRuntime = mod.createMockRuntime;
    }
    return createMockRuntime(Object.assign({}, options, extra));
  };

  if (options.forceMock || wantsMockMode()) {
    return ensureMock();
  }
  if (typeof WebAssembly === 'undefined') {
    return ensureMock();
  }

  const probe = await probeRuntimeModule();
  if (!probe.ok) {
    return ensureMock({ reason: 'failed', error: probe.error });
  }

  let factory = null;
  try {
    const mod = await import(RUNTIME_MODULE_URL);
    factory = mod.default || mod.createNj2meModule || mod.nj2me;
    if (typeof factory !== 'function') {
      throw new Error('nj2me.js has no createNj2meModule factory export');
    }
  } catch (error) {
    return ensureMock({ error });
  }

  let instance = null;
  let runtime = null;
  const engineLog = [];
  /*
   * Emscripten defaults printErr to console.error, which turns ordinary engine
   * logs ("[nj2me] running ...", codec notices) into browser console errors.
   * Route them explicitly: engine chatter -> info/debug, warnings/abnormal
   * output -> warn, and keep every raw line on runtime.engineLog for the tools
   * panel and for tests.  Nothing here should call console.error.
   */
  const routeEngineOutput = (level) => (text) => {
    const line = String(text === undefined || text === null ? '' : text);
    engineLog.push({ level, text: line });
    if (engineLog.length > 500) { engineLog.shift(); }
    if (line.startsWith('[nj2me]')) {
      console.info(line);
    } else if (level === 'err') {
      console.warn(line);
    } else {
      console.debug(line);
    }
  };
  try {
    instance = await factory({
      locateFile: (path) => new URL(path, import.meta.url).href,
      print: routeEngineOutput('out'),
      printErr: routeEngineOutput('err')
    });
    runtime = new WasmRuntime(instance);
    runtime.engineLog = engineLog;
  } catch (error) {
    return ensureMock({ error });
  }

  /* A shell refuses to run against a module built from a different header. */
  let api = -1;
  try { api = runtime.apiVersion() | 0; } catch (e) { api = -1; }
  if (api !== NJW.API_VERSION) {
    try { runtime.dispose(); } catch (e) { /* ignore */ }
    return ensureMock({ reason: 'incompatible', got: api, want: NJW.API_VERSION });
  }

  try {
    await runtime.initFileSystem();
  } catch (e) {
    /* Files not persistent is degraded, not fatal: keep playing. */
  }
  return runtime;
}

/** Runtime kind shown in the library footer ("wasm" | "mock"). */
export function runtimeStorageWarning(runtime) {
  if (!runtime || runtime.mock) { return null; }
  if (runtime.fsPersistent === false) {
    return 'storage';
  }
  return null;
}
