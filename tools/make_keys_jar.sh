#!/usr/bin/env bash
#
# Build the input-effect fixture: a Canvas that repaints a DIFFERENT flat
# colour on every key or pointer event.
#
# It exists to test the test.  tools/check_input_effect.sh decides "did the
# input reach the game?" by comparing a frame from an input run against a
# frame from a no-input run, and that comparison is only trustworthy if it is
# known to be able to go BOTH ways:
#
#   * this fixture is deterministic with no input (one colour, fixed), so the
#     control run must agree with itself and the check must not report
#     INCONCLUSIVE;
#   * it changes colour on ANY key, so the input run must differ, and a
#     delivery path that silently swallows events makes the check FAIL.
#
# A real game cannot serve as that control: it animates on its own, so
# "it changed" says nothing about the input.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/keysdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/KeysDemo.java" <<'JAVA'
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

/*
 * Flat colour, changed by any input.  The first colour is fixed, so two runs
 * with no input are byte-identical; the counter is drawn as a bar whose width
 * encodes the number of events, so two different events cannot cancel out to
 * the same picture.
 */
public class KeysDemo extends MIDlet {
    static class Pad extends Canvas {
        int n = 0;
        Pad() { setFullScreenMode(true); }
        protected void paint(Graphics g) {
            int c = 0x102030 + n * 0x000711;
            g.setColor(c | 0xFF000000);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
        void bump() { n++; repaint(); }
        protected void keyPressed(int k)  { bump(); }
        protected void keyReleased(int k) { }
        /*
         * Each callback reports itself, at its own coordinates, so a test can
         * tell which one the runtime actually called.  Before this the drag and
         * release bodies were empty, which meant a runtime that misrouted a
         * drag into pointerPressed looked exactly like one that got it right:
         * nothing observable changed.
         */
        protected void pointerPressed(int x, int y) {
            System.out.println("PTR pressed " + x + "," + y);
            bump();
        }
        protected void pointerDragged(int x, int y) {
            System.out.println("PTR dragged " + x + "," + y);
        }
        protected void pointerReleased(int x, int y) {
            System.out.println("PTR released " + x + "," + y);
        }
    }
    Pad pad = new Pad();
    public void startApp() { Display.getDisplay(this).setCurrent(pad); }
    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/KeysDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: KeysDemo\r\nMIDlet-1: KeysDemo, , KeysDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
