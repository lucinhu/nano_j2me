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
# visible Displayable" before the game ever painted a frame.  The runtime
# delivered showNotify() synchronously and used that state to reject the mode
# change.  Keep showNotify() deferred until presentation, while isShown() already
# identifies the current Displayable.  Launch wrappers query isShown() before
# the first paint and otherwise incorrectly resume their MIDlet a second time.
#
# The fixture reproduces the exact sequence and prints what it observed, so
# the ordering is asserted from the outside instead of being assumed:
#
#   FSORDER current shown=true shows=0  <- selected, callback still deferred
#   FSORDER full=true                  <- mode change remains legal
#   FSORDER painted shown=true full=true shows=1
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
        int shows;
        protected void showNotify() { shows++; }
        FsCanvas() {
            Display.getDisplay(FsOrder.self).setCurrent(this);
            System.out.println("FSORDER current shown=" + isShown() + " shows=" + shows);
            setFullScreenMode(true);
            System.out.println("FSORDER full=" + isFullScreenMode());
        }
        protected void paint(Graphics g) {
            System.out.println("FSORDER painted shown=" + isShown()
                               + " full=" + isFullScreenMode() + " shows=" + shows);
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
