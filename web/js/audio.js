/*
 * nano_j2me web shell - audio output
 *
 * The C side uses a pull model: the shell asks how much audio the engine can
 * produce (njw_audio_read) and schedules it on a WebAudio graph.  Nothing is
 * pushed asynchronously, so this is driven once per animation frame from the
 * game loop.
 *
 * Design notes:
 *  - The AudioContext is created / resumed from the first user gesture (the
 *    click that starts a game).  Browsers refuse to start audio otherwise.
 *  - We keep roughly 120 ms of audio scheduled ahead, in chunks of ~60 ms,
 *    with a hard cap on the work done in one pump so a slow frame can never
 *    make the page spin.
 *  - The engine may change the sample rate or channel count at any time (a
 *    new MIDlet, a different Sound player); because every chunk reads the
 *    current values when it is built, a change simply takes effect on the
 *    next chunk.  No state needs resetting.
 *  - njw_audio_note_playing(frames) tells the runtime how many frames are
 *    scheduled but not audible yet; the runtime uses this to decide how much
 *    clock time audio has "used up".  We recompute it from the scheduled
 *    buffer start times.
 */

const MAX_AHEAD_SECONDS = 0.12;
const CHUNK_SECONDS = 0.06;
const MAX_CHUNKS_PER_PUMP = 8;
const MAX_FRAMES_PER_CHUNK = 16384;

function clamp(value, min, max) {
  return value < min ? min : (value > max ? max : value);
}

export class AudioOutput {
  constructor(runtime) {
    this.runtime = runtime;
    this.ctx = null;
    this.gain = null;
    this.muted = false;
    this.volume = 0.8;
    this.suspended = true;

    this._nextTime = 0;
    this._pending = [];          // { start, end, frames } scheduled sources
    this._sources = new Set();
    this._lastNote = -1;
    this._lastRate = 0;
    this._channels = 1;
    this._warned = false;
    this._blocked = false;
  }

