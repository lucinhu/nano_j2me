"""Mutate a MIDlet's own class file and see whether the runtime reports an error,
crashes, or HANGS.

A hang is as much a defect as a crash: a malformed JAR that spins forever is a
denial of service, and the project's rule is a loud error, not a stuck process.

WHAT THIS TOOL GOT WRONG, twice, and both are worth keeping.

First it reported "300 clean, 0 crashes" while testing nothing: the JAR it
built contained only the mutated class, so the MIDlet itself was missing and
every case failed identically before reaching the mutation.

Then it reported "18 of 36 mutations HANG" as the project's most serious
defect class.  It had run without --run-frames, and nj_midp_run loops until the
MIDlet exits - so a game that never calls notifyDestroyed() runs forever BY
DESIGN.  The control settled it: the UNMUTATED hello.jar "hangs" the same way.

A third, quieter one: a fixed 6 s budget per case.  Run under `ctest -j` it
failed twice in a row while passing alone in 10 s, because a busy machine
makes any absolute timeout a coin flip.  There is now a control run first and
the budget is derived from it.

A fourth: the 'methodcount' and 'fieldcount' kinds used to write at fixed
offsets 6 and 4 - which are the class-file version numbers, not the counts.
They reported coverage the tool did not have.  The counts and the structural
kinds below now come from an actual walk of the constant pool and the method
attributes, so 'methodcount', 'fieldcount', 'exc_handler' and 'super_self'
mutate what they claim to mutate.

WHAT THE MUTATIONS EXERCISE (all fixed on 2026-10-27; see
docs/notes/2026-10-27-malformed-class-hangs.md and
tests/test_class_hardening.c): an exception table entry whose handler resumes
past the end of the code array used to livelock the unwind loop; getfield /
putfield / invokespecial receivers were dereferenced without checking they are
object references; and a class naming itself as its superclass or
super-interface recursed the linker until the C stack died.  All of these now
raise a Java exception.  The tool is a ctest gate: `crashed` or `HUNG` is a
failure.
"""
import os, random, struct, subprocess, sys, tempfile, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

def pick_source():
    """A JAR to mutate, preferring ones the repository can always build.

    tests/games/ is gitignored and must stay that way, so a fresh checkout has
    no hello.jar in it; tests/fixtures/ holds the same kind of thing and is
    produced by the build.
    """
    cands = [os.path.join(HERE,'..','tests','fixtures',n) for n in
             ('keysdemo.jar','gamedemo.jar','blockdemo.jar')]
    cands.append(os.path.join(HERE,'..','tests','games','hello.jar'))
    for c in cands:
        if os.path.exists(c):
            return c
    sys.stderr.write('fuzz_classfile: no MIDlet fixture to mutate; skipping\n')
    sys.exit(0)

SRC = pick_source()
BIN = sys.argv[1] if len(sys.argv) > 1 else './build/nj2me'
N   = int(sys.argv[2]) if len(sys.argv) > 2 else 60
PER = 6
random.seed(20261012)

with zipfile.ZipFile(SRC) as z:
    entries = [(n, z.read(n)) for n in z.namelist() if not n.endswith('/')]
d = dict(entries)
mf = d['META-INF/MANIFEST.MF'].decode('latin-1')
line = [l for l in mf.splitlines() if l.startswith('MIDlet-1:')][0]
cls_name = line.split(',')[-1].strip().replace('.', '/') + '.class'
base = d[cls_name]
print('mutating', cls_name, len(base), 'bytes,', N, 'cases')

