#!/usr/bin/env bash
#
# Build the colour-order fixture: a Canvas of three vertical bars, pure red,
# pure green and pure blue, left to right, unchanged for the life of the MIDlet.
#
# It exists because "the colours look wrong" is not a bug report a port can act
# on.  A 16-bit 5-6-5 pixel has to survive three separate handed-overs before it
# reaches the panel - the rasteriser writes it, the port expands it to the
# window's format, the compositor hands it to the display - and each of those
# can swap red and blue on its own.  Pure primaries in a known order turn the
# question into one measurement: sample the middle row, and either the left
# third is red or it is blue.
#
#   tools/make_colordemo_jar.sh [out.jar]
#
# Run it, screenshot, and read the pixels with tools/sample_screenshot.py.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/colordemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/ColorDemo.java" <<'JAVA'
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

/*
 * Three bars, no animation, no input handling: the picture is a constant, so
 * two screenshots taken at any time must agree pixel for pixel and one phone
 * can be compared against another.
 *
 * The colours are written as 0xRRGGBB because that is what MIDP's setColor
 * means; anything that swaps red and blue did it after this call.
 */
public class ColorDemo extends MIDlet {
    static class Bars extends Canvas {
        Bars() { setFullScreenMode(true); }
        protected void paint(Graphics g) {
            int w = getWidth();
            int h = getHeight();
            int a = w / 3;
            g.setColor(0xFF0000);
            g.fillRect(0, 0, a, h);
            g.setColor(0x00FF00);
            g.fillRect(a, 0, a, h);
            g.setColor(0x0000FF);
            g.fillRect(a * 2, 0, w - a * 2, h);
        }
    }
    Bars bars = new Bars();
    public void startApp() { Display.getDisplay(this).setCurrent(bars); }
    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/ColorDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ColorDemo\r\nMIDlet-1: ColorDemo, , ColorDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
