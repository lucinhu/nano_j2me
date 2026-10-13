#!/usr/bin/env bash
#
# Build the sensor fixture: a MIDlet that exercises javax.microedition.sensor
# (JSR 256).
#
# What it answers, and why it needs a fixture rather than a screenshot:
#
#   - does SensorManager.findSensors() see a sensor at all, and does the
#     SensorInfo it hands back carry the port's own vocabulary (url, quantity,
#     model, channel names, units, ranges, sampling rate);
#   - does a SensorConnection really produce a sample, from both getData() and
#     a DataListener - the listener one is the interesting half, because it is
#     called from the frame pump and nothing else in the runtime drives it;
#   - does the state machine (OPENED -> LISTENING -> OPENED -> CLOSED) hold;
#   - what happens for a quantity that does not exist (an empty array, not a
#     fabricated zero).
#
# The desktop build ships a synthetic accelerometer in its headless port
# (1.500, -2.250, 9.750, fixed so this fixture can assert on the numbers), and
# the Android port reads the real one.  On an emulator:
#
#   adb emu sensor set acceleration 1.5:-2.25:9.75
#   adb shell am start ... GameActivity --es jar sensordemo.jar ...
#
# so the same fixture is the unit test on a desktop and the end-to-end test on
# a device.
#
#   tools/make_sensor_jar.sh [out.jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/sensordemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/SensorDemo.java" <<'JAVA'
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;
import javax.microedition.sensor.Channel;
import javax.microedition.sensor.ChannelInfo;
import javax.microedition.sensor.Condition;
import javax.microedition.sensor.ConditionListener;
import javax.microedition.sensor.Data;
import javax.microedition.sensor.DataListener;
import javax.microedition.sensor.MeasurementRange;
import javax.microedition.sensor.SensorConnection;
import javax.microedition.sensor.SensorInfo;
import javax.microedition.sensor.SensorManager;

/*
 * Every step prints through System.out (which both the headless runner and
 * the Android port put in their log) and onto the screen, so the same fixture
 * answers "did it work" in a log and in a screenshot.
 *
 * Nothing here throws out of startApp(): a failure is printed as FAILED, so a
 * run always reaches "done" and the log says which step was reached.
 */
