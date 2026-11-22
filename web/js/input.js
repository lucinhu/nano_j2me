/*
 * nano_j2me web shell - input
 *
 * One place for everything that turns user intent into nj_key / njw_pointer /
 * njw_text_replace calls:
 *
 *   keyboard      : arrives once on keydown (repeat filtered), released on
 *                   keyup; all held keys are released when the game is paused,
 *                   the window loses focus or the game view is left.
 *   on-screen pad : pointer-events based (multi-touch, press on finger-down,
 *                   release on up/cancel/leave), with a pressed visual state.
 *   pointer       : canvas pointer drag is mapped from CSS pixels to the
 *                   framebuffer resolution and forwarded to njw_pointer().
 *   text bridge   : a hidden <textarea> mirrors the MIDlet text editor; the
 *                   game loop polls pollText() every frame and we push the
 *                   whole editor value + caret with njw_text_replace().
 *
 * Keyboard map (kept in one table so it is easy to audit; see web/README.md):
 *
 *   Arrows        D-pad       13 UP, 14 DOWN, 15 LEFT, 16 RIGHT
 *   Enter / Space Fire        17
 *   F1            Soft left   18
 *   F2 / Escape   Soft right  19
 *   Backspace     Clear       20
 *   0..9          digits      1..10  (number row and numpad)
 *   *             star        11
 *   #             pound       12
 *   Q W E R       GAME_A..D   21..24
 *   PageUp / PageDown (and media volume keys, when the browser exposes them)
 *                             25 VOLUP, 26 VOLDOWN
 *
 * While the text bridge has focus, printable keys, arrows, Backspace/Delete,
 * Home/End and IME composition are handled by the textarea itself (its input
 * event turns into njw_text_replace); Enter commits a single-line editor by
 * sending nj_key FIRE, Escape cancels by sending SOFT_RIGHT, matching the
 * Android shell's IME behaviour.
 */

import { NJW, NJ_KEYS } from './module.js';
import { focusWithoutScroll, prefersReducedMotion } from './ui.js';

const CODE_MAP = new Map([
  ['ArrowUp', NJ_KEYS.UP],
  ['ArrowDown', NJ_KEYS.DOWN],
  ['ArrowLeft', NJ_KEYS.LEFT],
  ['ArrowRight', NJ_KEYS.RIGHT],
  ['Enter', NJ_KEYS.FIRE],
  ['NumpadEnter', NJ_KEYS.FIRE],
  ['Space', NJ_KEYS.FIRE],
  ['F1', NJ_KEYS.SOFT_LEFT],
  ['F2', NJ_KEYS.SOFT_RIGHT],
  ['Escape', NJ_KEYS.SOFT_RIGHT],
  ['Backspace', NJ_KEYS.CLEAR],
  ['NumpadMultiply', NJ_KEYS.STAR],
  ['NumpadDivide', NJ_KEYS.POUND],
  ['KeyQ', NJ_KEYS.GAME_A],
  ['KeyW', NJ_KEYS.GAME_B],
  ['KeyE', NJ_KEYS.GAME_C],
  ['KeyR', NJ_KEYS.GAME_D],
  ['PageUp', NJ_KEYS.VOLUP],
  ['PageDown', NJ_KEYS.VOLDOWN],
  ['AudioVolumeUp', NJ_KEYS.VOLUP],
  ['AudioVolumeDown', NJ_KEYS.VOLDOWN]
]);

/*
 * Standard-mapping gamepad (kept in one table too).  The shell polls
 * navigator.getGamepads() once per game frame; buttons map to the same nj_key
 * codes as the keyboard and on-screen pad, so a controller behaves like the
 * phone keypad:
 *
 *   A                  FIRE 17
 *   B X Y LB           GAME_A..D 21..24   (B is a game key, not soft-right,
 *                                          which exits several MIDlets)
 *   RB                 CLEAR 20
 *   LT / Start         SOFT_LEFT 18
 *   RT / Back          SOFT_RIGHT 19
 *   D-pad / left stick UP DOWN LEFT RIGHT 13..16 (deadzone 0.45)
 */
