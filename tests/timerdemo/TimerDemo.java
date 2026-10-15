/*
 * A java.util.Timer task that parks must not take the MIDlet down with it.
 *
 * The MIDP timer runs its tasks on a thread whose loop calls a NATIVE
 * (nj2me/util/TimerThread.pump), and that native is what calls the task's
 * run().  When the task parks there, the thread is blocked inside a C call:
 * the top frame on its stack is the native, and a native frame has no
 * bytecode at all.  The scheduler used to hand that frame to the interpreter
 * anyway, and five collected titles died with
 *
 *   VerifyError: code runs past the end of the method
 *   at nj2me/util/TimerThread.pump pc=0
 *
 * The doorbell thread is what makes that reproducible: it is created BEFORE
 * the Timer, so a scheduler pass meets it first and it is still the timer
 * thread's turn later in the same pass, and it keeps notifying LOCK for as
 * long as the task is parked.
 *
 *   TIMER painted=5 ticks=<n>    the canvas is live (printed from paint())
 *   TIMER task done ticks=<n>    the parked task resumed and returned
 *
 * The second line is the important one: a runtime that abandons the parked
 * frame, or that resumes it as bytecode, never reaches it.
 */
import java.util.Timer;
import java.util.TimerTask;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public class TimerDemo extends MIDlet {
    static int paints;
    static int ticks;
    static boolean taskParked;
    static boolean taskDone;
    static final Object LOCK = new Object();

    private Doorbell bell;
    private Timer timer;
    private DemoCanvas canvas;

    /* Started before the Timer on purpose: the thread order is half of what
     * makes the old bug fire, the other half being the repeated notify. */
    static class Doorbell extends Thread {
        public void run() {
            /* Keep notifying for the whole capture. Returning after the first
             * callback leaves its next periodic invocation without a notifier. */
            for (int i = 0; i < 5000 && paints < 30; i++) {
                synchronized (LOCK) {
                    if (taskParked) {
                        LOCK.notify();
                    }
                }
                try {
                    Thread.sleep(2);
                } catch (InterruptedException e) {
                }
            }
        }
    }

    static class Ticker extends TimerTask {
        public void run() {
            ticks++;
            synchronized (LOCK) {
                taskParked = true;
                try {
                    LOCK.wait();
                } catch (InterruptedException e) {
                }
                taskParked = false;
            }
            taskDone = true;
            System.out.println("TIMER task done ticks=" + ticks);
        }
    }

    static class DemoCanvas extends Canvas {
        protected void paint(Graphics g) {
            paints++;
            g.setColor(0x204060);
            g.fillRect(0, 0, getWidth(), getHeight());
            if (paints == 5) {
                System.out.println("TIMER painted=5 ticks=" + ticks);
            }
        }
    }

    public TimerDemo() {
        bell = new Doorbell();
        bell.start();
        timer = new Timer();
        timer.schedule(new Ticker(), 0, 20);
    }

    public void startApp() {
        if (canvas == null) {
            canvas = new DemoCanvas();
            Display.getDisplay(this).setCurrent(canvas);
        }
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
        if (timer != null) {
            timer.cancel();
        }
    }
}
