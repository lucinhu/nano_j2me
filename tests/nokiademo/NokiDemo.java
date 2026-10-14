/*
 * The Nokia UI API, called the way the collected titles call it.
 *
 * This is the vendor surface the corpus leans on hardest: FullCanvas (83
 * titles), DirectGraphics (74), DirectUtils (73), Sound (14), DeviceControl
 * (10).  A vendor class that is missing is caught by the title and worked
 * around, but a vendor METHOD that is missing is a NoSuchMethodError in the
 * middle of a frame - so what this fixture checks is not that the calls exist,
 * but that they answer with something a game can use.
 *
 * It prints one line per family and installs a Canvas at the end, because a
 * MIDlet that shows nothing exits non-zero in this runner.
 */
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.midlet.MIDlet;

import com.nokia.mid.sound.Sound;
import com.nokia.mid.ui.DeviceControl;
import com.nokia.mid.ui.DirectGraphics;
import com.nokia.mid.ui.DirectUtils;

public class NokiDemo extends MIDlet {

    private static class Blank extends Canvas {
        protected void paint(Graphics g) {
            g.setColor(0);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    public void startApp() {
        /* --- DirectUtils.createImage + DirectGraphics ------------------- */
        Image img = DirectUtils.createImage(16, 16, 0xFFFFFFFF);
        DirectGraphics dg = DirectUtils.getDirectGraphics(img.getGraphics());
        dg.setARGBColor(0xFFFF0000);
        dg.drawTriangle(0, 0, 15, 0, 7, 15, 0xFFFF0000);

        int[] rgb = new int[16 * 16];
        img.getRGB(rgb, 0, 16, 0, 0, 16, 16);
        /* The top edge of the triangle runs from (0,0) to (15,0), so (7,0) is
         * on it and (15,15) is not: that is the difference between drawTriangle
         * drawing and not drawing, which a sample of the middle would not see. */
        int edge = rgb[0 * 16 + 7];
        int outside = rgb[15 * 16 + 15];
        System.out.println("NOKIA img=" + img.getWidth() + "x" + img.getHeight()
                           + " edge=" + Integer.toHexString(edge)
                           + " outside=" + Integer.toHexString(outside)
                           + " alpha=" + dg.getAlphaComponent());

        /* one byte per pixel, drawn as the grey level of the same value */
        byte[] grey = new byte[4];
        grey[0] = (byte) 0x80;
        grey[1] = (byte) 0x80;
        grey[2] = (byte) 0x80;
        grey[3] = (byte) 0x80;
        dg.drawPixels(grey, null, 0, 2, 0, 0, 2, 2, 0, 1);
        img.getRGB(rgb, 0, 16, 0, 0, 16, 16);
        byte[] back = new byte[4];
        dg.getPixels(back, null, 0, 2, 0, 0, 2, 2, 1);
        System.out.println("NOKIA bytes=" + Integer.toHexString(rgb[0])
                           + " read=" + (back[0] & 0xFF));

        /* the int[] overload: ARGB comes back unchanged */
        int[] argb = new int[4];
        argb[0] = 0xFF112233;
        dg.drawPixels(argb, false, 0, 2, 4, 4, 2, 2, 0, 12);
        img.getRGB(rgb, 0, 16, 0, 0, 16, 16);
        System.out.println("NOKIA argb=" + Integer.toHexString(rgb[4 * 16 + 4]));

        /* --- DirectUtils.createImage(byte[], int, int) ------------------ */
        try {
            java.io.InputStream in =
                getClass().getResourceAsStream("/tiny.png");
            byte[] data = new byte[512];
            int n = in.read(data);
            in.close();
            Image png = DirectUtils.createImage(data, 0, n);
            System.out.println("NOKIA png=" + png.getWidth() + "x" + png.getHeight());
        } catch (Exception e) {
            System.out.println("NOKIA png=failed " + e);
        }

        /* --- Sound ------------------------------------------------------ */
        Sound s = new Sound(1, 1000);
        s.setGain(50);
        s.play(1);
        int playing = s.getState();
        s.stop();
        s.resume();
        int[] fmts = Sound.getSupportedFormats();
        System.out.println("NOKIA snd=" + playing + "," + s.getGain() + ","
                           + s.getState() + "," + fmts.length + ","
                           + Sound.getConcurrentSoundCount(1));

        /* --- DeviceControl: the side effects have nothing to observe ---- */
        DeviceControl.startVibra(50, 100);
        DeviceControl.stopVibra();
        DeviceControl.flashLights(100);
        DeviceControl.vibrate(10);
        System.out.println("NOKIA devctl=ok");

        Display.getDisplay(this).setCurrent(new Blank());
        notifyDestroyed();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
