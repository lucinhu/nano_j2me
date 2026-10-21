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

# An IMA ADPCM WAV: 8 kHz, mono, 4-bit, block align 256, four blocks (2020
# samples = 252500 us).  Civilisation V's whole soundtrack is in this format,
# and before it was decoded the game threw "not a PCM WAV" from inside paint()
# once per frame.  Written here by encoding a sine with the same step and index
# tables the decoder in src/midp/nj_lib_media.c uses.
python3 - "$WORK/classes/res/adpcm.wav" <<'PY'
import math, struct, sys

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
        73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
        408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,
        1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
        7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
        22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

RATE, BLOCK, BLOCKS = 8000, 256, 4
per_block = 2 * (BLOCK - 4) + 1
n = per_block * BLOCKS
samples = [int(12000 * math.sin(2 * math.pi * 440.0 * i / RATE)) for i in range(n)]

def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v

data = bytearray()
pos = 0
while pos < n:
    chunk = samples[pos:pos + per_block]
    pos += per_block
    pred = chunk[0]
    index = 0
    data += struct.pack('<hBB', pred, index, 0)
    nibbles = []
    for target in chunk[1:]:
        step = STEP[index]
        diff = target - pred
        sign = 0
        if diff < 0:
            sign = 8
            diff = -diff
        q = 0
        if diff >= step:
            q |= 4
            diff -= step
        if diff >= (step >> 1):
            q |= 2
            diff -= step >> 1
        if diff >= (step >> 2):
            q |= 1
        delta = (step >> 3)
        if q & 1:
            delta += step >> 2
        if q & 2:
            delta += step >> 1
        if q & 4:
            delta += step
        pred = clamp(pred - delta if sign else pred + delta, -32768, 32767)
        index = clamp(index + IDX[q | sign], 0, 88)
        nibbles.append(q | sign)
    for i in range(0, len(nibbles) - 1, 2):
        data.append(nibbles[i] | (nibbles[i + 1] << 4))
    if len(nibbles) % 2:
        data.append(nibbles[-1])

hdr = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
       + b'fmt ' + struct.pack('<IHHIIHH', 16, 0x11, 1, RATE, RATE // 2,
                               BLOCK, 4)
       + b'data' + struct.pack('<I', len(data)))
open(sys.argv[1], 'wb').write(hdr + data)
PY

# An AMR file.  Four 4.75 kbit/s frames of silence: byte 0 of each frame is the
# frame header (0x00 = mode 0, 13 bytes on the wire), and the sniffer has to
# recognise "#!AMR" whether or not anything names it ".amr".  The runtime decodes
# it itself, so the fixture proves the built-in decoder; a port that fills in
# nj_port.audio_decode answers first and overrides it.
{ printf '#!AMR\n'; head -c 64 /dev/zero; } > "$WORK/classes/res/tone.amr"

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

            /* 4. AMR: the runtime decodes narrowband itself, so this only
             * reaches the port hook when a port has a decoder of its own. */
            InputStream amr = getClass().getResourceAsStream("/res/tone.amr");
            if (amr == null) {
                System.out.println("AUDIO amr=missing");
            } else {
                try {
                    Player a = Manager.createPlayer(amr, "audio/amr");
                    a.realize();
                    a.prefetch();
                    a.start();
                    System.out.println("AUDIO amr=" + a.getDuration());
                    System.out.println("AUDIO amrstate=" + a.getState());
                    a.deallocate();
                    a.close();
                } catch (Exception e) {
                    System.out.println("AUDIO amrrefused=" + e.getMessage());
                }
            }
                in.close();
            }

            /* 3b. The same Player path, but the JAR's second WAV is IMA
             * ADPCM: the runtime has to decode it to PCM before the port
             * ever sees it. */
            InputStream adpcm = getClass().getResourceAsStream("/res/adpcm.wav");
            if (adpcm == null) {
                System.out.println("AUDIO adpcm=missing");
            } else {
                try {
                    Player d = Manager.createPlayer(adpcm, "audio/x-wav");
                    d.realize();
                    d.prefetch();
                    d.start();
                    System.out.println("AUDIO adpcm=" + d.getDuration());
                    System.out.println("AUDIO adpcmstate=" + d.getState());
                    d.deallocate();
                    d.close();
                } catch (Exception e) {
                    System.out.println("AUDIO adpcmrefused=" + e.getMessage());
                }
                adpcm.close();
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

# A SECOND MIDlet in the same JAR, and the reason is worth stating: AudioDemo
# above calls notifyDestroyed() when startApp() returns, so the frame loop ends
# at once and nothing pumps.  A WAV longer than the device buffer can only be
# delivered ACROSS frames, so testing that needs a MIDlet that stays alive.
cat > "$WORK/src/snd/AudioLoop.java" <<'JAVA'
package snd;

import javax.microedition.midlet.MIDlet;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import java.io.InputStream;

/*
 * Starts a WAV and stays running.  The test bounds it with --run-frames, and a
 * port that accepts only 256 frames per call can only receive the whole 4410
 * frame file if the runtime keeps pushing once per frame.
 */
public class AudioLoop extends MIDlet {

    /*
     * A screen, because a MIDlet with no Displayable does not get a frame loop
     * at all - the runner has nothing to present and stops.  A game with sound
     * has both; this fixture needs both for the same reason.
     */
    static class Screen extends javax.microedition.lcdui.Canvas {
        protected void paint(javax.microedition.lcdui.Graphics g) {
            g.setColor(0x000000);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    public void startApp() {
        try {
            javax.microedition.lcdui.Display.getDisplay(this)
                .setCurrent(new Screen());
            InputStream in = getClass().getResourceAsStream("/res/beep.wav");
            Player p = Manager.createPlayer(in, "audio/x-wav");
            p.realize();
            p.prefetch();
            p.start();
            System.out.println("AUDIO playing=1");
        } catch (Throwable t) {
            System.out.println("AUDIO playing=0 " + t);
        }
        /* deliberately NOT notifyDestroyed() */
    }
    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/snd/AudioDemo.java" "$WORK/src/snd/AudioLoop.java"

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
