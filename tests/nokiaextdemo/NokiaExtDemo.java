/*
 * Nokia UI extras fixture (round 154): Clipboard, GlobalIndicators,
 * TactileFeedback, SoftNotification, gestures, JoystickEventListener and
 * the DirectGraphicsImplemented marker.  Prints stable markers that
 * tools/check_nokiaext.sh asserts.
 */
import com.nokia.mid.ui.Clipboard;
import com.nokia.mid.ui.DirectGraphicsImplemented;
import com.nokia.mid.ui.GlobalIndicators;
import com.nokia.mid.ui.GlobalIndicatorsException;
import com.nokia.mid.ui.JoystickEventListener;
import com.nokia.mid.ui.SoftNotification;
import com.nokia.mid.ui.SoftNotificationListener;
import com.nokia.mid.ui.TactileFeedback;
import com.nokia.mid.ui.gestures.GestureInteractiveZone;
import com.nokia.mid.ui.gestures.GestureListener;
import com.nokia.mid.ui.gestures.GestureRegistrationManager;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public class NokiaExtDemo extends MIDlet
        implements SoftNotificationListener, JoystickEventListener, GestureListener {

    static class Marker implements DirectGraphicsImplemented {
    }

    public void startApp() {
        Marker marker = new Marker();
        System.out.println("NOKIAEXT marker=" + (marker instanceof DirectGraphicsImplemented));

        Clipboard.copyToClipboard("hello nano");
        System.out.println("NOKIAEXT clipboard=" + Clipboard.copyFromClipboard());
        Clipboard.copyToClipboard(null);
        System.out.println("NOKIAEXT clipboard_cleared="
                + (Clipboard.copyFromClipboard() == null));

        System.out.println("NOKIAEXT gi_constants=" + GlobalIndicators.MAIL_INDICATOR
                + "/" + GlobalIndicators.CHAT_INDICATOR);
        try {
            int old = GlobalIndicators.setGlobalIndicator(
                    GlobalIndicators.MAIL_INDICATOR, GlobalIndicators.INDICATOR_SHOW);
            int now = GlobalIndicators.getGlobalIndicator(
                    GlobalIndicators.MAIL_INDICATOR);
            System.out.println("NOKIAEXT gi old=" + old + " now=" + now);
        } catch (GlobalIndicatorsException e) {
            System.out.println("NOKIAEXT gi failed=" + e);
        }
        try {
            GlobalIndicators.getGlobalIndicator(1234);
            System.out.println("NOKIAEXT gi invalid=no");
        } catch (GlobalIndicatorsException e) {
            System.out.println("NOKIAEXT gi invalid=caught");
        }

        TactileFeedback feedback = new TactileFeedback();
        feedback.directFeedback(TactileFeedback.FEEDBACK_STYLE_BASIC);
        System.out.println("NOKIAEXT tactile supported=" + feedback.isTouchFeedbackSupported()
                + " style=" + TactileFeedback.FEEDBACK_STYLE_SENSITIVE);
        try {
            feedback.directFeedback(99);
            System.out.println("NOKIAEXT tactile invalid=no");
        } catch (IllegalArgumentException e) {
            System.out.println("NOKIAEXT tactile invalid=caught");
        }

        SoftNotification sn = SoftNotification.newInstance(42);
        sn.setListener(this);
        System.out.println("NOKIAEXT sn id=" + sn.getId());
        try {
            sn.setText("title", "body");
            sn.setSoftkeyLabels("Open", "Dismiss");
            sn.post();
            sn.remove();
            System.out.println("NOKIAEXT sn done=true");
        } catch (com.nokia.mid.ui.SoftNotificationException e) {
            System.out.println("NOKIAEXT sn failed=" + e);
        }

        GestureInteractiveZone zone = new GestureInteractiveZone(
                GestureInteractiveZone.GESTURE_TAP | GestureInteractiveZone.GESTURE_DRAG);
        GestureRegistrationManager.setListener(marker, this);
        System.out.println("NOKIAEXT gesture type=" + zone.getType()
                + " registered=" + GestureRegistrationManager.register(marker, zone));

        // Prove the runtime is healthy after the extras ran: install a real
        // Canvas and paint a few frames before leaving.
        Display.getDisplay(this).setCurrent(new TestCanvas());
    }

    class TestCanvas extends Canvas {
        private int paints;
        protected void paint(Graphics g) {
            g.setColor(0x0000FF00);
            g.fillRect(0, 0, getWidth(), getHeight());
            if (++paints >= 5) {
                notifyDestroyed();
            }
        }
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }

    public void notificationSelected(SoftNotification n) {
        System.out.println("NOKIAEXT sn selected");
    }

    public void notificationDismissed(SoftNotification n) {
        System.out.println("NOKIAEXT sn dismissed");
    }

    public void joystickEvent(int x, int y) {
        System.out.println("NOKIAEXT joystick " + x + "," + y);
    }
}
