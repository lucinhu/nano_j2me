/*
 * nano_j2me web shell - UI primitives
 *
 * Small, dependency-free building blocks shared by the library and the game
 * view: toasts, a modal confirm dialog, the global banner (used for mock /
 * fallback mode and storage warnings), a thin progress bar for long operations
 * and a few DOM helpers.  No frameworks, no external assets.
 */

import { t } from './i18n.js';

export const $ = (selector, root = document) => root.querySelector(selector);
export const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));

/** Create an element with class/text/attrs in one call (avoids innerHTML). */
export function el(tag, opts = {}, children = []) {
  const node = document.createElement(tag);
  if (opts.className) { node.className = opts.className; }
  if (opts.text !== undefined) { node.textContent = String(opts.text); }
  if (opts.html !== undefined) { node.innerHTML = opts.html; }
  if (opts.attrs) {
    for (const [key, value] of Object.entries(opts.attrs)) {
      if (value === undefined || value === null || value === false) { continue; }
      if (key === 'dataset') {
        for (const [dk, dv] of Object.entries(value)) { node.dataset[dk] = dv; }
      } else if (key === 'style') {
        Object.assign(node.style, value);
      } else {
        node.setAttribute(key, value === true ? '' : String(value));
      }
    }
  }
  for (const child of Array.isArray(children) ? children : [children]) {
    if (!child) { continue; }
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

/* ------------------------------------------------------------------ toast */

const MAX_TOASTS = 4;

/**
 * Show a transient message.  kind: 'info' | 'success' | 'warn' | 'error'.
 * Returns a dismiss function.
 */
export function toast(message, opts = {}) {
  const container = document.getElementById('toast-container');
  if (!container) { return () => {}; }
  while (container.children.length >= MAX_TOASTS) {
    container.firstElementChild?.remove();
  }
  const kind = opts.kind || 'info';
  const node = el('div', {
    className: 'toast toast--' + kind,
    attrs: { role: kind === 'error' ? 'alert' : 'status' }
  }, [
    el('span', { className: 'toast__dot', attrs: { 'aria-hidden': 'true' } }),
    el('span', { className: 'toast__text', text: message }),
    el('button', {
      className: 'toast__close',
      attrs: { type: 'button', 'aria-label': t('toast.close') }
    }, '×')
  ]);
  const dismiss = () => {
    if (!node.isConnected) { return; }
    node.classList.add('toast--leaving');
    window.setTimeout(() => node.remove(), 180);
  };
  node.querySelector('.toast__close').addEventListener('click', dismiss);
  container.append(node);
  if (opts.timeout !== 0) {
    window.setTimeout(dismiss, opts.timeout || (kind === 'error' ? 6000 : 3500));
  }
  return dismiss;
}

/* ----------------------------------------------------------------- banner */

let bannerActionUnbind = null;

/**
 * Persistent top banner, hidden by default.  Used for mock/fallback mode and
 * for "storage is not persistent in this build" warnings.
 */
export function showBanner(text, opts = {}) {
  const banner = document.getElementById('app-banner');
  if (!banner) { return; }
  banner.hidden = false;
  banner.classList.toggle('app-banner--warn', opts.kind === 'warn');
  banner.classList.toggle('app-banner--error', opts.kind === 'error');
  const textEl = banner.querySelector('.app-banner__text');
  if (textEl) { textEl.textContent = text; }
  const action = banner.querySelector('.app-banner__action');
  if (action) {
    if (bannerActionUnbind) {
      action.removeEventListener('click', bannerActionUnbind);
      bannerActionUnbind = null;
    }
    if (opts.actionLabel && typeof opts.onAction === 'function') {
      action.hidden = false;
      action.textContent = opts.actionLabel;
      bannerActionUnbind = () => { opts.onAction(); };
      action.addEventListener('click', bannerActionUnbind);
    } else {
      action.hidden = true;
    }
  }
}

export function hideBanner() {
  const banner = document.getElementById('app-banner');
  if (banner) { banner.hidden = true; }
}

/* --------------------------------------------------------------- progress */

let progressHideTimer = 0;

/**
 * Thin progress strip pinned to the bottom of the window.  label: text or
 * null; fraction: 0..1 or null for an indeterminate bar.
 */
export function showProgress(label, fraction = null) {
  const bar = document.getElementById('activity-bar');
  if (!bar) { return; }
  window.clearTimeout(progressHideTimer);
  bar.hidden = false;
  bar.classList.remove('activity-bar--done');
  const labelEl = bar.querySelector('.activity-bar__label');
  if (labelEl) { labelEl.textContent = label || ''; }
  setProgressFraction(fraction);
}

export function setProgressFraction(fraction) {
  const bar = document.getElementById('activity-bar');
  if (!bar) { return; }
  const fill = bar.querySelector('.activity-bar__fill');
  if (!fill) { return; }
  if (fraction === null || fraction === undefined) {
    bar.classList.add('activity-bar--indeterminate');
    fill.style.width = '100%';
  } else {
    bar.classList.remove('activity-bar--indeterminate');
    fill.style.width = Math.max(0, Math.min(1, fraction)) * 100 + '%';
  }
}

export function hideProgress(delay = 0) {
  const bar = document.getElementById('activity-bar');
  if (!bar) { return; }
  window.clearTimeout(progressHideTimer);
  const finish = () => {
    bar.hidden = true;
    bar.classList.remove('activity-bar--indeterminate');
  };
  if (delay > 0) {
    progressHideTimer = window.setTimeout(finish, delay);
  } else {
    finish();
  }
}

/* ---------------------------------------------------------------- confirm */

/**
 * Promise-based confirm dialog.  Resolves true when the confirm button is
 * pressed, false on cancel / Escape / backdrop click.
 * opts: { title, message, confirmLabel, cancelLabel, danger }
 */
export function confirmDialog(opts = {}) {
  const dialog = document.getElementById('confirm-dialog');
  if (!dialog || typeof dialog.showModal !== 'function') {
    /* Last-resort fallback: never block silently. */
    return Promise.resolve(window.confirm(opts.message || opts.title || ''));
  }
  const titleEl = dialog.querySelector('#confirm-title');
  const bodyEl = dialog.querySelector('#confirm-body');
  const okBtn = dialog.querySelector('#confirm-ok');
  const cancelBtn = dialog.querySelector('#confirm-cancel');
  if (titleEl) { titleEl.textContent = opts.title || ''; }
  if (bodyEl) { bodyEl.textContent = opts.message || ''; }
  if (okBtn) {
    okBtn.textContent = opts.confirmLabel || t('dialog.confirm');
    okBtn.classList.toggle('btn--danger', !!opts.danger);
  }
  if (cancelBtn) { cancelBtn.textContent = opts.cancelLabel || t('dialog.cancel'); }

  return new Promise((resolve) => {
    let settled = false;
    const finish = (value) => {
      if (settled) { return; }
      settled = true;
      okBtn?.removeEventListener('click', onOk);
      cancelBtn?.removeEventListener('click', onCancel);
      dialog.removeEventListener('cancel', onCancelEvent);
      dialog.removeEventListener('click', onBackdrop);
      if (dialog.open) {
        try { dialog.close(); } catch (e) { /* already closed */ }
      }
      resolve(value);
    };
    const onOk = () => finish(true);
    const onCancel = () => finish(false);
    const onCancelEvent = (event) => { event.preventDefault(); finish(false); };
    const onBackdrop = (event) => { if (event.target === dialog) { finish(false); } };
    okBtn?.addEventListener('click', onOk);
    cancelBtn?.addEventListener('click', onCancel);
    dialog.addEventListener('cancel', onCancelEvent);
    dialog.addEventListener('click', onBackdrop);
    try {
      if (!dialog.open) { dialog.showModal(); }
      okBtn?.focus();
    } catch (e) {
      finish(false);
    }
  });
}

/* ------------------------------------------------------------------- misc */

/** Scroll-free focus helper that does not jump the page. */
export function focusWithoutScroll(node) {
  if (!node || typeof node.focus !== 'function') { return; }
  try {
    node.focus({ preventScroll: true });
  } catch (e) {
    node.focus();
  }
}

/** True when the user prefers reduced motion. */
export function prefersReducedMotion() {
  try {
    return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  } catch (e) {
    return false;
  }
}

/** Toggle a button between "default" and "pressed/active" visual states. */
export function setButtonActive(button, active) {
  if (!button) { return; }
  button.classList.toggle('is-active', !!active);
  button.setAttribute('aria-pressed', active ? 'true' : 'false');
}

/** Download bytes or a Blob through an <a download> (works offline). */
export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.rel = 'noopener';
  document.body.append(link);
  link.click();
  link.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 30000);
}

/** Escape a string for use in HTML text (only for the rare innerHTML use). */
export function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, (ch) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[ch]);
}
