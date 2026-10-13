import java.io.UnsupportedEncodingException;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Encoding names are case-insensitive, and every spelling of the platform
 * encoding has to decode the same way.
 *
 * 疯狂战机_2013 called getBytes("utf-8") and the runtime answered
 * UnsupportedEncodingException, because the name table was matched with strcmp.
 * A scan of the collected titles found the same non-canonical spelling in 48
 * jars, so this is a class of failure and not one title's typo: the fixture
 * walks the spellings a title actually writes - "utf-8", "Utf-8", "utf8",
 * "iso-8859-1", "ISO8859_1", "Ascii" - and asserts they all agree with the
 * canonical name.
 *
 * Three things are checked, because passing one of them is easy and passing all
 * three is the actual requirement:
 *   1. every spelling is ACCEPTED (no UnsupportedEncodingException),
 *   2. every spelling produces the SAME bytes as the canonical name,
 *   3. those bytes round-trip back to the original string.
 * A table that accepts a spelling but maps it to the wrong decoder passes 1 and
 * fails 2; a decoder that writes the right length but the wrong continuation
 * bytes passes 2 and fails 3.
 *
 * The text is deliberately not ASCII-only in either direction: the UTF-8 group
 * uses characters outside Latin-1 so a Latin-1 decoder is visibly wrong, and the
 * refusal group asserts that a name the runtime does not implement still
 * REFUSES rather than quietly decoding as Latin-1.
 */
public class EncDemo extends MIDlet {
    /* h + e-acute + llo + space + two Han characters.  13 bytes as UTF-8
     * (1+2+3+1+3+3); the Han characters are what make a Latin-1 mis-decode
     * impossible to miss. */
    private static final String MIXED = "h\u00e9llo \u4e2d\u6587";
    /* The same shape, but representable in Latin-1, for the aliases of it. */
    private static final String LATIN = "h\u00e9llo w\u00f6rld";

    private static final String[] UTF8_NAMES = {
        "UTF-8", "utf-8", "Utf-8", "UTF8", "utf8", "Unicode-1-1-UTF-8"
    };
    private static final String[] LATIN_NAMES = {
        "ISO-8859-1", "iso-8859-1", "ISO8859_1", "ISO8859-1", "iso8859_1",
        "latin1", "Latin-1", "L1", "8859_1", "ASCII", "Ascii", "US-ASCII"
    };

    private static int accepted(String text, String name) {
        try {
            return text.getBytes(name).length;
        } catch (UnsupportedEncodingException e) {
            return -1;
        } catch (Throwable t) {
            return -2;
        }
    }

    /* Does the name produce the same bytes as `canonical`, and do those bytes
     * decode back to `text` through the same name? */
    private static boolean agrees(String text, String name, String canonical) {
        try {
            byte[] mine = text.getBytes(name);
            byte[] theirs = text.getBytes(canonical);
            if (mine.length != theirs.length) {
                return false;
            }
            for (int i = 0; i < mine.length; i++) {
                if (mine[i] != theirs[i]) {
                    return false;
                }
            }
            return text.equals(new String(mine, name))
                    && text.equals(new String(mine, canonical));
        } catch (Throwable t) {
            return false;
        }
    }

    protected void startApp() {
        try {
            int utf8Bytes = accepted(MIXED, "UTF-8");
            boolean utf8Same = true;
            for (int i = 0; i < UTF8_NAMES.length; i++) {
                if (accepted(MIXED, UTF8_NAMES[i]) != utf8Bytes
                        || !agrees(MIXED, UTF8_NAMES[i], "UTF-8")) {
                    utf8Same = false;
                }
            }

            int latinBytes = accepted(LATIN, "ISO-8859-1");
            boolean latinSame = true;
            for (int i = 0; i < LATIN_NAMES.length; i++) {
                if (accepted(LATIN, LATIN_NAMES[i]) != latinBytes
                        || !agrees(LATIN, LATIN_NAMES[i], "ISO-8859-1")) {
                    latinSame = false;
                }
            }

            /* The no-argument calls are the platform encoding, which is
             * ISO-8859-1 here: a title that omits the name must get the same
             * bytes as one that spells it out. */
            byte[] viaDefault = LATIN.getBytes();
            byte[] viaName = LATIN.getBytes("ISO-8859-1");
            boolean defaultIsLatin = viaDefault.length == viaName.length;
            for (int i = 0; defaultIsLatin && i < viaDefault.length; i++) {
                defaultIsLatin = viaDefault[i] == viaName[i];
            }
            boolean defaultRoundTrip = LATIN.equals(new String(viaDefault));

            /* An encoding this runtime does not implement still has to refuse.
             * The point of the table is that "I do not know this name" and "this
             * name means Latin-1" are different answers. */
            boolean refused = false;
            try {
                MIXED.getBytes("Shift_JIS");
            } catch (UnsupportedEncodingException e) {
                refused = true;
            }
            boolean constructorRefused = false;
            try {
                byte[] b = { (byte) 0x82, (byte) 0xA0 };
                String unused = new String(b, "Shift_JIS");
                if (unused == null) {
                    System.out.println("");
                }
            } catch (UnsupportedEncodingException e) {
                constructorRefused = true;
            }
            /* A null name is a NullPointerException, not the platform default. */
            boolean nullRefused = false;
            try {
                MIXED.getBytes((String) null);
            } catch (NullPointerException e) {
                nullRefused = true;
            }

            System.out.println("ENC utf8=" + utf8Bytes
                    + " utf8same=" + utf8Same
                    + " latin=" + latinBytes
                    + " latinsame=" + latinSame
                    + " default=" + defaultIsLatin
                    + " defaultround=" + defaultRoundTrip
                    + " refuse=" + refused
                    + " ctorrefuse=" + constructorRefused
                    + " nullrefuse=" + nullRefused);
        } catch (Throwable t) {
            System.out.println("ENC FAILED " + t);
        }
        Display.getDisplay(this).setCurrent(new V());
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class V extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0xFFFFFF);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0x000080);
        g.fillRect(0, 0, getWidth() / 2, getHeight() / 2);
    }
}
