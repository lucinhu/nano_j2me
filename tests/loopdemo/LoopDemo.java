import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/*
 * A MIDlet that runs its own frame loop inside startApp() and sleeps in it.
 *
 * This is the shape 仙剑奇侠传完美版 uses: the game never returns from
 * startApp(), because on a handset the AMS paints from its own event thread
 * and the game is free to spin.  An emulator whose display loop only runs
 * after startApp() returns presents nothing at all for such a title - which
 * is exactly what used to happen.
 */
public class LoopDemo extends MIDlet {
    private final Screen screen = new Screen();

    static class Screen extends Canvas {
        int painted;

        protected void paint(Graphics g) {
            painted++;
            int w = getWidth();
            int h = getHeight();
            g.setColor(0x00102030);
            g.fillRect(0, 0, w, h);
            g.setColor(0x0000FF00);
            g.fillRect(0, 0, (painted * 3) % w, 8);
        }
    }

    public void startApp() {
        Display.getDisplay(this).setCurrent(screen);
        /* Never returns: the game's own loop, forever. */
        for (;;) {
            try {
                Thread.sleep(1);
            } catch (InterruptedException e) {
                /* keep going, the way the real title does */
            }
        }
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