const GAMEPAD_BUTTON_MAP = new Map([
  [0, NJ_KEYS.FIRE],        /* A */
  [1, NJ_KEYS.GAME_A],      /* B: a game key, not "soft right" (which exits
                               several MIDlets, e.g. the Hello World demo) */
  [2, NJ_KEYS.GAME_B],      /* X */
  [3, NJ_KEYS.GAME_C],      /* Y */
  [4, NJ_KEYS.GAME_D],      /* LB */
  [5, NJ_KEYS.CLEAR],       /* RB */
  [6, NJ_KEYS.SOFT_LEFT],   /* LT */
  [7, NJ_KEYS.SOFT_RIGHT],  /* RT */
  [8, NJ_KEYS.SOFT_RIGHT],  /* Back / select */
  [9, NJ_KEYS.SOFT_LEFT],   /* Start */
  [12, NJ_KEYS.UP],
  [13, NJ_KEYS.DOWN],
  [14, NJ_KEYS.LEFT],
  [15, NJ_KEYS.RIGHT]
]);
const GAMEPAD_STICK_DEADZONE = 0.45;

const DIGIT_CODE = /^(?:Digit|Numpad)([0-9])$/;

/*
 * Actions whose keyboard binding the player may override per game.  The shell
 * keeps the defaults in CODE_MAP; a game record may carry
 * `keyMap: { fire: 'KeyZ', clear: 'KeyX', ... }` (KeyboardEvent.code values)
 * and setKeyOverrides() applies it without touching anything else.
 */
export const KEY_ACTIONS = Object.freeze([
  { id: 'up', key: NJ_KEYS.UP, code: 'ArrowUp' },
  { id: 'down', key: NJ_KEYS.DOWN, code: 'ArrowDown' },
  { id: 'left', key: NJ_KEYS.LEFT, code: 'ArrowLeft' },
  { id: 'right', key: NJ_KEYS.RIGHT, code: 'ArrowRight' },
  { id: 'fire', key: NJ_KEYS.FIRE, code: 'Enter' },
  { id: 'softLeft', key: NJ_KEYS.SOFT_LEFT, code: 'F1' },
  { id: 'softRight', key: NJ_KEYS.SOFT_RIGHT, code: 'F2' },
  { id: 'clear', key: NJ_KEYS.CLEAR, code: 'Backspace' }
]);

/** Human label for a KeyboardEvent.code, for the remap dialog. */
export function codeLabel(code) {
  if (typeof code !== 'string' || code === '') { return '?'; }
  if (code === 'Space') { return 'Space'; }
  if (code === 'ArrowUp') { return '↑'; }
  if (code === 'ArrowDown') { return '↓'; }
  if (code === 'ArrowLeft') { return '←'; }
  if (code === 'ArrowRight') { return '→'; }
  if (code === 'Escape') { return 'Esc'; }
  if (code === 'Backspace') { return '⌫'; }
  if (code === 'Enter' || code === 'NumpadEnter') { return 'Enter'; }
  if (/^Key[A-Z]$/.test(code)) { return code.slice(3); }
  if (/^Digit[0-9]$/.test(code)) { return code.slice(5); }
  if (/^Numpad[0-9]$/.test(code)) { return 'Num ' + code.slice(6); }
  if (/^F[0-9]{1,2}$/.test(code)) { return code; }
  return code.replace(/^(Ctrl|Alt|Shift|Meta)(Left|Right)$/, '$1');
}

/** Map a KeyboardEvent to nj_key, or null when the shell should ignore it. */
export function keyFromEvent(event) {
  const code = event.code || '';
  /*
   * '*' and '#' must be checked before the digit table: on common layouts they
   * are Shift+Digit8 / Shift+Digit3, so event.code would map them to '8'/'3'.
   */
  if (event.key === '*') { return NJ_KEYS.STAR; }
  if (event.key === '#') { return NJ_KEYS.POUND; }
  const digit = DIGIT_CODE.exec(code);
  if (digit) {
    /* 0..9 -> 1..10 (nj_key NUM_0 is 1). */
    return NJ_KEYS.NUM_0 + Number(digit[1]);
  }
  const mapped = CODE_MAP.get(code);
  return mapped === undefined ? null : mapped;
}

const TEXT_INPUT_MODES = {
  1: 'email',
  2: 'numeric',
  3: 'tel',
  4: 'url',
  5: 'decimal'
};

function clamp(value, min, max) {
  return value < min ? min : (value > max ? max : value);
}

function isFormField(node) {
  if (!node) { return false; }
  const tag = node.tagName;
  return tag === 'TEXTAREA' || tag === 'INPUT' || tag === 'SELECT' || node.isContentEditable === true;
}

