#!/usr/bin/env python3
"""Inventory J2ME API classes the corpus references but the runtime does not define.

Implemented classes come from the builtin class tables (`nj_builtin_class`)
and the `NJ_C_*` name macros.  A reference is "missing" when a class the JAR
names is in an optional/vendor namespace, not in that set, and not shipped by
the JAR itself — exactly the kind of gap that ends in `NoClassDefFoundError`
at run time.  (Retail ports often bundle handset classes such as
`com/nokia/mid/ui/r.class`; those are not gaps.)

Usage: tools/check_api_coverage.py [--rows ROWS.jsonl|--dirs LIST] [--top N]

  --rows   rows.jsonl from a sweep (uses each row's jar_path)
  --dirs   colon-separated directories scanned recursively for *.jar
  --top    how many missing classes to print (default 50)

The output is two tables: missing classes by JAR count, and the jars that
reference the largest number of missing classes.
"""
import argparse, json, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Namespaces that are optional APIs (everything a handset may or may not ship).
PREFIXES = (
    "javax/microedition/", "com/nokia/", "com/siemens/", "com/samsung/",
    "com/motorola/", "com/sonyericsson/", "com/mascot/", "com/nokia/mid/",
    "com/vodafone/", "com/sprintpcs/", "com/microemu/", "org/microedition/",
    "com/mojang/", "net/rim/", "com/blackberry/", "com/palm/", "com/mot/",
    "com/lg/", "com/sharp/", "com/nec/", "com/panasonic/", "com/ericsson/",
    "com/sagem/", "com/alcatel/", "org/kwis/", "com/sun/midp/",
)


def strip_comment(line):
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == '/' and not q and out and out[-1] == '/':
            out.pop()
            break
        out.append(ch)
    return "".join(out)


