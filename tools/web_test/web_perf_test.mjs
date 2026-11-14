#!/usr/bin/env node
/*
 * nano_j2me - web_test/web_perf_test.mjs
 *
 * Performance smoke test for the Emscripten build: starts a pinned set of
 * light/medium titles, warms them up and measures how many frames the wasm
 * interpreter can pump per second under Node.  This is the wasm counterpart of
 * the native `--fps 0` throughput numbers in the docs.
 *
 * The floors are deliberately conservative (a fraction of what the reference
 * machine measures): the point is to catch catastrophic regressions - a
 * per-frame allocation, a lost incremental present, a broken scheduler fast
 * path - on unknown CI hardware, not to benchmark the host.  The measured
 * numbers go into <evidence>/perf-summary.{txt,json} so trend comparisons stay
 * possible without failing the build.
 *
 * Every frame is pumped with a fixed NJW dt (1000/60 ms), so the numbers are
 * emulation throughput, not display rate: a 60 Hz game needs 60 pumps/s, which
 * leaves a wide margin even on slow machines.
 *
 * usage: node tools/web_test/web_perf_test.mjs [--evidence DIR]
 *            [--frames N] [--warmup N]
 *        --frames  frames per title (default 600, 100..20000)
 *        --warmup  unmeasured warm-up frames (default 60, 0..2000)
 * Exit status: 0 when every title stays above its floor and the heap is
 * stable, 1 otherwise.
 */

import fs from 'node:fs';
import path from 'node:path';

import {
    FRAME_MS,
    TestRun,
    assert,
    assertEqual,
    assertRange,
    configureRunner,
    installJar,
    loadModule,
    moduleJsPath,
    moduleWasmPath,
    njwError,
    njwStop,
    parseArgs,
    pumpFrames,
    readCString,
    repoRoot,
    requireModuleBuilt,
    resolveEvidenceDir,
    startJar,
} from './web_common.mjs';

/* Conservative floors: roughly a tenth of the reference-machine numbers
 * (game15 ~6.1k fps, bubblet ~3.8k, slimevolley ~7.9k, 1942 ~7.9k).  These
 * still fail an order-of-magnitude regression while tolerating a very slow
 * or virtualized CI host. */
const TITLES = Object.freeze([
    { rel: 'tests/games/game15.jar', floorFps: 600 },
    { rel: 'tests/games/bubblet.jar', floorFps: 400 },
    { rel: 'tests/games/slimevolley.jar', floorFps: 700 },
    { rel: 'tests/games/1942空战_10403.jar', floorFps: 700 },
]);

const DEFAULT_FRAMES = 600;
const DEFAULT_WARMUP = 60;
const MAX_HEAP_GROWTH = 32 * 1024 * 1024;

const USAGE = `usage: node tools/web_test/web_perf_test.mjs [--evidence DIR]
           [--frames N] [--warmup N]

  --frames N   frames per title, default ${DEFAULT_FRAMES} (100..20000)
  --warmup N   warm-up frames, default ${DEFAULT_WARMUP} (0..2000)
  --evidence   evidence directory (default .local-tools/debug-runs/web-verify-<stamp>)
`;

function nowMs() {
    return Number(process.hrtime.bigint() / 1000n) / 1000;
}

async function measure(m, title, frames, warmup) {
    const absPath = path.join(repoRoot, title.rel);
    assert(fs.existsSync(absPath), `missing test JAR ${title.rel}`);
    configureRunner(m, {
        id: 'perf-' + path.basename(title.rel, '.jar').replace(/[^a-zA-Z0-9_.-]+/g, '_')
    });
    const fsPath = installJar(m, absPath);
    const rc = startJar(m, fsPath);
    assertEqual(rc, 0, `njw_start(${path.basename(title.rel)})`);
    assertEqual(m._njw_running(), 1, 'engine running after start');
    if (warmup > 0) { pumpFrames(m, warmup); }
    const epochBefore = m._njw_frame_epoch();
    const framesBefore = m._njw_frames();
    const wallStart = nowMs();
    const run = pumpFrames(m, frames);
    const wallMs = nowMs() - wallStart;
    assert(!run.fatal, `fatal engine error while measuring: ${njwError(m) || 'unknown'}`);
    assertEqual(run.frames, frames, 'every requested pump ran');
    const advanced = Number(m._njw_frames()) - Number(framesBefore);
    assert(advanced >= frames, `frame counter advanced only ${advanced} for ${frames} pumps`);
    const fps = wallMs > 0 ? (frames * 1000) / wallMs : Infinity;
    const drew = run.drew;
    const epoch = m._njw_frame_epoch() - epochBefore;
    njwStop(m);
    assertEqual(m._njw_running(), 0, 'engine stopped after measurement');
    assert(fps >= title.floorFps,
        `${path.basename(title.rel)}: ${fps.toFixed(0)} fps is below the conservative floor ${title.floorFps} fps`);
    assert(epoch >= 0, 'frame epoch went backwards');
    return { name: path.basename(title.rel), fps, wallMs, frames, drew, epoch };
}

