#!/usr/bin/env python3
"""Compare the m3g API this runtime declares with the JSR 184 one.

The runtime's surface lives in two tables in src/midp/nj_lib_m3g.c: the class
table (which names, for every class, its superclass and its field/method
arrays) and one `nj_builtin_method` array per class.  This script parses those
the way the compiler would - expanding the NJ_C_* string macros, joining
adjacent string literals, reading the class row shapes - and prints what the
reference list has that the runtime does not.

Usage:
    tools/m3g_api_audit.py [--source src/midp/nj_lib_m3g.c]
                           [--reference tools/data/m3g-api-1.1.txt]
                           [--missing out.txt]

Exit status is 0 when nothing is missing, 1 otherwise, so it can be a test.
"""

import argparse
import re
import sys

PKG = "javax.microedition.m3g"


def strip_comments(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def read_macros(src):
    """#define NAME <string expression>  ->  {NAME: value}"""
    macros = {}
    for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\n]*)',
                         src, re.M):
        name, body = m.group(1), m.group(2).strip()
        if body.startswith('"') or body.startswith("NJ_C_"):
            macros[name] = body
    # expand iteratively: values may be built out of other macros
    for _ in range(8):
        changed = False
        for name, body in list(macros.items()):
            new = expand(body, macros, name)
            if new != body:
                macros[name] = new
                changed = True
        if not changed:
            break
    return macros