export class GameInput {
  /**
   * @param {object} opts
   * @param {object} opts.runtime  facade from module.js (WasmRuntime/MockRuntime)
   * @param {HTMLCanvasElement} opts.canvas
   * @param {HTMLElement} opts.controls on-screen pad container
   * @param {HTMLTextAreaElement} opts.textarea hidden text bridge
   * @param {HTMLElement} opts.textIndicator
   * @param {() => void} [opts.onGesture] called on the first user gesture in
   *        the game view, so the audio context can be resumed from a gesture.
   * @param {(code: number, down: boolean) => void} [opts.onKey]
   */
  constructor(opts) {
    this.runtime = opts.runtime;
    this.canvas = opts.canvas;
    this.controls = opts.controls;
    this.textarea = opts.textarea;
    this.textIndicator = opts.textIndicator;
    this.onGesture = typeof opts.onGesture === 'function' ? opts.onGesture : null;
    this.onKey = typeof opts.onKey === 'function' ? opts.onKey : null;
    this.onGamepad = typeof opts.onGamepad === 'function' ? opts.onGamepad : null;

    this.enabled = false;
    this.textActive = false;
    this._multiline = false;
    this._maxLength = 0;
    this._gestureSeen = false;

    this._heldCodes = new Map();      // physical event.code -> nj_key
    this._keyMap = new Map(CODE_MAP); // effective map (defaults + per-game overrides)
    this._keyOverrides = {};          // last argument of setKeyOverrides()
    this._gamepadHeld = new Set();    // nj_key currently pressed on a pad
    this._controlPointers = new Map(); // pointerId -> { code, button }
    this._canvasPointers = new Map(); // pointerId -> { x, y }

    this._onKeyDown = this._onKeyDown.bind(this);
    this._onKeyUp = this._onKeyUp.bind(this);
    this._onWindowBlur = this._onWindowBlur.bind(this);
    this._onControlsDown = this._onControlsDown.bind(this);
    this._onControlsMove = this._onControlsMove.bind(this);
    this._onControlsRelease = this._onControlsRelease.bind(this);
    this._onCanvasDown = this._onCanvasDown.bind(this);
    this._onCanvasMove = this._onCanvasMove.bind(this);
    this._onCanvasUp = this._onCanvasUp.bind(this);
    this._onTextInput = this._onTextInput.bind(this);
    this._onTextKeyDown = this._onTextKeyDown.bind(this);
    this._onTextBlur = this._onTextBlur.bind(this);
    this._onTextDone = this._onTextDone.bind(this);
    this._onGamepadConnected = this._onGamepadConnected.bind(this);
    this._onGamepadDisconnected = this._onGamepadDisconnected.bind(this);
    this._attached = false;
  }

  /**
   * Apply a per-game keyboard override table: { actionId: KeyboardEvent.code }.
   * Default bindings stay in place for actions that are not overridden, and an
   * override replaces (never duplicates) the default codes of its action.
   */
  setKeyOverrides(overrides) {
    this._keyOverrides = overrides && typeof overrides === 'object' ? overrides : {};
    const map = new Map(CODE_MAP);
    for (const action of KEY_ACTIONS) {
      const code = this._keyOverrides[action.id];
      if (typeof code !== 'string' || code === '') { continue; }
      for (const [defaultCode, key] of Array.from(map)) {
        if (key === action.key) { map.delete(defaultCode); }
      }
      map.set(code, action.key);
    }
    this._keyMap = map;
  }

  attach() {
    if (this._attached) { return; }
    this._attached = true;
    window.addEventListener('keydown', this._onKeyDown, { capture: true });
    window.addEventListener('keyup', this._onKeyUp, { capture: true });
    window.addEventListener('blur', this._onWindowBlur);
    window.addEventListener('gamepadconnected', this._onGamepadConnected);
    window.addEventListener('gamepaddisconnected', this._onGamepadDisconnected);

    if (this.controls) {
      this.controls.addEventListener('pointerdown', this._onControlsDown, { passive: false });
      this.controls.addEventListener('pointermove', this._onControlsMove);
      this.controls.addEventListener('pointerup', this._onControlsRelease);
      this.controls.addEventListener('pointercancel', this._onControlsRelease);
      this.controls.addEventListener('contextmenu', (event) => event.preventDefault());
    }
    if (this.canvas) {
      this.canvas.addEventListener('pointerdown', this._onCanvasDown, { passive: false });
      this.canvas.addEventListener('pointermove', this._onCanvasMove);
      this.canvas.addEventListener('pointerup', this._onCanvasUp);
      this.canvas.addEventListener('pointercancel', this._onCanvasUp);
      this.canvas.addEventListener('lostpointercapture', this._onCanvasUp);
      this.canvas.addEventListener('contextmenu', (event) => event.preventDefault());
    }
    if (this.textarea) {
      this.textarea.addEventListener('input', this._onTextInput);
      this.textarea.addEventListener('keydown', this._onTextKeyDown);
      this.textarea.addEventListener('blur', this._onTextBlur);
      this.textarea.addEventListener('compositionend', this._onTextInput);
    }
    const done = document.getElementById('text-done');
    if (done) { done.addEventListener('click', this._onTextDone); }
  }

