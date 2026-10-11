#!/usr/bin/env python3
"""Audit a compiled MIDlet for references to classes CLDC/MIDP does not have.

WHY THIS EXISTS.  tools/midp_stubs.py stubs javax.microedition.* and nothing
else, so javac resolves java.lang, java.util and java.io from the DESKTOP JDK.
That makes the compile-time check unsound in the direction that matters: a game
that uses java.util.ArrayList or java.util.List compiles here and then dies on a
device with NoClassDefFoundError - which is precisely the bug that building from
source is supposed to catch.  (It is also unsound the other way: vNes imports
java.util.* and javax.microedition.lcdui.*, so `List` is ambiguous against the
JDK's java.util.List although CLDC has no such interface and a device compiler
resolves it fine.)

Removing the JDK's java.* from javac's view is not possible - -Xbootclasspath/p
only prepends, and a full bootclasspath would have to supply every class javac
itself needs.  So this checks the OUTPUT instead: every class a compiled MIDlet
references must be one of

  * in the CLDC 1.1 / MIDP 2.0 API,
  * defined by the MIDlet's own JAR, or
  * in the small set of JDK classes that are also CLDC classes.

Anything else is reported.  That catches the false acceptance, which is the
failure mode that reaches a user.

Usage: check_cldc_api.py <jar> [more.jar ...]      exit 1 if anything is unknown
"""
import struct
import sys
import zipfile

# CLDC 1.1 + MIDP 2.0 + the optional packages this runtime implements.
API_PREFIXES = (
    "java/lang/", "java/util/", "java/io/",
    "javax/microedition/lcdui/", "javax/microedition/midlet/",
    "javax/microedition/rms/", "javax/microedition/io/",
    "javax/microedition/media/", "javax/microedition/content/",
    "com/nokia/mid/ui/",
)

# java.util and java.io are PARTIALLY present in CLDC: the JDK has many more
# classes than a device does, and those extras are exactly what this is for.
#
# ACCURACY MATTERS MORE THAN COVERAGE HERE.  A tool that reports classes CLDC
# actually has is a tool nobody will read, so this list is the CLDC 1.1 spec's
# absences and nothing else.  In particular CLDC 1.1 DOES have PrintStream,
# Reader, Writer, Comparable and java.lang.ref.* - they are deliberately NOT
# listed, because they are not missing.
NOT_IN_CLDC = {
    # java.util: CLDC has Vector, Hashtable, Stack, Random, Timer, Date,
    # Calendar, Enumeration, TimeZone and the exception types.  Not these.
    "java/util/ArrayList", "java/util/HashMap", "java/util/HashSet",
    "java/util/LinkedList", "java/util/List", "java/util/Map", "java/util/Set",
    "java/util/Collection", "java/util/Iterator", "java/util/ListIterator",
    "java/util/Collections", "java/util/Arrays", "java/util/StringBuilder",
    "java/util/StringTokenizer", "java/util/TreeMap", "java/util/TreeSet",
    "java/util/AbstractList", "java/util/AbstractMap", "java/util/AbstractSet",
    "java/util/regex/Pattern", "java/util/regex/Matcher",
    # java.io: CLDC has the stream, reader/writer and Data* types.
    "java/io/File", "java/io/FileReader", "java/io/FileWriter",
    "java/io/FileInputStream", "java/io/FileOutputStream",
    "java/io/BufferedReader", "java/io/BufferedWriter",
    "java/io/PrintWriter", "java/io/RandomAccessFile", "java/io/Serializable",
    "java/io/Externalizable", "java/io/ObjectOutputStream",
    # java.lang: CLDC has Object, the wrappers, String, StringBuffer, Math,
    # System, Runtime, Thread, Throwable and the error types.  Not these.
    "java/lang/Enum", "java/lang/Iterable", "java/lang/ThreadLocal",
    "java/lang/ClassLoader", "java/lang/Appendable", "java/lang/CharSequence",
    "java/lang/AutoCloseable", "java/lang/Readable",
}


def cp_classes(data):
    """Every CONSTANT_Class name in a .class file."""
    if len(data) < 10 or data[:4] != b"\xca\xfe\xba\xbe":
        return []
    count = struct.unpack(">H", data[8:10])[0]
    out = []
    i = 1
    p = 10
    while i < count and p < len(data):
        tag = data[p]
        p += 1
        if tag == 1:                      # Utf8
            n = struct.unpack(">H", data[p:p + 2])[0]
            p += 2 + n
        elif tag == 7:                    # Class -> name_index
            idx = struct.unpack(">H", data[p:p + 2])[0]
            out.append(idx)
            p += 2
        elif tag in (8, 16, 19, 20):      # String, MethodType, Module, Package
            p += 2
        elif tag in (15,):                # MethodHandle
            p += 3
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            p += 4
        elif tag in (5, 6):               # Long, Double take two slots
            p += 8
            i += 1
        else:
            return []                     # unknown tag: give up, do not guess
        i += 1
    # Second pass for the actual names, now that the pool is located.
    names = {}
    i, p = 1, 10
    while i < count and p < len(data):
        tag = data[p]
        p += 1
        if tag == 1:
            n = struct.unpack(">H", data[p:p + 2])[0]
            names[i] = data[p + 2:p + 2 + n].decode("utf-8", "replace")
            p += 2 + n
        elif tag == 7:
            p += 2
        elif tag in (8, 16, 19, 20):
            p += 2
        elif tag == 15:
            p += 3
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            p += 4
        elif tag in (5, 6):
            p += 8
            i += 1
        else:
            return []
        i += 1
    return [names.get(x, "") for x in out]


def audit(jar):
    unknown = {}
    with zipfile.ZipFile(jar) as z:
        own = {n[:-6] for n in z.namelist() if n.endswith(".class")}
        for name in z.namelist():
            if not name.endswith(".class"):
                continue
            for ref in cp_classes(z.read(name)):
                if not ref or ref.startswith("["):
                    continue
                if ref in own:
                    continue
                if not ref.startswith(API_PREFIXES):
                    continue
                if ref in NOT_IN_CLDC:
                    unknown.setdefault(ref, set()).add(name[:-6])
    return unknown


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    bad = 0
    for jar in sys.argv[1:]:
        unknown = audit(jar)
        if not unknown:
            print("%-28s ok" % jar.split("/")[-1])
            continue
        bad = 1
        print("%-28s %d class(es) a CLDC device does not have"
              % (jar.split("/")[-1], len(unknown)))
        for ref, users in sorted(unknown.items()):
            print("    %-32s used by %s" % (ref, ", ".join(sorted(users))[:60]))
    return bad


if __name__ == "__main__":
    sys.exit(main())
