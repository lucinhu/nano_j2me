#!/usr/bin/env node
/*
 * nano_j2me - tools/gen_web_compat_data.mjs
 *
 * Turns a `tools/check_web.sh --sweep` JSONL report into the small report the
 * browser shell ships as `web/data/compat.json`.  The library uses it to badge
 * known-tricky titles ("verified", "first 90 frames are black", "needs a bigger
 * heap", "constructor never returns"), which tells players why a title behaves
 * a certain way instead of leaving them guessing.
 *
 * The report is committed, so a normal `tools/build_web.sh` run needs no
 * network and no corpus; regenerate it after an audit:
 *
 *   tools/check_web.sh --sweep --evidence .local-tools/debug-runs/web-audit
 *   node tools/gen_web_compat_data.mjs --from .local-tools/debug-runs/web-audit/sweep-403.jsonl
 *
 * usage: node tools/gen_web_compat_data.mjs [--from FILE] [--out FILE] [--frames N]
 * Exit status: 0 on success, 2 on usage or missing input.
 */

import fs from 'node:fs';
import path from 'node:path';

const REPO_ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');

const NOTES = {
    ok: '',
    black: '前 90 帧没有非黑像素；原生在同一位置也是全黑（采样相位），不是移植差异',
    fatal: 'Java 堆不足：在工具面板把「游戏内存」调大后重试',
    start: 'MIDlet 构造器不返回（把游戏主循环放在构造器里），浏览器版无法运行',
    probe: '不是可识别的 MIDlet JAR（缺 MIDlet-1 或 JAR 损坏）',
    timeout: '加载超时',
    nodraw: '90 帧内没有出画',
    install: 'JAR 写入内存文件系统失败',
    load: 'JAR 读取失败',
    stopped: 'MIDlet 自行退出',
};

function usage() {
    process.stdout.write(
        'usage: node tools/gen_web_compat_data.mjs [--from FILE] [--out FILE] [--frames N]\n' +
        '  --from FILE   sweep JSONL report (default: newest .local-tools/debug-runs/*/sweep-*.jsonl)\n' +
        '  --out FILE    output path (default: web/data/compat.json)\n' +
        '  --frames N    audit frames per title, recorded in the report (default: 90)\n');
}

function parseArgs(argv) {
    const opts = { from: '', out: path.join(REPO_ROOT, 'web', 'data', 'compat.json'), frames: 90 };
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '-h' || arg === '--help') { opts.help = true; }
        else if (arg === '--from' && i + 1 < argv.length) { opts.from = argv[++i]; }
        else if (arg.startsWith('--from=')) { opts.from = arg.slice('--from='.length); }
        else if (arg === '--out' && i + 1 < argv.length) { opts.out = argv[++i]; }
        else if (arg.startsWith('--out=')) { opts.out = arg.slice('--out='.length); }
        else if (arg === '--frames' && i + 1 < argv.length) { opts.frames = Number(argv[++i]); }
        else if (arg.startsWith('--frames=')) { opts.frames = Number(arg.slice('--frames='.length)); }
        else { throw new Error('unknown argument: ' + arg); }
    }
    if (!Number.isInteger(opts.frames) || opts.frames < 30 || opts.frames > 2000) {
        throw new Error('--frames must be an integer in 30..2000');
    }
    return opts;
}

function newestSweepReport() {
    const root = path.join(REPO_ROOT, '.local-tools', 'debug-runs');
    if (!fs.existsSync(root)) { return ''; }
    let best = '';
    let bestMs = 0;
    for (const dir of fs.readdirSync(root, { withFileTypes: true })) {
        if (!dir.isDirectory()) { continue; }
        const candidate = path.join(root, dir.name);
        for (const name of fs.readdirSync(candidate)) {
            if (!/^sweep-.*\.jsonl$/.test(name)) { continue; }
            const file = path.join(candidate, name);
            const ms = fs.statSync(file).mtimeMs;
            if (ms > bestMs) { bestMs = ms; best = file; }
        }
    }
    return best;
}

function main() {
    let opts;
    try { opts = parseArgs(process.argv.slice(2)); } catch (error) {
        process.stderr.write(error.message + '\n');
        usage();
        return 2;
    }
    if (opts.help) { usage(); return 0; }
    const source = opts.from ? path.resolve(REPO_ROOT, opts.from) : newestSweepReport();
    if (!source || !fs.existsSync(source)) {
        process.stderr.write('no sweep report found; pass --from FILE\n');
        return 2;
    }

    const entries = {};
    const stages = {};
    let titles = 0;
    for (const line of fs.readFileSync(source, 'utf8').split('\n')) {
        if (!line.trim()) { continue; }
        let row;
        try { row = JSON.parse(line); } catch (error) { continue; }
        const name = typeof row.name === 'string' ? row.name : '';
        const size = Number(row.size) || 0;
        const stage = typeof row.stage === 'string' ? row.stage : 'unknown';
        if (!name || !size) { continue; }
        titles++;
        stages[stage] = (stages[stage] || 0) + 1;
        const entry = { stage };
        if (stage !== 'ok') {
            entry.title = typeof row.title === 'string' ? row.title : '';
            entry.note = NOTES[stage] || (typeof row.detail === 'string' ? row.detail : '');
        }
        entries[name + '|' + size] = entry;
    }
    if (!titles) {
        process.stderr.write('no titles parsed from ' + source + '\n');
        return 2;
    }

    const report = {
        schema: 1,
        generatedAt: new Date().toISOString(),
        source: path.relative(REPO_ROOT, source),
        frames: opts.frames,
        titles,
        stages,
        entries,
    };
    fs.mkdirSync(path.dirname(opts.out), { recursive: true });
    fs.writeFileSync(opts.out, JSON.stringify(report) + '\n');
    process.stdout.write(
        'wrote ' + path.relative(REPO_ROOT, opts.out) + ': ' + titles + ' titles audited, ' +
        Object.keys(entries).length + ' entries (' +
        Object.entries(stages).map(([stage, n]) => stage + '=' + n).join(', ') + ')\n');
    return 0;
}

process.exitCode = main();
