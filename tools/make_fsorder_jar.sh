#!/usr/bin/env bash
#
# Build the setCurrent/full-screen ordering fixture.
#
# 开心农场 did this, inside its Canvas constructor:
#
#     Display.getDisplay(midlet).setCurrent(this);
#     setFullScreenMode(true);
#
# and the runtime threw IllegalStateException "setFullScreenMode() on a
# visible Displayable" before the game ever painted a frame.  Displayable
# .setFullScreenMode() is required to throw while the Displayable is visible,
# so the runtime was not wrong to have that rule - it was wrong about WHEN the
# Displayable becomes visible.  setCurrent() was delivering showNotify()
# synchronously, so the Canvas was "visible" the instant setCurrent() returned.
# On a real device setCurrent() returns first and showNotify() arrives later,
# which is what makes the two-line sequence above legal there.
#
# The fixture reproduces the exact sequence and prints what it observed, so
# the ordering is asserted from the outside instead of being assumed:
#
#   FSORDER current shown=false     <- not visible yet, so the change is legal
#   FSORDER full=true               <- and it took effect
#   FSORDER painted shown=true full=true
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/fsorder.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/FsOrder.java" <<'JAVA'
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

public class FsOrder extends MIDlet {
    static FsOrder self;

    public void startApp() {
        self = this;
        new FsCanvas();
    }
    public void pauseApp() { }
    public void destroyApp(boolean u) { }

    static boolean done;

    static class FsCanvas extends Canvas {
        FsCanvas() {
            Display.getDisplay(FsOrder.self).setCurrent(this);
            System.out.println("FSORDER current shown=" + isShown());
            setFullScreenMode(true);
            System.out.println("FSORDER full=" + isFullScreenMode());
        }
        protected void paint(Graphics g) {
            System.out.println("FSORDER painted shown=" + isShown()
                               + " full=" + isFullScreenMode());
            g.setColor(0x102030);
            g.fillRect(0, 0, getWidth(), getHeight());
            /* One frame is the whole story; a MIDlet that never exits would
             * make the test hang rather than fail. */
            if (!FsOrder.done) {
                FsOrder.done = true;
                FsOrder.self.notifyDestroyed();
            }
        }
    }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/FsOrder.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FsOrder\r\nMIDlet-1: FsOrder, , FsOrder\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
