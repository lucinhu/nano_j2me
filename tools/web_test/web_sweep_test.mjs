#!/usr/bin/env node
/*
 * Full-corpus audit for the WASM web port.
 *
 * The pinned smoke suite (web_corpus_test.mjs) checks a hand-picked set of 42
 * titles.  This suite goes wider: every JAR under tests/games/ is started in
 * its own Node process, pumped for a few seconds of emulated time, and
 * classified from what came out:
 *
 *   ok        ran, drew, and the framebuffer has non-black pixels
 *   black     ran and drew, but no sampled frame had a single non-black pixel.
 *             Reported as a WARNING only: which frames a title shows at an
 *             exact frame count depends on how fast the host is, so a black
 *             last frame is not a stable regression signal.
 *   stopped   the MIDlet exited by itself (notifyDestroyed) without drawing
 *   nodraw    frames ticked but nothing was ever presented
 *   fatal     njw_frame() reported a fatal error
 *   start     njw_start() refused the title (bad JAR, heap, constructor loop)
 *   probe     the manifest could not be read at all
 *   install   the JAR could not be written into the Emscripten filesystem
 *   load      the WASM module did not load in the child process
 *   timeout   the child did not finish inside --timeout seconds
 *
 * Each title gets its own process on purpose: a single hanging or extremely
 * slow title must not take the whole audit down with it (the first version of
 * this audit reused one module and got stuck on title 25 of 403).
 *
 * Failures are only the non-ok stages that are not known parity.  The baseline
 * below is native-parity knowledge, not a claim that those titles are broken in
 * the port:
 *
 *   tests/games/1945空战英雄_6105.jar          excluded: no usable MIDlet (native fails too)
 *   tests/games/愤怒的小鸟_383.jar             fatal:    Java heap (needs ~64 MiB, native OOMs too)
 *   tests/games/毁灭战士3_10598.jar            start:    runs its game loop from the constructor
 *   tests/games/鲁宾逊漂流记汉化BT版_4677.jar  probe:    no MIDlet-1 (native refuses as well)
 *
 * Black titles (native parity title screens, fade-outs) only warn.  To catch a
 * mass-rendering regression the audit additionally fails when fewer than
 * --min-ok titles run and draw (default 380 of the 403).
 *
 * Usage:
 *   node tools/web_test/web_sweep_test.mjs [options]
 *
 *   --evidence DIR   evidence directory (default .local-tools/debug-runs/web-sweep-<stamp>)
 *   --frames N       frames per title (default 90, 30..2000)
 *   --timeout S      seconds per title (default 25, 5..300)
 *   --jobs N         titles in parallel (default 1, 1..8)
 *   --min-ok N       fail below this many ok titles (default 380)
 *   --limit N        only the first N titles (quick sanity run)
 *   --one JAR        internal: run a single title and print one JSON line
 *   -h, --help       this text
 *
 * Exit codes: 0 every title matched the baseline, 1 unexpected regression,
 * 2 usage error or missing WASM artifacts.  Evidence is written under
 * <evidence>/sweep-<titles>.jsonl and <evidence>/sweep-summary.txt.
 */
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    repoRoot,
    moduleJsPath,
    moduleWasmPath,
    FRAME_MS,
    resolveEvidenceDir,
    stampNow,
    loadModule,
    configureRunner,
    installJar,
    probeJar,
    startJar,
    pumpFrames,
    framebufferStats,
    njwError,
    njwStopReason,
    njwStop,
} from './web_common.mjs';

const GAMES_DIR = path.join(repoRoot, 'tests', 'games');
const DEFAULT_FRAMES = 90;
const DEFAULT_TIMEOUT_S = 25;
const DEFAULT_JOBS = 1;
const DEFAULT_MIN_OK = 380;

/** Titles that are not usable MIDlet suites regardless of the port. */
const EXCLUDED = new Set([
    '1945空战英雄_6105.jar',
]);

