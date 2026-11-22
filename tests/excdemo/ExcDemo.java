import javax.microedition.midlet.MIDlet;

/*
 * A MIDlet that throws, catches, and prints the trace.
 *
 * printStackTrace() used to print only the exception's class and message, so
 * a title that threw every frame said "exception: java.lang.NullPointerException"
 * and nothing else - no method, no line, no way to tell which call failed.
 * The corpus had three such titles and the output was useless for all of them.
 */
/*
 * Same package as the demo, package-private on purpose: the JVM lets code in
 * the same package instantiate it through Class.newInstance(), and handsets
 * rely on that (3D_Tetris does exactly this).  This is the positive half of
 * the accessibility test; hidden.ExcHidden is the negative half.
 */
class ExcSamePkg {
    public ExcSamePkg() { }
}

public class ExcDemo extends MIDlet {
    private void boom() {
        Object o = null;
        o.toString();               /* NullPointerException, thrown right here */
    }

    public void startApp() {
        try {
            boom();
        } catch (Exception e) {
            e.printStackTrace();
        }
        try {
            Object o = Class.forName("ExcSamePkg").newInstance();
            System.out.println("PACKAGE same-package ok " + (o != null));
        } catch (Exception e) {
            System.out.println("PACKAGE same-package FAILED " + e);
        }
        try {
            Class.forName("hidden.ExcHidden").newInstance();
        } catch (Exception e) {
            System.out.println("ACCESS " + e.getClass().getName() + ": " + e.getMessage());
        }
        notifyDestroyed();
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) { }
}
