import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.*;

/*
 * A deliberately STATIC screen: one fixed frame, no repaint, no timer.
 *
 * The Android GL presentation path needs a frame that is identical no matter
 * when the screenshot is taken; animated fixtures (LoopDemo) make "is the GL
 * path pixel-identical to the software path?" impossible to answer, because
 * two runs are captured at different animation phases.
 */
public class StaticDemo extends MIDlet
{
    static final class Screen extends Canvas
    {
        protected void paint(Graphics g)
        {
            int w = getWidth();
            int h = getHeight();
            g.setColor(0x102040);
            g.fillRect(0, 0, w, h);
            /* A coarse colour grid: one bad channel or a shifted row is
             * visible at a glance, and every cell is a different pixel. */
            for (int y = 0; y < h / 8; y++) {
                for (int x = 0; x < w / 8; x++) {
                    int r = (x * 255) / (w / 8);
                    int gr = (y * 255) / (h / 8);
                    int b = ((x + y) * 255) / ((w / 8) + (h / 8));
                    g.setColor(r, gr, b);
                    g.fillRect(x * 8, y * 8, 7, 7);
                }
            }
            /* Hard edges on the exact pixel grid. */
            g.setColor(0xFFFFFF);
            g.drawLine(0, 0, w - 1, h - 1);
            g.drawLine(w - 1, 0, 0, h - 1);
            g.drawRect(2, 2, w - 5, h - 5);
            g.setColor(0xFF0000);
            g.fillRect(w / 2 - 4, h / 2 - 4, 8, 8);
            g.setColor(0x00FF00);
            g.fillRect(1, h - 10, 12, 8);
            g.setColor(0x0000FF);
            g.fillRect(w - 13, 1, 12, 8);
            g.setColor(0xFFFFFF);
            g.drawString("STATIC", w / 2, 4, Graphics.TOP | Graphics.HCENTER);
        }
    }

    private Screen screen;

    protected void startApp()
    {
        if (screen == null) {
            screen = new Screen();
        }
        Display.getDisplay(this).setCurrent(screen);
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean u) { }
}