/** Baseline stages from the 2026-11-15 audit; native parity, not regressions. */
const BASELINE = new Map([
    ['愤怒的小鸟_383.jar', 'fatal'],
    ['毁灭战士3_10598.jar', 'start'],
    ['鲁宾逊漂流记汉化BT版_4677.jar', 'probe'],
]);

/** Titles that draw all black in the last audited frame, as native does. */
const BLACK_EXPECTED = new Set([
    '宠物王国6(去模拟器限制)_12473.jar',
    '纯爱物语3之梦想之翼_5112.jar',
    '都市赛车6火线追击_2230.jar',
    '灌篮高手-爆发!樱木_7947.jar',
    '开心农场BT版_6075.jar',
    '生化危机之恶化_8448.jar',
    'PC正版-使命召唤5_7199.jar',
]);
for (const name of BLACK_EXPECTED) BASELINE.set(name, 'black');

const STAGES = ['ok', 'black', 'stopped', 'nodraw', 'fatal', 'start', 'probe', 'install', 'load', 'timeout'];

// ---------------------------------------------------------------------------
// Small argument parser (the suite has its own flags, not the shared spec)
// ---------------------------------------------------------------------------

function failUsage(message) {
    console.error(`web_sweep_test: ${message}`);
    console.error("run 'node tools/web_test/web_sweep_test.mjs --help' for usage");
    process.exit(2);
}

function printUsage() {
    const text = fs.readFileSync(new URL(import.meta.url), 'utf8');
    const start = text.indexOf('/*');
    const end = text.indexOf('*/');
    process.stdout.write(text.slice(start + 2, end).replace(/^/gm, '') + '\n');
}

function parsePositiveInt(what, raw, min, max) {
    if (!/^\d+$/.test(raw)) failUsage(`${what} must be a positive integer, got '${raw}'`);
    const value = Number(raw);
    if (value < min || value > max) failUsage(`${what} must be between ${min} and ${max}, got ${value}`);
    return value;
}

function parseCli(argv) {
    const opts = {
        evidence: '',
        frames: DEFAULT_FRAMES,
        timeoutS: DEFAULT_TIMEOUT_S,
        jobs: DEFAULT_JOBS,
        minOk: DEFAULT_MIN_OK,
        limit: 0,
        one: '',
    };
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        const value = () => {
            if (i + 1 >= argv.length) failUsage(`${arg} needs a value`);
            return argv[++i];
        };
        switch (arg) {
        case '--evidence': opts.evidence = value(); break;
        case '--frames': opts.frames = parsePositiveInt('--frames', value(), 30, 2000); break;
        case '--timeout': opts.timeoutS = parsePositiveInt('--timeout', value(), 5, 300); break;
        case '--jobs': opts.jobs = parsePositiveInt('--jobs', value(), 1, 8); break;
        case '--min-ok': opts.minOk = parsePositiveInt('--min-ok', value(), 1, 100000); break;
        case '--limit': opts.limit = parsePositiveInt('--limit', value(), 1, 100000); break;
        case '--one': opts.one = value(); break;
        case '-h': case '--help': printUsage(); process.exit(0); break;
        default:
            if (arg.startsWith('--evidence=')) opts.evidence = arg.slice('--evidence='.length);
            else if (arg.startsWith('--frames=')) opts.frames = parsePositiveInt('--frames', arg.slice('--frames='.length), 30, 2000);
            else if (arg.startsWith('--timeout=')) opts.timeoutS = parsePositiveInt('--timeout', arg.slice('--timeout='.length), 5, 300);
            else if (arg.startsWith('--jobs=')) opts.jobs = parsePositiveInt('--jobs', arg.slice('--jobs='.length), 1, 8);
            else if (arg.startsWith('--min-ok=')) opts.minOk = parsePositiveInt('--min-ok', arg.slice('--min-ok='.length), 1, 100000);
            else if (arg.startsWith('--limit=')) opts.limit = parsePositiveInt('--limit', arg.slice('--limit='.length), 1, 100000);
            else failUsage(`unknown argument: ${arg}`);
        }
    }
    return opts;
}