async function main() {
    const args = parseArgs(process.argv.slice(2), { frames: 'int', warmup: 'int' });
    if (args.help) { process.stdout.write(USAGE); return 0; }
    const frames = args.frames ?? DEFAULT_FRAMES;
    const warmup = args.warmup ?? DEFAULT_WARMUP;
    try {
        assertRange(frames, 100, 20000, '--frames');
        assertRange(warmup, 0, 2000, '--warmup');
    } catch (error) {
        process.stderr.write(String(error.message) + '\n' + USAGE);
        return 2;
    }
    const evidenceDir = resolveEvidenceDir(args.evidence);
    console.log(`perf: ${TITLES.length} titles, ${warmup} warm-up + ${frames} measured frames each`);

    const t = new TestRun('web_perf');
    const rows = [];
    let m = null;

    await t.check('wasm build artifacts present (js + wasm)', () => {
        requireModuleBuilt();
        return `nj2me.js ${fs.statSync(moduleJsPath).size} B, nj2me.wasm ${fs.statSync(moduleWasmPath).size} B`;
    });

    await t.check('ES module instantiates', async () => {
        m = await loadModule();
        assert(Boolean(m.HEAPU8 && m.FS), 'Emscripten runtime handles are missing');
        return `initial heap ${(m.HEAPU8.length / 1048576).toFixed(0)} MiB`;
    });

    if (m === null) {
        t.recordFail('perf runnable', 'cannot continue without the wasm module');
        return t.finish();
    }

    const heapStart = m.HEAPU8.length;
    t.section(`throughput (${frames} frames/title, dt=${FRAME_MS.toFixed(3)} ms)`);
    for (const title of TITLES) {
        await t.check(`throughput ${path.basename(title.rel)}`, async () => {
            const row = await measure(m, title, frames, warmup);
            rows.push(row);
            return `${row.fps.toFixed(0)} fps (${row.wallMs.toFixed(1)} ms, drew ${row.drew}/${row.frames})`;
        });
    }

    t.section('summary');
    await t.check('WASM heap did not grow pathologically', () => {
        const growth = m.HEAPU8.length - heapStart;
        assert(growth <= MAX_HEAP_GROWTH, `heap grew ${(growth / 1048576).toFixed(1)} MiB`);
        return `heap ${(heapStart / 1048576).toFixed(0)} -> ${(m.HEAPU8.length / 1048576).toFixed(0)} MiB`;
    });

    await t.check('engine error log is empty', () => {
        const bad = (m.njTestLogs || []).filter((line) => /\[nj2me\] error:/i.test(line));
        assertEqual(bad.length, 0, `engine errors: ${bad.slice(0, 3).join(' | ')}`);
        return 'no engine errors';
    });

    await t.check('performance evidence written', () => {
        assert(rows.length === TITLES.length, `measured ${rows.length}/${TITLES.length} titles`);
        const lines = rows.map((row) => `${row.name}\t${row.frames} frames\t${row.fps.toFixed(0)} fps` +
            `\t${row.wallMs.toFixed(1)} ms\tdrew ${row.drew}`);
        fs.writeFileSync(path.join(evidenceDir, 'perf-summary.txt'), lines.join('\n') + '\n');
        fs.writeFileSync(path.join(evidenceDir, 'perf-summary.json'), JSON.stringify({
            apiVersion: m._njw_api_version(),
            runtimeVersion: readCString(m, m._njw_runtime_version()),
            frames, warmup, dtMs: FRAME_MS, titles: rows
        }, null, 2) + '\n');
        const engineLog = Array.isArray(m.njTestLogs) ? m.njTestLogs.join('\n') : '';
        fs.writeFileSync(path.join(evidenceDir, 'perf-engine.log'), engineLog + (engineLog ? '\n' : ''));
        return `${evidenceDir}/perf-summary.txt, perf-summary.json, perf-engine.log`;
    });

    console.log('\nperformance table:');
    for (const row of rows) {
        console.log(`  ${row.name.padEnd(24)} ${row.fps.toFixed(0).padStart(7)} fps  ${row.wallMs.toFixed(1).padStart(8)} ms`);
    }

    return t.finish();
}

process.exitCode = await main();
