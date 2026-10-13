import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/*
 * What a boxed value says when a title puts it in a string, and what equality
 * means for the wrappers.
 *
 * A wrapper class that forgets to override toString() is not a compile error
 * and not a link error: the call resolves up to Object.toString() and answers
 * "java.lang.Integer@1f2a3b".  模拟人生2_10762 builds its content path as
 * "/global_data/" + <content id>, so it asked the JAR for
 * "global_data/java.lang.Integer@c1803e7", threw "Failed to locate content:
 * 34795" into its own catch-all handler, and its game thread returned before
 * the first frame - a MIDlet that looked alive with a black screen.  The same
 * gap made every Hashtable keyed on a Long, Short, Character or Boolean
 * compare identities instead of values.
 *
 * The fixture prints one line; tools/check_boxing.sh asserts it field by field.
 */
public class BoxDemo extends MIDlet
{
    /* A MIDlet that never installs a Displayable is an error for the runner,
     * so this fixture installs a blank one - the point of the test is what the
     * wrapper prints, not what is on the screen. */
    static final class Blank extends Canvas
    {
        protected void paint(Graphics g)
        {
            g.setColor(0);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    protected void startApp()
    {
        String line;
        try {
            String path = "/global_data/" + new Integer(34795);
            line = "BOX path=" + path
                 + " long=" + new Long(4294967296L)
                 + " short=" + new Short((short) -12)
                 + " byte=" + new Byte((byte) 7)
                 + " char=" + new Character('A')
                 + " bool=" + new Boolean(true) + "," + new Boolean(false);
        } catch (Throwable t) {
            line = "BOX FAILED " + t;
        }
        System.out.println(line);

        String eq = "?";
        try {
            eq = (new Long(5L).equals(new Long(5L))) + ","
               + (new Long(5L).equals(new Long(6L))) + ","
               + (new Short((short) 5).equals(new Short((short) 5))) + ","
               + (new Character('A').equals(new Character('A'))) + ","
               + (new Boolean(true).equals(new Boolean(true))) + ","
               + (new Integer(5).equals(new Long(5L)));
        } catch (Throwable t) {
            eq = "FAILED " + t;
        }
        System.out.println("BOX eq=" + eq);

        String hash = "?";
        try {
            hash = new Long(4294967296L).hashCode() + ","
                 + new Short((short) -12).hashCode() + ","
                 + new Boolean(true).hashCode() + ","
                 + new Character('A').hashCode();
        } catch (Throwable t) {
            hash = "FAILED " + t;
        }
        System.out.println("BOX hash=" + hash);

        String fd = "?";
        try {
            fd = 1.5f + "," + 2.0f
               + "," + new Float(0.1f).equals(new Float(0.1f))
               + "," + new Float(0.0f).equals(new Float(-0.0f))
               + "," + new Float(1.5f).hashCode()
               + "," + 2.5d + "," + new Double(2.5d).hashCode();
        } catch (Throwable t) {
            fd = "FAILED " + t;
        }
        System.out.println("BOX f=" + fd);

        Display.getDisplay(this).setCurrent(new Blank());
        notifyDestroyed();
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }
}