public class SensorDemo extends MIDlet {
    static class Screen extends Canvas {
        String[] lines = new String[16];
        int count;
        Screen() { setFullScreenMode(true); }
        synchronized void log(String s) {
            System.out.println("SENSOR " + s);
            if (count < lines.length) { lines[count++] = s; }
            repaint();
        }
        protected void paint(Graphics g) {
            g.setColor(0xFFFFFF);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0x101010);
            g.setFont(Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN,
                                   Font.SIZE_SMALL));
            for (int i = 0; i < count; i++) {
                g.drawString(lines[i], 2, 2 + i * 11,
                             Graphics.TOP | Graphics.LEFT);
            }
        }
    }

    /*
     * A double as text without touching Double.toString(): three decimals are
     * all this fixture prints, and integer arithmetic keeps the same digits on
     * every port.
     */
    static String num(double v) {
        boolean neg = v < 0;
        long m = (long) ((neg ? -v : v) * 1000.0 + 0.5);
        long ip = m / 1000, fp = m % 1000;
        String fs = "" + fp;
        while (fs.length() < 3) { fs = "0" + fs; }
        return (neg ? "-" : "") + ip + "." + fs;
    }

    Screen screen = new Screen();
    SensorConnection conn;
    int events;

    public void startApp() {
        Display.getDisplay(this).setCurrent(screen);
        find();
        if (conn == null) {
            screen.log("done");
            return;
        }
        read();
        listen();
        screen.log("done");
    }

    private void find() {
        try {
            SensorInfo[] none = SensorManager.findSensors("nosuchquantity");
            screen.log("unknown count " + (none == null ? -1 : none.length));
        } catch (Throwable t) {
            screen.log("unknown FAILED " + t);
        }
        SensorInfo[] all = null;
        try {
            all = SensorManager.findSensors("acceleration");
            screen.log("count " + (all == null ? -1 : all.length));
        } catch (Throwable t) {
            screen.log("find FAILED " + t);
            return;
        }
        if (all == null || all.length == 0) {
            screen.log("no accelerometer on this port");
            return;
        }
        SensorInfo info = all[0];
        try {
            screen.log("url " + info.getUrl());
            screen.log("quantity " + info.getQuantity() + " context "
                       + info.getContextType() + " model " + info.getModel());
            /* The sampling rate is a property in JSR 256, not an accessor:
             * PROP_MAX_RATE reads back as text. */
            screen.log("rate " + info.getProperty(SensorInfo.PROP_MAX_RATE)
                       + " available " + info.isAvailable() + " conn "
                       + info.getConnectionType());
            screen.log("vendor " + info.getProperty(SensorInfo.PROP_VENDOR));
            screen.log("props " + (info.getPropertyNames() == null ? -1
                                   : info.getPropertyNames().length));
            screen.log("buffer " + info.getMaxBufferSize());
            ChannelInfo[] ch = info.getChannelInfos();
            screen.log("channels " + (ch == null ? -1 : ch.length));
            if (ch != null && ch.length > 0) {
                MeasurementRange[] mr = ch[0].getMeasurementRanges();
                screen.log("channel " + ch[0].getName() + " unit "
                           + ch[0].getUnit() + " type " + ch[0].getDataType()
                           + " scale " + num(ch[0].getScale()));
                if (mr != null && mr.length > 0) {
                    screen.log("range " + num(mr[0].getSmallestValue()) + ".."
                               + num(mr[0].getLargestValue()) + " res "
                               + num(mr[0].getResolution()));
                }
            }
        } catch (Throwable t) {
            screen.log("info FAILED " + t);
        }
        try {
            conn = SensorManager.getSensorConnection(info);
            screen.log("open " + (conn != null));
        } catch (Throwable t) {
            screen.log("open FAILED " + t);
        }
    }

    private void read() {
        try {
            screen.log("state " + conn.getState() + " opened "
                       + SensorConnection.STATE_OPENED);
            Data[] d = conn.getData(1);
            screen.log("data " + (d == null ? -1 : d.length));
            if (d != null && d.length > 0) {
                double[] v = d[0].getDoubleValues();
                if (v != null && v.length >= 3) {
                    screen.log("x " + num(v[0]) + " y " + num(v[1]) + " z "
                               + num(v[2]));
                }
                screen.log("sample " + d[0].getChannelInfo().getName()
                           + " ts " + (d[0].getTimestamp(0) > 0 ? "yes"
                                                                : "no")
                           + " valid " + d[0].isValid(0));
            }
            /* The long form is the one allowed to wait for a reading: a port
             * whose sensor reports from an interrupt has nothing to give on
             * the first call, so the timeout is what makes this line work on
             * a phone as well as on the synthetic desktop sensor. */
            Data[] buf = conn.getData(1, 500, true, true, true);
            screen.log("buffered " + (buf == null ? -1 : buf.length));
            ChannelInfo[] ch = conn.getSensorInfo().getChannelInfos();
            Channel c = conn.getChannel(ch[0]);
            screen.log("chan " + c.getChannelInfo().getName() + " url "
                       + c.getChannelUrl());
            /* setSampleRate() is this runtime's extension: JSR 256 leaves
             * sampling rate control optional and the API for it is in a
             * separate optional package. */
            c.setSampleRate(50.0f);
            screen.log("sampleRate " + (int) c.getSampleRate() + " of "
                       + c.getSampleRates().length);
            screen.log("errors " + conn.getErrorCodes().length + " text "
                       + (conn.getErrorText(1) != null));
            try {
                c.addCondition(new ConditionListener() {
                    public void conditionMet(SensorConnection s, Data data,
                                             Condition cond) { }
                }, null);
                screen.log("condition accepted");
            } catch (UnsupportedOperationException u) {
                screen.log("condition unsupported");
            }
        } catch (Throwable t) {
            screen.log("read FAILED " + t);
        }
    }

    private void listen() {
        try {
            conn.setDataListener(new DataListener() {
                public void dataReceived(SensorConnection s, Data[] d,
                                         boolean lost) {
                    events++;
                    String v = "?";
                    if (d != null && d.length > 0) {
                        double[] a = d[0].getDoubleValues();
                        if (a != null && a.length >= 3) {
                            v = num(a[0]) + "," + num(a[1]) + "," + num(a[2]);
                        }
                    }
                    screen.log("evt " + events + " lost " + lost + " " + v);
                    if (events >= 3) {
                        try { s.removeDataListener(); } catch (Throwable t) { }
                    }
                }
            }, 50);
            screen.log("listening " + conn.getState() + " is "
                       + SensorConnection.STATE_LISTENING);
        } catch (Throwable t) {
            screen.log("listen FAILED " + t);
        }
    }

    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$WORK/src/SensorDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SensorDemo\r\nMIDlet-1: SensorDemo, , SensorDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
