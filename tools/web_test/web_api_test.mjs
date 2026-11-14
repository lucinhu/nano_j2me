#!/usr/bin/env node
/*
 * nano_j2me - web_test/web_api_test.mjs
 *
 * Contract test for the JS <-> C API exported by the Emscripten build
 * (include/nj2me/nj_web.h, API version 1).  It instantiates
 * build/web/js/nj2me.js in plain Node, drives the VM through njw_frame() with
 * a fixed 1000/60 ms timestep and checks:
 *
 *   - module/version identity against the C headers (no hard-coded version)
 *   - manifest probing (title/vendor/version/MIDlet class/icon) for the four
 *     baseline JARs
 *   - error paths (empty path, missing/garbage JAR, bad MIDlet class) and
 *     recovery through njw_stop()
 *   - hello.jar baseline render: 240 frames, 240x320, every pixel non-black
 *   - screen-size and speed options, key + pointer input on game15.jar
 *   - save-state round trip + rejection of a truncated state, restart
 *   - audio: audiodemo.jar queues 8000 Hz mono frames within 300 frames
 *   - heap stability across two restart cycles
 *
 * Determinism: every njw_frame() call gets exactly 1000/60 ms, no wall clock
 * or randomness participates in an assertion.  Evidence (PNG frame dumps,
 * summaries, captured engine logs) is written under the evidence directory:
 *   node tools/web_test/web_api_test.mjs [--evidence DIR]
 *
 * Exit status: 0 when every check passes, 1 otherwise.
 */

import fs from 'node:fs';
import path from 'node:path';

import {
    FRAME_MS,
    KEY,
    NJW_FRAME_DREW,
    NJW_FRAME_RUNNING,
    NJW_OPT_MIDLET_CLASS,
    TestRun,
    assert,
    assertEqual,
    assertMatch,
    assertNonEmptyString,
    assertRange,
    configureRunner,
    describePump,
    dumpFramebufferPng,
    framebufferStats,
    headerApiVersion,
    headerRuntimeVersion,
    installBytes,
    installJar,
    loadModule,
    moduleJsPath,
    moduleWasmPath,
    njwError,
    njwStop,
    njwStopReason,
    parseArgs,
    probeJar,
    pumpFrames,
    readCString,
    repoRoot,
    requireModuleBuilt,
    resolveEvidenceDir,
    startJar,
    withCString,
} from './web_common.mjs';

// ---------------------------------------------------------------------------
// Baseline fixtures.  The two games are the ones the Lead's manual baseline
// used (tests/games/hello.jar, tests/games/game15.jar); the audio/bench
// fixtures cover the audio and notifyDestroyed paths.
// ---------------------------------------------------------------------------

const JARS = Object.freeze({
    hello: path.join(repoRoot, 'tests', 'games', 'hello.jar'),
    game15: path.join(repoRoot, 'tests', 'games', 'game15.jar'),
    audiodemo: path.join(repoRoot, 'tests', 'fixtures', 'audiodemo.jar'),
    bench: path.join(repoRoot, 'tests', 'fixtures', 'bench.jar'),
});

// Expected manifest values (verified against the runtime; changing a fixture
// means updating this table).
const METADATA = Object.freeze({
    hello: {
        title: 'Hello World',
        vendor: 'frog-parrot.net',
        version: '2.0',
        count: 1,
        midletClass: 'net.frog_parrot.hello.Hello',
        iconKind: 'png',
        iconLen: 191,
    },
    game15: {
        title: 'Game15MIDlet',
        vendor: 'see the source project',
        version: '1.0.0',
        count: 1,
        midletClass: 'Game15MIDlet',
        iconKind: 'none',
    },
    audiodemo: {
        title: 'AudioDemo',
        vendor: 'nano_j2me',
        version: '1.0.0',
        count: 1,
        midletClass: 'snd.AudioDemo',
        // This fixture declares /res/beep.wav as its MIDlet-1 icon, so the
        // API returns the stored RIFF bytes (not an image).
        iconKind: 'wav',
    },
    bench: {
        title: 'Bench',
        vendor: 'nano_j2me',
        version: '1.0.0',
        count: 1,
        midletClass: 'bench.Bench',
        iconKind: 'none',
    },
});

const SCREEN_W = 240;
const SCREEN_H = 320;
const PIXELS = SCREEN_W * SCREEN_H;
const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

