import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.*;

/*
 * A MIDlet whose constructor BLOCKS.
 *
 * com.ea.sims3.MonkeyApp does exactly this in the corpus, and the failure used
 * to be reported as "cannot instantiate com.ea.sims3.MonkeyApp: ?" - the ?
 * being nj_ref_class_name() on a NULL exception, because a blocked call sets
 * no exception and the bridge cannot suspend a C caller.
 *
 * This fixture exists so that message cannot regress to a question mark.
 */
public class BlockDemo extends MIDlet
{
    public BlockDemo()
    {
        try {
            Thread.sleep(30);
        } catch (InterruptedException e) {
            /* not reached */
        }
    }

    protected void startApp() { }
    protected void pauseApp() { }
    protected void destroyApp(boolean u) { }
}