  /** Create the AudioContext.  Call from a user gesture. */
  ensureContext() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') {
        this.ctx.resume().catch(() => { /* needs a gesture; retried later */ });
      }
      return this.ctx;
    }
    const Ctor = window.AudioContext || window.webkitAudioContext;
    if (!Ctor) {
      this._warn('AudioContext is not available in this browser');
      return null;
    }
    try {
      this.ctx = new Ctor({ latencyHint: 'interactive' });
      this.gain = this.ctx.createGain();
      this.gain.gain.value = this.muted ? 0 : this.volume;
      this.gain.connect(this.ctx.destination);
      this._nextTime = this.ctx.currentTime;
      this.suspended = false;
      if (this.ctx.state === 'suspended') {
        this.ctx.resume().catch(() => { /* gesture not trusted; resumed later */ });
      }
      return this.ctx;
    } catch (error) {
      this.ctx = null;
      this._warn('could not create AudioContext: ' + (error && error.message ? error.message : error));
      this._blocked = true;
      return null;
    }
  }

  setMuted(muted) {
    this.muted = !!muted;
    if (this.gain) {
      this.gain.gain.value = this.muted ? 0 : this.volume;
    }
  }

  setVolume(volume) {
    this.volume = clamp(Number(volume) || 0, 0, 1);
    if (this.gain && !this.muted) {
      this.gain.gain.value = this.volume;
    }
  }

  /** Pause: stop scheduling and let the graph go quiet. */
  suspend() {
    this.suspended = true;
    this._pending = [];
    this._lastNote = -1;
    if (this.ctx && this.ctx.state === 'running') {
      this.ctx.suspend().catch(() => { /* ignore */ });
    }
  }

  /** Resume after a pause; called from a gesture when possible. */
  resume() {
    this.suspended = false;
    if (!this.ctx) { return; }
    const now = this.ctx.currentTime;
    this._nextTime = Math.max(now, this._nextTime);
    if (this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => { /* stays suspended; pump is a no-op */ });
    }
  }

  /** Per-frame pump: if we are running dry, pull more audio and schedule it. */
  pump() {
    const ctx = this.ctx;
    if (!ctx || ctx.state !== 'running' || this.suspended) { return; }
    const runtime = this.runtime;
    if (!runtime || !runtime.audioAvailable()) { return; }

    let rate = 0;
    let channels = 1;
    try {
      rate = runtime.audioRate() || this._lastRate || 22050;
      channels = clamp(runtime.audioChannels() || 1, 1, 2);
    } catch (error) {
      rate = this._lastRate || 22050;
      channels = this._channels || 1;
    }
    this._lastRate = rate;
    this._channels = channels;

    let chunks = 0;
    while (chunks < MAX_CHUNKS_PER_PUMP && !this.suspended) {
      const now = ctx.currentTime;
      if (this._nextTime - now > MAX_AHEAD_SECONDS) { break; }
      const wantFrames = clamp(Math.floor(CHUNK_SECONDS * rate), 256, MAX_FRAMES_PER_CHUNK);
      let samples = null;
      try {
        samples = runtime.audioRead(wantFrames);
      } catch (error) {
        this._warn('njw_audio_read failed: ' + (error && error.message ? error.message : error));
        break;
      }
      if (!samples || samples.length === 0) { break; }
      const frames = Math.floor(samples.length / channels);
      if (frames <= 0) { break; }
      try {
        this._scheduleChunk(samples, frames, channels, rate);
      } catch (error) {
        this._warn('could not schedule audio: ' + (error && error.message ? error.message : error));
        break;
      }
      chunks += 1;
    }
    this._updatePending();
  }

  _scheduleChunk(samples, frames, channels, rate) {
    const ctx = this.ctx;
    const buffer = ctx.createBuffer(channels, frames, rate);
    const inv = 1 / 32768;
    for (let ch = 0; ch < channels; ch++) {
      const out = buffer.getChannelData(ch);
      for (let i = 0; i < frames; i++) {
        out[i] = (samples[i * channels + ch] || 0) * inv;
      }
    }
    const source = ctx.createBufferSource();
    source.buffer = buffer;
    source.connect(this.gain);
    const start = Math.max(ctx.currentTime + 0.02, this._nextTime);
    const end = start + frames / rate;
    source.start(start);
    this._nextTime = end;
    this._pending.push({ start, end, frames });
    this._sources.add(source);
    source.onended = () => { this._sources.delete(source); };
    /* Trim anything that has already finished so the list stays small. */
    if (this._pending.length > 64) {
      const now = ctx.currentTime;
      this._pending = this._pending.filter((entry) => entry.end > now);
    }
  }

  /** Tell the runtime how many frames are scheduled but not audible yet. */
  _updatePending() {
    const ctx = this.ctx;
    if (!ctx) { return; }
    const now = ctx.currentTime;
    const rate = this._lastRate || 22050;
    let pending = 0;
    for (const entry of this._pending) {
      if (entry.end <= now) { continue; }
      if (entry.start >= now) {
        pending += entry.frames;
      } else {
        const audible = (now - entry.start) * rate;
        pending += Math.max(0, Math.round(entry.frames - audible));
      }
    }
    if (pending !== this._lastNote) {
      this._lastNote = pending;
      try {
        this.runtime.audioNotePlaying(pending);
      } catch (error) {
        /* the runtime may not implement it; never let audio break the loop */
      }
    }
  }

  dispose() {
    for (const source of this._sources) {
      try { source.stop(); } catch (error) { /* already stopped */ }
    }
    this._sources.clear();
    this._pending = [];
    this._lastNote = -1;
    if (this.ctx) {
      try { this.ctx.close(); } catch (error) { /* ignore */ }
    }
    this.ctx = null;
    this.gain = null;
  }

  _warn(message) {
    if (this._warned) { return; }
    this._warned = true;
    console.warn('[audio] ' + message);
  }
}
