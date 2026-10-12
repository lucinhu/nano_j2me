import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.*;

/*
 * A MIDlet that installs its Displayable from a THREAD, not from startApp().
 *
 * Prehistoric Tribes (shi-qian-bu-luo) does exactly this, and the runner used
 * to declare it broken - "the MIDlet installed no Displayable; nothing to
 * show" - after eighteen bytecodes.  On a device a MIDlet may call
 * Display.setCurrent() from any thread at any time, so refusing to wait turns
 * "slow" into "broken".
 *
 * The thread sleeps first, so a runtime that checks once, straight after
 * startApp() returns, cannot pass this by luck.  That delay is the point of
 * the fixture.
 */
public class LateDisplayDemo extends MIDlet
{
    private final Canvas c;

    public LateDisplayDemo()
    {
        c = new Canvas() {
            protected void paint(Graphics g)
            {
                g.setColor(0x00102030);
                g.fillRect(0, 0, getWidth(), getHeight());
            }
        };
    }

    protected void startApp()
    {
        Thread t = new Thread(new Runnable() {
            public void run()
            {
                try { Thread.sleep(20); } catch (InterruptedException e) { }
                Display.getDisplay(LateDisplayDemo.this).setCurrent(c);
            }
        });
        t.start();
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean u) { }
}
