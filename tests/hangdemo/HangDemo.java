import javax.microedition.midlet.MIDlet;

/**
 * A MIDlet whose destroyApp() never returns - 红色警戒3_7037 in miniature.
 *
 * The run itself is fine: it paints, it spends its frame budget, it ends.  The
 * hang was always in teardown, and a teardown that never finishes is a process
 * that never exits, which is the one outcome a smoke test is not allowed to
 * accept.
 */
public class HangDemo extends MIDlet {
    protected void startApp() {
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
        /* Deliberately never returns.  The sleep is what 红色警戒3_7037 does:
         * a teardown that blocks rather than one that spins.  Both must be cut
         * short, and this is the shape that actually occurs. */
        while (true) {
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                /* keep going */
            }
        }
    }
}
