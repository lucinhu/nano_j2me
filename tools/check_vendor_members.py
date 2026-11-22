#!/usr/bin/env python3
"""Compare corpus bytecode member references against the runtime's C tables.

The class-level audit (`check_api_coverage.py`) only proves that every class a
game references exists.  A game can link the class and still call a member the
runtime never registered; it then catches `NoSuchMethodError`/`NoSuchFieldError`
and quietly degrades.  The linkage scanner (`check_corpus_linkage.py`) catches
the ones that actually execute, but unreached branches hide the rest.  This tool
goes the other way: it reads every METHOD/INTERFACE_METHOD constant-pool
reference out of the corpus and diffs it against the native class/method tables
generated from `src/midp/nj_lib_*.c` (including their `.inc` include files).

It is a static audit, so it reports references that are present in bytecode
even if the code path never runs; treat a hit as a candidate, then check whether
the title can reach it.

Usage:
  tools/check_vendor_members.py [JAR-OR-DIR ...] [--only PREFIX ...] [--verbose]
  tools/check_vendor_members.py                     # whole default corpus
"""
import argparse
import os
import subprocess
import tempfile
import re
import struct
import sys
import zipfile
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMIT = 400


def class_refs(b):
    """Return [(owner, name, desc)] for METHOD/INTERFACE_METHOD refs."""
    if len(b) < 10 or b[:4] != b"\xca\xfe\xba\xbe":
        return []
    n = struct.unpack(">H", b[8:10])[0]
    off = 10
    tags = [None] * n
    vals = [None] * n
    i = 1
    while i < n:
        tag = b[off]
        off += 1
        tags[i] = tag
        if tag == 1:
            ln = struct.unpack(">H", b[off:off + 2])[0]
            off += 2
            vals[i] = b[off:off + ln].decode("utf-8", "replace")
            off += ln
        elif tag in (3, 4):
            off += 4
        elif tag in (5, 6):
            off += 8
            i += 1
        elif tag in (7, 8, 16):
            vals[i] = struct.unpack(">H", b[off:off + 2])[0]
            off += 2
        elif tag in (9, 10, 11, 12, 18):
            vals[i] = struct.unpack(">HH", b[off:off + 4])
            off += 4
        elif tag == 15:
            off += 3
        else:
            return []
        i += 1
    out = []
    for j in range(1, n):
        if tags[j] not in (10, 11):
            continue
        ci, nti = vals[j]
        owner = vals[vals[ci]] if tags[ci] == 7 else None
        nt = vals[nti]
        if owner is None or tags[nti] != 12:
            continue
        name_i, desc_i = nt
        name = vals[name_i] if tags[name_i] == 1 else None
        desc = vals[desc_i] if tags[desc_i] == 1 else None
        if isinstance(owner, str) and isinstance(name, str) and isinstance(desc, str):
            out.append((owner, name, desc))
    return out


def eval_c_string(expr, macros):
    out = []
    for m in re.finditer(r'"([^"]*)"|([A-Za-z_]\w*)', expr):
        if m.group(1) is not None:
            out.append(m.group(1))
        else:
            tok = m.group(2)
            if tok in macros:
                out.append(macros[tok])
            else:
                return None
    return "".join(out)


def parse_tables(paths):
    """Return {class_name: {(name, desc)}} parsed from C source files."""
    macros = {}
    method_arrays = {}
    sources = []
    # Class-name macros live in headers (nj_midp.h, nj_internal.h, nj_vm.h ...),
    # so fold every header under src/ and include/ into the macro map.
    for sub in ("src", "include"):
        for dirpath, _dirs, files in os.walk(os.path.join(ROOT, sub)):
            for f in files:
                if not f.endswith(".h"):
                    continue
                try:
                    header = open(os.path.join(dirpath, f),
                                  encoding="utf-8").read()
                except OSError:
                    continue
                for name, val in re.findall(
                        r'#define\s+(NJ_C_\w+)\s+"([^"]+)"', header):
                    macros[name] = val
    for path in paths:
        text = open(path, encoding="utf-8").read()
        sources.append(text)
        for inc in re.findall(r'#include\s+"(nj_[^"]+\.inc)"', text):
            inc_path = os.path.join(os.path.dirname(path), inc)
            if os.path.exists(inc_path):
                sources.append(open(inc_path, encoding="utf-8").read())
    for text in sources:
        for name, val in re.findall(r'#define\s+(NJ_C_\w+)\s+"([^"]+)"', text):
            macros[name] = val
        for m in re.finditer(
                r"static const nj_builtin_method (\w+)\[\] = \{(.*?)\};",
                text, re.S):
            entries = []
            for mm in re.finditer(
                    r'M\(\s*"([^"]+)"\s*,\s*((?:"[^"]*"|[A-Za-z_]\w*|\s)+?)\s*,',
                    m.group(2)):
                desc = eval_c_string(mm.group(2), macros)
                if desc is not None:
                    entries.append((mm.group(1), desc))
            method_arrays[m.group(1)] = entries
    tables = defaultdict(set)
    for text in sources:
        for tbl in re.finditer(
                r"const nj_builtin_class \w+\[\] = \{(.*?)\n\};", text, re.S):
            body = tbl.group(1)
            for ent in re.finditer(
                    r"\{\s*(?:(\"[^\"]+\")|(NJ_C_\w+))\s*,"
                    r"\s*(?:\"[^\"]*\"|NULL|NJ_C_\w+)\s*,[^{}]*?\}", body, re.S):
                cls = ent.group(1)[1:-1] if ent.group(1) else macros.get(ent.group(2))
                arr = re.search(r"(\w+_methods)\s*,", ent.group(0))
                if arr and cls:
                    tables[cls].update(method_arrays.get(arr.group(1), []))
    return tables


