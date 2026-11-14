/*
 * nano_j2me web shell - single-engine ownership across browser tabs
 *
 * The wasm runtime mounts its persistence directory (IDBFS) and writes saves
 * through IndexedDB.  Two tabs each running an engine would fight over the
 * same records, so only one tab may own the engine at a time.
 *
 * Browsers give us a cheap coordination primitive: BroadcastChannel plus a
 * heartbeat lease.  A tab that wants to play asks "is anyone running?", waits
 * a short window for an owner to answer, and only then starts.  The owner
 * answers claims, sends a heartbeat while it holds the lease and announces a
 * release when it stops the engine (exit, pagehide, reload).
 *
 * The lock is deliberately advisory rather than a hard mutex: if a tab is
 * suspended or killed without releasing, the lease expires after LEASE_MS and
 * another tab can take over (the alternative - refusing to start forever -
 * would be worse).  Browsers without BroadcastChannel simply keep the old
 * "no coordination" behaviour, as does the Node test harness.
 */

const CHANNEL_NAME = 'nj2me.engine';
/** How long a claimant waits for an owner to answer before taking the lease. */
const CLAIM_WINDOW_MS = 160;
/** A silent owner is considered gone after this long. */
const LEASE_MS = 6000;
/** How often the owner confirms it is still alive. */
const HEARTBEAT_MS = 2000;

function makeId() {
  const bytes = new Uint8Array(8);
  if (globalThis.crypto && globalThis.crypto.getRandomValues) {
    globalThis.crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < bytes.length; i++) { bytes[i] = Math.floor(Math.random() * 256); }
  }
  return [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export class EngineOwner {
  constructor() {
    this.id = makeId();
    this.channel = typeof BroadcastChannel === 'function'
      ? new BroadcastChannel(CHANNEL_NAME)
      : null;
    this.held = false;
    /** Remote owner id we currently know about (null when the lease is free). */
    this.ownerId = null;
    this.ownerSeenAt = 0;
    this._heartbeat = 0;
    this._channel = this.channel;
    if (this.channel) {
      this.channel.addEventListener('message', (event) => this._onMessage(event.data));
      if (typeof window !== 'undefined' && window.addEventListener) {
        /* Closing/reloading the tab is not a user-visible event we can await,
           so release from the last lifecycle callback we get. */
        window.addEventListener('pagehide', () => this.release());
      }
    }
  }

  /** True when cross-tab coordination is available; false = old behaviour. */
  get supported() {
    return !!this.channel;
  }

  /** True when this tab owns the engine. */
  get isOwner() {
    return this.held;
  }

  /**
   * Try to become the single engine owner.
   * Resolves { ok: true } or { ok: false, ownerId } without throwing.
   * When BroadcastChannel is unavailable it resolves { ok: true, degraded: true }.
   */
  async claim() {
    if (!this.channel) { return { ok: true, degraded: true }; }
    if (this.held) { this._beat(); return { ok: true, id: this.id }; }

    /* Someone may still hold a live lease (e.g. they started before us, or a
       previous claim in this same tab was not released). */
    const known = this._liveOwner();
    if (known) { return { ok: false, ownerId: known.id }; }

    /* Probe the channel: a live owner answers claims immediately. */
    const replies = new Set();
    const onMessage = (event) => {
      const data = event.data || {};
      if (data.t === 'owner' && data.id && data.id !== this.id) { replies.add(data.id); }
    };
    this.channel.addEventListener('message', onMessage);
    try {
      this.channel.postMessage({ t: 'claim', id: this.id, at: Date.now() });
      await sleep(CLAIM_WINDOW_MS);
    } finally {
      this.channel.removeEventListener('message', onMessage);
    }
    if (replies.size) {
      this.ownerId = [...replies][0];
      this.ownerSeenAt = Date.now();
      return { ok: false, ownerId: this.ownerId };
    }

    /* Re-read the lease: a release or a heartbeat may have arrived while we
       were waiting, and take() below broadcasts our win either way. */
    const live = this._liveOwner();
    if (live && live.id !== this.id) { return { ok: false, ownerId: live.id }; }

    this.held = true;
    this.ownerId = this.id;
    this.ownerSeenAt = Date.now();
    this.channel.postMessage({ t: 'take', id: this.id, at: Date.now() });
    this._startHeartbeat();
    return { ok: true, id: this.id };
  }

  /** Give the engine back so another tab may claim it. */
  release() {
    if (this._heartbeat) {
      clearInterval(this._heartbeat);
      this._heartbeat = 0;
    }
    if (!this.held) { return; }
    this.held = false;
    if (this.channel) {
      this.channel.postMessage({ t: 'release', id: this.id });
      /* Our own channel does not receive our messages, so clear the remote
         owner bookkeeping explicitly: we were the owner. */
      if (this.ownerId === this.id) { this.ownerId = null; this.ownerSeenAt = 0; }
    }
  }

  /** Stop listening (tests / teardown). */
  dispose() {
    this.release();
    if (this.channel) {
      try { this.channel.close(); } catch (error) { /* ignore */ }
      this.channel = null;
    }
  }

  /* ------------------------------------------------------------ internals */

  _liveOwner() {
    if (!this.ownerId) { return null; }
    if (Date.now() - this.ownerSeenAt > LEASE_MS) {
      /* The owner stopped heartbeating without announcing a release: treat the
         lease as available rather than blocking the shell forever. */
      this.ownerId = null;
      this.ownerSeenAt = 0;
      return null;
    }
    return { id: this.ownerId };
  }

  _startHeartbeat() {
    if (this._heartbeat) { clearInterval(this._heartbeat); }
    this._heartbeat = setInterval(() => this._beat(), HEARTBEAT_MS);
    this._beat();
  }

  _beat() {
    if (!this.held || !this.channel) { return; }
    this.ownerSeenAt = Date.now();
    this.channel.postMessage({ t: 'beat', id: this.id, at: Date.now() });
  }

  _onMessage(data) {
    if (!data || typeof data !== 'object' || !this.channel) { return; }
    const remote = data.id;
    if (!remote || remote === this.id) { return; }
    switch (data.t) {
      case 'claim':
        /* Only the owner answers; a non-owner stays silent so the claimant can
           take an obviously free lease without a round trip. */
        if (this.held) {
          this.channel.postMessage({ t: 'owner', id: this.id, at: Date.now() });
        }
        break;
      case 'take':
        if (this.held) {
          /* Two claims raced.  Break the tie by id so both tabs agree on the
             winner instead of both running an engine. */
          if (remote < this.id) {
            this.release();
            this.ownerId = remote;
            this.ownerSeenAt = Date.now();
          } else {
            this.channel.postMessage({ t: 'owner', id: this.id, at: Date.now() });
          }
        } else {
          this.ownerId = remote;
          this.ownerSeenAt = Date.now();
        }
        break;
      case 'beat':
        this.ownerId = remote;
        this.ownerSeenAt = Date.now();
        break;
      case 'owner':
        this.ownerId = remote;
        this.ownerSeenAt = Date.now();
        break;
      case 'release':
        if (this.ownerId === remote) {
          this.ownerId = null;
          this.ownerSeenAt = 0;
        }
        break;
      default:
        break;
    }
  }
}
