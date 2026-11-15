#!/usr/bin/env python3
"""Fuzz the media decoders through the CLI (AMR, WAV/ADPCM and MIDI fixtures).

A damaged media resource inside an otherwise valid JAR must surface as a Java
exception or silence - not a process crash and not a hang.  The class and JAR
fuzzers (tools/fuzz_classfile.py, tools/fuzz_jar.py) stop at the container; this
one points at the decoders the runtime actually feeds with untrusted bytes.

Usage:
    python3 tools/fuzz_media.py [BIN [CASES [MUTATIONS [SEED]]]]

    BIN        nj2me CLI (default ./build/nj2me)
    CASES      mutated JARs to run (default 60)
    MUTATIONS  mutations per case (default 1)
    SEED       deterministic seed (default 1)

Fixtures: tests/fixtures/audiodemo.jar ships res/tone.amr, res/beep.wav and
res/adpcm.wav; tests/fixtures/mididemo.jar ships song.mid.  One target entry is
mutated per case; the rest of the JAR is rebuilt unchanged, so the ZIP
container itself stays valid and only the decoder sees the damage.

A case is CRASH when the process dies from a signal and HUNG when it exceeds the
budget (unmutated fixture time x 40, at least two seconds).  A clean exit -
including a Java exception that the fixture catches - counts as handled.
Exit status: 0 clean, 1 crash/hang found, 2 setup error.
"""
import os
import random
import shutil
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

TARGETS = [
    ('audiodemo.jar', 'res/tone.amr'),
    ('audiodemo.jar', 'res/beep.wav'),
    ('audiodemo.jar', 'res/adpcm.wav'),
    ('mididemo.jar', 'song.mid'),
]

# Frames to run: the audio fixture exercises playback, the MIDI fixture needs
# a moment before it opens the player.
FRAMES = {'audiodemo.jar': 240, 'mididemo.jar': 240}


def pick():
    found = []
    for jar, entry in TARGETS:
        path = os.path.join(ROOT, 'tests', 'fixtures', jar)
        if not os.path.exists(path):
            continue
        with zipfile.ZipFile(path) as z:
            if entry in z.namelist():
                found.append((path, jar, entry, z.read(entry)))
    if not found:
        sys.stderr.write('no media fixtures found under tests/fixtures\n')
        sys.exit(2)
    return found


FIXTURES = pick()


def mutate_on(data, kind, rnd):
    m = bytearray(data)
    if kind == 'byte':
        m[rnd.randrange(len(m))] ^= 1 << rnd.randrange(8)
    elif kind == 'run':
        off = rnd.randrange(max(1, len(m) - 4))
        for i in range(min(4, len(m) - off)):
            m[off + i] = rnd.randrange(256)
    elif kind == 'truncate':
        m = m[:max(1, rnd.randrange(len(m)))]
    elif kind == 'append':
        m = m + bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 32)))
    else:
        raise ValueError('unknown kind')
    return bytes(m)


def mutate_multi(data, kinds, rnd):
    for kind in kinds:
        try:
            data = mutate_on(data, kind, rnd)
        except (ValueError, IndexError):
            if len(data) > 1:
                data = data[:-1]
    return data


KINDS = ['byte', 'run', 'truncate', 'append']


def build_case(tmp, jar_path, entry, replacement):
    """Copy the fixture, replacing one entry through a rebuilt zip."""
    out = os.path.join(tmp, 'case.jar')
    with zipfile.ZipFile(jar_path) as src, \
            zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as dst:
        for name in src.namelist():
            data = replacement if name == entry else src.read(name)
            dst.writestr(name, data)
    return out


def run(jar, frames, timeout):
    cmd = [BIN, '--port', 'headless', '--size', '240x320', '--heap', '16',
           '--fps', '0', '--run-frames', str(frames), '--stats', jar]
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
    tmp = tempfile.mkdtemp(prefix='fzmedia-')
    try:
        budgets = {}
        for _path, jar, _entry, data in FIXTURES:
            control = build_case(tmp, _path, _entry, data)
            status, dt, _ = run(control, FRAMES[jar], 20.0)
            if status != 'clean':
                sys.stderr.write('control run for %s is not clean: %s\n'
                                 % (jar, status))
                return 2
            budgets[jar] = max(2.0, dt * 40.0)
        print('fuzzing media decoders from %d fixtures, %d cases, '
              '%d mutations/case, seed %d'
              % (len(FIXTURES), N, N_MUT, SEED))
        for jar, budget in budgets.items():
            print('  %s: control budget %.1fs, %d frames' % (jar, budget, FRAMES[jar]))
        rnd = random.Random(SEED)
        crashed = hung = 0
        for i in range(N):
            path, jar, entry, data = FIXTURES[i % len(FIXTURES)]
            kinds = [KINDS[i % len(KINDS)] if i < len(KINDS)
                     else rnd.choice(KINDS)]
            while len(kinds) < N_MUT:
                kinds.append(rnd.choice(KINDS))
            mutated = (mutate_multi(data, kinds, rnd) if N_MUT > 1
                       else mutate_on(data, kinds[0], rnd))
            case = build_case(tmp, path, entry, mutated)
            status, dt, err = run(case, FRAMES[jar], budgets[jar])
            if status == 'CRASH':
                crashed += 1
                print('\nCRASH case %d %s:%s kinds=%s\n%s'
                      % (i, jar, entry, kinds,
                         err.decode('utf-8', 'replace')))
            elif status == 'HUNG':
                hung += 1
                print('\nHUNG case %d %s:%s kinds=%s (%.1fs)'
                      % (i, jar, entry, kinds, dt))
            else:
                sys.stdout.write('.')
                sys.stdout.flush()
        print('\nclean: %d   crashed: %d   HUNG: %d' %
              (N - crashed - hung, crashed, hung))
        return 0 if (crashed == 0 and hung == 0) else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
