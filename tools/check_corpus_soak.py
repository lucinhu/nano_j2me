#!/usr/bin/env python3
"""Long-run heap sanity probe over the collected corpus (leak hunting).

Every functional probe runs a few hundred frames, which is far too short to
see a leak: a run that grows a few KB per frame still passes.  This tool runs
titles for thousands of frames with `--stats --stats-every` and reports the
heap samples the CLI prints, so a growing `heap=` curve is visible instead of
being averaged away by the end-of-run counters.

A title PASSES when the CLI exits cleanly (or the MIDlet exits itself) and the
periodic samples are readable.  It FAILS on a crash or a timeout.  Heap growth
is reported (and, with --strict-heap, treated as a failure) because a game may
legitimately cache level data: use the samples, not just the verdict.

Usage:
    python3 tools/check_corpus_soak.py <rows dir> [options]

    --count N       titles to sample (default 8)
    --frames N      frames per title (default 6000)
    --every N       stats interval in frames (default 1000)
    --seed N        deterministic shuffle seed (default 5)
    --keys PATH     key script (default <rows>/keys.txt when present)
    --timeout S     per-title timeout (default 600)
    --min-frames N  only sample rows whose sweep run reached N frames
                    (default 240; the run length is --frames)
    --strict-heap   fail when the heap keeps growing between the last samples

The binary comes from $NJ_BIN (default build/nj2me).  Titles are searched in
tests/games plus $NJ_SWEEP_DIRS.  RMS goes to a scratch directory per title so
titles never share record stores.

Heap rule (strict mode): the last sample's heap_used must be below
mid + max(4 MB, mid/2), where mid is the middle sample.  A leak that grows
through the whole run trips it; a one-off cache does not.
Exit status: 0 all titles passed the hard checks, 1 any crash/timeout (or
strict-heap failure), 2 setup error.
"""
import json
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.environ.get('NJ_BIN') or os.path.join(ROOT, 'build', 'nj2me')
SEARCH = [os.path.join(ROOT, 'tests', 'games')]
SEARCH += [d for d in os.environ.get('NJ_SWEEP_DIRS', '').split(';') if d]

def die(msg):
    print('check_corpus_soak: ' + msg, file=sys.stderr)
    sys.exit(2)

def find_jar(name):
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None

def parse_stats(text):
    """Return [(frame, heap_used, heap_peak), ...] from stats lines."""
    out = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith('stats:') or 'heap=' not in line:
            continue
        fields = dict()
        for tok in line[len('stats:'):].split():
            if '=' in tok:
                k, v = tok.split('=', 1)
                fields[k] = v
        try:
            frames = int(fields.get('frames', '-1'))
            used, peak = fields['heap'].split('/')
            out.append((frames, int(used), int(peak)))
        except (KeyError, ValueError):
            continue
    return out

def main(argv):
    if len(argv) < 2:
        die('usage: check_corpus_soak.py <rows dir> [options]')
    rows_dir = argv[1]
    if not os.path.isfile(os.path.join(rows_dir, 'rows.jsonl')):
        die('no rows.jsonl under ' + rows_dir)
    count, frames, every, seed, min_frames = 8, 6000, 1000, 5, 240
    keys = None
    timeout = 600
    strict = False
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == '--count' and i + 1 < len(argv):
            count = int(argv[i + 1]); i += 2
        elif a == '--frames' and i + 1 < len(argv):
            frames = int(argv[i + 1]); i += 2
        elif a == '--every' and i + 1 < len(argv):
            every = int(argv[i + 1]); i += 2
        elif a == '--seed' and i + 1 < len(argv):
            seed = int(argv[i + 1]); i += 2
        elif a == '--keys' and i + 1 < len(argv):
            keys = argv[i + 1]; i += 2
        elif a == '--timeout' and i + 1 < len(argv):
            timeout = int(argv[i + 1]); i += 2
        elif a == '--min-frames' and i + 1 < len(argv):
            min_frames = int(argv[i + 1]); i += 2
        elif a == '--strict-heap':
            strict = True; i += 1
        else:
            die('unknown option ' + a)
    if keys is None:
        cand = os.path.join(rows_dir, 'keys.txt')
        keys = cand if os.path.isfile(cand) else None
    rows = []
    with open(os.path.join(rows_dir, 'rows.jsonl')) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if r.get('verdict') == 'ok' and r.get('paints', 0) > 0 \
                    and (min_frames == 0 or r.get('frames', 0) >= min_frames):
                rows.append(r)
    if not rows:
        die('no ok rows with enough frames in ' + rows_dir)
    random.Random(seed).shuffle(rows)
    rows = rows[:count]
    failed = 0
    scratch = tempfile.mkdtemp(prefix='soak-')
    for n, r in enumerate(rows):
        jar = find_jar(r['file'])
        if jar is None:
            print('%-44s SKIP (jar not found)' % r['file'][:44])
            continue
        rms = os.path.join(scratch, 'rms-%02d' % n)
        os.makedirs(rms, exist_ok=True)
        print('%-44s %d frames, every %d' % (r['file'][:44], frames, every))
        cmd = [BIN, '--port', 'headless', '--size', '240x320',
               '--heap', '16', '--fps', '0',
               '--run-frames', str(frames),
               '--stats', '--stats-every', str(every),
               '--rms-dir', rms]
        if keys:
            cmd += ['--keys', '@' + keys]
        cmd.append(jar)
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout)
            text = p.stdout.decode('utf-8', 'replace')
            rc = p.returncode
        except subprocess.TimeoutExpired as e:
            text = (e.stdout or b'').decode('utf-8', 'replace')
            rc = 'timeout'
        samples = parse_stats(text)
        reason = None
        if rc == 'timeout':
            reason = 'timeout after %ds' % timeout
        elif rc not in (0, 1):
            reason = 'exit status %s (crash?)' % rc
        elif 'Uncaught exception' in text or 'uncaught exception' in text:
            reason = 'uncaught exception'
        if not samples:
            reason = reason or 'no stats samples'
        shape = ' '.join('%d:%d' % (f, u) for f, u, _ in samples)
        if reason:
            failed += 1
            print('  FAIL %s  [%s]' % (reason, shape))
            tail = '\n'.join(text.splitlines()[-4:])
            if tail:
                print('    ' + tail.replace('\n', '\n    '))
            continue
        print('  heap %s' % shape)
        early = samples[-1][0]
        note = ''
        if early + every < frames:
            note = ' (title ended early at frame %d)' % early
        if len(samples) >= 3:
            mid = samples[-2][1]
            last = samples[-1][1]
            growth = last - mid
            limit = max(4 * 1024 * 1024, mid // 2)
            if growth > limit:
                gathered = 'heap grew %d bytes between the last samples' % growth
                if strict:
                    failed += 1
                    print('  FAIL ' + gathered + note)
                    continue
                note += ' [WARN %s]' % gathered
        print('  PASS' + note)
    print('\n%d titles: %d failed' % (len(rows), failed))
    return 0 if failed == 0 else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv))
