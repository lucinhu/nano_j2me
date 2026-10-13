#!/usr/bin/env python3
"""Write Java stub sources for javax.microedition.m3g, for javac.

The fixture tests/m3gdemo/M3GDemo.java is compiled against the MIDP stubs in
tools/midp_stubs.py plus this package.  Like those stubs, these exist only so
javac can check the fixture's calls; the jar it is packed into is run on this
runtime, which supplies the real classes.  The member list is the same
reference tools/m3g_api_audit.py compares the runtime against, so a fixture
cannot be written against a member the runtime does not have.

Usage:
    tools/m3g_stubs.py                 # print the directory holding the sources
    tools/m3g_stubs.py --keep          # do not delete it on exit
    tools/m3g_stubs.py --quiet         # print only the directory
"""

import argparse
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFERENCE = os.path.join(ROOT, "tools", "data", "m3g-api-1.1.txt")

PRIMITIVE = {"V": "void", "Z": "boolean", "B": "byte", "C": "char",
             "S": "short", "I": "int", "J": "long", "F": "float", "D": "double"}
DEFAULT = {"boolean": "false", "byte": "0", "char": "0", "short": "0",
           "int": "0", "long": "0L", "float": "0.0f", "double": "0.0"}


def split_descriptor(desc):
    """Return the Java parameter list for a JVM method descriptor."""
    assert desc.startswith("(") and ")" in desc, desc
    params, body = desc[1:].split(")", 1)
    out, i = [], 0
    while i < len(params):
        c = params[i]
        arr = 0
        while c == "[":
            arr += 1
            i += 1
            c = params[i]
        if c == "L":
            end = params.index(";", i)
            name = params[i + 1:end].replace("/", ".")
            i = end + 1
        else:
            name = PRIMITIVE[c]
            i += 1
        out.append(name + "[]" * arr)
    ret = body
    arr = 0
    while ret.startswith("["):
        arr += 1
        ret = ret[1:]
    if ret == "V":
        jret = "void"
    elif ret.startswith("L"):
        jret = ret[1:-1].replace("/", ".") + "[]" * arr
    else:
        jret = PRIMITIVE[ret] + "[]" * arr
    return out, jret


def field_java_type(desc):
    if desc.startswith("["):
        return field_java_type(desc[1:]) + "[]"
    if desc.startswith("L"):
        return desc[1:-1].replace("/", ".")
    return PRIMITIVE[desc]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", default=REFERENCE)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    classes, supers, ctors, methods, fields = {}, {}, {}, {}, {}
    with open(args.reference, encoding="utf-8") as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            kind = parts[0]
            if kind == "CLASS":
                cls, sup = parts[1], parts[2]
                classes[cls] = cls.rsplit(".", 1)[1]
                supers[cls] = sup
            elif kind == "CTOR":
                ctors.setdefault(parts[1], []).append(parts[2])
            elif kind in ("METHOD", "STATIC"):
                methods.setdefault(parts[1], []).append(
                    (parts[2], parts[3], kind == "STATIC"))
            elif kind == "FIELD":
                fields.setdefault(parts[1], []).append(
                    (parts[2], parts[3], parts[4] if len(parts) > 4 else None))

    work = tempfile.mkdtemp(prefix="m3gstubs-")
    out = os.path.join(work, "javax", "microedition", "m3g")
    os.makedirs(out, exist_ok=True)

    for cls in sorted(classes):
        simple = classes[cls]
        sup = supers.get(cls)
        sup_simple = sup.rsplit(".", 1)[1] if sup and sup in classes else None
        body = ["package javax.microedition.m3g;",
                "",
                "public class %s%s {" % (simple,
                                        " extends " + sup_simple if sup_simple else "")]
        if "()V" not in ctors.get(cls, []):
            body.append("    protected %s() {" % simple)
            if sup_simple:
                body.append("        super();")
            body.append("    }")
        for fname, ptype, value in fields.get(cls, []):
            jtype = field_java_type(ptype)
            if value is None or value in ("null", "NULL"):
                body.append("    public static final %s %s = null;"
                            % (jtype, fname))
            else:
                body.append("    public static final %s %s = %s;"
                            % (jtype, fname, value))
        for desc in ctors.get(cls, []):
            params, _ = split_descriptor(desc)
            params_java = ", ".join("%s p%d" % (t, i) for i, t in enumerate(params))
            body.append("    public %s(%s) { }" % (simple, params_java))
        for name, desc, static in methods.get(cls, []):
            params, ret = split_descriptor(desc)
            params_java = ", ".join("%s p%d" % (t, i) for i, t in enumerate(params))
            head = "    public %s%s %s(%s) {" % ("static " if static else "",
                                                ret, name, params_java)
            body.append(head)
            if ret != "void":
                body.append("        return %s;" % DEFAULT.get(ret, "null"))
            body.append("    }")
        body.append("}")
        body.append("")
        with open(os.path.join(out, simple + ".java"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(body))
        if not args.quiet:
            print("wrote %s.java" % simple)

    print(work)
    if not args.keep:
        # The caller compiles while this process is alive only if it asked for
        # --keep; otherwise the directory is left for the caller to remove.
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
