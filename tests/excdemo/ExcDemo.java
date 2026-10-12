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
 * Package-private on purpose: Class.newInstance() is specified to refuse it
 * with IllegalAccessException, and the message must name the class or the
 * report is another "something went wrong somewhere".
 */
class ExcHidden {
    public ExcHidden() { }
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
            Class.forName("ExcHidden").newInstance();
        } catch (Exception e) {
            System.out.println("ACCESS " + e.getClass().getName() + ": " + e.getMessage());
        }
        notifyDestroyed();
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) { }
}