#
# Class-file layout walker.  The structural mutation kinds need real offsets,
# not guesses: the constant pool has variable-length entries, so every count,
# the class header and the Code attributes have to be found by walking the file
# the same way the loader does.
#
def class_layout(m):
    try:
        if m[:4] != b'\xca\xfe\xba\xbe':
            return None
        cp_count = struct.unpack_from('>H', m, 8)[0]
        if cp_count == 0:
            return None
        off = 10
        i = 1
        utf8 = {}
        while i < cp_count:
            tag = m[off]
            off += 1
            if tag == 1:
                ln = struct.unpack_from('>H', m, off)[0]
                off += 2
                utf8[i] = m[off:off + ln]
                off += ln
            elif tag in (3, 4):
                off += 4
            elif tag in (5, 6):      # long/double take two constant slots
                off += 8
                i += 1
            elif tag in (7, 8, 16, 19, 20):
                off += 2
            elif tag in (9, 10, 11, 12, 17, 18):
                off += 4
            elif tag == 15:
                off += 3
            else:
                return None
            i += 1
        cp_end = off
        this_off = cp_end + 2
        super_off = cp_end + 4
        ifc_count_off = cp_end + 6
        ifc_count = struct.unpack_from('>H', m, ifc_count_off)[0]
        p = ifc_count_off + 2 + 2 * ifc_count

        def skip_attrs(pos, count):
            for _ in range(count):
                ln = struct.unpack_from('>I', m, pos + 2)[0]
                pos += 6 + ln
            return pos

        field_count_off = p
        field_count = struct.unpack_from('>H', m, p)[0]
        p += 2
        for _ in range(field_count):
            acount = struct.unpack_from('>H', m, p + 6)[0]
            p = skip_attrs(p + 8, acount)

        method_count_off = p
        method_count = struct.unpack_from('>H', m, p)[0]
        p += 2
        exceptions = []          # (code_len, [(start_off, handler_off)])
        for _ in range(method_count):
            acount = struct.unpack_from('>H', m, p + 6)[0]
            p += 8
            for _ in range(acount):
                aname_i = struct.unpack_from('>H', m, p)[0]
                alen = struct.unpack_from('>I', m, p + 2)[0]
                body = p + 6
                if utf8.get(aname_i) == b'Code' and body + 12 <= len(m):
                    code_len = struct.unpack_from('>I', m, body + 4)[0]
                    ex_count = struct.unpack_from('>H', m, body + 8 + code_len)[0]
                    ex_base = body + 10 + code_len
                    ent = []
                    for k in range(ex_count):
                        s = ex_base + 8 * k
                        ent.append((s, s + 4))       # start_pc, handler_pc
                    exceptions.append((code_len, ent))
                p = body + alen
        return {
            'this_idx': struct.unpack_from('>H', m, this_off)[0],
            'super_off': super_off,
            'field_count_off': field_count_off,
            'method_count_off': method_count_off,
            'exceptions': exceptions,
        }
    except (struct.error, IndexError):
        return None

LAYOUT = class_layout(base)

def build(over):
    p = tempfile.mktemp(suffix='.jar')
    with zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, data in entries:
            z.writestr(n, over.get(n, data))
    return p

# Keep the per-kind first pass (i < len(KINDS) gives every kind one case) but
# only offer the structural kinds when the fixture could actually be walked.
KINDS = ['truncate','byte','cpcount','methodcount','cp_tag','attrlen','u2',
         'fieldcount','tail']
if LAYOUT is not None:
    KINDS.insert(3, 'super_self')
    if any(ent for (_cl, ent) in LAYOUT['exceptions']):
        KINDS.insert(4, 'exc_handler')

