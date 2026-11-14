/*
 * nano_j2me - web_test/web_common.mjs
 *
 * Shared helpers for the Emscripten/WASM node test harness.
 *
 * The tests drive build/web/js/nj2me.js exactly the way a browser shell does:
 * create the module, install the JARs in the Emscripten filesystem, marshal
 * strings by hand (there is no ccall/embind export), pump njw_frame() and
 * inspect the RGBA framebuffer.  Everything here is deterministic: the frame
 * loop always passes a fixed 1000/60 ms step, and no wall clock/random data
 * influences an assertion.
 *
 * Evidence (logs, PNG dumps) goes under .local-tools/debug-runs/, which is
 * git-ignored.  Nothing in this directory may commit or modify JARs.
 */

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath, pathToFileURL } from 'node:url';

// ---------------------------------------------------------------------------
// Repo layout / constants
// ---------------------------------------------------------------------------

const here = path.dirname(fileURLToPath(import.meta.url));
export const repoRoot = path.resolve(here, '..', '..');
export const wasmDir = path.join(repoRoot, 'build', 'web', 'js');
export const moduleJsPath = path.join(wasmDir, 'nj2me.js');
export const moduleWasmPath = path.join(wasmDir, 'nj2me.wasm');

export const FRAME_MS = 1000 / 60;

export const NJW_FRAME_RUNNING = 0x1;
export const NJW_FRAME_DREW = 0x2;
export const NJW_FRAME_LOADING = 0x4;

export const NJW_OPT_SCREEN_WIDTH = 1;
export const NJW_OPT_SCREEN_HEIGHT = 2;
export const NJW_OPT_HEAP_BYTES = 3;
export const NJW_OPT_SPEED_PERCENT = 4;
export const NJW_OPT_RMS_DIR = 5;
export const NJW_OPT_FS_DIR = 6;
export const NJW_OPT_DEVICE = 7;
export const NJW_OPT_MIDLET_CLASS = 8;
export const NJW_OPT_LOAD_STATE_PATH = 9;

export const NJW_POINTER_DOWN = 0;
export const NJW_POINTER_UP = 1;
export const NJW_POINTER_MOVE = 2;

// nj_key numeric ABI, from include/nj2me/nj_web.h.
export const KEY = Object.freeze({
    NONE: 0,
    '0': 1, '1': 2, '2': 3, '3': 4, '4': 5,
    '5': 6, '6': 7, '7': 8, '8': 9, '9': 10,
    STAR: 11, POUND: 12,
    UP: 13, DOWN: 14, LEFT: 15, RIGHT: 16, FIRE: 17,
    SOFT_LEFT: 18, SOFT_RIGHT: 19, CLEAR: 20,
    GAME_A: 21, GAME_B: 22, GAME_C: 23, GAME_D: 24,
    VOLUP: 25, VOLDOWN: 26, POWER: 27,
});

export const FS_ROOT = '/nano_j2me';
export const FS_JARS = FS_ROOT + '/jars';
export const FS_SAVES = FS_ROOT + '/saves';
export const FS_FILES = FS_ROOT + '/files';

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

function pad2(n) {
    return String(n).padStart(2, '0');
}

/** Local YYYYMMDD-HHMM stamp, matching the evidence directory convention. */
export function stampNow() {
    const d = new Date();
    return `${d.getFullYear()}${pad2(d.getMonth() + 1)}${pad2(d.getDate())}` +
        `-${pad2(d.getHours())}${pad2(d.getMinutes())}`;
}

/**
 * Resolve the evidence directory: explicit --evidence, else $WEB_EVIDENCE_DIR,
 * else .local-tools/debug-runs/web-verify-<YYYYMMDD-HHMM>.  Always created.
 */
export function resolveEvidenceDir(explicit) {
    const dir = explicit || process.env.WEB_EVIDENCE_DIR ||
        path.join(repoRoot, '.local-tools', 'debug-runs', `web-verify-${stampNow()}`);
    fs.mkdirSync(dir, { recursive: true });
    return dir;
}

