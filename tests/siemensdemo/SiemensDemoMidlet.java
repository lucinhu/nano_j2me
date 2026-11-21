import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;

public class SiemensDemoMidlet extends MIDlet {
    static SiemensDemoMidlet instance;
    private final SiemensDemo canvas = new SiemensDemo();

    public SiemensDemoMidlet() {
        instance = this;
    }

    protected void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
        new Thread(canvas).start();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