  detach() {
    if (!this._attached) { return; }
    this._attached = false;
    this.releaseAll();
    window.removeEventListener('keydown', this._onKeyDown, { capture: true });
    window.removeEventListener('keyup', this._onKeyUp, { capture: true });
    window.removeEventListener('blur', this._onWindowBlur);
    window.removeEventListener('gamepadconnected', this._onGamepadConnected);
    window.removeEventListener('gamepaddisconnected', this._onGamepadDisconnected);
    if (this.controls) {
      this.controls.removeEventListener('pointerdown', this._onControlsDown);
      this.controls.removeEventListener('pointermove', this._onControlsMove);
      this.controls.removeEventListener('pointerup', this._onControlsRelease);
      this.controls.removeEventListener('pointercancel', this._onControlsRelease);
    }
    if (this.canvas) {
      this.canvas.removeEventListener('pointerdown', this._onCanvasDown);
      this.canvas.removeEventListener('pointermove', this._onCanvasMove);
      this.canvas.removeEventListener('pointerup', this._onCanvasUp);
      this.canvas.removeEventListener('pointercancel', this._onCanvasUp);
      this.canvas.removeEventListener('lostpointercapture', this._onCanvasUp);
    }
    if (this.textarea) {
      this.textarea.removeEventListener('input', this._onTextInput);
      this.textarea.removeEventListener('keydown', this._onTextKeyDown);
      this.textarea.removeEventListener('blur', this._onTextBlur);
      this.textarea.removeEventListener('compositionend', this._onTextInput);
    }
    const done = document.getElementById('text-done');
    if (done) { done.removeEventListener('click', this._onTextDone); }
    this._closeEditor();
  }

  /** Enable/disable; disabling releases everything that is held. */
  setEnabled(enabled) {
    const next = !!enabled;
    if (this.enabled === next) { return; }
    this.enabled = next;
    if (!next) {
      this.releaseAll();
      this._closeEditor();
    }
  }

  /** Release every key, on-screen cap and canvas pointer exactly once. */
  releaseAll() {
    for (const [code, key] of this._heldCodes) {
      void code;
      this._emit(key, false);
    }
    this._heldCodes.clear();
    for (const key of Array.from(this._gamepadHeld)) {
      this._gamepadHeld.delete(key);
      this._emit(key, false);
    }
    for (const [pointerId, record] of this._controlPointers) {
      void pointerId;
      record.button.classList.remove('is-pressed');
      record.button.setAttribute('aria-pressed', 'false');
      this._emit(record.code, false);
    }
    this._controlPointers.clear();
    for (const [pointerId, point] of this._canvasPointers) {
      this._emitPointer(NJW.POINTER_UP, point.x, point.y, pointerId);
    }
    this._canvasPointers.clear();
  }

