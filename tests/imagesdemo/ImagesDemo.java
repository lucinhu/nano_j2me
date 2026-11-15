import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.midlet.MIDlet;

/**
 * Image-decoder fixture: decodes the checked-in JPEG and GIF test assets and
 * reports their sizes on the console, then paints a marker canvas.
 *
 * The point of this fixture is the console lines, not the screenshot: the
 * media fuzzer mutates res/sample.jpg or res/sample.gif and expects either a
 * caught exception (decoder refused the damage) or a decoded image, never a
 * crash or a hang.
 */
public class ImagesDemo extends MIDlet {
    protected void startApp() {
        try {
            Image jpeg = Image.createImage("/res/sample.jpg");
            System.out.println("IMAGE jpeg " + jpeg.getWidth() + "x" + jpeg.getHeight());
        } catch (Throwable t) {
            System.out.println("IMAGE jpeg refused: " + t);
        }
        try {
            byte[] data = readAll("/res/sample.gif");
            Image gif = Image.createImage(data, 0, data.length);
            System.out.println("IMAGE gif " + gif.getWidth() + "x" + gif.getHeight());
        } catch (Throwable t) {
            System.out.println("IMAGE gif refused: " + t);
        }

        Canvas canvas = new Canvas() {
            protected void paint(Graphics g) {
                g.setColor(0x000000);
                g.fillRect(0, 0, getWidth(), getHeight());
                g.setColor(0x00ff00);
                g.drawString("images", 5, 5, Graphics.TOP | Graphics.LEFT);
            }
        };
        Display.getDisplay(this).setCurrent(canvas);
    }

    private static byte[] readAll(String name) throws java.io.IOException {
        java.io.InputStream in = ImagesDemo.class.getResourceAsStream(name);
        if (in == null) {
            throw new java.io.IOException("no resource " + name);
        }
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[512];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
        }
        in.close();
        return out.toByteArray();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
