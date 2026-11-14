/*
 * nano_j2me web shell - persistence (IndexedDB)
 *
 * The browser is the library: JAR bytes, the metadata read out of the JAR by
 * njw_probe() and the three save-state slots per game all live in IndexedDB,
 * so a reload keeps everything and nothing is ever uploaded.
 *
 * Two object stores:
 *   games  keyPath "id"  - one record per imported JAR (jar bytes included)
 *   states keyPath "key" - one record per save slot ("<gameId>:<slot>"),
 *                          indexed by gameId so deleting a game can delete
 *                          its states in the same transaction.
 *
 * All functions are async and never throw synchronously.  Callers decide how
 * to surface failures (the library shows toasts, the game panel shows errors).
 */

const DB_NAME = 'nano-j2me';
const DB_VERSION = 1;
const STORE_GAMES = 'games';
const STORE_STATES = 'states';
const STATE_INDEX_GAME = 'gameId';

let dbPromise = null;

/** True when this browser offers IndexedDB at all. */
export function isAvailable() {
  try {
    return !!window.indexedDB;
  } catch (e) {
    return false;
  }
}

/** Open (once) and reuse the database connection. */
export function openDb() {
  if (dbPromise) { return dbPromise; }
  dbPromise = new Promise((resolve, reject) => {
    let request;
    try {
      request = indexedDB.open(DB_NAME, DB_VERSION);
    } catch (e) {
      dbPromise = null;
      reject(e);
      return;
    }
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE_GAMES)) {
        db.createObjectStore(STORE_GAMES, { keyPath: 'id' });
      }
      if (!db.objectStoreNames.contains(STORE_STATES)) {
        const store = db.createObjectStore(STORE_STATES, { keyPath: 'key' });
        store.createIndex(STATE_INDEX_GAME, 'gameId', { unique: false });
      }
    };
    request.onsuccess = () => {
      const db = request.result;
      db.onversionchange = () => {
        try { db.close(); } catch (e) { /* ignore */ }
        dbPromise = null;
      };
      resolve(db);
    };
    request.onerror = () => {
      dbPromise = null;
      reject(request.error || new Error('IndexedDB open failed'));
    };
    request.onblocked = () => {
      /* Another tab holds an old version open; keep waiting, the browser will
       * retry when that tab closes. */
    };
  });
  return dbPromise;
}

function requestValue(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('IndexedDB request failed'));
  });
}

/** Run fn(store) inside one transaction and resolve when it commits. */
async function withStore(storeName, mode, fn) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    let transaction;
    try {
      transaction = db.transaction(storeName, mode);
    } catch (e) {
      reject(e);
      return;
    }
    const store = transaction.objectStore(storeName);
    let result;
    let failed = null;
    transaction.oncomplete = () => {
      if (failed) { reject(failed); } else { resolve(result); }
    };
    transaction.onerror = () => { failed = failed || transaction.error; };
    transaction.onabort = () => { failed = failed || transaction.error || new Error('transaction aborted'); };
    try {
      const maybe = fn(store);
      if (maybe && typeof maybe.then === 'function') {
        maybe.then((value) => { result = value; }, (error) => { failed = error; try { transaction.abort(); } catch (e) {} });
      } else {
        result = maybe;
      }
    } catch (e) {
      failed = e;
      try { transaction.abort(); } catch (abortError) { /* ignore */ }
    }
  });
}

/* --------------------------------------------------------------- game ids */

const HEX = '0123456789abcdef';

function hexFromBytes(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i++) {
    out += HEX[(bytes[i] >> 4) & 15] + HEX[bytes[i] & 15];
  }
  return out;
}