/** Replace characters that are awkward in file names with '_'. */
export function sanitizeName(name) {
    return String(name).replace(/[^A-Za-z0-9._-]+/g, '_').slice(0, 80);
}

/**
 * Minimal argument parser shared by the node tests.  Accepts
 * `--evidence DIR` and any test-specific boolean/value options in `spec`
 * (e.g. { frames: 'int', filter: 'str' }); unknown options are an error.
 */
export function parseArgs(argv, spec = {}) {
    const out = { evidence: null, help: false, _: [] };
    for (const key of Object.keys(spec)) out[key] = spec[key] === 'int' ? null : null;
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        const eq = arg.indexOf('=');
        const name = eq >= 0 ? arg.slice(0, eq) : arg;
        const inlineValue = eq >= 0 ? arg.slice(eq + 1) : null;
        const takeValue = () => {
            if (inlineValue !== null) return inlineValue;
            if (i + 1 >= argv.length) throw new Error(`${name} needs a value`);
            return argv[++i];
        };
        if (name === '--evidence') {
            out.evidence = takeValue();
        } else if (name === '-h' || name === '--help') {
            out.help = true;
        } else if (name in spec) {
            const value = takeValue();
            out[name] = spec[name] === 'int' ? Number.parseInt(value, 10) : value;
        } else {
            throw new Error(`unknown argument: ${arg}`);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Read-only header parsing (so tests do not hard-code version numbers)
// ---------------------------------------------------------------------------

function readFileIfExists(file) {
    try {
        return fs.readFileSync(file, 'utf8');
    } catch {
        return null;
    }
}

/** NJW_API_VERSION from include/nj2me/nj_web.h (null when unparsable). */
export function headerApiVersion() {
    const text = readFileIfExists(path.join(repoRoot, 'include', 'nj2me', 'nj_web.h'));
    const m = text && /#define\s+NJW_API_VERSION\s+(\d+)/.exec(text);
    return m ? Number.parseInt(m[1], 10) : null;
}

/** NJ_VERSION_STRING from include/nj2me/nj.h (null when unparsable). */
export function headerRuntimeVersion() {
    const text = readFileIfExists(path.join(repoRoot, 'include', 'nj2me', 'nj.h'));
    const m = text && /#define\s+NJ_VERSION_STRING\s+"([^"]+)"/.exec(text);
    return m ? m[1] : null;
}

// ---------------------------------------------------------------------------
// Module loading
// ---------------------------------------------------------------------------

/** Throw a helpful error when the Emscripten build has not been produced. */
export function requireModuleBuilt() {
    const missing = [];
    if (!fs.existsSync(moduleJsPath)) missing.push(moduleJsPath);
    if (!fs.existsSync(moduleWasmPath)) missing.push(moduleWasmPath);
    if (missing.length) {
        throw new Error(
            'wasm module not built: missing ' + missing.join(', ') +
            ' — run tools/build_web.sh first');
    }
}

/**
 * Instantiate the Emscripten module.  Emscripten's ESM output resolves
 * nj2me.wasm next to itself via import.meta.url, but we pass an explicit
 * absolute file URL through locateFile so the test also works when the
 * module is imported from a different working directory.
 *
 * stdout/stderr written by the C runtime (print/printErr) is captured in
 * `m.njTestLogs` instead of polluting the test output.
 */
export async function loadModule(options = {}) {
    requireModuleBuilt();
    const createModule = (await import(pathToFileURL(moduleJsPath).href)).default;
    if (typeof createModule !== 'function') {
        throw new Error('module has no default createNj2meModule export');
    }
    const logs = [];
    const sink = (kind) => (line) => {
        if (logs.length < 500) logs.push(`${kind}: ${line}`);
    };
    const module = await createModule({
        locateFile: (file) => pathToFileURL(path.join(wasmDir, file)).href,
        print: sink('out'),
        printErr: sink('err'),
    });
    Object.defineProperty(module, 'njTestLogs', { value: logs, enumerable: false });
    if (!module.HEAPU8 || !module.FS) {
        throw new Error('module did not expose the Emscripten runtime (HEAPU8/FS)');
    }
    return module;
}

// ---------------------------------------------------------------------------
// String marshalling (no ccall/embind: allocate + stringToUTF8 + free)
// ---------------------------------------------------------------------------

export function allocCString(m, text) {
    const bytes = m.lengthBytesUTF8(text) + 1;
    const ptr = m._malloc(bytes);
    if (!ptr) throw new Error(`out of wasm memory marshalling ${bytes} bytes`);
    m.stringToUTF8(text, ptr, bytes);
    return ptr;
}

/** Run fn(ptr) with a transient NUL-terminated UTF-8 copy of `text`. */
export function withCString(m, text, fn) {
    const ptr = allocCString(m, text);
    try {
        return fn(ptr);
    } finally {
        m._free(ptr);
    }
}

/** Read a NUL-terminated UTF-8 string from wasm memory; '' for NULL. */
export function readCString(m, ptr) {
    return ptr ? m.UTF8ToString(ptr) : '';
}

// ---------------------------------------------------------------------------
// Filesystem helpers (MEMFS in node; the browser shell uses IDBFS)
// ---------------------------------------------------------------------------

export function ensureDirs(m, id) {
    m.FS.mkdirTree(FS_JARS);
    m.FS.mkdirTree(`${FS_SAVES}/${id}`);
    m.FS.mkdirTree(`${FS_FILES}/${id}`);
    return { saves: `${FS_SAVES}/${id}`, files: `${FS_FILES}/${id}` };
}

/** Write a host-side JAR file into the wasm FS and return its FS path. */
export function installJar(m, hostJarPath, fsName = path.basename(hostJarPath)) {
    m.FS.mkdirTree(FS_JARS);
    const bytes = new Uint8Array(fs.readFileSync(hostJarPath));
    const fsPath = `${FS_JARS}/${fsName}`;
    m.FS.writeFile(fsPath, bytes);
    return fsPath;
}

/** Write arbitrary bytes into the wasm FS (for negative tests). */
export function installBytes(m, fsName, bytes) {
    m.FS.mkdirTree(FS_JARS);
    const fsPath = `${FS_JARS}/${fsName}`;
    m.FS.writeFile(fsPath, bytes);
    return fsPath;
}

/**
 * Reset options and point this "game" at private RMS/FS directories.
 * Returns 0 on success; the caller asserts it.
 */
export function configureRunner(m, options = {}) {
    const { id = 'game', width = 240, height = 320, speed = null } = options;
    m._njw_options_reset();
    const rcW = m._njw_option_int(NJW_OPT_SCREEN_WIDTH, width);
    const rcH = m._njw_option_int(NJW_OPT_SCREEN_HEIGHT, height);
    if (speed !== null) m._njw_option_int(NJW_OPT_SPEED_PERCENT, speed);
    const dirs = ensureDirs(m, id);
    const rcRms = withCString(m, dirs.saves, (p) => m._njw_option_string(NJW_OPT_RMS_DIR, p));
    const rcFs = withCString(m, dirs.files, (p) => m._njw_option_string(NJW_OPT_FS_DIR, p));
    return { rcW, rcH, rcRms, rcFs, dirs };
}

export function startJar(m, fsPath) {
    return withCString(m, fsPath, (p) => m._njw_start(p));
}

export function probeJar(m, fsPath) {
    return withCString(m, fsPath, (p) => m._njw_probe(p));
}

export function njwError(m) {
    return readCString(m, m._njw_error());
}

export function njwStopReason(m) {
    return readCString(m, m._njw_stop_reason());
}

export function njwStop(m) {
    m._njw_stop();
}

// ---------------------------------------------------------------------------
// Frame pumping
// ---------------------------------------------------------------------------

/**
 * Pump up to `count` frames of 1000/60 ms.  Stops early on a fatal error or a
 * normal MIDlet exit (NJW_FRAME_RUNNING clear), mirroring the browser loop.
 */
export function pumpFrames(m, count, options = {}) {
    const dt = options.dt ?? FRAME_MS;
    const epochStart = m._njw_frame_epoch();
    const result = {
        frames: 0,
        drew: 0,
        lastFlags: 0,
        stopped: false,
        reason: '',
        fatal: false,
        error: '',
        epochDelta: 0,
        fps: null,
    };
    for (let i = 0; i < count; i++) {
        const flags = m._njw_frame(dt);
        result.frames++;
        result.lastFlags = flags;
        if (flags < 0) {
            result.fatal = true;
            result.error = njwError(m);
            break;
        }
        if (flags & NJW_FRAME_DREW) result.drew++;
        if (!(flags & NJW_FRAME_RUNNING)) {
            result.stopped = true;
            result.reason = njwStopReason(m);
            break;
        }
        if (options.onFrame) options.onFrame(i, flags);
    }
    result.epochDelta = m._njw_frame_epoch() - epochStart;
    return result;
}

// ---------------------------------------------------------------------------
// Framebuffer inspection / PNG dump
// ---------------------------------------------------------------------------

/**
 * Describe the last presented RGBA framebuffer.  `nonBlack` counts pixels
 * with a non-zero R/G/B channel; `distinctColors` is capped at 32 so the
 * cost stays bounded for a full 240x320 screen.
 */
export function framebufferStats(m) {
    const width = m._njw_width();
    const height = m._njw_height();
    const ptr = m._njw_framebuffer();
    const total = width * height;
    const stats = { width, height, ptr, total, nonBlack: 0, distinctColors: 0, opaque: 0, hash: 0 };
    if (!ptr || !total) return stats;
    const heap = m.HEAPU8;
    if (ptr + total * 4 > heap.length) {
        throw new Error(`framebuffer pointer ${ptr} + ${total * 4} bytes exceeds HEAPU8 (${heap.length})`);
    }
    const seen = new Set();
    let hash = 0x811c9dc5; // FNV-1a 32 bit over the RGBA bytes
    for (let i = 0; i < total; i++) {
        const p = ptr + i * 4;
        const r = heap[p];
        const g = heap[p + 1];
        const b = heap[p + 2];
        const a = heap[p + 3];
        if (r || g || b) stats.nonBlack++;
        if (a === 0xff) stats.opaque++;
        if (seen.size < 32) seen.add((r << 16) | (g << 8) | b);
        hash ^= heap[p]; hash = Math.imul(hash, 0x01000193);
        hash ^= heap[p + 1]; hash = Math.imul(hash, 0x01000193);
        hash ^= heap[p + 2]; hash = Math.imul(hash, 0x01000193);
        hash ^= heap[p + 3]; hash = Math.imul(hash, 0x01000193);
    }
    stats.distinctColors = seen.size;
    stats.hash = (hash >>> 0).toString(16).padStart(8, '0');
    return stats;
}

export function copyFramebuffer(m) {
    const width = m._njw_width();
    const height = m._njw_height();
    const ptr = m._njw_framebuffer();
    if (!ptr || !width || !height) return null;
    return m.HEAPU8.slice(ptr, ptr + width * height * 4);
}

// -- minimal dependency-free PNG writer (RGBA8, filter 0) --------------------

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(buf) {
    let c = 0xffffffff;
    for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length, 0);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body), 0);
    return Buffer.concat([length, body, crc]);
}

