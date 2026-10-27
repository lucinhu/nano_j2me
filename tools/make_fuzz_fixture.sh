#!/bin/sh
# make_fuzz_fixture.sh - build the checked-in malformed-class regression fixture
#
#   tools/make_fuzz_fixture.sh [output.jar]
#
# The default output is tests/fixtures/hardening_malformed.jar, which is
# committed because it is the input of tests/test_class_hardening.c: the test
# suite must be able to reproduce these failures without a JDK and without
# running the fuzzer.  (tests/fixtures/ is normally generated and gitignored;
# this one file is force-added, see `git ls-files tests/fixtures`.)
#
# The classes are assembled here byte by byte - no javac, no class library -
# because every one of them is malformed on purpose.  A Java source file
# cannot express a Code attribute whose exception handler resumes past the end
# of the code array, a class that names itself as its superclass, or a
# bytecode array that feeds an int to getfield; a class file can.
#
# Deterministic: fixed class-file version, fixed constant-pool order, fixed
# JAR entry order and timestamps (1980-01-01, stored entries), so running this
# script twice produces the same bytes and the printed SHA-256 is stable.
# Regenerate with:
#
#   tools/make_fuzz_fixture.sh
#   sha256sum tests/fixtures/hardening_malformed.jar
#
# Because tests/fixtures/ is gitignored, a regenerated file is picked up by
# the build only when it is committed explicitly:
#
#   git add -f tests/fixtures/hardening_malformed.jar
#
# What the fixture holds (each class has a static main([Ljava/lang/String;)V):
#
#   HardeningControl        valid class, main returns.  Proves the harness
#                           itself works, so a malformed-input failure cannot
#                           hide behind a broken fixture or JAR.
#   HardeningBadHandler     main is `aconst_null; athrow`, with an exception
#                           table entry range [0,2) and handler_pc == 2 ==
#                           code_len.  The throw unwinds into the entry, the
#                           interpreter resumes at pc 2, the fetch throws
#                           VerifyError, and the unwind matches the same entry
#                           again: a livelock with no bytecode retired and no
#                           scheduler yield.  Before the loader validated
#                           exception tables this ran at 100% CPU forever.
#   HardeningBadField       main pushes int 1 and executes getfield on it; the
#                           operand stack is untyped and there is no verifier,
#                           so the field fast path added the field offset to
#                           0x1 and memcpy'd from the result.  SIGSEGV, no
#                           exception.
#   HardeningBadPutfield    the write-side twin: int receiver, putfield to it.
#   HardeningCircularSuper  super_class names the class itself.  Linking it
#                           recursed class_link() into itself; the C stack ran
#                           out and the process died with SIGSEGV before any
#                           exception could be reported.
#   HardeningIfaceLoop      an interface that names itself in its interfaces
#                           array; linking it recursed the same way.
#
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-$ROOT/tests/fixtures/hardening_malformed.jar}

mkdir -p "$(dirname -- "$OUT")"

python3 - "$OUT" <<'PYEOF'
import hashlib
import struct
import sys
import zipfile

OUT = sys.argv[1]


def u1(v):
    return struct.pack('>B', v)


def u2(v):
    return struct.pack('>H', v)


def u4(v):
    return struct.pack('>I', v)


class CP:
    """A constant pool; index 0 is implicit and entries keep insertion order."""

    def __init__(self):
        self.entries = []
        self.n = 0

    def _add(self, payload):
        self.entries.append(payload)
        self.n += 1
        return self.n

    def utf8(self, s):
        b = s.encode('ascii')
        return self._add(u1(1) + u2(len(b)) + b)

    def cls(self, name_idx):
        return self._add(u1(7) + u2(name_idx))

    def nat(self, name_idx, desc_idx):
        return self._add(u1(12) + u2(name_idx) + u2(desc_idx))

    def fieldref(self, class_idx, nat_idx):
        return self._add(u1(9) + u2(class_idx) + u2(nat_idx))

    def bytes(self):
        # Constant pool version 45.3 (CLDC style: minor 3, major 45).
        return u2(self.n + 1) + b''.join(self.entries)


def code_attr(code_utf8_idx, max_stack, max_locals, code, handlers):
    body = (u2(max_stack) + u2(max_locals) + u4(len(code)) + bytes(code) +
            u2(len(handlers)))
    for (start_pc, end_pc, handler_pc, catch_cp) in handlers:
        body += u2(start_pc) + u2(end_pc) + u2(handler_pc) + u2(catch_cp)
    return u2(code_utf8_idx) + u4(len(body)) + body


def method(access, name_idx, desc_idx, code_utf8_idx,
           max_stack=0, max_locals=0, code=(), handlers=()):
    if code_utf8_idx is None:
        return u2(access) + u2(name_idx) + u2(desc_idx) + u2(0)
    attr = code_attr(code_utf8_idx, max_stack, max_locals, code, handlers)
    return u2(access) + u2(name_idx) + u2(desc_idx) + u2(1) + attr


def field(access, name_idx, desc_idx):
    return u2(access) + u2(name_idx) + u2(desc_idx) + u2(0)


def class_file(cp, access, this_idx, super_idx, interfaces, fields, methods):
    out = (u4(0xCAFEBABE) + u2(3) + u2(45) + cp.bytes() +
           u2(access) + u2(this_idx) + u2(super_idx) +
           u2(len(interfaces)) + b''.join(u2(i) for i in interfaces) +
           u2(len(fields)) + b''.join(fields) +
           u2(len(methods)) + b''.join(methods) +
           u2(0))                       # no class-level attributes
    return out


