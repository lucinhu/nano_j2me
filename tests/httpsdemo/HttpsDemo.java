import java.io.IOException;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.HttpsConnection;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * What the runtime answers about a connection's security types, as one line a
 * shell script can read.
 *
 * javax.microedition.io.HttpsConnection, SecurityInfo, SecureConnection and
 * javax.microedition.pki.Certificate are JSR 75 types.  急速公路狂飙－新年版_84
 * runs `c instanceof HttpsConnection` on every request; with the class absent
 * the interpreter raises NoClassDefFoundError while resolving it, inside a
 * working http:// connection.  book7's checkers sample names SecureConnection
 * and pki.Certificate the same way.
 *
 * The other half is that the answer has to be TRUE-or-FALSE correctly: this
 * runtime spells is-a as a super chain (a built-in class has no interface
 * list), so one implementation class for both schemes would answer true for an
 * http:// connection and send a title down its secure path.  So the fixture
 * asks BOTH questions of BOTH connections before it asks whether https works.
 *
 * Prints exactly one line:
 *
 *   HTTPS http_is_https=... https_is_https=... https_is_http=... url_ok=...
 *         secinfo=... types=...
 *
 * or `HTTPS ERROR <class>: <message>` when something threw.
 */
public class HttpsDemo extends MIDlet {

    protected void startApp() {
        String line;
        /* The runner refuses a MIDlet that installs no Displayable, and that
           check is right: a title with nothing to show is broken.  This probe
           only needs its startApp to run, so it puts up a one-colour canvas. */
        Display.getDisplay(this).setCurrent(new ProbeCanvas());
        try {
            line = run();
        } catch (Throwable t) {
            line = "HTTPS ERROR " + t.getClass().getName() + ": " + t.getMessage();
        }
        System.out.println(line);
        notifyDestroyed();
    }

    private static String run() throws Exception {
        Connection plain = Connector.open("http://example.invalid/probe");
        Connection secure = Connector.open("https://example.invalid/probe");

        boolean httpIsHttps = plain instanceof HttpsConnection;
        boolean httpsIsHttps = secure instanceof HttpsConnection;
        boolean httpsIsHttp = secure instanceof HttpConnection;

        HttpsConnection h = (HttpsConnection) secure;
        String url = h.getURL();

        String sec;
        try {
            h.getSecurityInfo();
            sec = "returned";
        } catch (IOException e) {
            sec = "IOException";
        }
        h.close();
        plain.close();

        boolean types = Class.forName("javax.microedition.io.SecureConnection") != null
                && Class.forName("javax.microedition.io.SecurityInfo") != null
                && Class.forName("javax.microedition.pki.Certificate") != null;

        return "HTTPS http_is_https=" + httpIsHttps
                + " https_is_https=" + httpsIsHttps
                + " https_is_http=" + httpsIsHttp
                + " url_ok=" + (url != null && url.startsWith("https://"))
                + " secinfo=" + sec
                + " types=" + types;
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class ProbeCanvas extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0x000000);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
