import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * A MIDlet whose constructor parks on another thread, which is how a loading
 * screen is written.
 *
 * The runtime calls a MIDlet's constructor from C (the AMS side), so while the
 * constructor waits the C caller has to keep the VM moving: the thread that
 * will wake it is a Java thread, and if nobody runs it the wait can never end.
 * 毁灭战士3_10598 died of exactly this - "construction blocked (it called
 * Thread.sleep or wait, and a C caller cannot be suspended)" - because the
 * pump only handled waits that carried a deadline.
 *
 * The fixture is deliberately tiny and reports what happened:
 *
 *   WAIT ready=true paints=0     printed by startApp(): the wait ended
 *   WAIT painted=5 ready=true    printed by paint(): the canvas is live
 *
 * tools/check_wait_pump.sh asserts both lines plus a non-empty frame count, so
 * a runtime that abandons the parked constructor, or that presents nothing
 * afterwards, fails the check instead of passing it quietly.
 */
public class WaitDemo extends MIDlet implements Runnable {
    static final Object LOCK = new Object();
    static boolean ready;
    static int paints;

    public WaitDemo() {
        /*
         * Start the worker first: it is the only thing that can wake this
         * thread, so a runtime that does not let it run parks here forever.
         */
        Thread worker = new Thread(this);
        worker.start();
        synchronized (LOCK) {
            while (!ready) {
                try {
                    LOCK.wait();
                } catch (InterruptedException e) {
                    /* A spurious wake-up just means going around again. */
                }
            }
        }
    }

    public void run() {
        String delay = System.getProperty("fixture.wait.ms");
        if (delay != null) {
            try {
                Thread.sleep(Integer.parseInt(delay));
            } catch (InterruptedException e) {
            }
        }
        synchronized (LOCK) {
            ready = true;
            LOCK.notifyAll();
        }
    }

    protected void startApp() {
        System.out.println("WAIT ready=" + ready + " paints=" + paints);
        Display display = Display.getDisplay(this);
        display.setCurrent(new WaitCanvas());
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class WaitCanvas extends Canvas {
    protected void paint(Graphics g) {
        WaitDemo.paints++;
        if (WaitDemo.paints == 5) {
            System.out.println("WAIT painted=5 ready=" + WaitDemo.ready);
        }
        g.setColor(0x2244AA);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
