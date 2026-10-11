#!/usr/bin/env bash
#
# Build the audio fixture MIDlet: tones, a tone Player walked through its state
# machine, a PlayerListener, and a WAV loaded from a JAR resource.
#
#   tools/make_audio_jar.sh
#   build/nj2me --port headless --heap 8 tests/fixtures/audiodemo.jar
#
# The point of the fixture is that the HEADLESS PORT RECORDS audio, so
# tests/test_audio.c can assert that the game asked for the right notes, in the
# right order, for the right durations, and that the Player reported the states
# MIDP says it should - without a sound card.  The MIDlet also prints its own
# view, so a run without the test harness still says what happened.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/audiodemo.jar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/src/snd" "$WORK/classes/res"

# A real PCM WAV: 22050 Hz, mono, 16-bit, 0.2 s of a 440 Hz sine.  Written by
# hand rather than by a library so the fixture needs nothing but Python, and so
# the header is exactly the one the decoder has to accept.
python3 - "$WORK/classes/res/beep.wav" <<'PY'
import math, struct, sys
rate, secs, freq = 22050, 0.2, 440.0
n = int(rate * secs)
samples = b''.join(struct.pack('<h', int(20000 * math.sin(2 * math.pi * freq * i / rate)))
                   for i in range(n))
hdr = (b'RIFF' + struct.pack('<I', 36 + len(samples)) + b'WAVE'
       + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16)
       + b'data' + struct.pack('<I', len(samples)))
open(sys.argv[1], 'wb').write(hdr + samples)
PY

cat > "$WORK/src/snd/AudioDemo.java" <<'JAVA'
package snd;

import javax.microedition.midlet.MIDlet;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.media.PlayerListener;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;
import java.io.InputStream;

public class AudioDemo extends MIDlet implements PlayerListener {

    private String events = "";
    private int calls = 0;

    public void playerUpdate(Player p, String event, Object data) {
        events = events + event + ";";
        calls++;
    }

    /*
     * A JSR-135 tone sequence: a BIG-ENDIAN two-byte length, then triples of
     * (note, duration-units, duration).  The length is the byte count of the
     * triples that follow, not the whole array.  Three notes: A4, C5, rest.
     */
    private static final byte[] SEQ = {
        0, 9,
        69, 1, 10,
        72, 1, 10,
        0,  1, 5
    };

    public void startApp() {
        try {
            /* 1. The one-shot call every game uses for a menu blip. */
            Manager.playTone(69, 50, Manager.TONE_STOP);
            Manager.playTone(72, 50, 0);
            Manager.playTone(60, 25, 0);

            /* 2. A tone Player, through the whole state machine. */
            Player p = Manager.createPlayer("tone://");
            int s0 = p.getState();
            p.addPlayerListener(this);
            p.realize();
            int s1 = p.getState();
            ToneControl tc = (ToneControl) p.getControl("ToneControl");
            if (tc != null) {
                tc.setToneSequence(SEQ);
            }
            p.prefetch();
            int s2 = p.getState();
            VolumeControl vc = (VolumeControl) p.getControl("VolumeControl");
            if (vc != null) {
                vc.setLevel(70);
            }
            p.start();
            int s3 = p.getState();
            p.stop();
            int s4 = p.getState();
            p.deallocate();
            int s5 = p.getState();
            p.close();

            System.out.println("AUDIO states=" + s0 + "," + s1 + "," + s2 + ","
                               + s3 + "," + s4 + "," + s5);
            System.out.println("AUDIO events=" + events);

            /* 3. A WAV from the JAR, played through the PCM path. */
            InputStream in = getClass().getResourceAsStream("/res/beep.wav");
            if (in == null) {
                System.out.println("AUDIO wav=missing");
            } else {
                Player w = Manager.createPlayer(in, "audio/x-wav");
                w.realize();
                w.prefetch();
                w.start();
                /* One value per line: the test's parser reads to end of line,
                 * and a line with two facts in it silently yields one. */
                System.out.println("AUDIO wav=" + w.getDuration());
                System.out.println("AUDIO wavstate=" + w.getState());
                w.deallocate();
                w.close();
                in.close();
            }

            /* 4. An unsupported protocol must be refused BY NAME. */
            try {
                Manager.createPlayer("http://example.com/a.mid");
                System.out.println("AUDIO http=accepted");
            } catch (Exception e) {
                System.out.println("AUDIO http=" + e.getMessage());
            }

            System.out.println("AUDIO calls=" + calls);
        } catch (Throwable t) {
            System.out.println("AUDIO failed=" + t);
        }
        notifyDestroyed();
    }

    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/snd/AudioDemo.java"

mkdir -p "$WORK/classes/META-INF"
cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MF'
Manifest-Version: 1.0
MIDlet-Name: AudioDemo
MIDlet-Version: 1.0.0
MIDlet-Vendor: nano_j2me
MIDlet-1: AudioDemo, /res/beep.wav, snd.AudioDemo
MF

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
echo "wrote $OUT"