  /**
   * Poll attached game pads once per game frame.
   *
   * The browser does not deliver button events, only the current state, so we
   * diff it against what the previous poll sent and emit one key-down/key-up
   * pair per edge.  D-pad buttons and the left stick both feed the D-pad.
   */
  pollGamepads() {
    if (!this.enabled || typeof navigator.getGamepads !== 'function') { return; }
    let pads;
    try {
      pads = navigator.getGamepads();
    } catch (error) {
      return;
    }
    if (!pads) { return; }
    const pressed = new Set();
    for (const pad of pads) {
      if (!pad || !pad.connected) { continue; }
      for (const [index, key] of GAMEPAD_BUTTON_MAP) {
        const button = pad.buttons && pad.buttons[index];
        if (button && button.pressed) { pressed.add(key); }
      }
      const axes = pad.axes || [];
      const x = axes[0] || 0;
      const y = axes[1] || 0;
      if (x <= -GAMEPAD_STICK_DEADZONE) { pressed.add(NJ_KEYS.LEFT); }
      if (x >= GAMEPAD_STICK_DEADZONE) { pressed.add(NJ_KEYS.RIGHT); }
      if (y <= -GAMEPAD_STICK_DEADZONE) { pressed.add(NJ_KEYS.UP); }
      if (y >= GAMEPAD_STICK_DEADZONE) { pressed.add(NJ_KEYS.DOWN); }
    }
    for (const key of pressed) {
      if (!this._gamepadHeld.has(key)) {
        this._gamepadHeld.add(key);
        this._notifyGesture();
        this._emit(key, true);
      }
    }
    for (const key of Array.from(this._gamepadHeld)) {
      if (!pressed.has(key)) {
        this._gamepadHeld.delete(key);
        this._emit(key, false);
      }
    }
  }

  _onGamepadConnected(event) {
    if (this.onGamepad) {
      this.onGamepad(true, event && event.gamepad ? event.gamepad.id : '');
    }
  }

  _onGamepadDisconnected(event) {
    this.releaseAll();
    if (this.onGamepad) {
      this.onGamepad(false, event && event.gamepad ? event.gamepad.id : '');
    }
  }

  /** Called by the game loop every frame. */
  pollText() {
    let active = false;
    try { active = !!this.runtime.textActive(); } catch (error) { active = false; }
    if (active === this.textActive) {
      if (active) { this._keepTextareaInSync(); }
      return;
    }
    this.textActive = active;
    if (active) {
      this._openEditor();
    } else {
      this._closeEditor();
    }
  }

  /* ------------------------------------------------------------- keyboard */

  _onKeyDown(event) {
    if (!this.enabled) { return; }
    if (event.ctrlKey || event.metaKey || event.altKey) { return; }
    if (isFormField(event.target)) { return; }
    const key = this._keyFromEvent(event);
    if (key === null) { return; }
    /* Game view owns these keys: no scrolling, no browser back/space quirks. */
    event.preventDefault();
    event.stopPropagation();
    if (event.repeat) { return; }
    if (this._heldCodes.has(event.code)) { return; }
    this._heldCodes.set(event.code, key);
    this._notifyGesture();
    this._emit(key, true);
  }

  _keyFromEvent(event) {
    const code = event.code || '';
    /* '*' and '#' are matched by character first: on common layouts they are
       Shift+Digit8 / Shift+Digit3, so the code table would read them as 8/3. */
    if (event.key === '*') { return NJ_KEYS.STAR; }
    if (event.key === '#') { return NJ_KEYS.POUND; }
    const digit = DIGIT_CODE.exec(code);
    if (digit) { return NJ_KEYS.NUM_0 + Number(digit[1]); }
    const mapped = this._keyMap.get(code);
    return mapped === undefined ? null : mapped;
  }

  _onKeyUp(event) {
    const key = this._heldCodes.get(event.code);
    if (key === undefined) { return; }
    this._heldCodes.delete(event.code);
    this._emit(key, false);
    if (this.enabled && !isFormField(event.target)) {
      event.preventDefault();
      event.stopPropagation();
    }
  }

  _onWindowBlur() {
    this.releaseAll();
  }

  /* ------------------------------------------------------- on-screen pad */

  _onControlsDown(event) {
    if (!this.enabled) { return; }
    const button = this._controlButtonAt(event);
    if (!button) { return; }
    event.preventDefault();
    if (this._controlPointers.has(event.pointerId)) { return; }
    try { this.controls.setPointerCapture(event.pointerId); } catch (error) { /* ignore */ }
    this._controlPointers.set(event.pointerId, { code: 0, button: null });
    this._pressControlButton(event.pointerId, button);
    this._notifyGesture();
    this._vibrate();
  }

  /** The keycap under this pointer event, if any. */
  _controlButtonAt(event) {
    let target = event.target;
    if (target && target.closest) { target = target.closest('[data-nj-key]'); }
    if (!target || !target.dataset || !target.dataset.njKey) {
      target = document.elementFromPoint(event.clientX, event.clientY);
      if (target && target.closest) { target = target.closest('[data-nj-key]'); }
    }
    if (!target || !target.dataset) { return null; }
    return Number.isFinite(Number(target.dataset.njKey)) ? target : null;
  }

