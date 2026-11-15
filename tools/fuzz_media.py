#!/usr/bin/env python3
"""Fuzz the media decoders through the CLI (AMR, WAV/ADPCM and MIDI fixtures).

A damaged media resource inside an otherwise valid JAR must surface as a Java
exception or silence - not a process crash and not a hang.  The class and JAR
fuzzers (tools/fuzz_classfile.py, tools/fuzz_jar.py) stop at the container; this
one points at the decoders the runtime actually feeds with untrusted bytes.

Usage:
    python3 tools/fuzz_media.py [BIN [CASES [MUTATIONS [SEED [multi]]]]

    BIN        nj2me CLI (default ./build/nj2me)
    CASES      mutated JARs to run (default 60)
    MUTATIONS  mutations per case (default 1)
    SEED       deterministic seed (default 1)
    multi      damage several DIFFERENT media entries of the same JAR per
               case instead of one (exercises several failing decoders in one
               run; MUTATIONS entries, at least two)

Fixtures: tests/fixtures/audiodemo.jar ships res/tone.amr, res/beep.wav and
res/adpcm.wav; tests/fixtures/mididemo.jar ships song.mid; gamedemo.jar decodes
res/player.png and res/tiles.png through Image.createImage (both the by-name and
the stream overload); imagesdemo.jar decodes a checked-in baseline JPEG and a
GIF through Image.createImage.  One target entry is
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
MULTI = len(sys.argv) > 5 and sys.argv[5] == 'multi'

TARGETS = [
    ('audiodemo.jar', 'res/tone.amr'),
    ('audiodemo.jar', 'res/beep.wav'),
    ('audiodemo.jar', 'res/adpcm.wav'),
    ('mididemo.jar', 'song.mid'),
    ('gamedemo.jar', 'res/player.png'),
    ('gamedemo.jar', 'res/tiles.png'),
    ('imagesdemo.jar', 'res/sample.jpg'),
    ('imagesdemo.jar', 'res/sample.gif'),
]

# Frames to run: the audio fixture exercises playback, the MIDI fixture needs
# a moment before it opens the player.
FRAMES = {'audiodemo.jar': 240, 'mididemo.jar': 240, 'gamedemo.jar': 120,
          'imagesdemo.jar': 60}


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

# The fixtures grouped by JAR, for the multi-target mode (which damages
# several entries of the same archive in one case).
BY_JAR = {}
for _path, _jar, _entry, _data in FIXTURES:
    BY_JAR.setdefault(_jar, []).append((_path, _entry, _data))


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
    return build_case_multi(tmp, jar_path, {entry: replacement})


def build_case_multi(tmp, jar_path, replacements):
    """Copy the fixture, replacing every entry in *replacements*."""
    out = os.path.join(tmp, 'case.jar')
    with zipfile.ZipFile(jar_path) as src, \
            zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as dst:
        for name in src.namelist():
            data = replacements.get(name, src.read(name))
            dst.writestr(name, data)
    return out


def keep_failure(case, jar, label):
    """Save a crashing/hanging case so it can be reproduced by hand."""
    out_dir = os.environ.get('NJ_FUZZ_KEEP')
    if not out_dir:
        return None
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, label + '.jar')
    shutil.copy(case, dst)
    return dst


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
              '%d mutations/case, seed %d, %s'
              % (len(FIXTURES), N, N_MUT, SEED,
                 'multi-target' if MULTI else 'single-target'))
        for jar, budget in budgets.items():
            print('  %s: control budget %.1fs, %d frames' % (jar, budget, FRAMES[jar]))
        rnd = random.Random(SEED)
        crashed = hung = 0
        jars = list(BY_JAR)
        for i in range(N):
            if MULTI and len(jars) > 1:
                jar = jars[i % len(jars)]
                entries = BY_JAR[jar]
                path = entries[0][0]
                want = max(2, N_MUT)
                chosen = rnd.sample(entries, min(want, len(entries)))
                replacements = {}
                kinds = []
                for _p, entry, data in chosen:
                    kind = KINDS[(i + len(kinds)) % len(KINDS)] \
                        if i < len(KINDS) else rnd.choice(KINDS)
                    kinds.append(kind)
                    replacements[entry] = (mutate_multi(data, [kind], rnd)
                                           if N_MUT > 1
                                           else mutate_on(data, kind, rnd))
                case = build_case_multi(tmp, path, replacements)
                label = '%s:%s' % (jar, '+'.join(sorted(replacements)))
            else:
                path, jar, entry, data = FIXTURES[i % len(FIXTURES)]
                kinds = [KINDS[i % len(KINDS)] if i < len(KINDS)
                         else rnd.choice(KINDS)]
                while len(kinds) < N_MUT:
                    kinds.append(rnd.choice(KINDS))
                mutated = (mutate_multi(data, kinds, rnd) if N_MUT > 1
                           else mutate_on(data, kinds[0], rnd))
                case = build_case(tmp, path, entry, mutated)
                label = '%s:%s' % (jar, entry)
            status, dt, err = run(case, FRAMES[jar], budgets[jar])
            if status == 'CRASH':
                crashed += 1
                saved = keep_failure(case, jar, 'media-crash-%d' % i)
                print('\nCRASH case %d %s kinds=%s%s\n%s'
                      % (i, label, kinds,
                         (' saved=' + saved) if saved else '',
                         err.decode('utf-8', 'replace')))
            elif status == 'HUNG':
                hung += 1
                saved = keep_failure(case, jar, 'media-hung-%d' % i)
                print('\nHUNG case %d %s kinds=%s (%.1fs)%s'
                      % (i, label, kinds, dt,
                         (' saved=' + saved) if saved else ''))
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
