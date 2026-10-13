#!/usr/bin/env python3
"""Audit nj_field_lookup(vm, CLASS, "name", "desc") against the builtin field tables.

The runtime asks for a field by name AND descriptor.  A class that declares
`{ "_nj_alpha", "F", ... }` and is asked for `("_nj_alpha", "I")` returns NULL,
and every native built on that lookup then silently does nothing - no
exception, no diagnostic, just a setter that sets nothing.  Two such bugs were
found by hand (a Light that could not be constructed at all, and a Node whose
alpha factor was never stored, so Node.setAlphaFactor() did nothing); this
finds the rest from the source, with no JAR and no JVM.

The tables are C, so this reads them the way the compiler would:

  * a macro that expands to a string literal is expanded - including the
    NJ_C_X macros that *are* class names, resolved per file, because
    NJ_C_G3D means javax/microedition/m3g/Graphics3D in one module and
    com/mascotcapsule/micro3d/v3/Graphics3D in another;
  * a descriptor written as "L" NJ_C_NODE ";" is one descriptor, not three;
  * an entry written through a macro - FLD(F_P_STATE, "I", 0) - is an entry.

Exits non-zero and names file:line for every lookup the classes cannot answer.
"""
import re
import sys
import pathlib

files = []
for root in ("src", "include"):
    d = pathlib.Path(root)
    if d.is_dir():
        files += sorted(d.rglob("*.c")) + sorted(d.rglob("*.h"))

DEF_STR = re.compile(r'#define\s+(\w+)\s+((?:"[^"]*"\s*)+)')
DEF_CALL = re.compile(r'#define\s+(\w+)\s*\(([^)]*)\)\s*(\{[^\n]*\})')


def strip_comments(text):
    """Blank out comments, keeping every newline so line numbers still line up."""
    def blank(m):
        return re.sub(r'[^\n]', " ", m.group(0))
    text = re.sub(r'/\*.*?\*/', blank, text, flags=re.S)
    return re.sub(r'//[^\n]*', blank, text)


def split_args(s):
    """Split on commas that are not inside quotes, braces or parentheses."""
    parts, depth, cur, q = [], 0, "", False
    for ch in s:
        if ch == '"':
            q = not q
        if not q:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "," and depth == 0:
                parts.append(cur)
                cur = ""
                continue
        cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


class Unit:
    """One translation unit: its macros, its field tables, its class rows."""

    def __init__(self, path, text):
        self.path = path
        text = strip_comments(text)
        self.text = text
        self.str_macro = {}
        for m in DEF_STR.finditer(text):
            self.str_macro[m.group(1)] = "".join(re.findall(r'"([^"]*)"', m.group(2)))
        self.call_macro = {}
        for m in DEF_CALL.finditer(text):
            self.call_macro[m.group(1)] = ([p.strip() for p in m.group(2).split(",")],
                                           m.group(3))
        self.tables = {}
        for m in re.finditer(r'nj_builtin_field\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\n\};', text, re.S):
            self.tables[m.group(1)] = self.parse_fields(m.group(2))
        self.methods = set(re.findall(r'nj_builtin_method\s+(\w+)\s*\[\s*\]', text))
        self.rows = {}
        for m in re.finditer(r'nj_builtin_class\s+\w+\s*\[\s*\]\s*=\s*\{(.*?)\n\};',
                             text, re.S):
            for item in split_args(m.group(1)):
                item = item.strip()
                if item.startswith("{"):
                    item = item[1:item.rindex("}")] if "}" in item else item[1:]
                parts = split_args(item)
                if len(parts) < 6:
                    continue
                cls = self.eval(parts[0])
                if "/" not in cls:
                    continue
                fields = parts[3].strip()
                if fields != "NULL" and fields not in self.tables:
                    continue
                meths = parts[5].strip()
                if meths != "NULL" and meths not in self.methods:
                    continue
                self.rows.setdefault(cls, (fields if fields != "NULL" else None,
                                           self.eval(parts[1]) or None))

    def eval(self, expr):
        """Concatenate the string literals an expression evaluates to.

        A `cond ? "a" : "b"` field name - both branches name a field of the same
        class with the same descriptor - is read as its first branch.
        """
        if "?" in expr:
            expr = expr.split("?", 1)[0]
        out = []
        for m in re.finditer(r'"([^"]*)"|([A-Za-z_]\w*)', expr):
            if m.group(1) is not None:
                out.append(m.group(1))
            elif m.group(2) in self.str_macro:
                out.append(self.str_macro[m.group(2)])
            elif m.group(2) in GLOBAL_MACROS:
                out.append(GLOBAL_MACROS[m.group(2)])
        return "".join(out)

    def expand_entry(self, item):
        item = item.strip()
        m = re.match(r'^(\w+)\s*\((.*)\)$', item, re.S)
        if m and m.group(1) in self.call_macro:
            params, body = self.call_macro[m.group(1)]
            args = split_args(m.group(2))
            if len(args) >= len(params):
                for p, a in zip(params, args):
                    body = re.sub(r'\b%s\b' % re.escape(p), a, body)
            return body
        return item

    def parse_fields(self, body):
        d = {}
        for item in split_args(body):
            item = self.expand_entry(item)
            if item.startswith("{"):
                item = item[1:item.rindex("}")] if "}" in item else item[1:]
            parts = split_args(item)
            if len(parts) < 2:
                continue
            fname, fdesc = self.eval(parts[0]), self.eval(parts[1])
            if fname:
                d.setdefault(fname, fdesc)
        return d