// ---------------------------------------------------------------------------
// Single-title mode: run one JAR in this process and print one JSON line.
// ---------------------------------------------------------------------------

/**
 * A title counts as black only when no sampled frame during the whole run had
 * a non-black pixel.  Sampling the last frame alone was unstable: several
 * titles (fade-outs, splash-to-menu transitions) are black at exactly frame 90
 * depending on wall-clock phase, which made the audit non-deterministic.  The
 * baseline therefore lists only titles whose first 90 frames are all black.
 */
function stageOf(pump, sawContent) {
    if (pump.fatal) return 'fatal';
    if (pump.drew === 0) return pump.stopped ? 'stopped' : 'nodraw';
    if (!sawContent) return 'black';
    return 'ok';
}

async function runOne(jarPath, frames) {
    const startedAt = Date.now();
    const record = {
        name: path.basename(jarPath),
        size: 0,
        stage: 'load',
        title: '',
        detail: '',
        frames: 0,
        drew: 0,
        stopped: false,
        reason: '',
        nonBlack: 0,
        colors: 0,
        ms: 0,
    };
    let m = null;
    try {
        record.size = fs.statSync(jarPath).size;
        m = await loadModule();
        const id = path.basename(jarPath, '.jar').replace(/[^\w\u4e00-\u9fff.-]+/g, '-').slice(0, 40) || 'game';
        configureRunner(m, { id });
        record.stage = 'install';
        const fsPath = installJar(m, jarPath);

        const probeRc = probeJar(m, fsPath);
        if (probeRc === 0) {
            record.title = m.UTF8ToString(m._njw_app_title());
        } else {
            record.stage = 'probe';
            record.detail = njwError(m);
            record.ms = Date.now() - startedAt;
            return record;
        }

        record.stage = 'start';
        if (startJar(m, fsPath) !== 0) {
            record.stage = 'start';
            record.detail = njwError(m);
            record.ms = Date.now() - startedAt;
            return record;
        }

        let sawContent = false;
        const pump = pumpFrames(m, frames, {
            dt: FRAME_MS,
            onFrame: (frame) => {
                if (sawContent || frame % 6 !== 0) { return; }
                if (framebufferStats(m).nonBlack > 0) { sawContent = true; }
            },
        });
        const last = framebufferStats(m);
        if (last.nonBlack > 0) { sawContent = true; }
        record.frames = pump.frames;
        record.drew = pump.drew;
        record.stopped = pump.stopped;
        record.reason = pump.stopped ? pump.reason : (pump.fatal ? pump.error : '');
        record.stage = stageOf(pump, sawContent);
        record.nonBlack = last.nonBlack;
        record.colors = last.distinctColors;
        return record;
    } catch (err) {
        record.stage = 'load';
        record.detail = String(err && err.message ? err.message : err);
        return record;
    } finally {
        if (m) {
            try {
                const stats = framebufferStats(m);
                record.nonBlack = stats.nonBlack;
                record.colors = stats.distinctColors;
            } catch (_) { /* stats are best effort */ }
            try { njwStop(m); } catch (_) { /* stop is best effort */ }
        }
        record.ms = Date.now() - startedAt;
        process.stdout.write(JSON.stringify(record) + '\n');
    }
}

// ---------------------------------------------------------------------------
// Audit mode: one child process per title, aggregated here.
// ---------------------------------------------------------------------------

function listTitles(limit) {
    const jars = fs.readdirSync(GAMES_DIR)
        .filter((name) => name.toLowerCase().endsWith('.jar'))
        .filter((name) => !EXCLUDED.has(name))
        .sort((a, b) => a.localeCompare(b, 'zh-Hans-CN'));
    return limit > 0 ? jars.slice(0, limit) : jars;
}

