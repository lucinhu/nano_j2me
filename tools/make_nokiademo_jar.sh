#!/bin/sh
# Build tests/fixtures/nokiademo.jar.  The fixture also needs a real PNG to
# hand DirectUtils.createImage(byte[], int, int), so the script writes a two by
# two red one with python and puts it in the jar.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="${1:-$ROOT/tests/fixtures/nokiademo.jar}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
CP="$STUBS"
javac --release 8 -nowarn -classpath "$CP" -d "$WORK/classes" \
      "$ROOT/tests/nokiademo/NokiDemo.java"
python3 - "$WORK/classes/tiny.png" <<'PY'
import struct, sys, zlib
def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
ihdr = struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0)      # 2x2, 8-bit RGB
raw = b"".join(b"\x00" + b"\xff\x00\x00" * 2 for _ in range(2))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
mkdir -p "$WORK/classes/META-INF"
cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MF'
Manifest-Version: 1.0
MIDlet-1: NokiDemo, , NokiDemo
MIDlet-Name: NokiDemo
MIDlet-Vendor: nano_j2me
MIDlet-Version: 1.0
MF
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class tiny.png )
echo "wrote $OUT"