GLOBAL_MACROS = {}
units = []
for path in files:
    text = pathlib.Path(path).read_text(errors="replace")
    for m in DEF_STR.finditer(text):
        GLOBAL_MACROS.setdefault(m.group(1),
                                 "".join(re.findall(r'"([^"]*)"', m.group(2))))
    units.append(Unit(path, text))

ROWS = {}
TABLES = {}
for u in units:
    for cls, (arr, sup) in u.rows.items():
        ROWS.setdefault(cls, (u, arr, sup))
    for name, d in u.tables.items():
        TABLES.setdefault((u.path, name), d)


def fields_of(cls, seen=None):
    seen = seen if seen is not None else set()
    if cls is None or cls in seen or cls not in ROWS:
        return {}
    seen.add(cls)
    u, arr, sup = ROWS[cls]
    out = dict(TABLES.get((u.path, arr), {})) if arr else {}
    for k, v in fields_of(sup, seen).items():
        out.setdefault(k, v)
    return out


problems, unknown = [], {}
count = 0
pat = re.compile(r'nj_field_lookup\s*\(\s*[^,\n]{1,120},\s*([^,\n]{1,120}),\s*([^,\n]{1,120}),\s*([^,\n]{1,120})\s*,\s*\d')
for u in units:
    for m in pat.finditer(u.text):
        fname = u.eval(m.group(2))
        if not fname:
            continue
        count += 1
        cls = u.eval(m.group(1))
        fdesc = u.eval(m.group(3))
        line = u.text[:m.start()].count("\n") + 1
        if cls not in ROWS:
            unknown[cls] = unknown.get(cls, 0) + 1
            continue
        fs = fields_of(cls)
        if fname not in fs:
            problems.append((u.path, line, cls, fname, fdesc, "no such field"))
        elif fs[fname] != fdesc:
            problems.append((u.path, line, cls, fname, fdesc, "declared as '%s'" % fs[fname]))

for path, line, cls, fname, fdesc, why in sorted(set(problems)):
    print("MISMATCH %s:%d: %s.%s asked as '%s', %s" % (path, line, cls, fname, fdesc, why))
if unknown:
    worst = sorted(unknown.items(), key=lambda kv: -kv[1])[:5]
    print("(no field table reached for: %s)" % ", ".join("%s x%d" % kv for kv in worst))
print("%d lookups, %d classes with a field table, %d problems" % (count, len(ROWS), len(set(problems))))
sys.exit(1 if problems else 0)
