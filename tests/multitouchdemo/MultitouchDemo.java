import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

import com.nokia.mid.ui.multipointtouch.MultipointTouch;
import com.nokia.mid.ui.multipointtouch.MultipointTouchListener;

/**
 * Fixture for the Nokia multipoint-touch extension.
 *
 * <p>It registers itself the way a touchscreen game does on an Asha handset:
 * fetch the singleton with {@code MultipointTouch.getInstance()} and register
 * an instance listener.  Every changed pointer id is printed with the state
 * and position the runtime reports, so a test can check that each finger is
 * delivered with its own id and that {@code getState/getX/getY} describe that
 * finger - the pattern Minecraft J2ME PE uses to draw its own controls.
 *
 * <p>The Canvas callbacks are synchronous with the same hand, so the fixture
 * also prints them: the MIDP single-pointer API must stay on the first finger
 * down while the listener sees the whole hand.
 *
 * <p>The canvas draws one coloured dot per active pointer (id 0 red, 1 green,
 * 2 blue, 3 yellow) plus the last MIDP single-pointer position (white
 * crosshair), so a headless screenshot can prove that independent fingers
 * arrive with independent coordinates.
 */
public class MultitouchDemo extends MIDlet {
    private final TouchCanvas canvas = new TouchCanvas();

    protected void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
        canvas.start();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
        notifyDestroyed();
    }
}

class TouchCanvas extends Canvas implements MultipointTouchListener {
    private static final int MAX = 8;
    private MultipointTouch mpt;
    private final int[] lastX = new int[MAX];
    private final int[] lastY = new int[MAX];
    private final int[] lastState = new int[MAX];
    private int events;
    private int canvasX = -1;
    private int canvasY = -1;

    void start() {
        for (int i = 0; i < MAX; i++) {
            lastState[i] = -1;
        }
        mpt = MultipointTouch.getInstance();
        System.out.println("MPT ready=" + (mpt != null));
        if (mpt != null) {
            mpt.addMultipointTouchListener(this);
        }
    }

    public void pointersChanged(int[] ids) {
        if (ids == null) {
            return;
        }
        for (int i = 0; i < ids.length; i++) {
            int id = ids[i];
            int state = mpt.getState(id);
            int x = mpt.getX(id);
            int y = mpt.getY(id);
            System.out.println("MPT id=" + id + " state=" + state
                    + " x=" + x + " y=" + y);
            if (id >= 0 && id < MAX) {
                lastState[id] = state;
                if (x != Integer.MIN_VALUE) {
                    lastX[id] = x;
                    lastY[id] = y;
                }
            }
            events++;
        }
        repaint();
    }

    protected void paint(Graphics g) {
        int w = getWidth();
        int h = getHeight();
        g.setColor(0x000000);
        g.fillRect(0, 0, w, h);
        g.setColor(0xFFFFFF);
        g.drawString("MPT events=" + events, 2, 2, Graphics.TOP | Graphics.LEFT);
        for (int id = 0; id < MAX; id++) {
            int st = lastState[id];
            if (st == 0 || st == 1) {
                drawFinger(g, id, lastX[id], lastY[id], FINGER_RGB[id], st == 1);
            }
        }
        if (canvasX >= 0) {
            g.setColor(0xFFFFFF);
            g.drawLine(canvasX - 6, canvasY, canvasX + 6, canvasY);
            g.drawLine(canvasX, canvasY - 6, canvasX, canvasY + 6);
        }
    }

    private static final int[] FINGER_RGB = {
        0xE53935, 0x43A047, 0x1E88E5, 0xFDD835,
        0x8E24AA, 0x00ACC1, 0xFB8C00, 0x6D4C41
    };

    private void drawFinger(Graphics g, int id, int x, int y, int color,
            boolean dragging) {
        g.setColor(color);
        g.fillArc(x - 9, y - 9, 18, 18, 0, 360);
        if (dragging) {
            g.setColor(0xFFFFFF);
            g.drawArc(x - 11, y - 11, 22, 22, 0, 360);
        }
        g.setColor(0xFFFFFF);
        g.drawString("id" + id, x + 10, y - 6, Graphics.TOP | Graphics.LEFT);
        g.drawString(x + "," + y, x + 10, y + 6, Graphics.TOP | Graphics.LEFT);
    }

    protected void pointerPressed(int x, int y) {
        canvasX = x;
        canvasY = y;
        System.out.println("CANVAS down " + x + " " + y);
        repaint();
    }

    protected void pointerDragged(int x, int y) {
        canvasX = x;
        canvasY = y;
        System.out.println("CANVAS drag " + x + " " + y);
        repaint();
    }

    protected void pointerReleased(int x, int y) {
        canvasX = x;
        canvasY = y;
        System.out.println("CANVAS up " + x + " " + y);
        repaint();
    }
}
