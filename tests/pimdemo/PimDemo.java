import java.util.Enumeration;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;
import javax.microedition.pim.Contact;
import javax.microedition.pim.ContactList;
import javax.microedition.pim.PIM;
import javax.microedition.pim.PIMException;

/**
 * JSR 75's phone book, as one line a shell script can read.
 *
 * 3D孤胆枪手2010_4634 opens the contact list and walks it:
 *
 *     PIM.getInstance().openPIMList(PIM.CONTACT_LIST, PIM.READ_ONLY)
 *         -> checkcast ContactList -> isSupportedField() / items() / close()
 *
 * and a fresh phone answers that with an empty list, not with a link error.
 * This fixture performs exactly that sequence so the runtime's answer is
 * testable: the list opens, no field is supported, items() is a real (empty)
 * Enumeration, close() returns, and the list types this build has no store for
 * raise PIMException.  An unknown mode raises IllegalArgumentException.
 *
 * Prints exactly one line:
 *
 *   PIM opened=true supported=false items=0 todo=PIMException mode=IAE
 *       names=1 closed=true
 *
 * or `PIM ERROR <class>: <message>`.
 */
public class PimDemo extends MIDlet {

    protected void startApp() {
        String line;
        Display.getDisplay(this).setCurrent(new Backdrop());
        try {
            line = run();
        } catch (Throwable t) {
            line = "PIM ERROR " + t.getClass().getName() + ": " + t.getMessage();
        }
        System.out.println(line);
        notifyDestroyed();
    }

    private static String run() throws Exception {
        PIM pim = PIM.getInstance();
        ContactList list = null;
        try {
            list = (ContactList) pim.openPIMList(PIM.CONTACT_LIST, PIM.READ_ONLY);
        } catch (PIMException e) {
            list = null;
        }

        boolean supported = list != null && list.isSupportedField(0);
        int items = 0;
        boolean closed = false;
        if (list != null) {
            Enumeration e = list.items();
            while (e.hasMoreElements()) {
                Contact c = (Contact) e.nextElement();
                items += c.countValues(0);
            }
            list.close();
            closed = true;
        }

        String todo;
        try {
            pim.openPIMList(PIM.TODO_LIST, PIM.READ_ONLY);
            todo = "opened";
        } catch (PIMException e) {
            todo = "PIMException";
        }

        String mode;
        try {
            pim.openPIMList(PIM.CONTACT_LIST, 9);
            mode = "opened";
        } catch (IllegalArgumentException e) {
            mode = "IAE";
        }

        String[] names = pim.listPIMLists(PIM.CONTACT_LIST);

        return "PIM opened=" + (list != null)
                + " supported=" + supported
                + " items=" + items
                + " todo=" + todo
                + " mode=" + mode
                + " names=" + (names == null ? -1 : names.length)
                + " closed=" + closed;
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class Backdrop extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0x000000);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
