import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

/**
 * Two promises about an array that has no class, and the empty string.
 *
 * 1. An array the RUNTIME created - String.getBytes() hands out a byte[] that
 *    was never given a class - is still an Object, so the methods of
 *    java/lang/Object are the ones it has: a phone answers
 *    getBytes().toString() with "[B@1a2b3c".  The emulator used to answer
 *    that question by dereferencing the missing class: the UC browser title
 *    calls String.length() on an int[] and then on a byte[] that
 *    String.getBytes() had just made, and the second call killed the
 *    emulator with SIGSEGV while it was building the message "X does not
 *    implement Y".  A phone raises no exception at all here, so the emulator
 *    must not crash either.
 *
 * 2. String.startsWith / endsWith / equals with an EMPTY argument are legal
 *    and have a defined answer: every string starts and ends with the empty
 *    string, and two empty strings are equal.  The runtime stores an empty
 *    string as a NULL payload, and the C standard does not allow that
 *    pointer to be handed to memcmp even for a length of zero - so the
 *    comparison has to be answered before the comparison is attempted.
 *
 * The fixture prints one line per promise so the checker can name the one that
 * broke.
 */
public class ArrClsDemo extends MIDlet {

    private static void say(String what, String value) {
        System.out.println("ARRCLS " + what + "=" + value);
    }

    private static void sayBool(String what, boolean value) {
        say(what, value ? "true" : "false");
    }

    /**
     * Call an instance method on an array and report what came back.
     *
     * The call is made through Object.toString(), which no array has: the
     * point is the ANSWER, not the text.
     */
    private static void tryToString(String tag, byte[] raw) {
        try {
            String s = raw.toString();
            say(tag + ".threw", "none");
            say(tag + ".text", s == null ? "null" : s);
        } catch (Throwable t) {
            say(tag + ".threw", t.getClass().getName());
            say(tag + ".msg", t.getMessage() == null ? "" : t.getMessage());
        }
    }

    protected void startApp() {
        /* 1. An array the MIDlet allocated has a class, so the answer was
         *    always a NoSuchMethodError naming "[B". */
        tryToString("heap", new byte[3]);

        /* 2. An array the RUNTIME allocated has no class at all.  The same
         *    question, the same answer - and no crash. */
        tryToString("native", "abc".getBytes());

        /* 3. The empty string. */
        sayBool("sw.empty", "abc".startsWith(""));
        sayBool("sw.hit", "abc".startsWith("ab"));
        sayBool("sw.miss", "abc".startsWith("x"));
        sayBool("ew.empty", "abc".endsWith(""));
        sayBool("ew.hit", "abc".endsWith("bc"));
        sayBool("ew.miss", "abc".endsWith("x"));
        sayBool("eq.empty", "".equals(""));
        sayBool("eq.hit", "abc".equals("abc"));
        sayBool("eq.miss", "abc".equals("abd"));

        /* 4. An argument that is not a String at all.  The compiler only lets
         *    a non-String through a cast, and the cast has to be checked: the
         *    answer is a ClassCastException, not a crash and not a silent
         *    comparison against nothing. */
        try {
            String bogus = (String)(Object)new int[1];
            sayBool("sw.nonstring", bogus.startsWith("a"));
        } catch (Throwable t) {
            say("sw.nonstring", "threw " + t.getClass().getName());
        }

        /* 5. Leave the way a demo leaves: a displayable and a clean exit, so
         *    the runner's exit code is about the promises above and nothing
         *    else. */
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
