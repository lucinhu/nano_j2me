/*
 * Fixture for the Nokia UI CanvasItem overlay family.
 *
 * The canvas paints a solid blue frame; a 40x30 CanvasGraphicsItem at (10,20)
 * paints solid green.  The runtime must composite the item after the canvas
 * paint, so the frame shows green inside the item rectangle and blue outside.
 */
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

import com.nokia.mid.ui.CanvasGraphicsItem;

public class CanvasItemDemo extends MIDlet implements Runnable {
    static final int BG = 0x0000FF;
    static final int ITEM = 0x00FF00;
    static final int ITEM_X = 10;
    static final int ITEM_Y = 20;
    static final int ITEM_W = 40;
    static final int ITEM_H = 30;

    private DemoCanvas canvas;
    private BoxItem item;

    static class BoxItem extends CanvasGraphicsItem {
        BoxItem(int w, int h) {
            super(w, h);
        }

        protected void paint(Graphics g) {
            g.setColor(ITEM);
            g.fillRect(0, 0, getWidth(), getHeight());
            System.out.println("CANVASITEM item painted");
        }
    }

    static class DemoCanvas extends Canvas {
        protected void paint(Graphics g) {
            g.setColor(BG);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    public void startApp() {
        canvas = new DemoCanvas();
        item = new BoxItem(ITEM_W, ITEM_H);
        item.setParent(canvas);
        item.setPosition(ITEM_X, ITEM_Y);
        item.setVisible(true);
        System.out.println("CANVASITEM attached=" + item.getParent().getClass().getName()
                + " x=" + item.getPositionX() + " y=" + item.getPositionY()
                + " w=" + item.getWidth() + " h=" + item.getHeight());
        try {
            item.setZPosition(-1);
            System.out.println("CANVASITEM z accepted");
        } catch (IllegalArgumentException e) {
            System.out.println("CANVASITEM z rejected");
        }
        try {
            item.setParent(new Object());
            System.out.println("CANVASITEM parent accepted");
        } catch (IllegalArgumentException e) {
            System.out.println("CANVASITEM parent rejected");
        }
        Display.getDisplay(this).setCurrent(canvas);
        System.out.println("CANVASITEM display set");
        new Thread(this).start();
    }

    public void run() {
        try {
            Thread.sleep(600);
        } catch (InterruptedException e) {
            // ignore
        }
        System.out.println("CANVASITEM done");
        notifyDestroyed();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
