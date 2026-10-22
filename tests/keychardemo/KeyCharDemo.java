import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;

/**
 * What Displayable.getKeyChar() answers.
 *
 * MIDP 2.0 puts getKeyChar() on Displayable: "Gets the character representing
 * the key that was pressed."  A Canvas-based title that draws its own editor
 * asks for it in keyPressed(), because the key code alone only says which
 * button was hit - the character is what the user actually typed.
 *
 * The runtime had no getKeyChar() at all, so a title that called it got a
 * NoSuchMethodError on the first key press.  The fixture prints one line per
 * press so the checker can name the key that was mapped wrongly.
 */
public class KeyCharDemo extends MIDlet {

    protected void startApp() {
        Canvas c = new Canvas() {
            protected void keyPressed(int keyCode) {
                int ch;
                try {
                    ch = getKeyChar();
                } catch (Throwable t) {
                    System.out.println("KEYCHAR code=" + keyCode + " threw "
                                       + t.getClass().getName());
                    return;
                }
                System.out.println("KEYCHAR code=" + keyCode + " char=" + ch);
            }

            protected void paint(Graphics g) {
                g.setColor(0xFFFFFF);
                g.fillRect(0, 0, getWidth(), getHeight());
            }
        };
        Display.getDisplay(this).setCurrent(c);
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
