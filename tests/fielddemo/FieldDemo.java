/*
 * FieldDemo - a static field that hides an instance field of the same name.
 *
 * This is the shape 疯狂农场2_2375 uses, and it took the title's screen away:
 * its class h declares `static Vector b` while its superclass j declares an
 * instance `Vector b`, so resolving the Fieldref `h.b:Ljava/util/Vector;` by
 * asking for an instance field first walked straight past h's own field,
 * found j's, and linked a getstatic/putstatic to an instance offset.  A
 * getstatic then read the field off a NULL receiver and a putstatic dropped
 * the value on the floor - both silently - so a Vector the constructor had
 * just stored read back as null and the next addElement() threw
 * NullPointerException somewhere else entirely.
 *
 * javac will not let one class declare two fields with the same name, so the
 * same-class half of that case (h also has `boolean b` and `int b` next to the
 * Vector) cannot be written here; the fixture covers the part that can be
 * expressed and that the runtime got wrong.
 *
 * The MIDlet reports what it sees instead of asserting, so the same jar can be
 * run anywhere and the check script owns the expectations.
 */
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;
import java.util.Vector;

class FieldParent {
    /* Instance field: same name, same descriptor as the child's static one. */
    Vector b = new Vector();
}

class FieldChild extends FieldParent {
    /* Static field hiding the parent's instance field. */
    static Vector b;

    static void seed()
    {
        b = new Vector();
        b.addElement("first");
    }

    static String peek()
    {
        if (b == null) {
            return "null";
        }
        return b.size() + ":" + (String) b.elementAt(0);
    }
}

public class FieldDemo extends MIDlet {
    public void startApp()
    {
        String before = FieldChild.peek();
        FieldChild.seed();
        String after = FieldChild.peek();

        /* A second round trip, to catch a read that only works because the
         * value happened to sit in the slot the first write went to. */
        FieldChild.b.addElement("second");
        String grown = FieldChild.peek();

        System.out.println("FIELD before=" + before + " after=" + after
                           + " grown=" + grown);

        Form f = new Form("FieldDemo");
        f.append("static field");
        Display.getDisplay(this).setCurrent(f);
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) { }
}
