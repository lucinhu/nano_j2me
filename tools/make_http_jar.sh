#!/usr/bin/env bash
#
# Build the HTTP fixture: a MIDlet that exercises javax.microedition.io.
#
# What it answers, and why it needs a fixture rather than a screenshot:
#
#   - does Connector.open("http://...") return an object at all (before this
#     work it threw ConnectionNotFoundException);
#   - do the URL accessors agree with the URL the MIDlet passed in;
#   - does a request really go out, and does the response come back with the
#     status, the headers and the body the server sent;
#   - does a POST carry the body the MIDlet wrote to openOutputStream().
#
# The URL points at 10.0.2.2, which is how an Android emulator reaches the
# host's loopback interface, so the fixture is run against a server started on
# the machine running the emulator:
#
#   mkdir -p /tmp/httpsrv && printf 'hello from the host\n' > /tmp/httpsrv/index.txt
#   python3 /tmp/httpsrv/echo_server.py   # answers GET and POST with a body
#   (a plain "python3 -m http.server" answers GET only, and replies 501 to POST)
#
# On a desktop build with a port that has no HTTP stack the same fixture is the
# NEGATIVE test: Connector.open() succeeds, and the first response call throws
# "this build has no HTTP support; the port must supply it".
#
#   tools/make_http_jar.sh [out.jar] [url]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/httpdemo.jar}"
# 10.0.2.2 is the host loopback as seen from an Android emulator.
URL="${2:-http://10.0.2.2:8081/index.txt}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/classes/META-INF"

cat > "$WORK/src/HttpDemo.java" <<JAVA
import java.io.InputStream;
import java.io.OutputStream;
import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
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
public class HttpDemo extends MIDlet {
    static final String URL = "$URL";

    static class Screen extends Canvas {
        String[] lines = new String[12];
        int count;
        Screen() { setFullScreenMode(true); }
        synchronized void log(String s) {
            System.out.println("HTTP " + s);
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
        get();
        post();
        screen.log("done");
    }

    private void get() {
        HttpConnection c = null;
        try {
            c = (HttpConnection) Connector.open(URL);
            screen.log("open ok");
            screen.log("host " + c.getHost() + " port " + c.getPort()
                       + " file " + c.getFile() + " proto " + c.getProtocol());
            c.setRequestProperty("X-Nj2me", "fixture");
            screen.log("method " + c.getRequestMethod());
            int code = c.getResponseCode();
            screen.log("code " + code + " msg " + c.getResponseMessage());
            screen.log("type " + c.getType() + " len " + c.getLength());
            screen.log("hdr " + c.getHeaderField("X-Nj2me-Echo"));
            /* The numeric accessors: a header the server really sends, read by
             * name and by index, so the block the port copied is exercised
             * both ways. */
            screen.log("hdr server " + c.getHeaderField("Server"));
            screen.log("hdr0 " + c.getHeaderFieldKey(0) + " = "
                       + c.getHeaderField(0));
            InputStream in = c.openInputStream();
            StringBuffer sb = new StringBuffer();
            int ch;
            while ((ch = in.read()) >= 0 && sb.length() < 120) {
                sb.append((char) ch);
            }
            in.close();
            screen.log("body[" + sb.length() + "] " + sb.toString().trim());
        } catch (Throwable t) {
            screen.log("GET FAILED " + t);
        } finally {
            close(c);
        }
    }

    private void post() {
        HttpConnection c = null;
        try {
            c = (HttpConnection) Connector.open(URL + "?post=1");
            c.setRequestMethod(HttpConnection.POST);
            c.setRequestProperty("Content-Type", "text/plain");
            OutputStream out = c.openOutputStream();
            out.write("nj2me=1&x=2".getBytes());
            out.close();
            screen.log("post query " + c.getQuery());
            int pcode = c.getResponseCode();
            screen.log("post code " + pcode);
            /* Read the echo: a 200 alone would not prove the body arrived. */
            InputStream pin = c.openInputStream();
            StringBuffer psb = new StringBuffer();
            int pch;
            while ((pch = pin.read()) >= 0 && psb.length() < 120) {
                psb.append((char) pch);
            }
            pin.close();
            screen.log("post body[" + psb.length() + "] " + psb.toString().trim());
        } catch (Throwable t) {
            screen.log("POST FAILED " + t);
        } finally {
            close(c);
        }
    }

    private void close(HttpConnection c) {
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
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" "$WORK/src/HttpDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: HttpDemo\r\nMIDlet-1: HttpDemo, , HttpDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT (url $URL)"