/** Write an RGBA8888 buffer as an 8-bit RGBA PNG.  Returns bytes written. */
export function writePng(filePath, rgba, width, height) {
    if (!rgba || rgba.length < width * height * 4) {
        throw new Error(`writePng: buffer ${rgba ? rgba.length : 0} bytes < ${width * height * 4}`);
    }
    const stride = width * 4;
    const raw = Buffer.alloc((stride + 1) * height);
    for (let y = 0; y < height; y++) {
        raw[y * (stride + 1)] = 0; // filter: none
        raw.set(rgba.subarray(y * stride, (y + 1) * stride), y * (stride + 1) + 1);
    }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type: RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        pngChunk('IHDR', ihdr),
        pngChunk('IDAT', zlib.deflateSync(raw, { level: 6 })),
        pngChunk('IEND', Buffer.alloc(0)),
    ]);
    fs.writeFileSync(filePath, png);
    return png.length;
}

/** Convenience: dump the current (last presented) framebuffer as PNG. */
export function dumpFramebufferPng(m, filePath) {
    const width = m._njw_width();
    const height = m._njw_height();
    const pixels = copyFramebuffer(m);
    if (!pixels) throw new Error('dumpFramebufferPng: no framebuffer presented yet');
    return writePng(filePath, pixels, width, height);
}