  /** Press, or slide to, the key held by one control pointer. */
  _pressControlButton(pointerId, button) {
    const record = this._controlPointers.get(pointerId);
    if (!record || record.button === button) { return; }
    const code = Number(button.dataset.njKey);
    if (!Number.isFinite(code)) { return; }
    if (record.button) { this._releaseControlButton(record); }
    record.code = code;
    record.button = button;
    button.classList.add('is-pressed');
    button.setAttribute('aria-pressed', 'true');
    this._emit(code, true);
  }

  /** Release the key held by one control pointer (without forgetting it). */
  _releaseControlButton(record) {
    if (!record.button) { return; }
    record.button.classList.remove('is-pressed');
    record.button.setAttribute('aria-pressed', 'false');
    this._emit(record.code, false);
    record.button = null;
    record.code = 0;
  }

  /** Sliding a held finger between keycaps presses the one under it. */
  _onControlsMove(event) {
    const record = this._controlPointers.get(event.pointerId);
    if (!record) { return; }
    const button = this._controlButtonAt(event);
    if (button) {
      this._pressControlButton(event.pointerId, button);
    } else if (record.button) {
      this._releaseControlButton(record);
    }
  }

  _onControlsRelease(event) {
    const record = this._controlPointers.get(event.pointerId);
    if (!record) { return; }
    this._releaseControlButton(record);
    this._controlPointers.delete(event.pointerId);
  }

  _vibrate() {
    if (prefersReducedMotion()) { return; }
    try {
      if (navigator.vibrate && navigator.maxTouchPoints > 0) { navigator.vibrate(8); }
    } catch (error) { /* unsupported */ }
  }

  /* ----------------------------------------------------- canvas pointer */

  _onCanvasDown(event) {
    if (!this.enabled || this.textActive) { return; }
    event.preventDefault();
    try { this.canvas.setPointerCapture(event.pointerId); } catch (error) { /* ignore */ }
    const point = this._canvasPoint(event);
    this._canvasPointers.set(event.pointerId, { x: point.x, y: point.y });
    this._notifyGesture();
    this._emitPointer(NJW.POINTER_DOWN, point.x, point.y, event.pointerId);
  }

  _onCanvasMove(event) {
    const point = this._canvasPointers.get(event.pointerId);
    if (!point) { return; }
    const next = this._canvasPoint(event);
    point.x = next.x;
    point.y = next.y;
    this._emitPointer(NJW.POINTER_MOVE, next.x, next.y, event.pointerId);
  }

  _onCanvasUp(event) {
    const point = this._canvasPointers.get(event.pointerId);
    if (!point) { return; }
    const at = this._canvasPoint(event);
    this._canvasPointers.delete(event.pointerId);
    this._emitPointer(NJW.POINTER_UP, at.x, at.y, event.pointerId);
  }

  _canvasPoint(event) {
    const rect = this.canvas.getBoundingClientRect();
    const width = this.runtime.width() || this.canvas.width || 240;
    const height = this.runtime.height() || this.canvas.height || 320;
    if (!rect.width || !rect.height) { return { x: 0, y: 0 }; }
    return {
      x: clamp(Math.round((event.clientX - rect.left) * width / rect.width), 0, width - 1),
      y: clamp(Math.round((event.clientY - rect.top) * height / rect.height), 0, height - 1)
    };
  }

  /* --------------------------------------------------------- text bridge */

  _openEditor() {
    const textarea = this.textarea;
    if (!textarea) { return; }
    let state = null;
    try { state = this.runtime.textState(); } catch (error) { state = null; }
    const multiline = !!(state && state.multiline);
    this._multiline = multiline;
    this._maxLength = state && state.maxLength > 0 ? state.maxLength : 0;
    textarea.value = this.runtime.textCurrent() || '';
    if (this._maxLength) {
      textarea.maxLength = this._maxLength;
    } else {
      textarea.removeAttribute('maxlength');
    }
    const kind = state ? (state.constraints & 0xffff) : 0;
    textarea.inputMode = TEXT_INPUT_MODES[kind] || 'text';
    textarea.enterKeyHint = multiline ? 'enter' : 'done';
    textarea.setAttribute('data-multiline', multiline ? '1' : '0');
    textarea.classList.toggle('text-bridge--multiline', multiline);
    if (this.textIndicator) {
      this.textIndicator.hidden = false;
    }
    try {
      const cursor = state && Number.isFinite(state.cursor) ? state.cursor : textarea.value.length;
      textarea.focus({ preventScroll: true });
      textarea.setSelectionRange(Math.min(cursor, textarea.value.length), Math.min(cursor, textarea.value.length));
    } catch (error) {
      focusWithoutScroll(textarea);
    }
    this._syncIndicator();
  }