def jars_under(paths):
    for p in paths:
        if os.path.isdir(p):
            for dirpath, _dirs, files in os.walk(p):
                for f in sorted(files):
                    if f.lower().endswith((".jar", ".jad")):
                        yield os.path.join(dirpath, f)
        elif p.lower().endswith((".jar", ".jad")):
            yield p


def tables_from_binary(binary):
    """Ask the CLI for the authoritative builtin table dump."""
    with tempfile.NamedTemporaryFile(delete=False, suffix=".tsv") as tmp:
        path = tmp.name
    try:
        subprocess.run([binary, "--dump-builtin-api", path], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        tables = defaultdict(set)
        supers = {}
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 5 and parts[2] == "method":
                    tables[parts[0]].add((parts[3], parts[4]))
                elif len(parts) >= 4 and parts[2] == "class":
                    supers[parts[0]] = parts[1] or None
        tables["__supers__"] = supers  # type: ignore[assignment]
        return tables
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser(
        description="Static member audit of the corpus against the runtime's "
                    "native class/method tables.",
        usage="%(prog)s [JAR-OR-DIR ...] [--only PREFIX ...] [--verbose]")
    ap.add_argument("paths", nargs="*", default=[],
                    help="JAR files or directories (default: corpus)")
    ap.add_argument("--only", action="append", default=[],
                    help="only report classes whose name starts with PREFIX")
    ap.add_argument("--verbose", action="store_true",
                    help="also list the jars that produced each reference")
    ap.add_argument("--all-modules", action="store_true",
                    help="audit every nj_lib_*.c table, not just vendor modules")
    ap.add_argument("--bin", default=os.environ.get("NJ_BIN"),
                    help="runtime binary to dump the authoritative member "
                         "tables from (preferred; else parse the C sources)")
    args = ap.parse_args()

    paths = args.paths
    if not paths:
        cache = os.environ.get(
            "NJ_CORPUS_CACHE",
            os.path.join(os.path.expanduser("~"), ".cache", "nano_j2me", "corpus"))
        paths = [os.path.join(ROOT, "tests", "games"), cache]

    if args.all_modules:
        src_paths = []
        for sub in ("core", "midp"):
            src_dir = os.path.join(ROOT, "src", sub)
            if os.path.isdir(src_dir):
                src_paths += sorted(
                    os.path.join(src_dir, f) for f in os.listdir(src_dir)
                    if f.startswith("nj_lib_") and f.endswith(".c"))
    else:
        vendor = ["nokia", "siemens", "sprint", "immersion", "samsung",
                  "mascot", "pki", "location"]
        src_paths = [os.path.join(ROOT, "src", "midp", f"nj_lib_{v}.c")
                     for v in vendor]
        src_paths = [p for p in src_paths if os.path.exists(p)]
    supers = {}
    if args.bin and os.path.exists(args.bin):
        tables = tables_from_binary(args.bin)
        supers = tables.pop("__supers__", {})
        print(f"member tables: {len(tables)} classes from {args.bin} dump")
    else:
        tables = parse_tables(src_paths)
        print(f"member tables: {len(tables)} classes from {len(src_paths)} "
              f"module(s)")

    missing = defaultdict(set)
    who = defaultdict(set)
    checked = 0
    ref_classes = set()
    scanned = 0
    for jar in jars_under(paths):
        try:
            z = zipfile.ZipFile(jar)
        except (OSError, zipfile.BadZipFile):
            continue
        scanned += 1
        base = os.path.basename(jar)
        provided = {e[:-6] for e in z.namelist() if e.endswith(".class")}
        for entry in z.namelist():
            if not entry.endswith(".class"):
                continue
            try:
                refs = class_refs(z.read(entry))
            except Exception:
                continue
            for owner, name, desc in refs:
                if owner in provided:
                    continue          # the jar ships its own copy
                methods = tables.get(owner)
                if methods is None:
                    continue
                ref_classes.add(owner)
                checked += 1
                # JVM resolution walks the superclass chain.
                found = False
                cur = owner
                for _ in range(64):
                    if (name, desc) in tables.get(cur, ()):
                        found = True
                        break
                    cur = supers.get(cur)
                    if not cur:
                        break
                if found:
                    continue
                missing[owner].add((name, desc))
                who[(owner, name, desc)].add(base)

    total = sum(len(v) for v in missing.values())
    print(f"scanned {scanned} archives, checked {checked} member refs over "
          f"{len(ref_classes)} referenced classes "
          f"({sum(1 for c in ref_classes if c in tables)} of them audited), "
          f"{len(missing)} classes with missing members, {total} signature(s)")
    shown = 0
    truncated = False
    for owner in sorted(missing):
        if args.only and not any(owner.startswith(p) for p in args.only):
            continue
        print(f"{owner}")
        for name, desc in sorted(missing[owner]):
            if shown >= LIMIT:
                truncated = True
                break
            jars = sorted(who[(owner, name, desc)])
            extra = f"  <- {', '.join(jars[:3])}" if args.verbose else ""
            print(f"   {name}{desc}{extra}")
            shown += 1
        if truncated:
            break
    if truncated:
        print(f"... truncated at {LIMIT}; use --only to narrow")
    return 1 if shown else 0


if __name__ == "__main__":
    sys.exit(main())