def implemented_classes():
    names = set()
    macro_re = re.compile(r'#\s*define\s+NJ_C_[A-Z0-9_]+\s+"([^"]+)"')
    lit_re = re.compile(
        r'^\s*(?:\{\s*"|NJ_[A-Z0-9_]+\(")([A-Za-z][A-Za-z0-9_/$]*)')
    for base in ("src", "include"):
        for dp, _, fns in os.walk(os.path.join(ROOT, base)):
            for fn in fns:
                if not fn.endswith((".c", ".h")):
                    continue
                try:
                    text = open(os.path.join(dp, fn), encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for m in macro_re.finditer(text):
                    names.add(m.group(1))
                if "nj_builtin_class" in text:
                    for line in text.splitlines():
                        m = lit_re.match(strip_comment(line))
                        if m:
                            names.add(m.group(1))
    return names


def jar_class_refs(path):
    """Return the set of internal class names a JAR's classes reference."""
    refs = set()
    try:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if not info.filename.endswith(".class"):
                    continue
                try:
                    data = z.read(info)
                except Exception:
                    continue
                try:
                    refs |= class_refs(data)
                except Exception:
                    continue
    except Exception:
        return None
    return refs


def jar_provided_classes(path):
    """Return the class names a JAR itself ships (own or bundled .class files).

    A JAR may bundle its dependencies (retail ports ship handset classes such as
    com/nokia/mid/ui/r.class); such a reference is not an API gap for that
    title, because the runtime will load the class from the JAR.
    """
    names = set()
    try:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                name = info.filename
                if not name.endswith(".class") or name.startswith("META-INF/"):
                    continue
                names.add(name[:-len(".class")])
    except Exception:
        return None
    return names


def u2(b, i):
    return (b[i] << 8) | b[i + 1]


def u4(b, i):
    return (b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]


def class_refs(b):
    if len(b) < 10 or u4(b, 0) != 0xCAFEBABE:
        return set()
    cp_count = u2(b, 8)
    utf8 = {}
    classes = []
    i = 10
    idx = 1
    while idx < cp_count:
        tag = b[i]
        if tag == 1:
            ln = u2(b, i + 1)
            utf8[idx] = b[i + 3:i + 3 + ln].decode("utf-8", "replace")
            i += 3 + ln
        elif tag in (7, 8, 16):
            if tag == 7:
                classes.append(u2(b, i + 1))
            i += 3
        elif tag in (15,):
            i += 4
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            i += 5
        elif tag in (5, 6):
            i += 9
            idx += 1
        else:
            break
        idx += 1
    out = set()
    for ci in classes:
        name = utf8.get(ci)
        if not name:
            continue
        while name.startswith("["):
            name = name[1:]
        if name.startswith("L") and name.endswith(";"):
            name = name[1:-1]
        if name:
            out.add(name)
    return out


def wanted(name, impl):
    if name in impl:
        return False
    return name.startswith(PREFIXES)


JAVA_ROOTS = ("javax", "com", "org", "ru")


def kemulator_classes(root):
    """Class names implemented by a KEmulator-style source tree."""
    names = set()
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if not fn.endswith(".java"):
                continue
            rel = os.path.relpath(os.path.join(dp, fn), root)[:-5]
            parts = rel.split(os.sep)
            for i, part in enumerate(parts):
                if part in JAVA_ROOTS:
                    names.add("/".join(parts[i:]))
                    break
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows")
    ap.add_argument("--dirs")
    ap.add_argument("--top", type=int, default=50)
    ap.add_argument("--kemulator", help="KEmulator source tree to diff against")
    args = ap.parse_args()

    if args.kemulator:
        impl = implemented_classes()
        seen_k = kemulator_classes(args.kemulator)
        missing = sorted(n for n in seen_k if n not in impl)
        print("implemented classes: %d" % len(impl))
        print("KEmulator classes: %d, not implemented here: %d"
              % (len(seen_k), len(missing)))
        for name in missing:
            print("  " + name)
        return

    jars = []
    if args.rows:
        seen = set()
        for line in open(args.rows, encoding="utf-8"):
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            p = r.get("jar_path") or os.path.join(ROOT, r.get("file", ""))
            if os.path.isfile(p) and p not in seen:
                seen.add(p)
                jars.append(p)
    if args.dirs:
        for d in args.dirs.split(":"):
            for dp, _, fns in os.walk(d):
                for fn in fns:
                    if fn.endswith(".jar"):
                        jars.append(os.path.join(dp, fn))
    if not jars:
        ap.error("no JARs found")

    impl = implemented_classes()
    if args.kemulator:
        seen_k = kemulator_classes(args.kemulator)
        missing = sorted(n for n in seen_k if n not in impl)
        print("KEmulator classes: %d, not implemented here: %d"
              % (len(seen_k), len(missing)))
        for name in missing:
            print("  " + name)
        return
    missing_count = {}
    per_jar = {}
    total_refs = 0
    for jar in sorted(set(jars)):
        refs = jar_class_refs(jar)
        if refs is None:
            continue
        total_refs += 1
        provided = jar_provided_classes(jar) or set()
        miss = sorted(n for n in refs if wanted(n, impl) and n not in provided)
        if miss:
            per_jar[jar] = miss
            for n in miss:
                missing_count[n] = missing_count.get(n, 0) + 1

    print("implemented classes: %d" % len(impl))
    print("jars scanned: %d, jars with missing API refs: %d"
          % (total_refs, len(per_jar)))
    print()
    print("== missing classes by JAR count ==")
    for name, cnt in sorted(missing_count.items(), key=lambda kv: (-kv[1], kv[0]))[:args.top]:
        print("%5d  %s" % (cnt, name))
    print()
    print("== jars with the most missing classes ==")
    for jar, miss in sorted(per_jar.items(), key=lambda kv: -len(kv[1]))[:15]:
        print("%3d  %s" % (len(miss), os.path.basename(jar)))


if __name__ == "__main__":
    main()