def expand(text, macros, self_name=None):
    """Expand NJ_C_* macros and join adjacent C string literals."""
    def repl(m):
        name = m.group(0)
        if name == self_name or name not in macros:
            return name
        return macros[name]

    text = re.sub(r'\b[A-Za-z_][A-Za-z0-9_]*\b', repl, text)
    # join string literals: "a" "b" -> "ab"
    def join(m):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(0))
        return '"' + "".join(parts) + '"'

    text = re.sub(r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")+', join, text)
    return text.strip()


def descriptor(text, macros):
    """Turn a C string expression into the descriptor it produces."""
    text = expand(text, macros)
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', text)
    if not parts:
        return None
    return "".join(parts)


def split_top(body):
    """Split a table body on commas that are not inside parentheses, braces,
    brackets or string literals.  A descriptor like "(L" NJ_C_X ";)" carries a
    semicolon and a comma-free string, so the splitter only has to track
    nesting and quotes - which is why this is not a regex."""
    items = []
    cur = ""
    depth = 0
    quote = False
    i = 0
    while i < len(body):
        c = body[i]
        if quote:
            cur += c
            if c == "\\":
                if i + 1 < len(body):
                    cur += body[i + 1]
                    i += 2
                    continue
            elif c == '"':
                quote = False
        elif c == '"':
            quote = True
            cur += c
        elif c in "([{":
            depth += 1
            cur += c
        elif c in ")]}":
            depth -= 1
            cur += c
        elif c == "," and depth == 0:
            items.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    if cur.strip():
        items.append(cur.strip())
    return items


def parse_method_arrays(src, macros):
    """static const nj_builtin_method NAME[] = { M("n","d",flags), ... };"""
    arrays = {}
    for m in re.finditer(r'static const nj_builtin_method[ \t]+([A-Za-z_][A-Za-z0-9_]*)\[\]'
                         r'[ \t]*=[ \t]*\{(.*?)\n\};', src, re.S):
        name, body = m.group(1), m.group(2)
        entries = []
        for item in split_top(body):
            item = item.strip()
            if not item.startswith("M(") or not item.endswith(")"):
                continue
            args = item[2:-1]
            parts = split_top(args)
            if len(parts) < 3:
                continue
            mname = descriptor(parts[0], macros)
            desc = descriptor(parts[1], macros)
            if mname is None or desc is None:
                continue
            flags = " ".join(parts[2:])
            entries.append((mname, desc, "NJ_ACC_STATIC" in flags))
        arrays[name] = entries
    return arrays


def parse_class_table(src, macros):
    """const nj_builtin_class nj_lib_m3g_classes[] = { { cls, super, flags, ... } };"""
    m = re.search(r'nj_lib_m3g_classes\[\][ \t]*=[ \t]*\{(.*?)\n\};', src, re.S)
    if not m:
        return []
    rows = []
    body = m.group(1)
    # split top-level rows on '},' boundaries, keeping nested braces together
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
        cls, sup = parts[0], parts[1]
        # both a macro name (NJ_C_O3D) and a plain literal work through
        # descriptor(): expand() turns the macro into its literal first
        cls_v = descriptor(cls, macros)
        sup_v = descriptor(sup, macros)
        if not cls_v:
            continue
        methods = parts[5].strip() if len(parts) > 5 else "NULL"
        out.append((cls_v, sup_v, methods))
    return out


def load_reference(path):
    ref = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("NOTE"):
            continue
        if line.startswith("CLASS "):
            parts = line.split()
            ref.append(("CLASS", parts[1], parts[2] if len(parts) > 2 else "java.lang.Object"))
        elif line.startswith("CTOR "):
            parts = line.split()
            ref.append(("METHOD", parts[1], "<init>", parts[2], False))
        elif line.startswith("METHOD ") or line.startswith("STATIC "):
            parts = line.split()
            # METHOD <class> <name> <descriptor>   (four fields: the descriptor
            # has no spaces)
            if len(parts) == 4:
                ref.append(("METHOD", parts[1], parts[2], parts[3], parts[0] == "STATIC"))
        elif line.startswith("FIELD "):
            parts = line.split()
            # FIELD <class> <name> <type> [<value>]  -- a constant, not a method
            if len(parts) >= 4:
                ref.append(("FIELD", parts[1], parts[2], parts[3], False))
    return ref


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default="src/midp/nj_lib_m3g.c")
    ap.add_argument("--reference", default="tools/data/m3g-api-1.1.txt")
    ap.add_argument("--missing", default=None)
    args = ap.parse_args()

    src = strip_comments(open(args.source, encoding="utf-8").read())
    macros = read_macros(src)
    arrays = parse_method_arrays(src, macros)
    classes = parse_class_table(src, macros)

    runtime = {}     # dotted class -> {"super":..., "methods": set()}
    for cls, sup, marray in classes:
        cname = cls.replace("/", ".")
        entry = runtime.setdefault(cname, {"super": (sup or "").replace("/", "."),
                                           "methods": set()})
        if marray and marray != "NULL":
            for mname, desc, static in arrays.get(marray, []):
                entry["methods"].add((mname, desc, static))

    ref = load_reference(args.reference)
    missing = []
    extra = []
    ref_classes = {c for kind, c, *_ in ref if kind == "CLASS"}
    for kind, cls, *rest in ref:
        if kind == "CLASS":
            if cls not in runtime:
                missing.append(f"CLASS {cls} {rest[0]}")
            continue
        if kind == "FIELD":
            # constants are not modelled as fields yet; only report the class
            continue
        name, desc, static = rest
        got = runtime.get(cls, {"methods": set()})["methods"]
        if not any(n == name and d == desc for n, d, s in got):
            missing.append(f"{'STATIC' if static else 'METHOD'} {cls} {name} {desc}")
    for cls, info in runtime.items():
        if cls not in ref_classes:
            extra.append(f"CLASS {cls}")
            continue
        ref_methods = {(n, d) for kind, c, *r in ref if kind == "METHOD" and c == cls
                       for n, d in [(r[0], r[1])]}
        for n, d, s in sorted(info["methods"]):
            if (n, d) not in ref_methods and not (n == "<init>" and (("<init>", d) in ref_methods)):
                extra.append(f"{'STATIC' if s else 'METHOD'} {cls} {n} {d}")

    print(f"runtime: {len(runtime)} classes, "
          f"{sum(len(v['methods']) for v in runtime.values())} constructors+methods")
    print(f"reference: {len(ref_classes)} classes, "
          f"{sum(1 for k, *_ in ref if k == 'METHOD')} constructors+methods")
    print(f"missing from the runtime: {len(missing)}")
    for line in missing[:40]:
        print("  " + line)
    if len(missing) > 40:
        print(f"  ... and {len(missing) - 40} more")
    print(f"declared by the runtime but not in the reference: {len(extra)}")
    for line in extra[:20]:
        print("  " + line)
    if len(extra) > 20:
        print(f"  ... and {len(extra) - 20} more")
    if args.missing:
        with open(args.missing, "w", encoding="utf-8") as fh:
            fh.write("\n".join(missing) + "\n")
        print(f"wrote {args.missing}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
