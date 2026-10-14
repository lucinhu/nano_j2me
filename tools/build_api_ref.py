#!/usr/bin/env python3
"""Build a reference member list for the classes a game actually touches.

The m3g round started with a hand-built reference (tools/data/m3g-api-1.1.txt)
and an audit that compared it against src/midp/nj_lib_m3g.c; that is how 164
missing members were found before a single title complained.  This does the same
for the core CLDC 1.1 and MIDP 2.0 classes, but builds the reference from the
javadoc mirror instead of by hand.

    tools/build_api_ref.py            # writes tools/data/api-core.txt
    tools/api_audit.py                # compares it against the runtime

The output is one line per member:

    java/io/DataOutputStream writeChars (Ljava/lang/String;)V M

Fields are skipped: the audit is about the members a title calls, and a field
that exists but is never bound is caught by tests/test_natives.c instead.

Pages come from the J2ME_Docs mirror, because it serves the two spec versions
this runtime implements (CLDC 1.1 and MIDP 2.0) as plain javadoc, and the
default javadoc layout puts every declared member in the detail sections with
its signature in the anchor.
"""
import os
import re
import sys
import urllib.request

MIRROR = "https://nikita36078.github.io/J2ME_Docs/docs"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "api-core.txt")

# (spec, class) for everything a MIDP title can reasonably reach.  The list is
# the audit's scope: a member missing here is not checked, so it is worth
# keeping it to the classes that appear in real manifests and stack traces.
CLASSES = [
    ("cldc-1.1", "java/lang/Object"), ("cldc-1.1", "java/lang/String"),
    ("cldc-1.1", "java/lang/StringBuffer"), ("cldc-1.1", "java/lang/Math"),
    ("cldc-1.1", "java/lang/System"), ("cldc-1.1", "java/lang/Runtime"),
    ("cldc-1.1", "java/lang/Thread"), ("cldc-1.1", "java/lang/Class"),
    ("cldc-1.1", "java/lang/Throwable"), ("cldc-1.1", "java/lang/Exception"),
    ("cldc-1.1", "java/lang/Integer"), ("cldc-1.1", "java/lang/Long"),
    ("cldc-1.1", "java/lang/Short"), ("cldc-1.1", "java/lang/Byte"),
    ("cldc-1.1", "java/lang/Character"), ("cldc-1.1", "java/lang/Boolean"),
    ("cldc-1.1", "java/lang/Float"), ("cldc-1.1", "java/lang/Double"),
    ("cldc-1.1", "java/io/InputStream"), ("cldc-1.1", "java/io/OutputStream"),
    ("cldc-1.1", "java/io/ByteArrayInputStream"),
    ("cldc-1.1", "java/io/ByteArrayOutputStream"),
    ("cldc-1.1", "java/io/DataInputStream"), ("cldc-1.1", "java/io/DataOutputStream"),
    ("cldc-1.1", "java/io/Reader"), ("cldc-1.1", "java/io/Writer"),
    ("cldc-1.1", "java/io/InputStreamReader"),
    ("cldc-1.1", "java/io/OutputStreamWriter"),
    ("cldc-1.1", "java/io/PrintStream"),
    ("cldc-1.1", "java/util/Vector"), ("cldc-1.1", "java/util/Hashtable"),
    ("cldc-1.1", "java/util/Random"), ("cldc-1.1", "java/util/Calendar"),
    ("cldc-1.1", "java/util/Date"), ("cldc-1.1", "java/util/TimeZone"),
    ("cldc-1.1", "java/util/Stack"),
    ("midp-2.0", "javax/microedition/midlet/MIDlet"),
    ("midp-2.0", "javax/microedition/lcdui/Display"),
    ("midp-2.0", "javax/microedition/lcdui/Displayable"),
    ("midp-2.0", "javax/microedition/lcdui/Canvas"),
    ("midp-2.0", "javax/microedition/lcdui/Graphics"),
    ("midp-2.0", "javax/microedition/lcdui/Image"),
    ("midp-2.0", "javax/microedition/lcdui/Font"),
    ("midp-2.0", "javax/microedition/lcdui/Alert"),
    ("midp-2.0", "javax/microedition/lcdui/AlertType"),
    ("midp-2.0", "javax/microedition/lcdui/Form"),
    ("midp-2.0", "javax/microedition/lcdui/TextBox"),
    ("midp-2.0", "javax/microedition/lcdui/List"),
    ("midp-2.0", "javax/microedition/lcdui/ChoiceGroup"),
    ("midp-2.0", "javax/microedition/lcdui/Choice"),
    ("midp-2.0", "javax/microedition/lcdui/Command"),
    ("midp-2.0", "javax/microedition/lcdui/Item"),
    ("midp-2.0", "javax/microedition/lcdui/StringItem"),
    ("midp-2.0", "javax/microedition/lcdui/ImageItem"),
    ("midp-2.0", "javax/microedition/lcdui/Ticker"),
    ("midp-2.0", "javax/microedition/lcdui/Gauge"),
    ("midp-2.0", "javax/microedition/lcdui/DateField"),
    ("midp-2.0", "javax/microedition/lcdui/TextField"),
    ("midp-2.0", "javax/microedition/lcdui/Spacer"),
    ("midp-2.0", "javax/microedition/lcdui/game/GameCanvas"),
    ("midp-2.0", "javax/microedition/lcdui/game/Layer"),
    ("midp-2.0", "javax/microedition/lcdui/game/LayerManager"),
    ("midp-2.0", "javax/microedition/lcdui/game/Sprite"),
    ("midp-2.0", "javax/microedition/lcdui/game/TiledLayer"),
    ("midp-2.0", "javax/microedition/rms/RecordStore"),
    ("midp-2.0", "javax/microedition/rms/RecordEnumeration"),
    ("midp-2.0", "javax/microedition/media/Manager"),
    ("midp-2.0", "javax/microedition/media/Player"),
    ("midp-2.0", "javax/microedition/media/Controllable"),
    ("midp-2.0", "javax/microedition/media/control/VolumeControl"),
    ("midp-2.0", "javax/microedition/media/control/ToneControl"),
    ("midp-2.0", "javax/microedition/io/Connector"),
    ("midp-2.0", "javax/microedition/io/Connection"),
    ("midp-2.0", "javax/microedition/io/InputConnection"),
    ("midp-2.0", "javax/microedition/io/OutputConnection"),
    ("midp-2.0", "javax/microedition/io/StreamConnection"),
    ("midp-2.0", "javax/microedition/io/HttpConnection"),
    # The Nokia UI API, because it is the one vendor extension the collected
    # titles call in numbers: 83 of the 395 reference FullCanvas, 74
    # DirectGraphics, 73 DirectUtils and 14 Sound.  A vendor class that is
    # missing is caught by the title (it wraps the call in a try), but a vendor
    # METHOD that is missing is a NoSuchMethodError in the middle of a frame -
    # the same silent-gap shape as java.lang.Integer.toString().
    ("Nokia_UI_API_1_1", "com/nokia/mid/ui/FullCanvas"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/ui/DirectGraphics"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/ui/DirectUtils"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/ui/DeviceControl"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/ui/DirectGraphicsImplemented"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/sound/Sound"),
    ("Nokia_UI_API_1_1", "com/nokia/mid/sound/SoundListener"),
]

PRIMITIVE = {
    "void": "V", "boolean": "Z", "byte": "B", "char": "C", "short": "S",
    "int": "I", "long": "J", "float": "F", "double": "D",
}

SECTIONS = re.compile(r"^(keywords|navbar|skip-navbar|field_summary|"
                      r"constructor_summary|method_summary|methods_inherited|"
                      r"field_detail|constructor_detail|method_detail|"
                      r"navbar_bottom|skip-navbar_bottom)")


def descriptor(arg):
    """java.lang.String -> Ljava/lang/String;, int[] -> [I."""
    arg = arg.strip()
    dims = 0
    while arg.endswith("[]"):
        dims += 1
        arg = arg[:-2]
    arg = arg.replace(" ", "")
    if arg in PRIMITIVE:
        base = PRIMITIVE[arg]
    else:
        base = "L" + arg.replace(".", "/") + ";"
    return "[" * dims + base


def members(html, simple):
    """Every declared constructor and method of the page, as (name, desc).

    A javadoc anchor names a constructor after its class (`String(byte[])`), so
    the class's own simple name has to become `<init>` here - the runtime's
    tables and every other tool in this directory spell it that way.
    """
    out = []
    for raw in re.findall(r'NAME="([^"]+)"', html):
        if SECTIONS.match(raw) or "(" not in raw or not raw.endswith(")"):
            continue
        name, args = raw.split("(", 1)
        args = args[:-1]
        parts = [] if args.strip() == "" else args.split(",")
        desc = "(" + "".join(descriptor(p) for p in parts) + ")"
        if name == "<init>" or name == "" or name == simple:
            out.append(("<init>", desc))
        else:
            out.append((name.strip(), desc))
    return out


def main():
    lines = []
    failed = []
    for spec, cls in CLASSES:
        url = "%s/%s/%s.html" % (MIRROR, spec, cls)
        try:
            with urllib.request.urlopen(url, timeout=30) as r:
                html = r.read().decode("utf-8", "replace")
        except Exception as exc:                      # noqa: BLE001
            failed.append((cls, exc))
            continue
        seen = set()
        for name, desc in members(html, cls.rsplit("/", 1)[-1]):
            key = (cls, name, desc)
            if key in seen:
                continue
            seen.add(key)
            lines.append("%-46s %-22s %-38s M" % (cls, name, desc))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s: %d members over %d classes" %
          (OUT, len(lines), len(CLASSES) - len(failed)))
    for cls, exc in failed:
        print("  no page for %s: %s" % (cls, exc), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
