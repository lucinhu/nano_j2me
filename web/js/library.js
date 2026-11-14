/*
 * nano_j2me web shell - library
 *
 * The home screen: import JARs (file chooser or drag & drop), keep them in
 * IndexedDB together with probed manifest metadata and their bytes, render
 * cover-art tiles (real MIDlet icon when the JAR has one, generated
 * placeholder otherwise), search, sort, delete-with-confirm and start a game.
 *
 * Import path: bytes -> IndexedDB id -> write into the engine FS under
 * /nano_j2me/jars -> njw_probe() -> manifest metadata + icon -> IndexedDB.
 * The FS write also means the game start path only has to check "is the jar
 * already there?".
 *
 * In mock mode (?mock=1 or wasm not built) any file can be imported; the mock
 * runtime derives a fake title from the file name.
 */

import {
  $,
  el,
  toast,
  showProgress,
  setProgressFraction,
  hideProgress,
  confirmDialog
} from './ui.js';
import {
  t,
  formatBytes,
  formatRelativeTime
} from './i18n.js';
import { FS_ROOT, FS_JARS, FS_SAVES, FS_FILES } from './module.js';
import {
  isAvailable,
  listGames,
  getGame,
  putGame,
  deleteGame,
  gameIdForBytes,
  storageEstimate
} from './saves.js';

const LS_SORT = 'nj2me.library.sort';
const SORTS = ['recent', 'imported', 'name', 'size'];

const FILE_ACCEPT = /\.jar$/i;

function hashString(text) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

function firstGlyph(text) {
  const chars = Array.from(String(text || '?').trim());
  return chars.length ? chars[0].toUpperCase() : '?';
}

function stemOf(fileName) {
  return String(fileName || 'game.jar')
    .replace(/\.[^./\\]+$/, '')
    .replace(/[._]+/g, ' ')
    .trim() || 'Game';
}

export class LibraryView {
  /**
   * @param {object} opts
   * @param {object} opts.runtime Runtime facade
   * @param {(game: object) => void} opts.onPlay
   */
  constructor(opts) {
    this.runtime = opts.runtime;
    this.onPlay = opts.onPlay;

    this.view = $('#library-view');
    this.grid = $('#game-grid');
    this.empty = $('#library-empty');
    this.emptyTitle = $('#library-empty-title');
    this.emptyBody = $('#library-empty-body');
    this.emptyHint = $('#library-empty-hint');
    this.emptyImport = $('#library-empty-import');
    this.countEl = $('#library-count');
    this.searchInput = $('#search-input');
    this.sortSelect = $('#sort-select');
    this.fileInput = $('#file-input');
    this.importBtn = $('#import-btn');
    this.dropZone = $('#drop-zone');
    this.storageEl = $('#library-storage');
    this.engineEl = $('#library-engine');
    this.footerHint = $('#library-footer-hint');

    this.games = [];
    this.query = '';
    this.sort = loadSort();
    this._objectUrls = [];
    this._dragDepth = 0;
    this._importing = false;
    this._searchTimer = 0;
    this._dbReady = true;
  }

  init() {
    if (this.importBtn && this.fileInput) {
      this.importBtn.addEventListener('click', () => this.fileInput.click());
      this.fileInput.addEventListener('change', () => {
        const files = Array.from(this.fileInput.files || []);
        this.fileInput.value = '';
        this.importFiles(files);
      });
    }
    if (this.emptyImport && this.fileInput) {
      this.emptyImport.addEventListener('click', () => this.fileInput.click());
    }
    if (this.searchInput) {
      this.searchInput.addEventListener('input', () => {
        window.clearTimeout(this._searchTimer);
        this._searchTimer = window.setTimeout(() => {
          this.query = this.searchInput.value.trim();
          this.render();
        }, 120);
      });
      this.searchInput.addEventListener('search', () => {
        this.query = this.searchInput.value.trim();
        this.render();
      });
    }
    if (this.sortSelect) {
      this.sortSelect.value = this.sort;
      this.sortSelect.addEventListener('change', () => {
        this.sort = SORTS.includes(this.sortSelect.value) ? this.sortSelect.value : 'recent';
        saveSort(this.sort);
        this.render();
      });
    }
    this._bindDragAndDrop();
    this._updateEngineLabel();

    if (!isAvailable()) {
      this._dbReady = false;
      toast(t('engine.noStorage'), { kind: 'error', timeout: 8000 });
    }
    this.load();
  }

