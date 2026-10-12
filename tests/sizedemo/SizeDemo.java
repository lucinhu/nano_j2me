import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * A MIDlet whose output depends on the screen size it is given.
 *
 * The corpus picks the first size a title will accept and, until this fixture
 * existed, never asked whether the title draws THERE.  宠物王国2紫_5550
 * renders 28 colours at 240x320 and exactly one at 176x208, and 176x208 is
 * first in the list - so the game was written off as "not drawing" at a size
 * it had never been asked to draw at.
 *
 * Two colours when it draws and one when it does not, because the corpus calls
 * a frame flat below MIN_COLOURS=2: a fixture that painted one colour in both
 * cases would pass whether the retry happened or not.
 */
public class SizeDemo extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new V(this));
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class V extends Canvas {
    private final MIDlet midlet;
    private int paints;

    V(MIDlet midlet) {
        this.midlet = midlet;
    }

    protected void paint(Graphics g) {
        if (getWidth() > 200) {
            g.setColor(0x00CC00);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xCC0000);
            g.fillRect(0, 0, getWidth() / 2, getHeight() / 2);
        }
        if (getWidth() == 128) {
            /*
             * Also the "ends before the first dump" case: three frames, then
             * out.  With a dump every 40 frames the corpus has no frames to
             * measure and reports "nothing was ever presented" - which is what
             * happened to seven Apress demos once the smoke keys started
             * ending them early.  Two colours, so the retry is visible.
             */
            g.setColor(0x0000CC);
            g.fillRect(0, 0, getWidth(), getHeight() / 2);
            g.setColor(0xCCCC00);
            g.fillRect(0, getHeight() / 2, getWidth(), getHeight() / 2);
            if (++paints >= 3) {
                midlet.notifyDestroyed();
            }
        }
        /* Other narrow screens are left to the background on purpose. */
    }
}
