#!/usr/bin/env bash
#
# Build the text fixture: one static screen of Latin and Chinese text, drawn at
# the three MIDP font sizes.
#
# It exists for two questions that a screenshot cannot answer by eye:
#
#   - does a supplied TrueType/system face actually rasterise, or does the
#     screen come back with the built-in bitmap face and nobody notices;
#   - when the built-in CJK face is compiled out (NJ_ENABLE_FONT_CJK=0, which
#     is what the Android build does to save ~480 KB per ABI), is the system
#     font really taking over the Chinese text, or is it blank?
#
# Both are "count the ink in this band" questions, so the bands are fixed:
# a Latin line, then Chinese lines, then a Latin line, all separated by enough
# white space that a band cannot pick up its neighbour's ink.
#
#   tools/make_text_jar.sh [out.jar]
#
# Run it headless and read the frame with tools/check_text_glyphs.sh.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/textdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/TextDemo.java" <<'JAVA'
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;

/*
 * A constant picture: no animation, no input, no timers, so any two frames of
 * this MIDlet are identical and a band's ink count is a fixed number.
 *
 * The strings are written as escapes rather than literal characters so that
 * the file does not depend on the editor's encoding - a fixture that silently
 * loses its Chinese text would pass a "is there ink" check for the wrong
 * reason.
 */
public class TextDemo extends MIDlet {
    static class Screen extends Canvas {
        Screen() { setFullScreenMode(true); }
        protected void paint(Graphics g) {
            g.setColor(0xFFFFFF);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0x000000);
            line(g, Font.SIZE_SMALL, 8, "Nano J2ME 0123456789");
            line(g, Font.SIZE_MEDIUM, 40, "\u6c49\u5b57\u6d4b\u8bd5 \u6e38\u620f\u5f00\u59cb");
            line(g, Font.SIZE_LARGE, 90, "\u4e2d\u6587\u663e\u793a\u6b63\u5e38");
            line(g, Font.SIZE_SMALL, 150, "\u5206\u8fa8\u7387\u4e0e\u5b57\u4f53");
            line(g, Font.SIZE_MEDIUM, 200, "The quick brown fox");
        }
        private void line(Graphics g, int size, int y, String s) {
            g.setFont(Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, size));
            g.drawString(s, 8, y, Graphics.TOP | Graphics.LEFT);
        }
    }
    Screen screen = new Screen();
    public void startApp() { Display.getDisplay(this).setCurrent(screen); }
    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/TextDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: TextDemo\r\nMIDlet-1: TextDemo, , TextDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
