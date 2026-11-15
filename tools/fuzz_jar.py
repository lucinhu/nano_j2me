#!/usr/bin/env python3
"""Fuzz the JAR container itself (not just class bytes) through the CLI.

A malformed JAR must come back as a handled loader error - not a crash and not
a hang.  The class fuzzer (tools/fuzz_classfile.py) exercises the class parser;
this one exercises the ZIP reader, the manifest parser and the resource loader.

Usage:
    python3 tools/fuzz_jar.py [BIN [CASES [MUTATIONS [SEED]]]]

    BIN        nj2me CLI (default ./build/nj2me)
    CASES      number of mutated JARs to run (default 60)
    MUTATIONS  mutations applied per case (default 1)
    SEED       deterministic seed (default 1)

Mutation kinds:
    byte       flip one byte at a random offset
    truncate   cut the file at a random offset
    append     append random bytes (bad offset tables)
    localhdr   corrupt a local file header signature
    cd         corrupt the central directory signature
    eocd       corrupt the end-of-central-directory signature
    manifest   corrupt bytes inside META-INF/MANIFEST.MF
    resource   corrupt bytes inside a resource entry's data

The tool first runs the unmutated JAR to derive a per-case budget (mutated
cases get 40x that, at least two seconds); a case that exceeds it is a HANG.
Exit status: 0 clean, 1 crash/hang found, 2 setup error.
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))

BIN = sys.argv[1] if len(sys.argv) > 1 else './build/nj2me'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 60
N_MUT = int(sys.argv[3]) if len(sys.argv) > 3 else 1
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 1


def pick_source():
    for name in ('gamedemo.jar', 'keysdemo.jar'):
        path = os.path.join(ROOT, 'tests', 'fixtures', name)
        if os.path.exists(path):
            return path
    sys.stderr.write('no fixture JAR found under tests/fixtures\n')
    sys.exit(2)


SRC = pick_source()
BASE = open(SRC, 'rb').read()


def region_of(name):
    """Return (start, end) of an entry's data and the local/cd records."""
    with zipfile.ZipFile(SRC) as z:
        info = z.getinfo(name)
        with open(SRC, 'rb') as f:
            f.seek(info.header_offset)
            head = f.read(30)
            name_len = struct.unpack('<H', head[26:28])[0]
            local_start = info.header_offset
            data_start = info.header_offset + 30 + name_len + \
                struct.unpack('<H', head[28:30])[0]
            data_end = data_start + info.compress_size
        return local_start, data_start, data_end


def mutate_on(buf, kind, rnd):
    m = bytearray(buf)
    if kind == 'byte' or kind == 'rnd':
        m[rnd.randrange(len(m))] = rnd.randrange(256)
    elif kind == 'truncate':
        m = m[:max(1, rnd.randrange(len(m)))]
    elif kind == 'append':
        m = m + bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 64)))
    elif kind == 'localhdr':
        # first local header signature
        off = m.find(b'PK\x03\x04')
        if off < 0:
            raise ValueError('no local header')
        m[off:off + 4] = b'PK\x03\x05'
    elif kind == 'cd':
        off = m.rfind(b'PK\x01\x02')
        if off < 0:
            raise ValueError('no central directory')
        m[off:off + 4] = b'PK\x01\x03'
    elif kind == 'eocd':
        off = m.rfind(b'PK\x05\x06')
        if off < 0:
            raise ValueError('no end of central directory')
        m[off:off + 4] = b'PK\x05\x07'
    elif kind == 'manifest':
        with zipfile.ZipFile(SRC) as z:
            info = z.getinfo('META-INF/MANIFEST.MF')
        _ls, ds, de = region_of('META-INF/MANIFEST.MF')
        if ds >= de:
            raise ValueError('empty manifest')
        m[ds + rnd.randrange(de - ds)] = rnd.randrange(256)
    elif kind == 'resource':
        with zipfile.ZipFile(SRC) as z:
            names = [n for n in z.namelist()
                     if not n.endswith('/') and not n.endswith('.class')]
        if not names:
            raise ValueError('no resource entries')
        _ls, ds, de = region_of(rnd.choice(names))
        if ds >= de:
            raise ValueError('empty resource')
        m[ds + rnd.randrange(de - ds)] = rnd.randrange(256)
    else:
        raise ValueError('unknown kind')
    return bytes(m)


def mutate_multi(buf, kinds, rnd):
    for kind in kinds:
        try:
            buf = mutate_on(buf, kind, rnd)
        except (ValueError, KeyError, IndexError, struct.error):
            # structural target moved/truncated by an earlier mutation: keep
            # the current (still malformed) buffer
            if buf and len(buf) > 1:
                buf = buf[:-1]
    return buf


KINDS = ['byte', 'truncate', 'append', 'localhdr', 'cd', 'eocd',
         'manifest', 'resource']


def keep_failure(case_jar, label):
    """Save a crashing/hanging case so it can be reproduced by hand."""
    out_dir = os.environ.get('NJ_FUZZ_KEEP')
    if not out_dir:
        return None
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, label + '.jar')
    shutil.copy(case_jar, dst)
    return dst


def run(jar, timeout):
    cmd = [BIN, '--port', 'headless', '--size', '240x320', '--heap', '16',
           '--run-frames', '20', '--stats', jar]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'HUNG', timeout, b''
    dt = time.time() - t0
    if r.returncode < 0:
        return 'CRASH', dt, r.stderr[-400:]
    return 'clean', dt, r.stderr[-400:]


def main():
    tmp = tempfile.mkdtemp(prefix='fzjar-')
    try:
        case_jar = os.path.join(tmp, 'case.jar')
        shutil.copy(SRC, case_jar)
        status, dt, _ = run(case_jar, 10.0)
        if status != 'clean':
            sys.stderr.write('control run is not clean: %s\n' % status)
            return 2
        budget = max(2.0, dt * 40.0)
        print('fuzzing %s (%d bytes), %d cases, %d mutations/case, seed %d'
              % (os.path.basename(SRC), len(BASE), N, N_MUT, SEED))
        print('control: unmutated fixture took %.2fs, so a case is a hang after '
              '%.1fs' % (dt, budget))
        rnd = random.Random(SEED)
        crashed = hung = 0
        for i in range(N):
            kinds = [KINDS[i % len(KINDS)] if i < len(KINDS)
                     else rnd.choice(KINDS)]
            while len(kinds) < N_MUT:
                kinds.append(rnd.choice(KINDS))
            data = (mutate_multi(BASE, kinds, rnd) if N_MUT > 1
                    else mutate_on(BASE, kinds[0], rnd))
            with open(case_jar, 'wb') as f:
                f.write(data)
            status, dt, err = run(case_jar, budget)
            if status == 'CRASH':
                crashed += 1
                saved = keep_failure(case_jar, 'jar-crash-%d' % i)
                print('\nCRASH case %d kinds=%s rc=%s%s\n%s'
                      % (i, kinds, status,
                         (' saved=' + saved) if saved else '',
                         err.decode('utf-8', 'replace')))
            elif status == 'HUNG':
                hung += 1
                saved = keep_failure(case_jar, 'jar-hung-%d' % i)
                print('\nHUNG case %d kinds=%s (%.1fs)%s'
                      % (i, kinds, dt, (' saved=' + saved) if saved else ''))
            else:
                sys.stdout.write('.')
                sys.stdout.flush()
        print('\nclean: %d   crashed: %d   HUNG (>%.1fs): %d'
              % (N - crashed - hung, crashed, budget, hung))
        return 0 if (crashed == 0 and hung == 0) else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
