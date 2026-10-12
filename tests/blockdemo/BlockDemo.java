import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.*;

/*
 * A MIDlet whose constructor SLEEPS - a splash delay, which is common.
 *
 * com.ea.sims3.MonkeyApp and engine.midlet.nokia7650.Start both do this in the
 * corpus, and both used to fail to start at all, reported as "construction
 * blocked (it called Thread.sleep or wait, and a C caller cannot be
 * suspended)".  That was only half true: a C caller cannot be suspended, but
 * it can wait for the thread to be runnable again and resume the frame.
 *
 * The sleep is deliberately long enough to measure (300 ms) so the test can
 * tell "the runtime waited" apart from "the runtime skipped the sleep".
 */
public class BlockDemo extends MIDlet
{
    public BlockDemo()
    {
        try {
            Thread.sleep(300);
        } catch (InterruptedException e) {
            /* not reached */
        }
    }

    protected void startApp() { }
    protected void pauseApp() { }
    protected void destroyApp(boolean u) { }
}
