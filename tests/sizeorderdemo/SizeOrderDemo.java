import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Reproduces the sizeChanged() timing race NeonRunner hit.
 *
 * The game thread stores its boot renderer into a field; sizeChanged(), when
 * it runs with the boot width after that store, "restores" the field from a
 * backup that is only set later (null here).  A runtime that delivers the
 * very first sizeChanged() lazily on the first paint - i.e. after startApp()
 * already started the game thread - therefore leaves the renderer null and
 * every paint throws.  Delivering the first sizeChanged() while the MIDlet
 * thread is still inside setCurrent() leaves the field null first and lets the
 * game thread's store win.
 */
public class SizeOrderDemo extends MIDlet {
    private TestCanvas canvas;

    protected void startApp() {
        canvas = new TestCanvas();
        Display.getDisplay(this).setCurrent(canvas);
        new Thread(canvas).start();
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }

    static final class Renderer {
        int kind;
        Renderer(int kind) { this.kind = kind; }
    }

    static final class TestCanvas extends Canvas implements Runnable {
        private Renderer boot;
        private Renderer backup;
        private boolean printedPaint;
        private boolean printedSize;

        public void run() {
            boot = new Renderer(1);
            System.out.println("SIZEORDER thread boot=" + (boot != null));
            for (int i = 0; i < 30; i++) {
                repaint();
                try {
                    Thread.sleep(50);
                } catch (InterruptedException e) {
                    // ignore
                }
            }
        }

        protected void sizeChanged(int w, int h) {
            if (!printedSize) {
                printedSize = true;
                System.out.println("SIZEORDER sizeChanged " + w + "x" + h
                        + " boot=" + (boot != null));
            }
            if (w == 240) {
                boot = backup;
            }
        }

        protected void paint(Graphics g) {
            if (boot == null) {
                throw new NullPointerException("renderer");
            }
            g.setColor(0x00FF00);
            g.fillRect(0, 0, getWidth(), getHeight());
            if (!printedPaint) {
                printedPaint = true;
                System.out.println("SIZEORDER paint renderer=" + boot.kind);
            }
        }
    }
}