/** Run one title in a child process; resolves to the parsed record. */
function runTitleChild(jarPath, opts) {
    return new Promise((resolve) => {
        const self = fileURLToPath(import.meta.url);
        const child = spawn(process.execPath, [
            self, '--one', jarPath, '--frames', String(opts.frames),
        ], {
            cwd: repoRoot,
            env: process.env,
            stdio: ['ignore', 'pipe', 'pipe'],
        });
        let stdout = '';
        let stderr = '';
        let timedOut = false;
        const timer = setTimeout(() => {
            timedOut = true;
            child.kill('SIGKILL');
        }, opts.timeoutS * 1000);
        child.stdout.on('data', (chunk) => { stdout += chunk; });
        child.stderr.on('data', (chunk) => {
            stderr += chunk;
            if (stderr.length > 4000) stderr = stderr.slice(-4000);
        });
        child.on('close', () => {
            clearTimeout(timer);
            const name = path.basename(jarPath);
            if (!timedOut) {
                const line = stdout.trim().split('\n').filter(Boolean).pop();
                if (line) {
                    try {
                        const record = JSON.parse(line);
                        record.name = record.name || name;
                        resolve(record);
                        return;
                    } catch (_) { /* fall through to the diagnostic record */ }
                }
            }
            if (timedOut) {
                resolve({ name, size: 0, stage: 'timeout', title: '', detail: `no result after ${opts.timeoutS}s`, frames: 0, drew: 0, stopped: false, reason: '', nonBlack: 0, colors: 0, ms: opts.timeoutS * 1000 });
                return;
            }
            resolve({ name, size: 0, stage: 'load', title: '', detail: (stderr.trim() || `child exited without a result (code ${child.exitCode})`).split('\n').pop(), frames: 0, drew: 0, stopped: false, reason: '', nonBlack: 0, colors: 0, ms: 0 });
        });
    });
}

function classify(record) {
    const expected = BASELINE.get(record.name);
    if (expected === record.stage) return 'expected';
    if (expected && record.stage === 'ok') return 'improved';
    if (!expected && record.stage === 'ok') return 'expected';
    if (record.stage === 'black') return 'warning';
    return 'unexpected';
}

function formatRecord(record, index, total) {
    const label = String(index + 1).padStart(String(total).length);
    return `[${label}/${total}] ${record.stage.padEnd(8)} ${record.name} (${record.ms} ms)${record.title ? '  — ' + record.title : ''}`;
}

