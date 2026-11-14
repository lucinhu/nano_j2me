#!/usr/bin/env node
/*
 * nano_j2me - web_test/web_corpus_test.mjs
 *
 * Corpus smoke test for the Emscripten build: probes, starts and runs a
 * deterministic set of JARs through build/web/js/nj2me.js under Node with a
 * fixed 1000/60 ms timestep.  This is the WASM port's equivalent of the
 * native/headless "40-title batch" smoke test.
 *
 * The corpus is a pinned list (not a directory scan) so the test is
 * deterministic across machines and locales:
 *   - the first 40 titles of tests/games/ in the repository's zh_CN pinyin
 *     order, minus 1945空战英雄_6105.jar, which also fails in the native
 *     build and is therefore not a supported title;
 *   - plus tests/fixtures/audiodemo.jar (audio path) and
 *     tests/fixtures/bench.jar (notifyDestroyed path).
 * 41 entries total.  Update this list deliberately when the corpus changes.
 *
 * Per title the test asserts:
 *   - the JAR is staged into MEMFS at its real size
 *   - njw_probe() succeeds and reports a non-empty MIDlet name/class and a
 *     self-consistent icon (pointer/length inside HEAPU8)
 *   - njw_start() succeeds, the MIDlet draws at least once and the engine
 *     reports no fatal error for up to DEFAULT_FRAMES frames
 *   - a MIDlet that calls notifyDestroyed() reports a non-empty stop reason
 *   - the framebuffer is 240x320 after the run (geometry never changes)
 *   - njw_stop() returns the engine to the stopped state
 *
 * Evidence: <evidence>/corpus-summary.txt (one line per title, including
 * framebuffer stats and wall time), <evidence>/corpus-engine.log (captured
 * engine stderr/stdout) and <evidence>/corpus/NN-<name>.png (last presented
 * frame).  Default evidence directory: .local-tools/debug-runs/web-verify-<stamp>.
 *
 * usage: node tools/web_test/web_corpus_test.mjs [--evidence DIR] [--frames N]
 *        (--frames defaults to 120; minimum 30, because the render assertions
 *         need the first frames to settle)
 * Exit status: 0 when every title passes, 1 otherwise.
 */

import fs from 'node:fs';
import path from 'node:path';

import {
    TestRun,
    assert,
    assertEqual,
    assertNonEmptyString,
    assertRange,
    configureRunner,
    describePump,
    dumpFramebufferPng,
    framebufferStats,
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
    sanitizeName,
    startJar,
} from './web_common.mjs';

// Pinned corpus, in the order of the native baseline's `ls tests/games/*.jar
// | head -40` run on a zh_CN locale (pinyin collation), minus the known-bad
// 1945空战英雄_6105.jar.  Two fixtures append the audio/exit paths.
const CORPUS = Object.freeze([
    'tests/games/1942空战_10403.jar',
    'tests/games/3D版田园物语_6744.jar',
    'tests/games/3D城市桌球_8299.jar',
    'tests/games/3D疯狂过山车S60v5版_3875.jar',
    'tests/games/3D孤胆枪手2010_4634.jar',
    'tests/games/3D滚球_11586.jar',
    'tests/games/3D激爆拳击_8805.jar',
    'tests/games/3D科林麦克雷拉力赛之尘埃_6600.jar',
    'tests/games/3D拉力赛-进化汉化版_6366.jar',
    'tests/games/3D摩托赛车_9287.jar',
    'tests/games/3D台球之王_5014.jar',
    'tests/games/3D坦克对战1944_7139.jar',
    'tests/games/3D跳台滑雪2011_4507.jar',
    'tests/games/3D武装直升机2_archive.jar',
    'tests/games/阿尔卑斯牧场物语_7043.jar',
    'tests/games/暗黑启示录2－黑暗王朝_888.jar',
    'tests/games/傲气雄鹰_10432.jar',
    'tests/games/傲世三国志－群雄争霸_5633.jar',
    'tests/games/傲视三国之赵云传_11024.jar',
    'tests/games/奥特曼-宇宙英雄超银河传说_2233.jar',
    'tests/games/百鬼商店破解版_10526.jar',
    'tests/games/百战天虫2010中文版_5380.jar',
    'tests/games/孢子生命起源_6515.jar',
    'tests/games/爆笑三国斗地主_10552.jar',
    'tests/games/蹦球传说_7801.jar',
    'tests/games/蹦球传说美化版_2959.jar',
    'tests/games/比基尼美女海底影相_9643.jar',
    'tests/games/冰淇淋少女成长日记_7307.jar',
    'tests/games/波斯王子经典版_9715.jar',
    'tests/games/波斯王子之时之沙中文版_11315.jar',
    'tests/games/波斯王子之武者之心_8424.jar',
    'tests/games/捕鱼达人3金装版_1511.jar',
    'tests/games/彩虹岛汉化版_10241.jar',
    'tests/games/彩京打击者1945_10623.jar',
    'tests/games/苍穹默示录完美运行版_4035.jar',
    'tests/games/超级酒吧女生_4728.jar',
    'tests/games/超级马里奥_10283.jar',
    'tests/games/超级坦克大战2汉化版_531.jar',
    'tests/games/超时空帝国机甲风暴_12065.jar',
    'tests/fixtures/audiodemo.jar',
    'tests/fixtures/bench.jar',
]);

