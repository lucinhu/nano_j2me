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
    private MultipointTouch mpt;
    private int events;

    void start() {
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
            System.out.println("MPT id=" + id + " state=" + state
                    + " x=" + mpt.getX(id) + " y=" + mpt.getY(id));
            events++;
        }
        repaint();
    }

    protected void paint(Graphics g) {
        g.setColor(0);
        g.fillRect(0, 0, getWidth(), getHeight());
    }

    protected void pointerPressed(int x, int y) {
        System.out.println("CANVAS down " + x + " " + y);
    }

    protected void pointerDragged(int x, int y) {
        System.out.println("CANVAS drag " + x + " " + y);
    }

    protected void pointerReleased(int x, int y) {
        System.out.println("CANVAS up " + x + " " + y);
    }
}
