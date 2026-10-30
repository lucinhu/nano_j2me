import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/*
 * Two threads run the same critical section and deliberately yield the CPU
 * inside it, which is exactly what a cooperative scheduler must not let a
 * second thread exploit.
 *
 * The runtime used to treat synchronized as bookkeeping: monitorenter was
 * recorded on the frame and nothing ever blocked, on the theory that a
 * thread is never preempted inside a critical section.  The quantum runs out
 * there all the same, and this fixture reported
 *
 *   LOCK counter=400 overlap=true
 *
 * against that runtime - both workers inside the same synchronized block.
 * With a real monitor the exclusivity flag never trips and the count is
 * exact.  tools/check_lockdemo.sh holds the expectations.
 */
public class LockDemo extends MIDlet {
    static final Object LOCK = new Object();
    static int counter;
    static boolean inside;
    static boolean overlap;
    static int finished;

    static class Worker extends Thread {
        public void run() {
            for (int i = 0; i < 200; i++) {
                synchronized (LOCK) {
                    if (inside) {
                        overlap = true;
                    }
                    inside = true;
                    /* Give the scheduler every chance to switch threads while
                     * the monitor is held. */
                    for (int j = 0; j < 40; j++) {
                        Thread.yield();
                    }
                    counter++;
                    inside = false;
                }
            }
            synchronized (LOCK) {
                finished++;
                LOCK.notifyAll();
            }
        }
    }

    static class Screen extends Canvas {
        protected void paint(Graphics g) {
            g.setColor(0xFFFFFF);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    protected void startApp() {
        System.out.println("LOCK start");
        Display.getDisplay(this).setCurrent(new Screen());
        new Worker().start();
        new Worker().start();
        /*
         * The constructor-thread wait: notifyAll() wakes it, but it must not
         * run the code after wait() until it owns the monitor again.
         */
        synchronized (LOCK) {
            while (finished < 2) {
                try {
                    LOCK.wait();
                } catch (InterruptedException ie) {
                    /* keep waiting */
                }
            }
        }
        System.out.println("LOCK counter=" + counter + " overlap=" + overlap);
        notifyDestroyed();
    }

    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}
}
