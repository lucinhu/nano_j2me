/*
 * Class.forName(String) must initialise the class (JVMS 5.5 / CLDC
 * java.lang.Class contract).  GoldenSolitaire's resource manager calls
 * Class.forName("p") purely for that side effect: the interface's static
 * fields allocate handles that size the archive's object table.  When the
 * initialiser was skipped, the tables were sized without those slots and the
 * first indexed store in the game's resource manager threw
 * ArrayIndexOutOfBoundsException on every paint.
 *
 * The fixture prints the order of events, so the gate can assert that
 * InitProbe's static initialiser ran during Class.forName() and not later
 * when the field is first read.
 */
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class ClassInitDemo extends MIDlet implements Runnable {
    private TestCanvas canvas;

    public void startApp() {
        System.out.println("CLASSINIT before");
        try {
            Class c = Class.forName("InitProbe");
            System.out.println("CLASSINIT forName returned=" + (c != null));
        } catch (ClassNotFoundException e) {
            System.out.println("CLASSINIT failed=" + e);
        }
        System.out.println("CLASSINIT value=" + InitProbe.VALUE);
        canvas = new TestCanvas();
        Display.getDisplay(this).setCurrent(canvas);
        new Thread(this).start();
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) { }

    public void run() {
        try {
            Thread.sleep(400);
        } catch (InterruptedException ignored) {
        }
        notifyDestroyed();
    }

    private final class TestCanvas extends Canvas implements Runnable {
        TestCanvas() {
            new Thread(this).start();
        }

        public void run() {
            for (int i = 0; i < 5; ++i) {
                repaint();
                serviceRepaints();
                try {
                    Thread.sleep(60);
                } catch (InterruptedException ignored) {
                }
            }
        }

        protected void paint(Graphics g) {
            g.setColor(0x0000FF);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }
}