  _closeEditor() {
    if (this.textIndicator) { this.textIndicator.hidden = true; }
    if (this.textarea) {
      this.textarea.value = '';
      if (document.activeElement === this.textarea) {
        try { this.textarea.blur(); } catch (error) { /* ignore */ }
      }
    }
  }

  _syncIndicator() {
    const indicator = this.textIndicator;
    if (!indicator || indicator.hidden) { return; }
    const hint = document.getElementById('text-indicator-hint');
    if (hint) {
      hint.textContent = this._multiline ? '' : '';
    }
  }

  /** Keep the hidden textarea aligned when the MIDlet itself changes the text. */
  _keepTextareaInSync() {
    const textarea = this.textarea;
    if (!textarea || document.activeElement !== textarea) { return; }
    const current = this.runtime.textCurrent() || '';
    if (textarea.value === current) { return; }
    const caret = textarea.selectionStart || 0;
    textarea.value = current;
    try {
      const pos = Math.min(caret, current.length);
      textarea.setSelectionRange(pos, pos);
    } catch (error) { /* ignore */ }
  }

  _onTextInput(event) {
    if (!this.textActive || !this.textarea) { return; }
    if (event && event.isComposing) { /* still forward the composition preview */ }
    const value = this.textarea.value;
    const cursor = this.textarea.selectionStart === null
      ? value.length
      : this.textarea.selectionStart;
    try {
      this.runtime.textReplace(value, cursor);
    } catch (error) {
      console.warn('[input] textReplace failed', error);
      return;
    }
    const current = this.runtime.textCurrent();
    if (typeof current === 'string' && current !== value) {
      const pos = Math.min(cursor, current.length);
      this.textarea.value = current;
      try { this.textarea.setSelectionRange(pos, pos); } catch (error) { /* ignore */ }
    }
  }

  _onTextKeyDown(event) {
    if (!this.textActive) { return; }
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      this._emit(NJ_KEYS.SOFT_RIGHT, true);
      this._emit(NJ_KEYS.SOFT_RIGHT, false);
      return;
    }
    if (event.key === 'Enter' && !this._multiline) {
      /* Single-line editor: Enter is the commit gesture (nj_key FIRE). */
      event.preventDefault();
      event.stopPropagation();
      this._emit(NJ_KEYS.FIRE, true);
      this._emit(NJ_KEYS.FIRE, false);
      return;
    }
    /* Everything else (printable, arrows, Backspace, Home/End, IME) is native
     * textarea editing; the input event above pushes the result to the engine. */
  }

  _onTextBlur() {
    /* The MIDlet may keep its editor active even when the platform IME loses
     * focus (e.g. the tools panel was opened).  Keep the indicator so the
     * player can bring the keyboard back by tapping it. */
    if (this.textActive && this.textIndicator) { this.textIndicator.hidden = false; }
  }

  _onTextDone() {
    this._emit(NJ_KEYS.SOFT_RIGHT, true);
    this._emit(NJ_KEYS.SOFT_RIGHT, false);
  }

  /* --------------------------------------------------------------- helpers */

  _emit(key, down) {
    try {
      this.runtime.key(key, down);
    } catch (error) {
      console.warn('[input] njw_key failed', error);
    }
    if (this.onKey) { this.onKey(key, down); }
  }

  _emitPointer(action, x, y, id = 0) {
    try {
      if (typeof this.runtime.pointerMulti === 'function') {
        this.runtime.pointerMulti(id, action, x, y);
      } else {
        this.runtime.pointer(action, x, y);
      }
    } catch (error) {
      console.warn('[input] njw_pointer failed', error);
    }
  }

  _notifyGesture() {
    if (this._gestureSeen) { return; }
    this._gestureSeen = true;
    if (this.onGesture) { this.onGesture(); }
  }

  /** Re-arm the "first gesture" hook after leaving the game view. */
  resetGesture() {
    this._gestureSeen = false;
  }
}