// ---------------------------------------------------------------------------
// Tiny test runner: PASS/FAIL per check, summary, non-zero exit on failure
// ---------------------------------------------------------------------------

export class TestRun {
    constructor(name) {
        this.name = name;
        this.passed = 0;
        this.failed = 0;
        this.skipped = 0;
        this.failures = [];
        this.startedAt = Date.now();
    }

    section(title) {
        console.log(`\n== ${title}`);
    }

    recordPass(name, detail) {
        this.passed++;
        console.log(`PASS  ${name}${detail ? ` (${detail})` : ''}`);
    }

    recordFail(name, reason) {
        this.failed++;
        this.failures.push(`${name}: ${reason}`);
        console.log(`FAIL  ${name}: ${reason}`);
    }

    skip(name, reason) {
        this.skipped++;
        console.log(`SKIP  ${name}: ${reason}`);
    }

    /** Run one check; a throw marks it failed and continues. */
    async check(name, fn) {
        try {
            const detail = await fn();
            this.recordPass(name, typeof detail === 'string' ? detail : undefined);
            return true;
        } catch (err) {
            this.recordFail(name, err && err.message ? err.message : String(err));
            return false;
        }
    }

    finish() {
        const elapsedMs = Date.now() - this.startedAt;
        const total = this.passed + this.failed;
        console.log('');
        console.log(`== ${this.name}: ${this.passed}/${total} checks passed` +
            (this.skipped ? `, ${this.skipped} skipped` : '') +
            ` in ${(elapsedMs / 1000).toFixed(1)}s`);
        if (this.failed) {
            console.log('failed checks:');
            for (const failure of this.failures) console.log(`  - ${failure}`);
        }
        return this.failed ? 1 : 0;
    }
}

