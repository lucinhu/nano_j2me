"""Mutate a MIDlet's own class file and see whether the runtime reports an error,
crashes, or HANGS.

A hang is as much a defect as a crash: a malformed JAR that spins forever is a
denial of service, and the project's rule is a loud error, not a stuck process.
"""
import os, random, struct, subprocess, sys, tempfile, zipfile

#
#     tools/fuzz_classfile.py [binary] [cases]
#
# WHAT IT FOUND, and why it is not a CI gate yet.  Half of all mutations HANG
# the runtime rather than erroring - 18 of 36 - and a malformed JAR that spins
# forever is a denial of service.  One mechanism is fixed (an unbounded operand
# stack let a frame walk into its neighbour's slots and dereference class bytes
# as a pointer, obj=0x4e4a484800010000, i.e. the ASCII "NJHH"); others remain
# and are recorded in docs/COMPATIBILITY.md.
#
# Until they are fixed this stays a tool rather than a test: a gate that hangs
# is worse than no gate.
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '..', 'tests', 'games', 'hello.jar')
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

def build(over):
    p = tempfile.mktemp(suffix='.jar')
    with zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, data in entries:
            z.writestr(n, over.get(n, data))
    return p

KINDS = ['truncate','byte','cpcount','methodcount','cp_tag','attrlen','u2',
         'fieldcount','tail']

def mutate(kind):
    m = bytearray(base)
    if kind == 'truncate':
        return bytes(m[:random.randint(1, len(m)-1)])
    if kind == 'byte':
        m[random.randrange(len(m))] = random.randrange(256)
    elif kind == 'cpcount':
        struct.pack_into('>H', m, 8, random.choice([0,1,2,0xFFFF,0xFFFE]))
    elif kind == 'methodcount':
        struct.pack_into('>H', m, 6, random.choice([0,0xFFFF,0x7FFF]))
    elif kind == 'fieldcount':
        struct.pack_into('>H', m, 4, random.choice([0,0xFFFF]))
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

hangs, crashed, ok = [], [], 0
for i in range(N):
    kind = KINDS[i % len(KINDS)] if i < len(KINDS) else random.choice(KINDS)
    jar = build({cls_name: mutate(kind)})
    try:
        r = subprocess.run([BIN,'--port','headless','--heap','8',jar],
                           capture_output=True, timeout=PER)
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
print('clean: %d   crashed: %d   HUNG (>%ds): %d' % (ok, len(crashed), PER, len(hangs)))
for h in hangs[:12]:
    print('  HANG  case %d kind=%s' % h)
for c in crashed[:8]:
    print('  CRASH case %d kind=%s signal=%s %s' % c)