def mutate(kind):
    m = bytearray(base)
    if kind == 'truncate':
        return bytes(m[:random.randint(1, len(m)-1)])
    if kind == 'byte':
        m[random.randrange(len(m))] = random.randrange(256)
    elif kind == 'cpcount':
        struct.pack_into('>H', m, 8, random.choice([0,1,2,0xFFFF,0xFFFE]))
    elif kind == 'methodcount':
        struct.pack_into('>H', m, LAYOUT['method_count_off'],
                         random.choice([0,1,2,0xFFFF,0x7FFF]))
    elif kind == 'fieldcount':
        struct.pack_into('>H', m, LAYOUT['field_count_off'],
                         random.choice([0,1,0xFFFF,0x7FFF]))
    elif kind == 'super_self':
        # A class that names itself as its superclass: the linker used to
        # recurse into itself until the C stack ran out (SIGSEGV).
        struct.pack_into('>H', m, LAYOUT['super_off'], LAYOUT['this_idx'])
    elif kind == 'exc_handler':
        code_len, ent = random.choice([e for e in LAYOUT['exceptions'] if e[1]])
        start_off, handler_off = random.choice(ent)
        what = random.randrange(4)
        if what == 0:
            # Resume one past the last bytecode: the unwind livelock.
            struct.pack_into('>H', m, handler_off, code_len)
        elif what == 1:
            # Empty handler range: can never be entered, never empty in a
            # valid class.
            struct.pack_into('>H', m, start_off, code_len)
            struct.pack_into('>H', m, start_off + 2, code_len)
        else:
            # Handler somewhere outside the method entirely.
            struct.pack_into('>H', m, handler_off,
                             random.choice([0xFFFF, code_len + 1, code_len + 7]))
    elif kind == 'cp_tag':
        struct.pack_into('>B', m, 10, random.choice([0,2,17,18,19,20,99,255]))
    elif kind == 'attrlen':
        struct.pack_into('>I', m, random.randrange(10, max(11,len(m)-4)),
                         random.choice([0xFFFFFFFF,0x7FFFFFFF,0]))
    elif kind == 'u2':
        struct.pack_into('>H', m, random.randrange(max(1,len(m)-2)),
                         random.choice([0,0xFFFF]))
    else:
        m[len(m)-1] = random.randrange(256)
    return bytes(m)

#
# THE CONTROL COMES FIRST.  An absolute per-case timeout is a claim about this
# machine's speed, and this test runs under `ctest -j` alongside a corpus scan
# that loads every core.  Time the same command on the SAME unmutated fixture,
# in the same conditions, and let that decide what "too slow" means.
#
# This is not a nicety: with a flat 6 s budget the test passed alone in 10 s
# and failed twice in a row under load, which is a gate nobody can trust.
t0 = time.monotonic()
try:
    subprocess.run([BIN,'--port','headless','--heap','8','--run-frames','30',SRC],
                   capture_output=True, timeout=PER*10)
except subprocess.TimeoutExpired:
    sys.stderr.write('fuzz_classfile: the UNMUTATED fixture did not finish in '
                     '%ds; this machine is too loaded to judge anything\n' % (PER*10))
    sys.exit(0)
BASE = max(time.monotonic() - t0, 0.05)
BUDGET = max(PER, BASE * 20.0)
print('control: unmutated fixture took %.2fs, so a case is a hang after %.1fs'
      % (BASE, BUDGET))

hangs, crashed, ok = [], [], 0
for i in range(N):
    kind = KINDS[i % len(KINDS)] if i < len(KINDS) else random.choice(KINDS)
    jar = build({cls_name: mutate(kind)})
    try:
        # --run-frames is ESSENTIAL and its absence produced a completely wrong
        # result the first time: nj_midp_run loops until the MIDlet exits, so a
        # MIDlet that does not call notifyDestroyed() runs forever BY DESIGN.
        # Without this flag, unmutated hello.jar "hangs" too, and the tool
        # reported 18 of 36 mutations as hangs when most were ordinary games.
        r = subprocess.run([BIN,'--port','headless','--heap','8',
                            '--run-frames','30',jar],
                           capture_output=True, timeout=BUDGET)
        out = (r.stdout + r.stderr).decode('utf8','replace')
        if r.returncode < 0:
            crashed.append((i, kind, -r.returncode, out[-120:]))
        else:
            ok += 1
    except subprocess.TimeoutExpired:
        hangs.append((i, kind))
    finally:
        os.unlink(jar)
    sys.stdout.write('.' if not hangs or hangs[-1][0] != i else 'H')
    sys.stdout.flush()

print()
print('clean: %d   crashed: %d   HUNG (>%.1fs): %d' % (ok, len(crashed), BUDGET, len(hangs)))
for h in hangs[:12]:
    print('  HANG  case %d kind=%s' % h)
for c in crashed[:8]:
    print('  CRASH case %d kind=%s signal=%s %s' % c)

# A malformed class must produce a clean error: not a signal, not a hang.
sys.exit(1 if (crashed or hangs) else 0)
