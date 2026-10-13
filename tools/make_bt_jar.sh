#!/usr/bin/env bash
#
# Build the Bluetooth fixture: a MIDlet that exercises javax.bluetooth
# (JSR 82) through the port's Bluetooth hooks.
#
# What it answers, and why it needs a fixture rather than a screenshot:
#
#   - does LocalDevice see an adapter at all, and does it carry the port's own
#     address, name and class-of-device;
#   - does DiscoveryAgent.startInquiry() really end in deviceDiscovered() and
#     inquiryCompleted() - the discovery runs on the frame pump, so nothing
#     else in the runtime can prove it;
#   - does searchServices() produce ServiceRecords whose getConnectionURL()
#     is a URL a MIDlet can hand straight back to Connector.open();
#   - does a SERVER URL ("btspp://localhost:...") give a notifier whose
#     acceptAndOpen() returns a stream, and does that stream carry bytes in
#     both directions;
#   - does the RFCOMM client URL ("btspp://<address>:<channel>") go down the
#     byte-stream path and get refused by name when the port has no radio;
#   - does the L2CAP notifier (btl2cap://localhost:...) answer too.
#
# The desktop build ships a synthetic radio in its headless port (one local
# device, two peers, two services on the first peer, and a server whose
# accepted connections carry a greeting and then echo), so this fixture is a
# unit test on a desktop.  Android has a real adapter but the emulator does
# not, which is exactly the interesting case: getLocalDevice() has to throw
# BluetoothStateException rather than pretend.
#
#   tools/make_bt_jar.sh [out.jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/btdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/BtDemo.java" <<'JAVA'
import javax.bluetooth.DataElement;
import javax.bluetooth.DeviceClass;
import javax.bluetooth.DiscoveryAgent;
import javax.bluetooth.DiscoveryListener;
import javax.bluetooth.L2CAPConnection;
import javax.bluetooth.L2CAPConnectionNotifier;
import javax.bluetooth.LocalDevice;
import javax.bluetooth.RemoteDevice;
import javax.bluetooth.ServiceRecord;
import javax.bluetooth.UUID;
import javax.microedition.io.Connector;
import javax.microedition.io.StreamConnection;
import javax.microedition.io.StreamConnectionNotifier;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/*
 * Every step prints through System.out (which both the headless runner and
 * the Android port put in their log) and onto the screen, so the same fixture
 * answers "did it work" in a log and in a screenshot.
 *
 * The steps run in the listener callbacks rather than in a loop, because that
 * is where the runtime hands control back: startInquiry() returns immediately
 * and the frame pump drives the discovery, exactly as a real MIDlet's would.
 *
 * Nothing here throws out of startApp(): a failure is printed as FAILED, so a
 * run always reaches "done" and the log says which step was reached.
 */
