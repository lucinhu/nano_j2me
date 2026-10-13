import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.midlet.MIDlet;

/**
 * The clip is a rectangle of pixels, and moving the coordinate system does not
 * move it.
 *
 * MIDP 2.0 says so where a reader would not look for it - in
 * Graphics.translate(): "Operations on the coordinate system, such as
 * translate(), do not modify the clip.  The methods getClipX(), getClipY(),
 * getClipWidth() and getClipHeight() must return a rectangle that, if passed to
 * setClip without an intervening change to the Graphics object's coordinate
 * system, must result in the identical set of pixels in the clip."
 *
 * The two orders are therefore NOT the same thing, and a runtime that makes
 * them the same is wrong in a way that costs a whole game screen:
 * 帝国时代3-亚洲王朝_6288 draws its campaign map by translating the origin by
 * the camera offset (-336,-216) and then blitting tiles at map coordinates.
 * Every tile landed on the screen, but the clip had been dragged off it, so the
 * map was clipped away and the campaign showed a black field with only the
 * untranslated HUD on it.
 *
 * Four things are checked, because any one of them alone can pass by accident:
 *   a. setClip() then translate() - the square drawn at the new origin is
 *      discarded, because the clip stayed at the old one;
 *   b. translate() then setClip() - the clip lands in the CURRENT system and
 *      confines the square to (8..15, 8..15);
 *   c. an empty clip discards the drawing and keeps what was underneath;
 *   d. the numbers getClipX/Y/Width/Height report, fed straight back into
 *      setClip() with the coordinate system unchanged, select the same pixels -
 *      which is what makes them "relative to the origin" rather than absolute.
 */
public class ClipDemo extends MIDlet {
    private static final int RED = 0x00FF0000;
    private static final int GREEN = 0x0000FF00;

    private static int px(Image im, int x, int y) {
        int[] a = new int[1];
        im.getRGB(a, 0, 1, x, y, 1, 1);
        return a[0] & 0x00FFFFFF;
    }

    private static Image redImage() {
        Image im = Image.createImage(32, 32);
        Graphics g = im.getGraphics();
        g.setColor(RED);
        g.fillRect(0, 0, 32, 32);
        return im;
    }

    private static String check() {
        boolean a, b, c, d;
        int cx, cw, tx;

        /* a. setClip() first: translate() must leave the clip alone. */
        Image one = redImage();
        Graphics g = one.getGraphics();
        g.setClip(0, 0, 8, 8);
        g.translate(8, 8);
        g.setColor(GREEN);
        g.fillRect(0, 0, 32, 32);
        a = px(one, 8, 8) == RED && px(one, 15, 15) == RED;

        /* The clip, read back through the moved origin, is where it was: device
         * (0,0) is (-8,-8) in the current coordinate system. */
        cx = g.getClipX();
        cw = g.getClipWidth();
        tx = g.getTranslateX();

        /* d. Feeding those numbers back, with the coordinate system unchanged,
         * must select the identical set of pixels - still nothing drawn. */
        g.setClip(cx, g.getClipY(), cw, g.getClipHeight());
        g.setColor(GREEN);
        g.fillRect(0, 0, 32, 32);
        d = px(one, 8, 8) == RED;

        /* b. translate() first: setClip() takes coordinates in the current
         * system, so this time the square is confined to device (8..15). */
        Image two = redImage();
        Graphics h = two.getGraphics();
        h.translate(8, 8);
        h.setClip(0, 0, 8, 8);
        h.setColor(GREEN);
        h.fillRect(0, 0, 32, 32);
        b = px(two, 8, 8) == GREEN && px(two, 15, 15) == GREEN
                && px(two, 7, 7) == RED && px(two, 16, 16) == RED;

        /* c. An empty clip discards the drawing. */
        Image three = redImage();
        Graphics k = three.getGraphics();
        k.setClip(0, 0, 0, 0);
        k.setColor(GREEN);
        k.fillRect(0, 0, 32, 32);
        c = px(three, 0, 0) == RED;

        return "a=" + a + " b=" + b + " c=" + c + " d=" + d
                + " cx=" + cx + " cw=" + cw + " tx=" + tx;
    }

    public void startApp() {
        String line;
        try {
            line = check();
        } catch (Throwable t) {
            line = "FAILED " + t;
        }
        System.out.println("CLIP " + line);
        /* A MIDlet that installs no Displayable is not a title the runner can
         * show, and the run would exit non-zero for that reason alone. */
        Display.getDisplay(this).setCurrent(new V());
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}

class V extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0xFFFFFF);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
