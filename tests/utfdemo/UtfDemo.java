import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Both readUTF() forms have to decode the same bytes the same way.
 *
 * DataOutputStream.writeUTF writes modified UTF-8: a length, then one, two or
 * three bytes per character.  The instance form was implemented against that;
 * the static DataInputStream.readUTF(DataInput) form was missing entirely until
 * 世界冠军拉力赛3D_8360 called it.  The two share one decoder, and this fixture
 * is what says so - a static form that read the length differently, or counted
 * continuation bytes differently, would disagree here with the instance form.
 *
 * The text is deliberately not ASCII-only: a decoder that only ever sees single
 * bytes would pass an ASCII-only fixture.  The non-ASCII characters are written
 * as \\uXXXX escapes and the result is checked by code point, because javac reads
 * a source file in the platform charset - a literal é compiled on a machine
 * whose default is not UTF-8 becomes '?' before any of this runs, and the
 * fixture then tests nothing while reporting success.
 */
public class UtfDemo extends MIDlet {
    protected void startApp() {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            String text = "h\u00e9llo w\u00f6rld 1234567890";
            out.writeUTF(text);
            out.flush();
            byte[] data = bytes.toByteArray();

            DataInputStream one = new DataInputStream(new ByteArrayInputStream(data));
            String viaInstance = one.readUTF();

            DataInputStream two = new DataInputStream(new ByteArrayInputStream(data));
            String viaStatic = DataInputStream.readUTF(two);

            System.out.println("UTF bytes=" + data.length
                    + " instance=" + viaInstance
                    + " static=" + viaStatic
                    + " same=" + viaInstance.equals(viaStatic)
                    + " matches=" + text.equals(viaStatic)
                    /* The code point, not the glyph: whether this machine's
                     * console can print U+00E9 says nothing about whether the
                     * decoder produced it. */
                    + " len=" + viaStatic.length()
                    + " code=" + (int) viaStatic.charAt(1));
        } catch (Throwable t) {
            System.out.println("UTF FAILED " + t);
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
