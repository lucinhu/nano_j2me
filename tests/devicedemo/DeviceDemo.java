import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * What System.getProperty() answers, as one line a shell script can read.
 *
 * The device profile (see nj2me/nj_device.h) is a table of properties the
 * runtime answers on top of its own, and the point of it is that a title asking
 * "which handset am I on" gets an answer shaped like a real one.  That makes
 * three separate things testable, and this fixture reports all three rather
 * than the one the current build happens to get right:
 *
 *   1. the profile's own keys are visible, and each handset's spelling of the
 *      identity is there (a title uses com.nokia.mid.imei, com.nokia.IMEI or
 *      neither, depending on the firmware it was written against);
 *   2. the runtime's OWN facts are still answered with a profile installed -
 *      java.version and os.name are the two that a profile table must not be
 *      able to shadow, and both were unreachable before the lookup was ordered
 *      (the java.version rows sat behind a chain of else-ifs that answered the
 *      device first and fell off the end for everything else);
 *   3. a key NOBODY answers is null rather than a wrong guess, including for a
 *      key longer than the old 64-byte lookup buffer and for the empty string.
 *
 * The check script knows what each profile is supposed to contain, so the
 * fixture stays a reporter: it prints whatever it finds and asserts nothing.
 * That is deliberate - a fixture that hard-codes "Nokia6230/05.50" cannot also
 * be run under --device sonyericsson-k750, and running the same code under two
 * profiles is what tells a working profile from a hard-coded string.
 */
public class DeviceDemo extends MIDlet {
    /* Longer than any lookup buffer the runtime has had (the first version of
     * the property lookup compared at most 64 bytes and then treated the rest
     * as a match, so an over-long key could answer as a shorter one). */
    private static String longKey() {
        StringBuffer b = new StringBuffer();
        for (int i = 0; i < 200; i++) {
            b.append('k');
        }
        return b.toString();
    }

    /** The value, or "null" - one token, so the line parses with cut/grep. */
    private static String val(String key) {
        String v = System.getProperty(key);
        return v == null ? "null" : v;
    }

    private static String isNull(String key) {
        return System.getProperty(key) == null ? "true" : "false";
    }

    public void startApp() {
        String line;
        try {
            line = "DEV"
                    + " platform=" + val("microedition.platform")
                    + " nokiaimei=" + val("com.nokia.mid.imei")
                    + " nokiaoldimei=" + val("com.nokia.IMEI")
                    + " seimei=" + val("com.sonyericsson.imei")
                    + " imsi=" + val("com.nokia.mid.imsi")
                    + " mcc=" + val("com.nokia.mid.mcc")
                    + " kbd=" + val("com.nokia.keyboard.type")
                    + " override=" + val("com.example.test.override")
                    + " javaversion=" + val("java.version")
                    + " osname=" + val("os.name")
                    + " profiles=" + val("microedition.profiles")
                    + " unknown=" + isNull("com.example.no.such.key")
                    + " longkey=" + isNull(longKey())
                    + " emptykey=" + isNull("");
        } catch (Throwable t) {
            line = "DEV FAILED " + t;
        }
        System.out.println(line);
        Display.getDisplay(this).setCurrent(new V());
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class V extends Canvas {
    protected void keyPressed(int key) {
        System.out.println("KEY press " + key + " " + getKeyName(key));
    }

    protected void keyReleased(int key) {
        System.out.println("KEY release " + key + " " + getKeyName(key));
    }

    protected void paint(Graphics g) {
        g.setColor(0xFFFFFF);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0x008000);
        g.fillRect(0, 0, getWidth() / 2, getHeight() / 2);
    }
}
