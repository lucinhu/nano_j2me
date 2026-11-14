/*
 * nano_j2me web shell - service worker
 *
 * Makes the page work offline and installable.  The shell (HTML/CSS/ES
 * modules, manifest, icons) is precached on install; the wasm engine is
 * cached the first time it is fetched, because a page served without the
 * engine must still work in mock mode.  Navigation requests fall back to the
 * cached index.html when the network is gone.
 *
 * The cache is versioned: bump CACHE when the shell changes so an old copy
 * cannot keep serving stale modules.  `?nosw=1` in the page URL unregisters
 * the worker for local development (see js/main.js).
 */
'use strict';

const CACHE = 'nj2me-shell-2026-11-15g';
const scope = self.registration.scope;
const url = (path) => new URL(path, scope).href;

/* Everything a cold bookmark needs before the engine is downloaded. */
const SHELL = [
  './',
  './index.html',
  './manifest.json',
  './css/app.css',
  './assets/icon.svg',
  './assets/icon-192.png',
  './assets/icon-512.png',
  './assets/icon-maskable-512.png',
  './js/main.js',
  './js/module.js',
  './js/mock-module.js',
  './js/library.js',
  './js/game.js',
  './js/input.js',
  './js/audio.js',
  './js/saves.js',
  './js/ui.js',
  './js/i18n.js'
];

/* The engine is cached on first fetch; it is not required for mock mode. */
const ENGINE = ['./js/nj2me.js', './js/nj2me.wasm'];

self.addEventListener('install', (event) => {
  event.waitUntil((async () => {
    const cache = await caches.open(CACHE);
    /* allSettled: a missing engine or a shell-only dev directory must not
       fail the whole install. */
    await Promise.allSettled(SHELL.concat(ENGINE).map((path) => cache.add(url(path))));
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter((name) => name !== CACHE).map((name) => caches.delete(name)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  /* The shell probes the engine with HEAD before importing it, so HEAD
   * requests have to be answered from the cache too when offline. */
  const head = request.method === 'HEAD';
  if (!head && request.method !== 'GET') { return; }
  const target = new URL(request.url);
  if (target.origin !== self.location.origin) { return; }

  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    /* Cache entries are keyed by URL; match with a GET request even for the
     * HEAD probe, then answer HEAD with headers only. */
    const lookup = head ? new Request(request.url, { method: 'GET' }) : request;
    const hit = await cache.match(lookup, { ignoreSearch: request.mode === 'navigate' });
    if (hit) {
      if (!head) { return hit; }
      /* A HEAD response carries the cached headers but no body. */
      return new Response(null, {
        status: hit.status,
        statusText: hit.statusText,
        headers: hit.headers
      });
    }
    try {
      const response = await fetch(request);
      if (!head && response.ok) {
        /* A copy keeps the body readable for the page below. */
        await cache.put(request, response.clone());
      }
      return response;
    } catch (error) {
      if (request.mode === 'navigate') {
        const shell = await cache.match(url('./index.html'));
        if (shell) { return shell; }
      }
      throw error;
    }
  })());
});