/** FNV-1a 32-bit over the bytes, used when SubtleCrypto is unavailable. */
function fnv1a(bytes) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < bytes.length; i++) {
    hash ^= bytes[i];
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

/**
 * Stable game id derived from the JAR bytes (so re-importing the same file
 * updates the same library entry instead of duplicating it).  The id is safe
 * to use as a filesystem path segment: "g" + lowercase hex.
 */
export async function gameIdForBytes(bytes, fileName = '') {
  const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  try {
    if (window.crypto && window.crypto.subtle && window.crypto.subtle.digest) {
      /* digest() copies the data; it does not detach the caller's buffer. */
      const digest = await window.crypto.subtle.digest('SHA-256', view);
      const hash = hexFromBytes(new Uint8Array(digest));
      return 'g' + hash.slice(0, 24);
    }
  } catch (e) { /* fall through to FNV */ }
  let salt = 0;
  for (let i = 0; i < fileName.length; i++) {
    salt = (salt * 31 + fileName.charCodeAt(i)) >>> 0;
  }
  const a = fnv1a(view);
  const b = fnv1a(view.subarray(0, Math.min(view.length, 65536)));
  return 'g' + a.toString(16).padStart(8, '0') + b.toString(16).padStart(8, '0') + salt.toString(16).padStart(8, '0').slice(0, 8);
}

/* ------------------------------------------------------------------ games */

/** All games, newest import first (callers sort anyway). */
export async function listGames() {
  return withStore(STORE_GAMES, 'readonly', (store) => requestValue(store.getAll()));
}

export async function getGame(id) {
  return withStore(STORE_GAMES, 'readonly', (store) => requestValue(store.get(id)));
}

/**
 * Insert or replace a game record.  The caller passes the fully built record
 * (id, jar bytes, metadata, timestamps); this only normalises defaults.
 */
export async function putGame(record) {
  const now = Date.now();
  const normalised = {
    importedAt: now,
    lastPlayedAt: 0,
    playCount: 0,
    vendor: '',
    version: '',
    midletClasses: [],
    icon: null,
    probeSource: 'wasm',
    ...record
  };
  normalised.size = normalised.size || (normalised.jar ? normalised.jar.length : 0);
  await withStore(STORE_GAMES, 'readwrite', (store) => store.put(normalised));
  return normalised;
}

/** Delete a game and all of its save states (one transaction per store). */
export async function deleteGame(id) {
  await deleteStates(id);
  await withStore(STORE_GAMES, 'readwrite', (store) => store.delete(id));
}

/**
 * Mark a game as played: bumps playCount and sets lastPlayedAt.  Best effort:
 * a failure must not stop the game from starting.
 */
export async function markPlayed(id) {
  try {
    const game = await getGame(id);
    if (!game) { return null; }
    game.lastPlayedAt = Date.now();
    game.playCount = (game.playCount || 0) + 1;
    await withStore(STORE_GAMES, 'readwrite', (store) => store.put(game));
    return game;
  } catch (e) {
    return null;
  }
}

/* ------------------------------------------------------------- save states */

export const SAVE_SLOTS = 3;

function stateKey(gameId, slot) {
  return gameId + ':' + slot;
}

/** All three slots for a game, as a Map slot -> record (missing slots absent). */
export async function listStates(gameId) {
  const rows = await withStore(STORE_STATES, 'readonly', (store) => {
    const index = store.index(STATE_INDEX_GAME);
    return requestValue(index.getAll(gameId));
  });
  const map = new Map();
  for (const row of rows || []) {
    if (row && typeof row.slot === 'number') { map.set(row.slot, row); }
  }
  return map;
}

export async function getState(gameId, slot) {
  return withStore(STORE_STATES, 'readonly', (store) => requestValue(store.get(stateKey(gameId, slot))));
}

/**
 * Store a save-state blob in one slot.  bytes may be Uint8Array or ArrayBuffer.
 * Returns the stored record (with at/size/frames).
 */
export async function putState(gameId, slot, { bytes, frames = 0, title = '', engineApi = 0 }) {
  const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  /* Copy into a standalone ArrayBuffer: the caller may keep using its view
   * (the wasm heap) and IndexedDB must not see a detached buffer. */
  const copy = view.slice();
  const record = {
    key: stateKey(gameId, slot),
    gameId,
    slot,
    bytes: copy,
    size: copy.byteLength,
    at: Date.now(),
    frames: frames || 0,
    title: title || '',
    engineApi: engineApi || 0
  };
  await withStore(STORE_STATES, 'readwrite', (store) => store.put(record));
  return record;
}

export async function deleteState(gameId, slot) {
  await withStore(STORE_STATES, 'readwrite', (store) => store.delete(stateKey(gameId, slot)));
}

export async function deleteStates(gameId) {
  await withStore(STORE_STATES, 'readwrite', (store) => {
    const index = store.index(STATE_INDEX_GAME);
    const request = index.openKeyCursor(IDBKeyRange.only(gameId));
    request.onsuccess = () => {
      const cursor = request.result;
      if (cursor) {
        store.delete(cursor.primaryKey);
        cursor.continue();
      }
    };
    return null;
  });
}

/** Rough storage diagnostics for the settings dialog (best effort). */
export async function storageEstimate() {
  try {
    if (navigator.storage && navigator.storage.estimate) {
      const info = await navigator.storage.estimate();
      return { usage: info.usage || 0, quota: info.quota || 0 };
    }
  } catch (e) { /* ignore */ }
  return null;
}
