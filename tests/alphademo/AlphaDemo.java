import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

/**
 * The two promises a mutable Image makes, and what a title builds on them.
 *
 * MIDP 2.0 says of Image.createImage(int width, int height): "Every pixel
 * within the newly created image is white", and of the class: "Every pixel
 * within a mutable image is always fully opaque."  A title uses an off-screen
 * image exactly this way: create it, draw a scene into part of it, blit it -
 * and it relies on the parts it did not draw being white rather than seeing
 * through to whatever is behind them.
 *
 * The fixture prints one line per promise so the checker can name the one that
 * broke.
 */
public class AlphaDemo extends MIDlet {

    private static void say(String what, int argb) {
        System.out.println("ALPHA " + what + "=" + Integer.toHexString(argb));
    }

    private static int peek(Image img) {
        int[] px = new int[1];
        img.getRGB(px, 0, 1, 0, 0, 1, 1);
        return px[0];
    }

    protected void startApp() {
        /* 1. A fresh mutable image is white, and opaque. */
        Image m = Image.createImage(4, 4);
        say("fresh", peek(m));

        /* 2. Drawing a translucent image into a mutable image leaves the
         *    result fully opaque: a mutable image has no alpha channel to
         *    keep. */
        int[] semi = new int[] { 0x80FF0000, 0x80FF0000, 0x80FF0000, 0x80FF0000 };
        m.getGraphics().drawImage(Image.createRGBImage(semi, 2, 2, true), 0, 0,
                                  Graphics.TOP | Graphics.LEFT);
        say("drawn", peek(m));

        /* 3. A translucent SOURCE has to blend against what the mutable image
         *    already held - the white the spec promises - not against
         *    whatever happens to be behind the image on screen.  With white
         *    underneath and a 50% red on top the answer is a pink, and it must
         *    be opaque. */
        Image over = Image.createImage(2, 2);
        over.getGraphics().drawImage(Image.createRGBImage(semi, 2, 2, true), 0, 0,
                                     Graphics.TOP | Graphics.LEFT);
        say("blend", peek(over));

        /* 4. createRGBImage with processAlpha false ignores the alpha bytes. */
        int[] solid = new int[] { 0x0000FF00, 0x0000FF00, 0x0000FF00, 0x0000FF00 };
        say("rgbkeep", peek(Image.createRGBImage(solid, 2, 2, false)));

        /* 5. The immutable copy of a mutable image keeps what it held. */
        say("copy", peek(Image.createImage(m)));

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