public class BtDemo extends MIDlet implements DiscoveryListener {
    static class Screen extends Canvas {
        String[] lines = new String[32];
        int count;
        Screen() { setFullScreenMode(true); }
        synchronized void log(String s) {
            System.out.println("BT " + s);
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

    Screen screen = new Screen();
    DiscoveryAgent agent;
    RemoteDevice first;
    int found, services;

    public void startApp() {
        Display.getDisplay(this).setCurrent(screen);
        local();
    }

    private void local() {
        try {
            LocalDevice d = LocalDevice.getLocalDevice();
            screen.log("addr " + d.getBluetoothAddress());
            screen.log("name " + d.getFriendlyName());
            DeviceClass c = d.getDeviceClass();
            screen.log("class major " + c.getMajorDeviceClass()
                       + " minor " + c.getMinorDeviceClass());
            agent = d.getDiscoveryAgent();
            screen.log("agent " + (agent != null));
            screen.log("discoverable " + d.setDiscoverable(
                           DiscoveryAgent.GIAC));
        } catch (Throwable t) {
            /* An emulator has no adapter: this is the answer, not a crash. */
            screen.log("local FAILED " + t);
            screen.log("done");
            return;
        }
        discover();
    }

    private void discover() {
        try {
            boolean started = agent.startInquiry(DiscoveryAgent.GIAC, this);
            screen.log("inquiry started " + started);
        } catch (Throwable t) {
            screen.log("inquiry FAILED " + t);
            screen.log("done");
        }
    }

    public void deviceDiscovered(RemoteDevice d, DeviceClass c) {
        found++;
        try {
            screen.log("found " + found + " " + d.getBluetoothAddress()
                       + " " + d.getFriendlyName(false) + " major "
                       + c.getMajorDeviceClass());
        } catch (Throwable t) {
            screen.log("found " + found + " FAILED " + t);
        }
        if (first == null) { first = d; }
    }

    public void inquiryCompleted(int disc) {
        screen.log("inquiry done " + disc + "="
                   + DiscoveryListener.INQUIRY_COMPLETED + " found " + found);
        search();
    }

    private void search() {
        if (first == null) {
            /* No peer heard.  The connections are still worth walking: a
             * server URL is local (it does not need a peer to exist) and a
             * client URL to a device that is not there has to come back with
             * a sentence rather than a hang. */
            screen.log("no peer to search");
            connections();
            return;
        }
        try {
            UUID[] u = new UUID[1];
            u[0] = new UUID(0x1101L);
            screen.log("search uuid " + u[0]);
            int trans = agent.searchServices(new int[] { 0x0100, 0x0000 }, u,
                                             first, this);
            screen.log("search started " + trans);
        } catch (Throwable t) {
            screen.log("search FAILED " + t);
            screen.log("done");
        }
    }

    public void servicesDiscovered(int trans, ServiceRecord[] recs) {
        services++;
        try {
            screen.log("services " + services + " count " + recs.length);
            for (int i = 0; i < recs.length; i++) {
                screen.log("  record " + i + " url "
                           + recs[i].getConnectionURL(
                                 ServiceRecord.NOAUTHENTICATE_NOENCRYPT,
                                 false));
                DataElement n = recs[i].getAttributeValue(0x0100);
                screen.log("  record " + i + " name "
                           + (n == null ? "null" : "" + n.getValue()));
                screen.log("  record " + i + " host "
                           + recs[i].getHostDevice().getBluetoothAddress());
            }
        } catch (Throwable t) {
            screen.log("services FAILED " + t);
        }
    }

    public void serviceSearchCompleted(int trans, int resp) {
        screen.log("search done " + resp + "="
                   + DiscoveryListener.SERVICE_SEARCH_COMPLETED);
        /* The client URL first: with no radio the port refuses it by name,
         * which is the answer this port owes a MIDlet rather than a silent
         * dead stream. */
        connections();
    }

    private void connections() {
        client();
        server();
        l2cap();
        screen.log("done");
    }

    private void client() {
        try {
            StreamConnection c = (StreamConnection) Connector.open(
                "btspp://001122aabbcc:1");
            screen.log("client opened " + (c != null));
            c.close();
        } catch (Throwable t) {
            screen.log("client refused " + t);
        }
    }

    private void server() {
        StreamConnectionNotifier n = null;
        try {
            n = (StreamConnectionNotifier) Connector.open(
                "btspp://localhost:001122334455;name=nano_j2me");
            screen.log("server opened " + (n != null));
            StreamConnection c = n.acceptAndOpen();
            screen.log("accepted " + (c != null));
            byte[] buf = new byte[64];
            int got = c.openInputStream().read(buf);
            screen.log("read " + got + " " + text(buf, got));
            byte[] out = "ping\n".getBytes();
            c.openOutputStream().write(out);
            int back = c.openInputStream().read(buf);
            screen.log("echo " + back + " " + text(buf, back));
            c.close();
        } catch (Throwable t) {
            screen.log("server FAILED " + t);
        } finally {
            try { if (n != null) { n.close(); } } catch (Throwable t) { }
        }
    }

    private void l2cap() {
        L2CAPConnectionNotifier n = null;
        try {
            n = (L2CAPConnectionNotifier) Connector.open(
                "btl2cap://localhost:001122334455;name=nano_j2me-l2cap");
            L2CAPConnection c = n.acceptAndOpen();
            screen.log("l2cap accepted " + (c != null));
            byte[] buf = new byte[64];
            int got = c.receive(buf);
            screen.log("l2cap recv " + got + " " + text(buf, got));
            c.send("pong\n".getBytes());
            screen.log("l2cap mtu " + c.getReceiveMTU() + "/"
                       + c.getTransmitMTU());
            c.close();
        } catch (Throwable t) {
            screen.log("l2cap FAILED " + t);
        } finally {
            try { if (n != null) { n.close(); } } catch (Throwable t) { }
        }
    }

    static String text(byte[] b, int n) {
        String r = "";
        for (int i = 0; i < n && i < b.length; i++) {
            char ch = (char) (b[i] & 0xff);
            r += (ch == '\n') ? "." : ("" + ch);
        }
        return r;
    }

    public void pauseApp() { }

    public void destroyApp(boolean u) { }
}
JAVA

cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MF'
Manifest-Version: 1.0
MIDlet-Name: BtDemo
MIDlet-Version: 1.0
MIDlet-Vendor: nano_j2me
MIDlet-1: BtDemo, , BtDemo
MicroEdition-Configuration: CLDC-1.1
MicroEdition-Profile: MIDP-2.0
MF

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -proc:none -nowarn --release 8 -cp "$STUBS" -d "$WORK/classes" \
      "$WORK/src/BtDemo.java"
jar cfm "$OUT" "$WORK/classes/META-INF/MANIFEST.MF" -C "$WORK/classes" .
echo "$OUT"