// ---------------------------------------------------------------------------
// Small wasm-memory helpers (the build has no ccall/embind; only raw ptrs)
// ---------------------------------------------------------------------------

function saveState(m) {
    const blobPtrPtr = m._malloc(4);
    const lenPtr = m._malloc(4);
    try {
        m.HEAP32[blobPtrPtr >> 2] = 0;
        m.HEAP32[lenPtr >> 2] = 0;
        const rc = m._njw_save_state(blobPtrPtr, lenPtr);
        assertEqual(rc, 0, 'njw_save_state rc');
        const blobPtr = m.HEAP32[blobPtrPtr >> 2];
        const len = m.HEAP32[lenPtr >> 2];
        assert(blobPtr > 0, 'njw_save_state returned a NULL blob');
        assert(len > 0, 'njw_save_state returned length 0');
        const bytes = m.HEAPU8.slice(blobPtr, blobPtr + len);
        m._njw_free(blobPtr);
        return bytes;
    } finally {
        m._free(blobPtrPtr);
        m._free(lenPtr);
    }
}

function loadState(m, bytes) {
    const ptr = m._njw_alloc(bytes.length);
    assert(ptr > 0, `njw_alloc(${bytes.length}) failed`);
    try {
        m.HEAPU8.set(bytes, ptr);
        return m._njw_load_state(ptr, bytes.length);
    } finally {
        m._njw_free(ptr);
    }
}

function resetError(m, helloFsPath) {
    // runner_set_error() keeps the first failure, so a successful start is the
    // documented way to clear it before testing the next error path.
    configureRunner(m, { id: 'error-reset' });
    assertEqual(startJar(m, helloFsPath), 0, 'njw_start(hello) while resetting the error');
    njwStop(m);
    assertEqual(njwError(m), '', 'njw_error() was not cleared by a successful start');
}

function readIcon(m) {
    const lenPtr = m._malloc(4);
    try {
        m.HEAP32[lenPtr >> 2] = -1;
        const ptr = m._njw_app_icon(lenPtr);
        return { ptr, len: m.HEAP32[lenPtr >> 2] };
    } finally {
        m._free(lenPtr);
    }
}

// ---------------------------------------------------------------------------
// Test body
// ---------------------------------------------------------------------------

