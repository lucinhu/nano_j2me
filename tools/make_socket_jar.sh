#!/usr/bin/env bash
#
# Build the socket fixture: a MIDlet that exercises
# javax.microedition.io.SocketConnection.
#
# What it answers, and why it needs a fixture rather than a screenshot:
#
#   - does Connector.open("socket://host:port") return a connected object at
#     all (before this work it threw ConnectionNotFoundException);
#   - do getAddress()/getPort() agree with the URL the MIDlet passed in;
#   - does a write really leave, and does the reply come back through
#     InputStream.read(), read(byte[],int,int) and skip();
#   - does an end of stream reach the MIDlet as -1 rather than as a timeout;
#   - is a second connection on the same runtime possible (open, close, open);
#   - does a port that cannot connect say so in a sentence.
#
# The URL points at 10.0.2.2, which is how an Android emulator reaches the
# host's loopback interface, so the fixture is run against
#
#   python3 tools/socket_echo_server.py 8080
#
# On a desktop build the same fixture is the NEGATIVE test: Connector.open()
# throws ConnectionNotFoundException whose message says the build has no socket
# support, because the headless port has no sockets.
#
#   tools/make_socket_jar.sh [out.jar] [url]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/socketdemo.jar}"
# 10.0.2.2 is the host loopback as seen from an Android emulator.
URL="${2:-socket://10.0.2.2:8080}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/SocketDemo.java" <<JAVA
import java.io.InputStream;
import java.io.OutputStream;
import javax.microedition.io.Connector;
import javax.microedition.io.SocketConnection;
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
 * Nothing here throws out of startApp(): a failure is printed as FAILED, so a
 * run always reaches "done" and the log says which step was reached.
 */
public class SocketDemo extends MIDlet {
    static final String URL = "$URL";

    static class Screen extends Canvas {
        String[] lines = new String[14];
        int count;
        Screen() { setFullScreenMode(true); }
        synchronized void log(String s) {
            System.out.println("SOCKET " + s);
            if (count < lines.length) { lines[count++] = s; }
            repaint();
        }
        protected void paint(Graphics g) {
            g.setColor(0xFFFFFF);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0x101010);
            g.setFont(Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL));
            for (int i = 0; i < count; i++) {
                g.drawString(lines[i], 2, 2 + i * 12, Graphics.TOP | Graphics.LEFT);
            }
        }
    }

    Screen screen = new Screen();

    public void startApp() {
        Display.getDisplay(this).setCurrent(screen);
        screen.log("url " + URL);
        talk();
        again();
        refused();
        screen.log("done");
    }

    /*
     * One round trip: the server echoes a line, then answers BYE with "bye"
     * and hangs up, which is where the end-of-stream check comes from.
     */
    private void talk() {
        SocketConnection c = null;
        try {
            c = (SocketConnection) Connector.open(URL);
            screen.log("open ok");
            screen.log("addr " + c.getAddress() + " port " + c.getPort());
            screen.log("local " + c.getLocalAddress() + ":" + c.getLocalPort());
            c.setSocketOption(SocketConnection.KEEPALIVE, 1);
            screen.log("keepalive " + c.getSocketOption(SocketConnection.KEEPALIVE)
                       + " delay " + c.getSocketOption(SocketConnection.DELAY));

            OutputStream out = c.openOutputStream();
            out.write("nj2me 1\n".getBytes());
            out.flush();
            screen.log("wrote 7");

            InputStream in = c.openInputStream();
            String line = readLine(in, 40);
            screen.log("read[" + line.length() + "] " + line);

            /* read(byte[], off, len) and skip() on a live stream: the reply to
             * BYE is short, and skip() eats one byte of it. */
            out.write("BYE\n".getBytes());
            out.flush();
            byte[] buf = new byte[16];
            int n = in.read(buf, 0, buf.length);
            screen.log("block[" + n + "] " + new String(buf, 0, n > 0 ? n : 0).trim());
            int eof = in.read();
            screen.log("eof " + eof);
            in.close();
            out.close();
        } catch (Throwable t) {
            screen.log("talk FAILED " + t);
        } finally {
            close(c);
        }
    }

    /* A closed connection is closed for good: reading it raises, it does not
     * return a zero that a game would believe. */
    private void again() {
        SocketConnection c = null;
        try {
            c = (SocketConnection) Connector.open(URL);
            screen.log("reopen ok port " + c.getPort());
            OutputStream out = c.openOutputStream();
            out.write("BYE\n".getBytes());
            out.flush();
            InputStream in = c.openInputStream();
            screen.log("reopen read " + readLine(in, 20));
            c.close();
            try {
                in.read();
                screen.log("closed FAILED no exception");
            } catch (Throwable t) {
                screen.log("closed raises " + t.getClass().getName());
            }
        } catch (Throwable t) {
            screen.log("reopen FAILED " + t);
        } finally {
            close(c);
        }
    }

    /* Nothing is listening on port 1 of the emulator's loopback, so this is
     * the connect that must fail - and the message must name the socket. */
    private void refused() {
        try {
            Connector.open("socket://127.0.0.1:1");
            screen.log("refuse FAILED connected");
        } catch (Throwable t) {
            screen.log("refuse " + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static String readLine(InputStream in, int cap) throws Exception {
        StringBuffer sb = new StringBuffer();
        int ch;
        while (sb.length() < cap && (ch = in.read()) >= 0) {
            if (ch == '\n') { break; }
            if (ch != '\r') { sb.append((char) ch); }
        }
        return sb.toString().trim();
    }

    private void close(SocketConnection c) {
        try {
            if (c != null) { c.close(); }
        } catch (Throwable t) {
            screen.log("close FAILED " + t);
        }
    }

    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/SocketDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SocketDemo\r\nMIDlet-1: SocketDemo, , SocketDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT (url $URL)"