ACC_PUBLIC = 0x0001
ACC_STATIC = 0x0008
ACC_SUPER = 0x0020
ACC_INTERFACE = 0x0200
ACC_ABSTRACT = 0x0400
MAIN_DESC = '([Ljava/lang/String;)V'


def new_class(name):
    """Return (cp, this_class_idx, object_class_idx) for a plain class."""
    cp = CP()
    name_i = cp.utf8(name)
    this_cls = cp.cls(name_i)
    obj_i = cp.utf8('java/lang/Object')
    obj_cls = cp.cls(obj_i)
    return cp, this_cls, obj_cls


def simple_main(cp, code, max_stack, handlers=()):
    code_i = cp.utf8('Code')
    main_i = cp.utf8('main')
    desc_i = cp.utf8(MAIN_DESC)
    return method(ACC_PUBLIC | ACC_STATIC, main_i, desc_i, code_i,
                  max_stack=max_stack, max_locals=1, code=code,
                  handlers=handlers)


classes = {}

# --- a well-formed class, so the fixture cannot fail for a boring reason ---
cp, this_cls, obj_cls = new_class('HardeningControl')
classes['HardeningControl.class'] = class_file(
    cp, ACC_PUBLIC | ACC_SUPER, this_cls, obj_cls, [], [],
    [simple_main(cp, [0xb1], 0)])                         # return

# --- exception handler resumes outside the Code array (used to hang) ---
cp, this_cls, obj_cls = new_class('HardeningBadHandler')
classes['HardeningBadHandler.class'] = class_file(
    cp, ACC_PUBLIC | ACC_SUPER, this_cls, obj_cls, [], [],
    [simple_main(cp, [0x01, 0xbf], 1,                   # aconst_null; athrow
                 handlers=[(0, 2, 2, 0)])])             # handler_pc == code_len

# --- getfield on an int: wild read, used to SIGSEGV ---
cp, this_cls, obj_cls = new_class('HardeningBadField')
pad_i = cp.utf8('pad')
int_i = cp.utf8('I')
f_ref = cp.fieldref(this_cls, cp.nat(pad_i, int_i))
classes['HardeningBadField.class'] = class_file(
    cp, ACC_PUBLIC | ACC_SUPER, this_cls, obj_cls, [],
    [field(ACC_PUBLIC, pad_i, int_i)],
    [simple_main(cp, [0x04, 0xb4, f_ref >> 8, f_ref & 0xFF, 0x57, 0xb1],
                 1)])              # iconst_1; getfield #f_ref; pop; return

# --- putfield on an int: wild write, used to SIGSEGV ---
cp, this_cls, obj_cls = new_class('HardeningBadPutfield')
pad_i = cp.utf8('pad')
int_i = cp.utf8('I')
f_ref = cp.fieldref(this_cls, cp.nat(pad_i, int_i))
classes['HardeningBadPutfield.class'] = class_file(
    cp, ACC_PUBLIC | ACC_SUPER, this_cls, obj_cls, [],
    [field(ACC_PUBLIC, pad_i, int_i)],
    [simple_main(cp, [0x04, 0x05, 0xb5, f_ref >> 8, f_ref & 0xFF, 0xb1],
                 2)])              # iconst_1; iconst_2; putfield; return

# --- superclass loop: the class names itself as its superclass ---
cp = CP()
name_i = cp.utf8('HardeningCircularSuper')
this_cls = cp.cls(name_i)
code_i = cp.utf8('Code')
main_i = cp.utf8('main')
desc_i = cp.utf8(MAIN_DESC)
classes['HardeningCircularSuper.class'] = class_file(
    cp, ACC_PUBLIC | ACC_SUPER, this_cls, this_cls, [], [],
    [method(ACC_PUBLIC | ACC_STATIC, main_i, desc_i, code_i,
            max_stack=0, max_locals=1, code=[0xb1])])

# --- interface loop: an interface that lists itself as its super-interface ---
cp = CP()
name_i = cp.utf8('HardeningIfaceLoop')
this_cls = cp.cls(name_i)
obj_i = cp.utf8('java/lang/Object')
obj_cls = cp.cls(obj_i)
classes['HardeningIfaceLoop.class'] = class_file(
    cp, ACC_PUBLIC | ACC_INTERFACE | ACC_ABSTRACT, this_cls, obj_cls,
    [this_cls], [], [])

MANIFEST = (
    'Manifest-Version: 1.0\r\n'
    'MIDlet-Name: nano_j2me hardening fixture\r\n'
    'MIDlet-Version: 1.0.0\r\n'
    'MIDlet-Vendor: nano_j2me\r\n'
    'MIDlet-1: HardeningControl, , HardeningControl\r\n'
    'MicroEdition-Profile: MIDP-2.0\r\n'
    'MicroEdition-Configuration: CLDC-1.1\r\n'
    '\r\n'
)


def add_entry(zf, name, data):
    zi = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    zi.compress_type = zipfile.ZIP_STORED
    zi.create_system = 0
    zi.external_attr = 0o644 << 16
    zf.writestr(zi, data)


with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_STORED) as zf:
    add_entry(zf, 'META-INF/', b'')
    add_entry(zf, 'META-INF/MANIFEST.MF', MANIFEST.encode('ascii'))
    for name in sorted(classes):
        add_entry(zf, name, classes[name])

with open(OUT, 'rb') as f:
    digest = hashlib.sha256(f.read()).hexdigest()

print('wrote %s' % OUT)
print('sha256 %s' % digest)
PYEOF