async function main() {
    const args = parseArgs(process.argv.slice(2));
    if (args.help) {
        console.log('usage: node tools/web_test/web_api_test.mjs [--evidence DIR]');
        return 0;
    }
    const evidenceDir = resolveEvidenceDir(args.evidence);
    console.log(`evidence directory: ${evidenceDir}`);

    const t = new TestRun('web_api');
    let m = null;
    let heapInitial = 0;

    for (const [name, file] of Object.entries(JARS)) {
        if (!fs.existsSync(file)) {
            t.recordFail('fixtures present', `missing baseline JAR ${name}: ${file}`);
            return t.finish();
        }
    }

    // -- module / build identity ------------------------------------------
    t.section('module and ABI identity');

    await t.check('wasm build artifacts present (js + wasm)', () => {
        requireModuleBuilt();
        const jsBytes = fs.statSync(moduleJsPath).size;
        const wasmBytes = fs.statSync(moduleWasmPath).size;
        assert(jsBytes > 32 * 1024, `nj2me.js is only ${jsBytes} bytes`);
        assert(wasmBytes > 512 * 1024, `nj2me.wasm is only ${wasmBytes} bytes`);
        return `nj2me.js ${jsBytes} B, nj2me.wasm ${wasmBytes} B`;
    });

    await t.check('ES module instantiates', async () => {
        m = await loadModule();
        assert(Boolean(m.HEAPU8 && m.FS && m.MEMFS), 'Emscripten runtime handles are missing');
        heapInitial = m.HEAPU8.length;
        assertRange(heapInitial, 16 * 1024 * 1024, 1024 * 1024 * 1024, 'initial HEAPU8 size');
        return `initial heap ${(heapInitial / 1048576).toFixed(0)} MiB`;
    });

    if (m === null) {
        t.recordFail('module usable', 'cannot continue without the wasm module');
        return t.finish();
    }

    await t.check('njw_api_version matches NJW_API_VERSION in include/nj2me/nj_web.h', () => {
        const expected = headerApiVersion();
        assert(expected !== null, 'could not parse #define NJW_API_VERSION from include/nj2me/nj_web.h');
        assertEqual(m._njw_api_version(), expected, 'njw_api_version()');
        return `API version ${expected}`;
    });

    await t.check('njw_runtime_version matches NJ_VERSION_STRING in include/nj2me/nj.h', () => {
        const expected = headerRuntimeVersion();
        assert(expected !== null, 'could not parse #define NJ_VERSION_STRING from include/nj2me/nj.h');
        const actual = readCString(m, m._njw_runtime_version());
        assertEqual(actual, expected, 'njw_runtime_version()');
        return `runtime version ${actual}`;
    });

    await t.check('pre-start defaults: stopped, audio 22050 Hz mono, no text editor', () => {
        assertEqual(m._njw_running(), 0, 'njw_running() before start');
        assertEqual(m._njw_displayable_ready(), 0, 'njw_displayable_ready() before start');
        assertEqual(m._njw_audio_available(), 0, 'njw_audio_available()');
        assertEqual(m._njw_audio_rate(), 22050, 'njw_audio_rate()');
        assertEqual(m._njw_audio_channels(), 1, 'njw_audio_channels()');
        assertEqual(m._njw_speed(), 100, 'njw_speed() default');
        assertEqual(m._njw_text_active(), 0, 'njw_text_active()');
        assertEqual(m._njw_text_current(0, 0), 0, 'njw_text_current(NULL,0) with no editor');
        const a = m._malloc(4);
        const b = m._malloc(4);
        const c = m._malloc(4);
        const d = m._malloc(4);
        const e = m._malloc(4);
        const f = m._malloc(4);
        try {
            m.HEAP32[a >> 2] = -1;
            m._njw_text_state(a, b, c, d, e, f);
            assertEqual(m.HEAP32[a >> 2], 0, 'njw_text_state(active)');
        } finally {
            for (const ptr of [a, b, c, d, e, f]) m._free(ptr);
        }
        return 'defaults OK';
    });

    await t.check('unknown options are rejected', () => {
        assertEqual(m._njw_option_int(99, 1), -1, 'njw_option_int(unknown)');
        assertEqual(withCString(m, 'x', (p) => m._njw_option_string(99, p)), -1, 'njw_option_string(unknown)');
        assertEqual(m._njw_option_int(NJW_OPT_MIDLET_CLASS, 0), -1, 'njw_option_int(string option)');
        m._njw_options_reset();
        return 'option 99 and int-vs-string mismatch rejected';
    });

    // -- manifest probing --------------------------------------------------
    t.section('JAR probing (njw_probe)');

    const fsPaths = {};
    await t.check('baseline JARs copied into the Emscripten filesystem', () => {
        for (const [name, file] of Object.entries(JARS)) {
            fsPaths[name] = installJar(m, file);
            const hostSize = fs.statSync(file).size;
            const fsSize = m.FS.stat(fsPaths[name]).size;
            assertEqual(fsSize, hostSize, `MEMFS size of ${path.basename(file)}`);
        }
        return Object.keys(fsPaths).length + ' JARs installed';
    });

    for (const [name, expected] of Object.entries(METADATA)) {
        await t.check(`njw_probe metadata: ${name}.jar`, () => {
            assertEqual(probeJar(m, fsPaths[name]), 0, `njw_probe rc for ${name}`);
            assertEqual(readCString(m, m._njw_app_title()), expected.title, 'app_title');
            assertEqual(readCString(m, m._njw_app_vendor()), expected.vendor, 'app_vendor');
            assertEqual(readCString(m, m._njw_app_version()), expected.version, 'app_version');
            assertEqual(m._njw_midlet_count(), expected.count, 'midlet_count');
            assertEqual(readCString(m, m._njw_midlet_class_at(0)), expected.midletClass, 'midlet_class_at(0)');
            assertEqual(readCString(m, m._njw_midlet_class_at(1)), '', 'midlet_class_at(1) out of range');
            const icon = readIcon(m);
            if (icon.ptr) {
                assert(icon.len > 0, 'app_icon returned a pointer with length 0');
                assert(icon.ptr + icon.len <= m.HEAPU8.length, 'app_icon pointer+length outside HEAPU8');
            } else {
                assertEqual(icon.len, 0, 'app_icon returned NULL with non-zero length');
            }
            if (expected.iconKind === 'none') {
                assertEqual(icon.ptr, 0, 'expected no icon bytes');
                assertEqual(icon.len, 0, 'expected icon length 0');
            } else if (expected.iconKind === 'png') {
                assert(icon.ptr > 0, 'expected the PNG icon bytes');
                assertEqual(icon.len, expected.iconLen, 'icon length');
                const magic = Array.from(m.HEAPU8.slice(icon.ptr, icon.ptr + PNG_SIGNATURE.length));
                assert(magic.every((byte, i) => byte === PNG_SIGNATURE[i]),
                    `icon is not a PNG (signature ${magic.map((b) => b.toString(16)).join(' ')})`);
            } else if (expected.iconKind === 'wav') {
                assert(icon.ptr > 0, 'expected the stored icon resource bytes');
                const magic = String.fromCharCode(...m.HEAPU8.slice(icon.ptr, icon.ptr + 4));
                assertEqual(magic, 'RIFF', 'icon prefix');
            }
            return `${expected.title} / ${expected.midletClass} / icon ${icon.ptr ? icon.len + ' B' : 'none'}`;
        });
    }

    await t.check('njw_probe rejects a missing JAR', () => {
        resetError(m, fsPaths.hello);
        const rc = probeJar(m, '/nano_j2me/jars/does-not-exist.jar');
        assertEqual(rc, -1, 'njw_probe rc for a missing file');
        const error = njwError(m);
        assertMatch(error, /cannot read/i, 'njw_error() after failed probe');
        return error;
    });

    await t.check('njw_probe rejects garbage bytes', () => {
        resetError(m, fsPaths.hello);
        const fsPath = installBytes(m, 'garbage-probe.jar', Buffer.from('this is definitely not a JAR archive\n'));
        const rc = probeJar(m, fsPath);
        assertEqual(rc, -1, 'njw_probe rc for garbage data');
        const error = njwError(m);
        assertMatch(error, /MIDlet JAR|not a JAR|invalid/i, 'njw_error() after failed probe');
        return error;
    });

    // -- start error paths -------------------------------------------------
    t.section('njw_start error paths');

    await t.check('empty JAR path is rejected', () => {
        resetError(m, fsPaths.hello);
        assertEqual(startJar(m, ''), -1, 'njw_start("")');
        const error = njwError(m);
        assertMatch(error, /empty JAR path/i, 'njw_error() for empty path');
        assertEqual(m._njw_running(), 0, 'njw_running() after failed start');
        njwStop(m);
        return error;
    });

    await t.check('missing JAR file is rejected', () => {
        resetError(m, fsPaths.hello);
        assertEqual(startJar(m, '/nano_j2me/jars/nope.jar'), -1, 'njw_start(missing)');
        const error = njwError(m);
        assertMatch(error, /cannot read JAR/i, 'njw_error() for missing file');
        assertEqual(m._njw_running(), 0, 'njw_running() after failed start');
        njwStop(m);
        return error;
    });

    await t.check('non-JAR file is rejected', () => {
        resetError(m, fsPaths.hello);
        const fsPath = installBytes(m, 'garbage-start.jar', Buffer.from('\x00\x01\x02 not a zip at all'));
        assertEqual(startJar(m, fsPath), -1, 'njw_start(garbage)');
        const error = njwError(m);
        assertMatch(error, /cannot open JAR|not a JAR|invalid/i, 'njw_error() for garbage file');
        assertEqual(m._njw_running(), 0, 'njw_running() after failed start');
        njwStop(m);
        return error;
    });

    await t.check('unknown MIDlet class option is rejected', () => {
        resetError(m, fsPaths.hello);
        configureRunner(m, { id: 'badclass' });
        assertEqual(withCString(m, 'no.such.Class', (p) => m._njw_option_string(NJW_OPT_MIDLET_CLASS, p)),
            0, 'set NJW_OPT_MIDLET_CLASS');
        assertEqual(startJar(m, fsPaths.hello), -1, 'njw_start with bogus MIDlet class');
        const error = njwError(m);
        assertMatch(error, /cannot instantiate/i, 'njw_error() for bogus class');
        assertEqual(m._njw_running(), 0, 'njw_running() after failed start');
        njwStop(m);
        return error;
    });

    await t.check('options/state are reset after failed starts (hello starts)', () => {
        const cfg = configureRunner(m, { id: 'after-failures' });
        assertEqual(cfg.rcW, 0, 'SCREEN_WIDTH option');
        assertEqual(cfg.rcH, 0, 'SCREEN_HEIGHT option');
        assertEqual(cfg.rcRms, 0, 'RMS_DIR option');
        assertEqual(cfg.rcFs, 0, 'FS_DIR option');
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello) after failures');
        assertEqual(njwError(m), '', 'njw_error() after successful start');
        njwStop(m);
        assertEqual(m._njw_running(), 0, 'njw_running() after njw_stop');
        return 'start works after failures';
    });

    // -- hello.jar render baseline ----------------------------------------
    t.section('hello.jar render baseline');

    let helloPngPath = '';
    await t.check(`hello.jar: ${SCREEN_W}x${SCREEN_H}, 240 frames, every pixel drawn`, () => {
        const cfg = configureRunner(m, { id: 'hello-baseline' });
        assertEqual(cfg.rcW, 0, 'SCREEN_WIDTH option');
        assertEqual(cfg.rcH, 0, 'SCREEN_HEIGHT option');
        assertEqual(cfg.rcRms, 0, 'RMS_DIR option');
        assertEqual(cfg.rcFs, 0, 'FS_DIR option');
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello)');
        assertEqual(m._njw_running(), 1, 'njw_running() after start');
        assertEqual(njwError(m), '', 'njw_error() after start');

        const run = pumpFrames(m, 240);
        assert(!run.fatal, `fatal engine error: ${run.error}`);
        assert(!run.stopped, `hello.jar stopped early: ${run.reason}`);
        assertEqual(run.frames, 240, 'frames pumped');
        assertEqual(run.drew, 240, 'frames with NJW_FRAME_DREW');
        assertEqual(run.epochDelta, run.drew, 'frame epoch delta vs drew frames');
        assert((run.lastFlags & NJW_FRAME_RUNNING) !== 0, 'NJW_FRAME_RUNNING cleared');
        assert((run.lastFlags & NJW_FRAME_DREW) !== 0, 'last frame did not draw');
        assertEqual(m._njw_frames(), 240, 'njw_frames()');
        assertEqual(m._njw_frame_epoch(), 240, 'njw_frame_epoch() after start at 0');
        assertEqual(m._njw_displayable_ready(), 1, 'njw_displayable_ready()');
        assertEqual(m._njw_audio_available(), 0, 'hello.jar should not queue audio');

        const stats = framebufferStats(m);
        assertEqual(stats.width, SCREEN_W, 'framebuffer width');
        assertEqual(stats.height, SCREEN_H, 'framebuffer height');
        assert(stats.ptr > 0, 'njw_framebuffer() is NULL');
        assertEqual(stats.total, PIXELS, 'framebuffer pixel count');
        assertEqual(stats.nonBlack, PIXELS, 'non-black pixel count (lead baseline: all pixels)');
        assert(stats.distinctColors >= 2, 'framebuffer should contain at least 2 colors');
        return `${run.drew} drew frames, ${stats.nonBlack}/${PIXELS} non-black, colors=${stats.distinctColors}`;
    });

    await t.check('framebuffer pointer and dimensions stay stable across frames', () => {
        const before = framebufferStats(m);
        const run = pumpFrames(m, 2);
        assert(!run.fatal, `fatal engine error: ${run.error}`);
        const after = framebufferStats(m);
        assertEqual(after.ptr, before.ptr, 'njw_framebuffer() pointer');
        assertEqual(after.width, before.width, 'width');
        assertEqual(after.height, before.height, 'height');
        return `ptr=0x${after.ptr.toString(16)} unchanged`;
    });

    await t.check('hello.jar frame dumped as PNG evidence', () => {
        helloPngPath = path.join(evidenceDir, 'hello-240.png');
        const bytesWritten = dumpFramebufferPng(m, helloPngPath);
        const png = fs.readFileSync(helloPngPath);
        assertEqual(png.length, bytesWritten, 'bytes written vs file size');
        const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
        assert(signature.every((b, i) => png[i] === b), 'missing PNG signature');
        assertEqual(png.readUInt32BE(16), SCREEN_W, 'PNG IHDR width');
        assertEqual(png.readUInt32BE(20), SCREEN_H, 'PNG IHDR height');
        return `${helloPngPath} (${png.length} B)`;
    });

    await t.check('njw_stop on hello.jar: VM stops, non-empty stop reason', () => {
        njwStop(m);
        assertEqual(m._njw_running(), 0, 'njw_running() after njw_stop');
        const reason = njwStopReason(m);
        assertNonEmptyString(reason, 'njw_stop_reason()');
        return reason;
    });

    // -- options -----------------------------------------------------------
    t.section('options: screen size, speed');

    await t.check('320x240 screen option changes the framebuffer geometry', () => {
        configureRunner(m, { id: 'hello-320x240', width: 320, height: 240 });
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello) at 320x240');
        const run = pumpFrames(m, 60);
        assert(!run.fatal, `fatal engine error: ${run.error}`);
        assert(!run.stopped, `stopped early: ${run.reason}`);
        const stats = framebufferStats(m);
        assertEqual(stats.width, 320, 'width');
        assertEqual(stats.height, 240, 'height');
        assert(stats.nonBlack > 0, 'screen is fully black');
        assert(run.epochDelta > 0, 'no frame epochs observed');
        njwStop(m);
        assertEqual(m._njw_running(), 0, 'njw_running() after njw_stop');
        return `320x240, ${stats.nonBlack}/${stats.total} non-black`;
    });

    await t.check('speed option/getter clamp to 10..800', () => {
        configureRunner(m, { id: 'hello-speed', speed: 150 });
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello)');
        assertEqual(m._njw_speed(), 150, 'njw_speed() after start');
        pumpFrames(m, 5);
        m._njw_set_speed(200);
        assertEqual(m._njw_speed(), 200, 'njw_speed() after set 200');
        m._njw_set_speed(5);
        assertEqual(m._njw_speed(), 10, 'njw_speed() clamped low');
        m._njw_set_speed(100);
        assertEqual(m._njw_speed(), 100, 'njw_speed() after set 100');
        njwStop(m);
        return '150 -> 200 -> 10 (clamped) -> 100';
    });

    await t.check('njw_stop restores option defaults', () => {
        m._njw_options_reset();
        njwStop(m); // safe twice
        assertEqual(m._njw_running(), 0, 'njw_running() after double stop');
        configureRunner(m, { id: 'hello-after-stop' });
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello) after njw_stop');
        const run = pumpFrames(m, 30);
        assert(!run.fatal, `fatal engine error: ${run.error}`);
        assert(run.drew > 0, 'no frames drawn after restart');
        njwStop(m);
        return `${run.drew} frames drawn after restart`;
    });

    // -- input on game15.jar ----------------------------------------------
    t.section('input: keys and pointer on game15.jar');

    await t.check('game15.jar consumes key and pointer events', () => {
        configureRunner(m, { id: 'g15-input' });
        assertEqual(startJar(m, fsPaths.game15), 0, 'njw_start(game15)');
        let run = pumpFrames(m, 30);
        assert(!run.fatal && !run.stopped, `game15 failed to start: ${describePump(run)}`);
        assert(run.drew > 0, 'no frames drawn');

        const keys = [KEY.FIRE, KEY.LEFT, KEY.RIGHT, KEY.UP, KEY.DOWN,
            KEY.SOFT_LEFT, KEY.SOFT_RIGHT, KEY['5'], KEY.GAME_A];
        for (const key of keys) {
            m._njw_key(key, 1);
            run = pumpFrames(m, 2);
            assert(!run.fatal, `fatal after key ${key} down: ${run.error}`);
            assert(!run.stopped, `game15 stopped after key ${key}: ${run.reason}`);
            m._njw_key(key, 0);
            run = pumpFrames(m, 2);
            assert(!run.fatal, `fatal after key ${key} up: ${run.error}`);
            assert(!run.stopped, `game15 stopped after key ${key}: ${run.reason}`);
        }

        m._njw_pointer(0 /* down */, 120, 160);
        pumpFrames(m, 2);
        m._njw_pointer(2 /* move */, 130, 170);
        pumpFrames(m, 2);
        m._njw_pointer(1 /* up */, 130, 170);

        const final = pumpFrames(m, 10);
        assert(!final.fatal, `fatal engine error: ${final.error}`);
        assert(!final.stopped, `game15 stopped unexpectedly: ${final.reason}`);
        assert(final.epochDelta > 0, 'no display updates after input');
        assert((final.lastFlags & (NJW_FRAME_RUNNING | NJW_FRAME_DREW)) ===
            (NJW_FRAME_RUNNING | NJW_FRAME_DREW), `unexpected frame flags ${final.lastFlags}`);
        return `${final.epochDelta} frame epochs during ${keys.length} keys + touch`;
    });

    await t.check('njw_stop of game15 reports the clean exit stop reason', () => {
        njwStop(m);
        assertEqual(m._njw_running(), 0, 'njw_running() after njw_stop');
        const reason = njwStopReason(m);
        assertEqual(reason, 'the MIDlet stopped (exit code 0)', 'njw_stop_reason()');
        return reason;
    });

    // -- save states -------------------------------------------------------
    t.section('save states on game15.jar');

    let savedLen = 0;
    await t.check('save -> load round trip keeps the MIDlet running', () => {
        configureRunner(m, { id: 'g15-save' });
        assertEqual(startJar(m, fsPaths.game15), 0, 'njw_start(game15)');
        let run = pumpFrames(m, 40);
        assert(!run.fatal && !run.stopped, `game15 failed to start: ${describePump(run)}`);
        assertEqual(m._njw_can_save(), 1, 'njw_can_save()');

        const blob = saveState(m);
        savedLen = blob.length;
        // Lead baseline: ~360 KB; keep a generous band so VM changes do not
        // make the check meaningless but still catch empty/truncated blobs.
        assertRange(savedLen, 1000 * 100, 4 * 1024 * 1024, 'save-state size');

        run = pumpFrames(m, 20);
        assert(!run.fatal && !run.stopped, `fatal before load: ${describePump(run)}`);
        assertEqual(loadState(m, blob), 0, 'njw_load_state rc');
        run = pumpFrames(m, 20);
        assert(!run.fatal, `fatal after load: ${run.error}`);
        assert(!run.stopped, `stopped after load: ${run.reason}`);
        assert(run.drew > 0, 'nothing drawn after load');
        assertEqual(run.lastFlags & 3, 3, 'frame flags after load (lead baseline: 3)');
        assertEqual(m._njw_can_save(), 1, 'njw_can_save() after load');
        njwStop(m);
        return `blob ${savedLen} B, load rc 0, flags 3`;
    });

    await t.check('truncated save state is rejected and the VM is unusable', () => {
        configureRunner(m, { id: 'g15-badsave' });
        assertEqual(startJar(m, fsPaths.game15), 0, 'njw_start(game15)');
        const run = pumpFrames(m, 20);
        assert(!run.fatal && !run.stopped, `game15 failed to start: ${describePump(run)}`);
        const blob = saveState(m);
        const truncated = blob.slice(0, Math.floor(blob.length / 2));
        assertEqual(loadState(m, truncated), -1, 'njw_load_state(truncated) rc');
        const error = njwError(m);
        assertNonEmptyString(error, 'njw_error() after rejected state');
        assertEqual(m._njw_frame(FRAME_MS) < 0, true, 'njw_frame after rejected state should be fatal');
        assertEqual(m._njw_running(), 0, 'njw_running() after rejected state');
        njwStop(m);
        return error;
    });

    await t.check('VM recovers after a failed load (stop -> start works)', () => {
        configureRunner(m, { id: 'g15-recover' });
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello) after failed load');
        const run = pumpFrames(m, 60);
        assert(!run.fatal, `fatal engine error: ${run.error}`);
        assert(!run.stopped, `stopped: ${run.reason}`);
        assertEqual(run.drew, 60, 'hello should draw every frame after recovery');
        njwStop(m);
        return 'restart after fatal state succeeded';
    });

    // -- audio -------------------------------------------------------------
    t.section('audio: audiodemo.jar');

    await t.check('audiodemo queues 8000 Hz mono frames within 300 frames', () => {
        configureRunner(m, { id: 'audio' });
        assertEqual(startJar(m, fsPaths.audiodemo), 0, 'njw_start(audiodemo)');
        let frames = 0;
        let avail = 0;
        let stopped = false;
        let stopReason = '';
        let fatal = '';
        while (frames < 300) {
            const flags = m._njw_frame(FRAME_MS);
            frames++;
            if (flags < 0) {
                fatal = njwError(m);
                break;
            }
            avail = m._njw_audio_available();
            if (avail > 0) break;
            if (!(flags & NJW_FRAME_RUNNING)) {
                stopped = true;
                stopReason = njwStopReason(m);
                break;
            }
        }
        assert(fatal === '', `fatal engine error: ${fatal}`);
        assert(avail > 0, `no audio queued within 300 frames (stopped=${stopped}: ${stopReason})`);
        const rate = m._njw_audio_rate();
        const channels = m._njw_audio_channels();
        assertEqual(rate, 8000, 'audio rate');
        assertEqual(channels, 1, 'audio channels');

        const samples = avail * channels;
        const buf = m._njw_alloc(samples * 2);
        assert(buf > 0, `njw_alloc(${samples * 2}) failed`);
        try {
            const got = m._njw_audio_read(buf, avail);
            assert(got > 0, `njw_audio_read returned ${got} frames`);
            assert(got <= avail, `njw_audio_read returned ${got} > available ${avail}`);
            const pcm = new Int16Array(m.HEAPU8.buffer, buf, got * channels);
            let nonZero = 0;
            for (let i = 0; i < pcm.length; i++) if (pcm[i] !== 0) nonZero++;
            assert(nonZero > 0, 'audio buffer contains only silence');
            m._njw_audio_note_playing(got);
            return `${got} frames @ ${rate} Hz x${channels} after ${frames} emu frame(s), ${nonZero}/${pcm.length} non-zero samples`;
        } finally {
            m._njw_free(buf);
            njwStop(m);
        }
    });

    // -- heap stability ----------------------------------------------------
    t.section('memory and restart stability');

    await t.check('HEAPU8 length stable across two restart cycles', () => {
        const lengths = [];
        for (let cycle = 0; cycle < 2; cycle++) {
            configureRunner(m, { id: `heap-${cycle}` });
            assertEqual(startJar(m, fsPaths.hello), 0, `njw_start(hello) cycle ${cycle}`);
            const run = pumpFrames(m, 30);
            assert(!run.fatal && !run.stopped, `cycle ${cycle} failed: ${describePump(run)}`);
            njwStop(m);
            assertEqual(m._njw_running(), 0, `njw_running() after cycle ${cycle}`);
            lengths.push(m.HEAPU8.length);
        }
        assertEqual(lengths[0], lengths[1], 'HEAPU8 length cycle 1 vs cycle 2');
        const growth = lengths[1] - heapInitial;
        assert(growth <= 16 * 1024 * 1024, `HEAPU8 grew by ${growth} bytes across restarts`);
        assertRange(lengths[1], 16 * 1024 * 1024, 1024 * 1024 * 1024, 'HEAPU8 size after restarts');
        return `${(heapInitial / 1048576).toFixed(0)} MiB -> ${(lengths[1] / 1048576).toFixed(0)} MiB (growth ${growth} B)`;
    });

    await t.check('single module instance keeps working after many JAR switches', () => {
        configureRunner(m, { id: 'final-hello' });
        assertEqual(startJar(m, fsPaths.hello), 0, 'njw_start(hello)');
        const run = pumpFrames(m, 60);
        assert(!run.fatal && !run.stopped, `final run failed: ${describePump(run)}`);
        assertEqual(run.drew, 60, 'hello should draw every frame');
        const stats = framebufferStats(m);
        assertEqual(stats.nonBlack, PIXELS, 'baseline pixels changed after reuse');
        njwStop(m);
        return 'module reuse OK';
    });

    // -- evidence summary ---------------------------------------------------
    const failed = t.finish();
    try {
        fs.writeFileSync(path.join(evidenceDir, 'web_api_summary.txt'),
            `${t.passed}/${t.passed + t.failed} checks passed, ${t.failed} failed\n` +
            (t.failures.length ? t.failures.map((f) => `FAIL ${f}`).join('\n') + '\n' : '') +
            `hello PNG: ${helloPngPath}\n`);
        if (m && m.njTestLogs.length) {
            fs.writeFileSync(path.join(evidenceDir, 'web_api_engine.log'), m.njTestLogs.join('\n') + '\n');
        }
    } catch (err) {
        console.error(`warning: could not write evidence summary: ${err.message}`);
    }
    return failed;
}

main().then((code) => {
    process.exitCode = code;
}).catch((err) => {
    console.error(`web_api_test: FATAL: ${err && err.stack ? err.stack : err}`);
    process.exitCode = 1;
});
