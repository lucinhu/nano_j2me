import java.io.IOException;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Samsung's SM/SMS helper, as one line a shell script can read.
 *
 * Three collected titles - 三国塔防－曹操传_1610, 宠物王国2紫_5550 and
 * 魂斗罗3代-完全版_3784 - build an SM, set its data and destination and hand it
 * to SMS.send().  With the vendor classes absent that sequence died on
 * NoClassDefFoundError inside their billing/share feature.
 *
 * There is no SMS transport in this runtime, so the honest answers are the SDK's
 * own: isSupported() is false, and send() raises the IOException the API
 * declares.  A title that asks first skips the message; one that does not gets
 * a catchable exception instead of a linkage error.
 *
 * Prints exactly one line:
 *
 *   SMS supported=false data=ok dest=ok send=IOException
 *
 * or `SMS ERROR <class>: <message>`.
 */
public class SamsungDemo extends MIDlet {

    protected void startApp() {
        String line;
        Display.getDisplay(this).setCurrent(new Backdrop());
        try {
            line = run();
        } catch (Throwable t) {
            line = "SMS ERROR " + t.getClass().getName() + ": " + t.getMessage();
        }
        System.out.println(line);
        notifyDestroyed();
    }

    private static String run() throws Exception {
        boolean supported = com.samsung.util.SMS.isSupported();

        com.samsung.util.SM sm = new com.samsung.util.SM();
        sm.setData("hello from a MIDlet");
        sm.setDestAddress("10086");
        boolean data = "hello from a MIDlet".equals(sm.getData());
        boolean dest = "10086".equals(sm.getDestAddress());

        String send;
        try {
            com.samsung.util.SMS.send(sm);
            send = "returned";
        } catch (IOException e) {
            send = "IOException";
        }

        return "SMS supported=" + supported
                + " data=" + (data ? "ok" : "lost")
                + " dest=" + (dest ? "ok" : "lost")
                + " send=" + send;
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class Backdrop extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0x000000);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