async function runAudit(opts) {
    const titles = listTitles(opts.limit);
    if (titles.length === 0) failUsage(`no JAR files under ${GAMES_DIR}`);
    console.log(`== web sweep audit: ${titles.length} title(s), ${opts.frames} frames each, ` +
        `${opts.timeoutS} s timeout, ${opts.jobs} job(s)`);

    const records = new Array(titles.length);
    let next = 0;
    const worker = async () => {
        for (;;) {
            const index = next++;
            if (index >= titles.length) return;
            const jarPath = path.join(GAMES_DIR, titles[index]);
            const record = await runTitleChild(jarPath, opts);
            records[index] = record;
            console.log(formatRecord(record, index, titles.length));
        }
    };
    const startedAt = Date.now();
    await Promise.all(Array.from({ length: Math.min(opts.jobs, titles.length) }, worker));
    const elapsedS = ((Date.now() - startedAt) / 1000).toFixed(1);

    // ---- summary -----------------------------------------------------------
    const counts = Object.fromEntries(STAGES.map((stage) => [stage, 0]));
    const unexpected = [];
    const warnings = [];
    const improved = [];
    for (const record of records) {
        counts[record.stage] = (counts[record.stage] || 0) + 1;
        const kind = classify(record);
        if (kind === 'unexpected') unexpected.push(record);
        else if (kind === 'warning') warnings.push(record);
        else if (kind === 'improved') improved.push(record);
    }
    const minOk = opts.limit ? Math.min(opts.minOk, titles.length) : opts.minOk;
    if (counts.ok < minOk) {
        unexpected.push({
            name: '(ok count below the expected floor)',
            stage: 'ok',
            detail: `${counts.ok} ok titles < the expected floor ${minOk}`,
        });
    }
    const slowest = [...records].sort((a, b) => b.ms - a.ms).slice(0, 5);

    const evidenceDir = resolveEvidenceDir(opts.evidence ||
        path.join(repoRoot, '.local-tools', 'debug-runs', `web-sweep-${stampNow()}`));
    fs.mkdirSync(evidenceDir, { recursive: true });
    const jsonlPath = path.join(evidenceDir, `sweep-${titles.length}.jsonl`);
    const summaryPath = path.join(evidenceDir, 'sweep-summary.txt');
    fs.writeFileSync(jsonlPath, records.map((r) => JSON.stringify(r)).join('\n') + '\n');

    const summary = [];
    summary.push(`web sweep audit: ${titles.length} titles × ${opts.frames} frames`);
    summary.push(`frames: base ${path.relative(repoRoot, moduleJsPath)} / wasm ${path.relative(repoRoot, moduleWasmPath)}`);
    summary.push(`elapsed: ${elapsedS}s (jobs=${opts.jobs}, timeout=${opts.timeoutS}s)`);
    summary.push('');
    summary.push('stages: ' + STAGES.map((stage) => `${stage}=${counts[stage] || 0}`).join(' '));
    summary.push('');
    if (unexpected.length) {
        summary.push(`UNEXPECTED (${unexpected.length}):`);
        for (const r of unexpected) summary.push(`  ${r.stage.padEnd(8)} ${r.name}: ${r.detail || r.reason || '(no detail)'}`);
        summary.push('');
    }
    if (warnings.length) {
        summary.push(`warnings (${warnings.length}) — no non-black frame was sampled;`);
        summary.push('timing-sensitive, not a regression signal:');
        for (const r of warnings) summary.push(`  ${r.name}`);
        summary.push('');
    }
    if (improved.length) {
        summary.push(`improved over the baseline (${improved.length}):`);
        for (const r of improved) summary.push(`  ${r.name} now renders`);
        summary.push('');
    }
    summary.push('slowest:');
    for (const r of slowest) summary.push(`  ${r.ms} ms  ${r.stage.padEnd(8)} ${r.name}`);
    summary.push('');
    summary.push(unexpected.length ? 'RESULT: FAIL' : 'RESULT: PASS');
    fs.writeFileSync(summaryPath, summary.join('\n') + '\n');

    console.log('');
    for (const line of summary) console.log(line);
    const shown = evidenceDir.startsWith(repoRoot) ? path.relative(repoRoot, evidenceDir) : evidenceDir;
    console.log(`evidence: ${shown}/`);

    if (unexpected.length) {
        console.error(`web_sweep_test: ${unexpected.length} title(s) regressed against the baseline`);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

async function main() {
    const opts = parseCli(process.argv.slice(2));
    if (opts.one) {
        if (!fs.existsSync(opts.one)) failUsage(`no such JAR: ${opts.one}`);
        await runOne(path.resolve(opts.one), opts.frames);
        return 0;
    }
    if (!fs.existsSync(moduleJsPath) || !fs.existsSync(moduleWasmPath)) {
        console.error('web_sweep_test: wasm module not built (expected ' +
            path.relative(repoRoot, moduleJsPath) + ' and nj2me.wasm)');
        console.error('  build it with tools/build_web.sh (or run tools/check_web.sh --build --sweep)');
        return 2;
    }
    return runAudit(opts);
}

main().then((code) => {
    process.exitCode = code;
}).catch((err) => {
    console.error('web_sweep_test: ' + (err && err.stack ? err.stack : err));
    process.exitCode = 2;
});