// -- assertion helpers (throw plain Errors with useful messages) -------------

export function assert(condition, message) {
    if (!condition) throw new Error(message);
}

export function assertEqual(actual, expected, what) {
    if (actual !== expected) throw new Error(`${what}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
}

export function assertMatch(value, regexp, what) {
    if (!regexp.test(String(value))) throw new Error(`${what}: ${JSON.stringify(value)} does not match ${regexp}`);
}

export function assertEmptyString(value, what) {
    if (value !== '') throw new Error(`${what}: expected empty string, got ${JSON.stringify(value)}`);
}

export function assertNonEmptyString(value, what) {
    if (typeof value !== 'string' || value.length === 0) throw new Error(`${what}: expected a non-empty string, got ${JSON.stringify(value)}`);
}

export function assertRange(value, min, max, what) {
    if (!Number.isFinite(value) || value < min || value > max) {
        throw new Error(`${what}: expected ${min}..${max}, got ${value}`);
    }
}

/** Short one-line summary of a pumpFrames result, for failure messages. */
export function describePump(result) {
    if (result.fatal) return `fatal error after ${result.frames} frames: ${result.error}`;
    if (result.stopped) return `MIDlet stopped after ${result.frames} frames: ${result.reason}`;
    return `${result.frames} frames, drew ${result.drew}, last flags ${result.lastFlags}`;
}