const DEFAULT_FRAMES = 120;
// Below this a title's first frames may still be black/undisplayable, so the
// render assertions would report false failures; keep --frames >= this.
const MIN_FRAMES = 30;
const SCREEN_W = 240;
const SCREEN_H = 320;
const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

/** Best-effort classification of the stored icon resource bytes. */
function iconKind(m, icon) {
    if (!icon.ptr) return 'none';
    const b = m.HEAPU8;
    const png = PNG_SIGNATURE.every((byte, i) => b[icon.ptr + i] === byte);
    if (png) return 'png';
    if (b[icon.ptr] === 0xff && b[icon.ptr + 1] === 0xd8 && b[icon.ptr + 2] === 0xff) return 'jpeg';
    if (String.fromCharCode(b[icon.ptr], b[icon.ptr + 1], b[icon.ptr + 2], b[icon.ptr + 3]) === 'RIFF') {
        return 'riff';
    }
    return 'other';
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

async function runOne(m, index, relPath, frames, evidenceDir, summaryLines) {
    const hostPath = path.join(repoRoot, relPath);
    assert(fs.existsSync(hostPath), `corpus JAR is missing: ${relPath}`);
    const name = path.basename(relPath);
    const tag = `${String(index + 1).padStart(2, '0')}-${sanitizeName(name.replace(/\.jar$/i, ''))}`;

    // Stage into MEMFS under a per-title name so same-named JARs cannot clash.
    const fsPath = installJar(m, hostPath, `${String(index + 1).padStart(2, '0')}-${name}`);
    assertEqual(m.FS.stat(fsPath).size, fs.statSync(hostPath).size, `MEMFS size of ${name}`);

    // Probe.
    assertEqual(probeJar(m, fsPath), 0, `njw_probe rc for ${name}`);
    const title = readCString(m, m._njw_app_title());
    const midletClass = readCString(m, m._njw_midlet_class_at(0));
    assertNonEmptyString(title, `app_title for ${name}`);
    assertNonEmptyString(midletClass, `midlet_class_at(0) for ${name}`);
    assertRange(m._njw_midlet_count(), 1, 64, `midlet_count for ${name}`);
    const icon = readIcon(m);
    if (icon.ptr) {
        assert(icon.len > 0, `app_icon pointer with length 0 for ${name}`);
        assert(icon.ptr + icon.len <= m.HEAPU8.length, `app_icon pointer/len outside HEAPU8 for ${name}`);
    } else {
        assertEqual(icon.len, 0, `app_icon NULL with non-zero length for ${name}`);
    }
    const kind = iconKind(m, icon);

    // Start and run.  The finally block guarantees the VM is stopped even when
    // an assertion fails, so one bad title cannot poison the rest of the run.
    const cfg = configureRunner(m, { id: `corpus-${String(index + 1).padStart(2, '0')}` });
    assertEqual(cfg.rcW, 0, `SCREEN_WIDTH option for ${name}`);
    assertEqual(cfg.rcH, 0, `SCREEN_HEIGHT option for ${name}`);
    assertEqual(cfg.rcRms, 0, `RMS_DIR option for ${name}`);
    assertEqual(cfg.rcFs, 0, `FS_DIR option for ${name}`);
    assertEqual(startJar(m, fsPath), 0, `njw_start rc for ${name}`);
    assertEqual(m._njw_running(), 1, `njw_running() after start for ${name}`);

    try {
        const startedAt = Date.now();
        const run = pumpFrames(m, frames);
        const elapsedMs = Date.now() - startedAt;

        assert(!run.fatal, `fatal engine error for ${name}: ${run.error}`);
        assert(run.frames >= 1, `no frame was pumped for ${name}`);
        if (run.stopped) {
            assertNonEmptyString(run.reason, `stop reason for ${name}`);
        } else {
            assertEqual(m._njw_running(), 1, `njw_running() after ${run.frames} frames for ${name}`);
            assert(run.drew >= 1, `${name} ran ${run.frames} frames without drawing (${describePump(run)})`);
            assertEqual(m._njw_displayable_ready(), 1, `njw_displayable_ready() for ${name}`);
        }

        const stats = framebufferStats(m);
        assertEqual(stats.width, SCREEN_W, `framebuffer width for ${name}`);
        assertEqual(stats.height, SCREEN_H, `framebuffer height for ${name}`);
        assert(stats.ptr > 0, `njw_framebuffer() is NULL for ${name}`);
        assertEqual(stats.total, SCREEN_W * SCREEN_H, `framebuffer pixel count for ${name}`);
        if (!run.stopped) {
            // Every playable title in the pinned corpus paints something within
            // the run (measured at the default 120-frame length).
            assert(stats.nonBlack > 0, `${name} drew only black frames (${describePump(run)})`);
        }

        // Evidence: last presented frame (all titles in the corpus present one).
        const pngDir = path.join(evidenceDir, 'corpus');
        fs.mkdirSync(pngDir, { recursive: true });
        dumpFramebufferPng(m, path.join(pngDir, `${tag}.png`));

        njwStop(m);
        assertEqual(m._njw_running(), 0, `njw_running() after njw_stop for ${name}`);

        const exit = run.stopped ? `exited: ${run.reason}` : 'running';
        const detail = `${relPath} | '${title}' [${midletClass}] icon=${kind}` +
            (kind === 'none' ? '' : `(${icon.len} B)`) +
            ` | frames=${run.frames} drew=${run.drew} ${exit}` +
            ` | fb=${stats.nonBlack}/${stats.total} non-black, colors=${stats.distinctColors}, hash=${stats.hash}` +
            ` | ${elapsedMs} ms`;
        summaryLines.push(detail);
        return detail;
    } finally {
        if (m._njw_running()) njwStop(m);
    }
}

async function main() {
    // parseArgs() spec keys include the leading dashes (see web_common.mjs).
    const args = parseArgs(process.argv.slice(2), { '--frames': 'int' });
    if (args.help) {
        console.log('usage: node tools/web_test/web_corpus_test.mjs [--evidence DIR] [--frames N]');
        return 0;
    }
    const framesArg = args['--frames'];
    const frames = framesArg === null || framesArg === undefined ? DEFAULT_FRAMES : framesArg;
    if (!Number.isInteger(frames) || frames < MIN_FRAMES || frames > 2000) {
        console.error(`--frames must be an integer in ${MIN_FRAMES}..2000 (default ${DEFAULT_FRAMES})`);
        return 2;
    }
    const evidenceDir = resolveEvidenceDir(args.evidence);
    console.log(`evidence directory: ${evidenceDir}`);
    console.log(`corpus: ${CORPUS.length} JARs, ${frames} frames each`);

    const t = new TestRun('web_corpus');
    const summaryLines = [];
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
        t.recordFail('corpus runnable', 'cannot continue without the wasm module');
        return t.finish();
    }

    const heapStart = m.HEAPU8.length;
    t.section(`corpus (${CORPUS.length} titles, ${frames} frames each)`);
    for (let i = 0; i < CORPUS.length; i++) {
        const relPath = CORPUS[i];
        const name = path.basename(relPath);
        await t.check(`run ${name}`, () => runOne(m, i, relPath, frames, evidenceDir, summaryLines));
    }

    const heapEnd = m.HEAPU8.length;
    t.section('summary');

    fs.writeFileSync(path.join(evidenceDir, 'corpus-summary.txt'),
        summaryLines.join('\n') + '\n');
    const engineLog = Array.isArray(m.njTestLogs) ? m.njTestLogs.join('\n') : '';
    fs.writeFileSync(path.join(evidenceDir, 'corpus-engine.log'), engineLog + (engineLog ? '\n' : ''));

    await t.check('corpus evidence written', () => {
        assert(fs.statSync(path.join(evidenceDir, 'corpus-summary.txt')).size > 0, 'empty corpus-summary.txt');
        const pngs = fs.readdirSync(path.join(evidenceDir, 'corpus')).filter((f) => f.endsWith('.png'));
        if (t.failed === 0) {
            assertEqual(pngs.length, CORPUS.length, 'per-title PNG dumps');
        } else {
            assert(pngs.length > 0, 'no per-title PNG dumps were written');
        }
        return `${evidenceDir}/corpus-summary.txt, corpus-engine.log, ${pngs.length} PNGs`;
    });

    await t.check('engine error log is empty', () => {
        const bad = (m.njTestLogs || []).filter((line) => /\[nj2me\] error:/i.test(line));
        assertEqual(bad.length, 0, `engine errors for ${bad.length} title(s): ${bad.slice(0, 3).join(' | ')}`);
        return 'no fatal engine errors during corpus run';
    });

    await t.check('WASM heap stayed within budget during the corpus run', () => {
        const growth = heapEnd - heapStart;
        assert(growth <= 512 * 1024 * 1024, `heap grew by ${(growth / 1048576).toFixed(0)} MiB`);
        assertEqual(m._njw_running(), 0, 'a MIDlet is still running after the corpus');
        const cfg = configureRunner(m, { id: 'post-corpus' });
        assertEqual(cfg.rcW, 0, 'SCREEN_WIDTH option after corpus');
        assertEqual(startJar(m, installJar(m, path.join(repoRoot, 'tests/games/hello.jar'))), 0,
            'njw_start(hello) after the corpus');
        assertEqual(m._njw_running(), 1, 'njw_running() after post-corpus start');
        njwStop(m);
        return `heap ${(heapStart / 1048576).toFixed(0)} -> ${(heapEnd / 1048576).toFixed(0)} MiB, restart OK`;
    });

    return t.finish();
}

process.exitCode = await main();
