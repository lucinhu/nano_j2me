// Round 163 fixture: the Siemens mobile extended APIs (com.siemens.mp.*) and
// Connector.openInputStream("resource:...").
//
// The canvas paints a dark background, then draws a red sprite through a
// LayerManager, a white 8x8 block through ExtendedImage.blitToScreen, runs the
// Light calls, and reads "resource:resdemo.txt" from its own JAR.  The gate
// script asserts both the logged markers and the resulting pixels.

import java.io.InputStream;

import javax.microedition.io.Connector;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.midlet.MIDlet;

import com.siemens.mp.color_game.Layer;
import com.siemens.mp.color_game.LayerManager;
import com.siemens.mp.color_game.Sprite;
import com.siemens.mp.game.ExtendedImage;
import com.siemens.mp.game.Light;

public class SiemensDemo extends Canvas implements Runnable {
    private static final int BG = 0x102030;
    private boolean firstPaint = true;

    SiemensDemo() {
        setFullScreenMode(true);
    }

    protected void paint(Graphics g) {
        g.setColor(BG);
        g.fillRect(0, 0, getWidth(), getHeight());
        try {
            Image img = Image.createImage(8, 8);
            Graphics ig = img.getGraphics();
            ig.setColor(0xFF0000);
            ig.fillRect(0, 0, 8, 8);

            Sprite sprite = new Sprite(img);
            sprite.setPosition(100, 40);
            LayerManager lm = new LayerManager();
            lm.append(sprite);
            System.out.println("SIEMENS manager_size=" + lm.getSize()
                    + " sprite_x=" + sprite.getX() + " sprite_y=" + sprite.getY()
                    + " visible=" + sprite.isVisible());
            lm.paint(g, 0, 0);
            System.out.println("SIEMENS sprite=ok");

            ExtendedImage xi = new ExtendedImage(img);
            xi.clear((byte) 0);
            xi.blitToScreen(10, 300);
            System.out.println("SIEMENS extended_image=ok");

            Light.setLightOn();
            Light.setLightOff();
            System.out.println("SIEMENS light=ok");

            LayerManager empty = new LayerManager();
            empty.setViewWindow(0, 0, 200, 200);
            empty.append(new BlockLayer(4, 4));
            empty.paint(g, 0, 0);
            System.out.println("SIEMENS layer_manager=ok");
        } catch (Throwable t) {
            if (firstPaint) {
                System.out.println("SIEMENS failed=" + t);
            }
        }
        if (firstPaint) {
            firstPaint = false;
            try {
                InputStream in = Connector.openInputStream("resource:resdemo.txt");
                byte[] b = new byte[32];
                int n = in.read(b, 0, b.length);
                in.close();
                String s = new String(b, 0, n).trim();
                System.out.println("SIEMENS resource=" + s + " len=" + n);
            } catch (Throwable t) {
                System.out.println("SIEMENS resource_failed=" + t);
            }
        }
    }

    public void run() {
        try {
            Thread.sleep(800);
        } catch (InterruptedException ignored) {
        }
        Display.getDisplay(SiemensDemoMidlet.instance).setCurrent((Canvas) null);
        SiemensDemoMidlet.instance.notifyDestroyed();
    }

    static class BlockLayer extends Layer {
        BlockLayer(int w, int h) {
            super(w, h);
        }

        public void paint(Graphics g) {
            g.setColor(0x00FF00);
            g.fillRect(getX(), getY(), getWidth(), getHeight());
        }
    }
}
