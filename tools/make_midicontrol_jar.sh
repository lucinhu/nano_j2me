#!/usr/bin/env bash
#
# Build the MIDIControl fixture: a MIDlet that takes a MIDI Player's
# MIDIControl and calls every method on it, printing what each one answered.
#
# MIDIControl is the one control that acts on a song while it plays, and the
# calls travel further than the usual ones: Player.getControl() has to hand
# back an object, and setChannelVolume()/shortMidiEvent() have to reach the
# renderer - this library's, or a port's own synthesizer.  A call that only
# moved a number in a Java object would still "work" and be silent, so the
# fixture prints the answers instead of drawing them.
#
#   tools/make_midicontrol_jar.sh [out.jar]
#
# Run it headless and read the MIDICTL lines:
#   build/nj2me --port headless --run-frames 40 tests/fixtures/midicontrol.jar
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/midicontrol.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/res" "$WORK/classes/META-INF"

# A four second song: one note, a long rest, then a second note.  Long enough
# that the Player is still playing while the control is exercised.
python3 - "$WORK/res/song.mid" <<'PY'
import struct, sys
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))
body = (b"\x00\xff\x51\x03\x07\xa1\x20"          # 500000 us per quarter
        + b"\x00\x90\x45\x64"                     # note on at tick 0
        + b"\x60\x80\x45\x40"                     # off 96 ticks later
        + vlq(3904) + b"\x90\x45\x64"             # second note at tick 4000
        + b"\x60\x80\x45\x40" + b"\x00\xff\x2f\x00")
track = b"MTrk" + struct.pack(">I", len(body)) + body
head = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480)
open(sys.argv[1], "wb").write(head + track)
PY

cat > "$WORK/src/MidiCtlDemo.java" <<'JAVA'
import java.io.InputStream;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.media.Controllable;
import javax.microedition.media.control.MIDIControl;
import javax.microedition.midlet.MIDlet;

/*
 * Every MIDIControl method, called in the order a game calls them, with the
 * answer printed.  The calls are wrapped one by one: a method this runtime
 * does not have would otherwise end the MIDlet, and the point of the fixture
 * is to report which ones answered.
 */
public class MidiCtlDemo extends MIDlet {
    protected void startApp() {
        try {
            InputStream in = getClass().getResourceAsStream("/song.mid");
            Player p = Manager.createPlayer(in, "audio/midi");
            p.realize();
            p.prefetch();
            System.out.println("MIDICTL state=" + p.getState()
                               + " duration=" + p.getDuration());
            Object c = ((Controllable) p).getControl("MIDIControl");
            System.out.println("MIDICTL control=" + (c == null ? "null"
                               : c.getClass().getName()));
            if (!(c instanceof MIDIControl)) {
                System.out.println("MIDICTL not-a-MIDIControl");
                p.close();
                notifyDestroyed();
                return;
            }
            MIDIControl m = (MIDIControl) c;
            p.start();
            m.setChannelVolume(0, 40);
            System.out.println("MIDICTL vol=" + m.getChannelVolume(0));
            m.setProgram(0, 0, 40);
            int[] prog = m.getProgram(0);
            System.out.println("MIDICTL program=" + (prog == null ? "null"
                               : prog[0] + "/" + prog[1]));
            m.shortMidiEvent(0x90, 60, 100);
            m.shortMidiEvent(0x80, 60, 0);
            byte[] raw = new byte[] { 0x00, (byte) 0x90, 62, 100,
                                      0x60, (byte) 0x80, 62, 0 };
            System.out.println("MIDICTL long=" + m.longMidiEvent(raw, 0, 4)
                               + "," + m.longMidiEvent(raw, 4, 4));
            System.out.println("MIDICTL key=" + m.getKeyName(0, 0, 60)
                               + " name=" + m.getProgramName(0, 40)
                               + " bank=" + m.isBankQuerySupported()
                               + " banks=" + m.getBankList(true).length
                               + " programs=" + m.getProgramList(0).length);
            m.setChannelVolume(0, 100);
            System.out.println("MIDICTL vol2=" + m.getChannelVolume(0)
                               + " time=" + p.getMediaTime());
            p.stop();
            p.close();
            System.out.println("MIDICTL done");
        } catch (Throwable t) {
            System.out.println("MIDICTL threw " + t);
        }
        notifyDestroyed();
    }
    protected void pauseApp() { }
    protected void destroyApp(boolean u) { }
}
JAVA

cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MF'
Manifest-Version: 1.0
MIDlet-1: MidiCtlDemo, , MidiCtlDemo
MIDlet-Name: MidiCtlDemo
MIDlet-Vendor: nano_j2me
MIDlet-Version: 1.0
MF

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/MidiCtlDemo.java"
cp "$WORK/res/song.mid" "$WORK/classes/song.mid"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class song.mid )
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
