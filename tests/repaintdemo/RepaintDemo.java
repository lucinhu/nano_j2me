import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * The MIDP idiom that paints through a field, and the two halves of the runtime
 * that have to cooperate for it to work.
 *
 * The worker keeps the idiom running for longer than the run lasts (the checker
 * stops it with --run-frames), so every frame the runtime presents comes from
 * the application's own paint().
 *
 * A title that renders from paint() usually publishes the state to draw in a
 * field, asks for a repaint, forces it, and clears the field again:
 *
 *     state = 1; canvas.repaint(); canvas.serviceRepaints(); state = 0;
 *
 * The clearing is deliberate: outside that window there is nothing to draw, and
 * paint() returns after clearing the screen.  Two things must therefore hold:
 *
 *   - repaint() with no arguments must request a repaint of the whole canvas.
 *     Routed through the four-argument form it read x/y/w/h out of argument
 *     slots that repaint() does not have, saw a width of zero, and asked for
 *     nothing - so serviceRepaints() returned immediately and the worker thread
 *     below never painted at all.
 *
 *   - the runner must not paint again in a frame the application has already
 *     painted.  Its own per-frame paint would arrive with the field back at 0
 *     and wipe the screen the title had just drawn (仙魔飞行棋_417 does exactly
 *     this with mobile/k.f, and its map stayed black for the whole run).
 *
 * The checker reads the frames the run presents and requires the last one to
 * still hold the red block this paint() draws inside the window.
 */
public class RepaintDemo extends MIDlet {

    static DemoCanvas canvas;
    static volatile int state;
    static volatile int iterations;

    public void startApp() {
        canvas = new DemoCanvas();
        Display.getDisplay(this).setCurrent(canvas);

        Thread worker = new Thread(new Runnable() {
            public void run() {
                /*
                 * Keep painting for longer than the run lasts: the point of
                 * the fixture is that every presented frame comes from the
                 * application's own paint, so the last one is still red when
                 * the frame budget ends.
                 */
                for (int i = 0; i < 300; i++) {
                    state = 1;
                    canvas.repaint();
                    canvas.serviceRepaints();
                    state = 0;
                    iterations = i + 1;
                    System.out.println("REPAINT loop=" + iterations
                                       + " paints=" + DemoCanvas.paints);
                    try {
                        Thread.sleep(20);
                    } catch (InterruptedException e) {
                        break;
                    }
                }
            }
        });
        worker.start();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }

    /**
     * Clears to black, then draws a red block only while `state` says so.  The
     * clear is what makes a stray second paint visible: it leaves a black frame.
     */
    static class DemoCanvas extends Canvas {

        static int paints;

        protected void paint(Graphics g) {
            paints++;
            int w = getWidth();
            int h = getHeight();
            g.setColor(0x000000);
            g.fillRect(0, 0, w, h);
            if (state == 0) {
                return;
            }
            g.setColor(0xFF0000);
            g.fillRect(4, 4, w - 8, h - 8);
        }
    }
}
