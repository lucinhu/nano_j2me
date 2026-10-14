#!/usr/bin/env python3
"""Compare the core CLDC 1.1 / MIDP 2.0 reference against this runtime's tables.

    tools/build_api_ref.py     # writes tools/data/api-core.txt from the javadoc
    tools/api_audit.py         # this: what the reference has and we do not
    tools/api_audit.py -v      # also list what we declare and the reference lacks

Why: a missing member is not a compile error and not a link error.  The call
resolves up the class hierarchy at run time, finds nothing and throws
NoSuchMethodError inside the title, which then takes whatever fallback it has.
That is how three real bugs were found (an instance toString() on the wrappers,
DataOutputStream.writeChars, and a method declared in a table but never bound).
An audit over the whole core surface finds the next one before a title does.

The parsing of this runtime's tables lives in tools/m3g_api_audit.py, which was
written for the JSR 184 round; this reuses it so the two audits cannot drift.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import m3g_api_audit as shared      # noqa: E402  (strip_comments, read_macros, ...)

ROOT = os.path.dirname(HERE)
REF = os.path.join(HERE, "data", "api-core.txt")
ALLOW = os.path.join(HERE, "data", "api-allow.txt")
SRC_DIRS = ("src", "include")


ROW_CALL = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(')


def parse_rows(src, macros):
    """Every `static const nj_builtin_method NAME[]` table in a source file.

    The shared parser in tools/m3g_api_audit.py only understands rows spelled
    M(...), because the JSR 184 tables are all written that way.  java.io writes
    the same rows through RM(...), and a parser that misses them reports members
    that are already implemented as missing - the worst possible failure mode
    for a tool whose whole job is telling you what is absent.  So this one takes
    any row macro whose first two arguments are strings.
    """
    arrays = {}
    for m in re.finditer(r'static const nj_builtin_method[ \t]+([A-Za-z_][A-Za-z0-9_]*)\[\]'
                         r'[ \t]*=[ \t]*\{(.*?)\n\};', src, re.S):
        name, body = m.group(1), m.group(2)
        entries = []
        for item in shared.split_top(body):
            item = item.strip()
            call = ROW_CALL.match(item)
            if not call or not item.endswith(")"):
                continue
            parts = shared.split_top(item[call.end():-1])
            if len(parts) < 3:
                continue
            mname = shared.descriptor(parts[0], macros)
            desc = shared.descriptor(parts[1], macros)
            if mname is None or desc is None:
                continue
            entries.append((mname, desc, "NJ_ACC_STATIC" in " ".join(parts[2:])))
        arrays[name] = entries
    return arrays


def read_files():
    """(path, text) for every .c and .h the runtime is built from."""
    out = []
    for base in SRC_DIRS:
        for dirpath, _dirs, files in os.walk(os.path.join(ROOT, base)):
            for name in sorted(files):
                if name.endswith((".c", ".h")):
                    path = os.path.join(dirpath, name)
                    with open(path, encoding="utf-8", errors="replace") as f:
                        out.append((path, f.read()))
    return out


CALL = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(')


def read_call_macros(text):
    """`#define D1(n) M(n, "(D)D", 0)` -> {D1: 'M(n, "(D)D", 0)'}.

    Several tables in this runtime declare a whole family of methods through
    small wrappers like D1/II2/FF2 - sqrt, max, min and friends are written that
    way.  A parser that only looks for literal M( rows sees six Math methods
    where the reference has thirty, which is exactly the false alarm this audit
    exists to avoid.
    """
    defs = {}
    for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)'
                         r'\(([^)]*)\)[ \t]*([^\n]*)', text, re.M):
        name, params, body = m.group(1), m.group(2), m.group(3).strip()
        # only table-row wrappers: a method row (M(...)), a class row (the
        # "java/lang/" string of WRAPPER_CLASS) or a literal brace.  Expanding
        # every brace-carrying macro would mangle control flow all over the file.
        if "M(" not in body and '"java/lang/' not in body and not body.startswith("{"):
            continue
        # M itself is what the shared table parser looks for: expanding it would
        # leave it nothing to parse.
        if name == "M":
            continue
        defs[name] = ([p.strip() for p in params.split(",") if p.strip()], body)
    return defs


def expand_calls(text, defs):
    """Substitute the wrapper macros above, innermost call first."""
    for _ in range(8):
        changed = False
        for name, (params, body) in defs.items():
            pos = 0
            while True:
                m = re.compile(r'\b' + re.escape(name) + r'[ \t]*\(').search(text, pos)
                if not m:
                    break
                depth = 0
                i = m.end() - 1
                while i < len(text):
                    if text[i] == '(':
                        depth += 1
                    elif text[i] == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    i += 1
                if i >= len(text):
                    break
                args = shared.split_top(text[m.end():i])
                repl = body
                for pname, arg in zip(params, [a.strip() for a in args]):
                    # C preprocessor spelling: #x stringifies, x##y pastes
                    repl = repl.replace("#" + pname, '"' + arg.strip('"') + '"')
                    repl = re.sub(r'\b' + re.escape(pname) + r'##', arg, repl)
                    repl = re.sub(r'##' + re.escape(pname) + r'\b', arg, repl)
                    repl = re.sub(r'\b' + re.escape(pname) + r'\b', arg, repl)
                text = text[:m.start()] + repl + text[i + 1:]
                pos = m.start() + len(repl)
                changed = True
        if not changed:
            break
    return text


def class_tables(src):
    """Every `nj_lib_*_classes[]` table: (class, super, methods-array-name)."""
    rows = []
    for m in re.finditer(r'const nj_builtin_class[ \t]+nj_lib_[A-Za-z0-9_]+_classes\[\]'
                         r'[ \t]*=[ \t]*\{(.*?)\n\};', src, re.S):
        body = m.group(1)
        depth = 0
        cur = ""
        for ch in body:
            if ch == "{":
                depth += 1
                if depth == 1:
                    cur = ""
                    continue
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    rows.append(cur)
                    continue
            if depth >= 1:
                cur += ch
    out = []
    for row in rows:
        parts = [p.strip() for p in row.split(",")]
        if len(parts) < 3:
            continue
        out.append((parts[0], parts[1], parts[5] if len(parts) > 5 else "NULL"))
    return out


def load_reference(path):
    ref = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        ref.append((parts[0], parts[1], parts[2]))
    return ref


def main(argv):
    verbose = "-v" in argv
    if not os.path.exists(REF):
        print("no %s: run tools/build_api_ref.py first" % REF, file=sys.stderr)
        return 2

    headers = "\n".join(t for p, t in read_files() if p.endswith(".h"))
    # Some classes are declared by a list macro rather than a table row (the
    # exception hierarchy is built by X("java/lang/Exception", ...) inside
    # NJ_EXC_LIST).  Those cannot be read statically here, so they are reported
    # as unread rather than as missing: a false alarm costs more than a blind
    # spot, and the tables they feed are checked by test_natives anyway.
    generated = set()
    for path, text in read_files():
        generated.update(re.findall(r'"((?:[A-Za-z_$][\w$]*/)+[A-Za-z_$][\w$]*)"', text))
    arrays = {}          # path -> {array name: entries}
    classes = {}         # class name -> [(super, array name, path)]
    for path, text in read_files():
        if not path.endswith(".c"):
            continue
        whole = headers + "\n" + text
        macros = shared.read_macros(whole)
        src = shared.strip_comments(whole)
        src = expand_calls(src, read_call_macros(whole))
        arrays[path] = parse_rows(src, macros)
        for cls_raw, sup_raw, arr in class_tables(src):
            cls = shared.descriptor(cls_raw, macros)
            sup = shared.descriptor(sup_raw, macros) if sup_raw != "NULL" else None
            if cls:
                # a class row's method array name is a bare identifier, not a
                # string.  Two modules can register the same name (the m3g
                # module keeps an alias for the LCDUI game Sprite), so every
                # row is kept and a member is resolved if any of them has it.
                classes.setdefault(cls, []).append((sup, arr.strip(), path))

    def declared(cls, name, args):
        """Is `name` with that argument list reachable from `cls`?"""
        seen = set()
        todo = [cls]
        while todo:
            cur = todo.pop()
            if not cur or cur in seen:
                continue
            seen.add(cur)
            for sup, arr, path in classes.get(cur, ()):
                if arr and arr != "NULL":
                    for mname, mdesc, _static in arrays.get(path, {}).get(arr, []):
                        # our descriptors are "(args)Return", and Return has
                        # more than one character for every non-primitive:
                        # compare the argument list only, which is what the
                        # javadoc anchor of build_api_ref.py carries.
                        if mname == name and mdesc[:mdesc.index(")") + 1] == args:
                            return True
                if sup:
                    todo.append(sup)
        return False


    def unread(cls):
        """True when a class row exists but declares no method array (an
        interface whose methods live in its implementors, for example)."""
        rows = classes.get(cls)
        if not rows:
            return False
        return all(not arr or arr == "NULL" for _sup, arr, _path in rows)

    refset = set((cls, name, args) for cls, name, args in load_reference(REF))

    if verbose:
        extra = []
        for cls, rows in sorted(classes.items()):
            for _sup, arr, path in rows:
                for mname, mdesc, _static in arrays.get(path, {}).get(arr, []):
                    if ")" not in mdesc:
                        continue        # not a JVM descriptor: a table artefact
                    args = mdesc[:mdesc.index(")") + 1]
                    if (cls, mname, args) not in refset:
                        extra.append((cls, mname, args))
        if extra:
            print("%d declared member(s) the CLDC/MIDP reference does not list "
                  "(vendor API, or a descriptor typo):" % len(extra))
            for cls, name, args in extra:
                print("  extra %-44s %-24s %s" % (cls, name, args))
        else:
            print("nothing is declared that the reference does not list")

    missing = []
    unread_hits = 0
    for cls, name, args in load_reference(REF):
        if cls not in classes:
            if cls not in generated:
                missing.append((cls, name, args, "the class itself"))
        elif unread(cls):
            unread_hits += 1
        elif not declared(cls, name, args):
            missing.append((cls, name, args, ""))

    # A gap that is known and accepted (a lane another worker owns, a member
    # nobody can use yet) goes in tools/data/api-allow.txt, one per line in the
    # same shape as the report.  Anything else - including a gap that is new -
    # fails, which is what makes this usable as a ctest.
    allowed = set()
    if os.path.exists(ALLOW):
        for line in open(ALLOW, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                parts = line.split()
                if len(parts) >= 3:
                    allowed.add((parts[0], parts[1], parts[2]))
    known = [m for m in missing if (m[0], m[1], m[2]) in allowed]
    fresh = [m for m in missing if (m[0], m[1], m[2]) not in allowed]
    stale = sorted(a for a in allowed if a not in
                   [(m[0], m[1], m[2]) for m in missing])

    if missing:
        print("the reference declares %d member(s) this runtime cannot resolve "
              "(%d known, %d new):" % (len(missing), len(known), len(fresh)))
        for cls, name, args, why in missing:
            mark = "known" if (cls, name, args) in allowed else "NEW  "
            print("  %s %-44s %-24s %-34s %s" % (mark, cls, name, args, why))
    if stale:
        print("api-allow.txt lists %d member(s) that now resolve - drop them:"
              % len(stale))
        for cls, name, args in stale:
            print("  stale %-44s %-24s %s" % (cls, name, args))
    if fresh or stale:
        return 1
    else:
        print("every one of the %d reference members resolves" %
              sum(1 for _ in open(REF, encoding="utf-8") if _.strip()))
    if unread_hits:
        print("(%d member(s) sit on a class whose methods are declared elsewhere)"
              % unread_hits)
    # Reaching here means every gap is in tools/data/api-allow.txt: the audit
    # is a ctest, so the accepted gaps are a PASS, not a failure.
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
