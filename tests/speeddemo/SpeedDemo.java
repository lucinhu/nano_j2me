import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

/**
 * How fast the MIDlet's own clock runs (tests/speeddemo/SpeedDemo.java).
 *
 * A port's speed control scales the one clock a MIDlet can read, so the MIDlet
 * itself cannot show the change: inside the VM, "3 seconds of sleep" measures
 * as 3 seconds whether the game is running at half speed or at three times it.
 * The honest proof is outside - how much WALL time those 3 seconds took - so
 * this fixture sleeps a fixed, known amount of MIDlet time (3 x 1000 ms),
 * prints what its own clock measured (which must not move), and exits so the
 * caller can time the process:
 *
 *   at 100%:  ~3 s of wall clock, the same as with no speed control at all
 *   at 300%:  ~1 s
 *   at  50%:  ~6 s
 *
 * The sleeps are a second each rather than thirty of 100 ms because a parked
 * thread is only resumed at a frame boundary: many short sleeps each pile up
 * most of a frame of MIDlet time as overhead and blur the wall-clock ratio.
 */
public class SpeedDemo extends MIDlet {

    private static final int STEPS = 3;
    private static final int STEP_MS = 1000;

    protected void startApp() {
        long t0 = System.currentTimeMillis();
        for (int i = 0; i < STEPS; i++) {
            try {
                Thread.sleep(STEP_MS);
            } catch (InterruptedException e) {
                /* not reached: nothing interrupts a MIDlet thread here */
            }
        }
        long t1 = System.currentTimeMillis();
        System.out.println("SPEED slept " + (STEPS * STEP_MS)
                           + " ms of MIDlet time, its own clock measured "
                           + (t1 - t0) + " ms");

        Display.getDisplay(this).setCurrent(new Canvas() {
            protected void paint(Graphics g) {
                g.setColor(0x000000);
                g.fillRect(0, 0, getWidth(), getHeight());
            }
        });
        notifyDestroyed();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
