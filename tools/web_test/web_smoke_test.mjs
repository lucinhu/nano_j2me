#!/usr/bin/env node
/*
 * nano_j2me - web_test/web_smoke_test.mjs
 *
 * Minimal smoke test for a freshly built Emscripten module: load it, check the
 * runtime handles and the version identity, exercise the filesystem and the
 * option API, and - when the private test corpus is available - render
 * hello.jar for 60 frames.  It is what a CI job runs after tools/build_web.sh,
 * so a broken export list, a stale module or a wasm instantiation failure is
 * caught without needing the (git-ignored) tests/games/ corpus.
 *
 * usage: node tools/web_test/web_smoke_test.mjs [--evidence DIR]
 * Exit status: 0 when the smoke checks pass, 1 otherwise.
 */

import fs from 'node:fs';
import path from 'node:path';

import {
    TestRun,
    assert,
    assertEqual,
    configureRunner,
    dumpFramebufferPng,
    framebufferStats,
    FRAME_MS,
    installJar,
    loadModule,
    moduleJsPath,
    moduleWasmPath,
    njwStop,
    parseArgs,
    pumpFrames,
    readCString,
    repoRoot,
    requireModuleBuilt,
    resolveEvidenceDir,
    startJar,
} from './web_common.mjs';

async function main() {
    const args = parseArgs(process.argv.slice(2));
    if (args.help) {
        process.stdout.write(
            'usage: node tools/web_test/web_smoke_test.mjs [--evidence DIR]\n' +
            '  Minimal post-build smoke test of the wasm module (no corpus needed).\n');
        return 0;
    }
    const evidenceDir = resolveEvidenceDir(args.evidence);
    const t = new TestRun('web_smoke');
    let m = null;

    await t.check('wasm build artifacts present (js + wasm)', () => {
        requireModuleBuilt();
        return `nj2me.js ${fs.statSync(moduleJsPath).size} B, ` +
            `nj2me.wasm ${fs.statSync(moduleWasmPath).size} B`;
    });

    await t.check('compatibility report is present and well formed', () => {
        const reportPath = path.join(repoRoot, 'build', 'web', 'data', 'compat.json');
        assert(fs.existsSync(reportPath),
            `missing ${path.relative(repoRoot, reportPath)}; run tools/build_web.sh`);
        const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
        assert(report.schema === 1, `unexpected compat report schema: ${report.schema}`);
        assert(report.entries && typeof report.entries === 'object',
            'compat report has no entries object');
        assert(report.stages && typeof report.stages === 'object',
            'compat report has no stage counts');
        const badged = Object.keys(report.entries).length;
        assert(badged === Number(report.titles || 0),
            `every audited title should have an entry (${badged}/${report.titles})`);
        const warns = badged - Number((report.stages || {}).ok || 0);
        return `${report.titles} titles audited (${warns} flagged)`;
    });

    await t.check('ES module instantiates with the JS<->C exports', async () => {
        m = await loadModule();
        assert(Boolean(m.HEAPU8 && m.FS), 'Emscripten runtime handles are missing');
        const needed = ['_njw_api_version', '_njw_runtime_version', '_njw_start', '_njw_frame',
            '_njw_stop', '_njw_framebuffer', '_njw_audio_read', '_njw_save_state'];
        for (const name of needed) assert(typeof m[name] === 'function', `export missing: ${name}`);
        return `heap ${(m.HEAPU8.length / 1048576).toFixed(0)} MiB`;
    });

    if (m === null) {
        t.recordFail('smoke runnable', 'cannot continue without the wasm module');
        return t.finish();
    }

    await t.check('runtime identity is readable', () => {
        const api = m._njw_api_version();
        const version = readCString(m, m._njw_runtime_version());
        assertEqual(api, 1, 'njw_api_version()');
        assert(/^\d+\.\d+\.\d+$/.test(version), `runtime version looks wrong: ${JSON.stringify(version)}`);
        return `API ${api}, runtime ${version}`;
    });

    await t.check('option API and MEMFS work', () => {
        m._njw_options_reset();
        assertEqual(m._njw_option_int(1, 240), 0, 'SCREEN_WIDTH option accepted');
        const probe = '/tmp/nj2me-smoke.txt';
        m.FS.writeFile(probe, new TextEncoder().encode('nano_j2me smoke'));
        assertEqual(new TextDecoder().decode(m.FS.readFile(probe)), 'nano_j2me smoke',
            'MEMFS write/read round-trip');
        m.FS.unlink(probe);
        return 'options + MEMFS OK';
    });

    const helloJar = path.join(repoRoot, 'tests/games/hello.jar');
    if (fs.existsSync(helloJar)) {
        await t.check('hello.jar renders 60 frames', () => {
            configureRunner(m, { id: 'smoke-hello' });
            const rc = startJar(m, installJar(m, helloJar));
            assertEqual(rc, 0, 'njw_start(hello)');
            assertEqual(m._njw_running(), 1, 'engine running');
            const run = pumpFrames(m, 60);
            assertEqual(run.drew, 60, 'drew frames');
            const stats = framebufferStats(m);
            assertEqual(stats.width, 240, 'framebuffer width');
            assertEqual(stats.height, 320, 'framebuffer height');
            assertEqual(stats.nonBlack, stats.total, 'every rendered pixel is non-black');
            dumpFramebufferPng(m, path.join(evidenceDir, 'smoke-hello.png'));
            njwStop(m);
            assertEqual(m._njw_running(), 0, 'engine stopped');
            return `${stats.nonBlack} non-black pixels, dt=${FRAME_MS.toFixed(3)} ms`;
        });
    } else {
        t.skip('hello.jar renders 60 frames', 'private test corpus not present (tests/games is git-ignored)');
    }

    await t.check('smoke evidence written', () => {
        fs.writeFileSync(path.join(evidenceDir, 'web-smoke-summary.txt'),
            `checked at ${new Date().toISOString()}\n` +
            `smoke artifact: ${moduleJsPath}\n`);
        return `${evidenceDir}/web-smoke-summary.txt`;
    });

    return t.finish();
}

process.exitCode = await main();