  /* ------------------------------------------------------------- loading */

  async load() {
    try {
      this.games = await listGames();
    } catch (error) {
      console.warn('[library] could not read IndexedDB', error);
      this.games = [];
      if (this._dbReady) {
        toast(t('engine.loadFailed', { message: error && error.message ? error.message : error }), { kind: 'error' });
      }
    }
    this.render();
    this._updateStorage();
  }

  _visibleGames() {
    const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });
    let games = this.games.slice();
    if (this.query) {
      const needle = this.query.toLowerCase();
      games = games.filter((game) => {
        const haystack = [game.title, game.fileName, game.vendor]
          .filter(Boolean)
          .join(' ')
          .toLowerCase();
        return haystack.includes(needle);
      });
    }
    switch (this.sort) {
      case 'name':
        games.sort((a, b) => collator.compare(a.title || a.fileName || '', b.title || b.fileName || ''));
        break;
      case 'size':
        games.sort((a, b) => (b.size || 0) - (a.size || 0));
        break;
      case 'imported':
        games.sort((a, b) => (b.importedAt || 0) - (a.importedAt || 0));
        break;
      case 'recent':
      default:
        games.sort((a, b) => {
          const recent = (b.lastPlayedAt || 0) - (a.lastPlayedAt || 0);
          return recent !== 0 ? recent : (b.importedAt || 0) - (a.importedAt || 0);
        });
        break;
    }
    return games;
  }

  render() {
    if (!this.grid) { return; }
    this._revokeObjectUrls();
    const games = this._visibleGames();
    this.grid.textContent = '';

    for (const game of games) {
      this.grid.append(this._buildTile(game));
    }

    const total = this.games.length;
    if (this.countEl) {
      if (this.query) {
        this.countEl.textContent = t('library.count', { n: games.length, m: total });
      } else {
        this.countEl.textContent = t('library.countAll', { m: total });
      }
    }

    if (this.empty) {
      const showEmpty = games.length === 0;
      this.empty.hidden = !showEmpty;
      if (showEmpty) {
        if (total === 0) {
          if (this.emptyTitle) { this.emptyTitle.textContent = t('library.emptyTitle'); }
          if (this.emptyBody) { this.emptyBody.textContent = t('library.emptyBody'); }
          if (this.emptyHint) { this.emptyHint.textContent = t('library.emptyHint'); }
          if (this.emptyImport) { this.emptyImport.hidden = false; }
        } else {
          if (this.emptyTitle) { this.emptyTitle.textContent = t('library.searchEmpty', { q: this.query }); }
          if (this.emptyBody) { this.emptyBody.textContent = ''; }
          if (this.emptyHint) { this.emptyHint.textContent = t('library.clearSearch'); }
          if (this.emptyImport) { this.emptyImport.hidden = true; }
        }
      }
    }
    if (this.engineEl) { this._updateEngineLabel(); }
    if (this.footerHint) { this.footerHint.textContent = t('library.footerHint'); }
  }

  _buildTile(game) {
    const title = game.title || game.fileName || t('common.unknown');
    const tile = el('div', {
      className: 'game-tile',
      attrs: {
        role: 'button',
        tabindex: '0',
        'data-id': game.id,
        'aria-label': title
      }
    });

    const cover = el('div', { className: 'game-tile__cover' });
    if (game.icon && game.icon.bytes && game.icon.bytes.length) {
      const blob = new Blob([game.icon.bytes], { type: game.icon.mime || 'image/png' });
      const url = URL.createObjectURL(blob);
      this._objectUrls.push(url);
      const img = el('img', {
        className: 'game-tile__icon',
        attrs: { src: url, alt: '', draggable: 'false', loading: 'lazy' }
      });
      img.addEventListener('error', () => {
        if (img.parentNode) {
          img.parentNode.replaceChildren(this._placeholder(title));
        }
      });
      cover.append(img);
    } else {
      cover.append(this._placeholder(title));
    }
    if (game.probeSource === 'mock') {
      cover.append(el('span', { className: 'game-tile__tag', text: 'MOCK' }));
    }

    const meta = el('div', { className: 'game-tile__meta' }, [
      el('div', { className: 'game-tile__title', text: title }),
      el('div', {
        className: 'game-tile__sub',
        text: [game.vendor, formatBytes(game.size || 0)].filter(Boolean).join(' · ')
      })
    ]);

    const played = game.lastPlayedAt
      ? t('library.playCount', { n: game.playCount || 1 }) + ' · ' + formatRelativeTime(game.lastPlayedAt)
      : t('library.neverPlayed');
    meta.append(el('div', { className: 'game-tile__sub game-tile__sub--dim', text: played }));

    const del = el('button', {
      className: 'game-tile__delete',
      attrs: {
        type: 'button',
        title: t('common.delete'),
        'aria-label': t('common.delete') + ': ' + title
      }
    }, '×');
    del.addEventListener('click', (event) => {
      event.stopPropagation();
      this.deleteGame(game);
    });

    tile.append(cover, meta, del);

    const play = () => this._play(game);
    tile.addEventListener('click', play);
    tile.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        play();
      }
    });
    return tile;
  }

  _placeholder(title) {
    const hue = hashString(title) % 360;
    const placeholder = el('div', { className: 'game-tile__placeholder' }, firstGlyph(title));
    placeholder.style.background =
      'linear-gradient(145deg, hsl(' + hue + ', 62%, 46%), hsl(' + ((hue + 42) % 360) + ', 66%, 24%))';
    return placeholder;
  }

  _play(game) {
    if (this._importing) { return; }
    if (this.onPlay) { this.onPlay(game); }
  }

  _revokeObjectUrls() {
    for (const url of this._objectUrls) {
      try { URL.revokeObjectURL(url); } catch (error) { /* ignore */ }
    }
    this._objectUrls = [];
  }

  /* -------------------------------------------------------------- import */

  async importFiles(fileList) {
    const files = Array.from(fileList || []).filter(Boolean);
    if (!files.length || this._importing) { return; }
    const accepted = files.filter((file) => this.runtime.mock || FILE_ACCEPT.test(file.name));
    if (!accepted.length) {
      toast(t('library.notJarFile'), { kind: 'warn' });
      return;
    }
    this._importing = true;
    let imported = 0;
    let lastTitle = '';
    for (let index = 0; index < accepted.length; index++) {
      const file = accepted[index];
      showProgress(t('library.importingMany', {
        done: index + 1,
        total: accepted.length,
        name: file.name
      }), index / accepted.length);
      try {
        const title = await this._importOne(file);
        if (title) { lastTitle = title; }
        imported += 1;
      } catch (error) {
        console.warn('[library] import failed', file.name, error);
        toast(t('library.importFailed', {
          name: file.name,
          message: error && error.message ? error.message : String(error)
        }), { kind: 'error', timeout: 8000 });
      }
      setProgressFraction((index + 1) / accepted.length);
    }
    hideProgress(250);
    this._importing = false;
    if (imported > 0) {
      toast(imported === 1
        ? t('library.imported', { title: lastTitle })
        : t('library.importedMany', { n: imported }), { kind: 'success' });
      await this.load();
    }
  }

  async _importOne(file) {
    let bytes = null;
    try {
      bytes = new Uint8Array(await file.arrayBuffer());
    } catch (error) {
      throw new Error('could not read file: ' + (error && error.message ? error.message : error));
    }
    if (!bytes.length) { throw new Error(t('library.notJar')); }

    const id = await gameIdForBytes(bytes, file.name);
    const existing = await getGame(id).catch(() => null);
    const metadata = await this._probe(file, bytes, id);
    const record = {
      id,
      fileName: file.name,
      size: bytes.length,
      jar: bytes,
      importedAt: Date.now(),
      lastPlayedAt: existing ? existing.lastPlayedAt || 0 : 0,
      playCount: existing ? existing.playCount || 0 : 0,
      title: metadata.title || stemOf(file.name),
      vendor: metadata.vendor || '',
      version: metadata.version || '',
      midletClasses: metadata.midletClasses,
      icon: metadata.icon,
      probeSource: this.runtime.mock ? 'mock' : 'wasm'
    };
    await putGame(record);
    if (existing) {
      toast(t('library.duplicate', { title: record.title }), { kind: 'info' });
    }
    return record.title;
  }

  async _probe(file, bytes, id) {
    const runtime = this.runtime;
    try {
      runtime.mkdirp(FS_ROOT);
      runtime.mkdirp(FS_JARS);
      runtime.mkdirp(FS_SAVES);
      runtime.mkdirp(FS_FILES);
      /* In mock mode the path basename is what the fake probe title comes
       * from; in wasm mode the manifest is authoritative and the id path is
       * reused by the game start path. */
      const probePath = runtime.mock
        ? FS_JARS + '/' + encodeURIComponent(file.name)
        : FS_JARS + '/' + id + '.jar';
      runtime.writeFile(probePath, bytes);
      const ok = runtime.probe(probePath);
      if (!ok) {
        throw new Error(runtime.error() || t('library.notJar'));
      }
      const title = runtime.appTitle() || '';
      const vendor = runtime.appVendor() || '';
      const version = runtime.appVersion() || '';
      const midletClasses = [];
      const count = runtime.midletCount() | 0;
      for (let i = 0; i < Math.min(count, 32); i++) {
        const name = runtime.midletClassAt(i);
        if (name) { midletClasses.push(name); }
      }
      let icon = null;
      try { icon = runtime.appIcon(); } catch (error) { icon = null; }
      return { title, vendor, version, midletClasses, icon };
    } catch (error) {
      if (error && /not a|invalid|failed|probe/i.test(error.message || '')) {
        throw error;
      }
      throw new Error(runtime.error() || (error && error.message ? error.message : t('library.notJar')));
    }
  }

  async deleteGame(game) {
    const title = game.title || game.fileName || t('common.unknown');
    const confirmed = await confirmDialog({
      title: t('library.deleteTitle'),
      message: t('library.deleteBody', { title }),
      confirmLabel: t('common.delete'),
      cancelLabel: t('common.cancel'),
      danger: true
    });
    if (!confirmed) { return; }
    try {
      await deleteGame(game.id);
      toast(t('library.deleted', { title }), { kind: 'success' });
      await this.load();
    } catch (error) {
      toast(t('library.deleteFailed', { message: error && error.message ? error.message : error }), { kind: 'error' });
    }
  }

  /* --------------------------------------------------------- drag & drop */

  _bindDragAndDrop() {
    const hasFiles = (event) => {
      const types = event.dataTransfer && event.dataTransfer.types;
      return !!types && Array.from(types).includes('Files');
    };
    window.addEventListener('dragenter', (event) => {
      if (!hasFiles(event) || this.view.hidden) { return; }
      event.preventDefault();
      this._dragDepth += 1;
      this._showDropZone(true);
    });
    window.addEventListener('dragover', (event) => {
      if (!hasFiles(event) || this.view.hidden) { return; }
      event.preventDefault();
      if (event.dataTransfer) { event.dataTransfer.dropEffect = 'copy'; }
    });
    window.addEventListener('dragleave', (event) => {
      if (!hasFiles(event) || this.view.hidden) { return; }
      this._dragDepth = Math.max(0, this._dragDepth - 1);
      if (this._dragDepth === 0) { this._showDropZone(false); }
    });
    window.addEventListener('drop', (event) => {
      if (this.view.hidden) { return; }
      const files = event.dataTransfer && event.dataTransfer.files;
      if (!files || !files.length) { return; }
      event.preventDefault();
      this._dragDepth = 0;
      this._showDropZone(false);
      this.importFiles(files);
    });
  }

  _showDropZone(show) {
    if (!this.dropZone) { return; }
    this.dropZone.hidden = !show;
    this.dropZone.classList.toggle('drop-zone--active', show);
  }

  /* --------------------------------------------------------------- misc */

  async _updateStorage() {
    if (!this.storageEl) { return; }
    const info = await storageEstimate();
    if (!info) {
      this.storageEl.textContent = '';
      return;
    }
    this.storageEl.textContent = formatBytes(info.usage || 0) + ' / ' + formatBytes(info.quota || 0);
  }

  _updateEngineLabel() {
    if (!this.engineEl) { return; }
    if (this.runtime.mock) {
      this.engineEl.textContent = t('library.engineMock');
      this.engineEl.dataset.kind = 'mock';
    } else {
      this.engineEl.textContent = t('library.engineWasm');
      this.engineEl.dataset.kind = 'wasm';
    }
  }

  refreshI18n() {
    if (this.sortSelect) {
      for (const option of this.sortSelect.options) {
        if (option.dataset.i18n) { option.textContent = t(option.dataset.i18n); }
      }
      this.sortSelect.value = this.sort;
    }
    this.render();
  }

  dispose() {
    this._revokeObjectUrls();
  }
}

function loadSort() {
  try {
    const saved = localStorage.getItem(LS_SORT);
    if (SORTS.includes(saved)) { return saved; }
  } catch (error) { /* ignore */ }
  return 'recent';
}

function saveSort(sort) {
  try { localStorage.setItem(LS_SORT, sort); } catch (error) { /* ignore */ }
}
