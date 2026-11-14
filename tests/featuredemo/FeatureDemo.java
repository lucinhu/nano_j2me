import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Reports the frontend-facing facts that cannot be observed from the C side
 * alone, and exercises the vibration path long enough for a 60 Hz frontend to
 * see it.
 *
 * Everything is printed as one "FEATURE key=value" line so the mock frontend
 * can count what it needs without parsing prose.  The canvas paints two
 * colours at every size, so --visual-required also works when this fixture is
 * used to check the nano_j2me_screen option.
 */
public class FeatureDemo extends MIDlet {
    private static final class V extends Canvas {
        protected void paint(Graphics g) {
            g.setColor(0x3366CC);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xFFCC00);
            g.fillRect(0, 0, getWidth() / 2, getHeight() / 2);
        }
    }

    public void startApp() {
        Display display = Display.getDisplay(this);
        V canvas = new V();

        display.setCurrent(canvas);

        String locale = System.getProperty("microedition.locale");
        System.out.println("FEATURE locale=" + (locale == null ? "null" : locale));
        System.out.println("FEATURE canvas=" + canvas.getWidth() + "x"
                           + canvas.getHeight());

        /* Long enough that a tight harness loop and a 60 Hz frontend both
         * service it while the motor is on.  The background thread stops it
         * explicitly, so the test observes both a nonzero and a zero call. */
        display.vibrate(400);
        new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(600);
                } catch (InterruptedException ignored) {
                    /* the MIDlet is going away; no stop call is needed */
                    return;
                }
                Display.getDisplay(FeatureDemo.this).vibrate(0);
            }
        }).start();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
