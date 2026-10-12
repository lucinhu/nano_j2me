/*
 * A MIDlet whose paint() throws on every single frame.
 *
 * The runner's frame budget used to be checked against the number of frames
 * actually painted, so this MIDlet never reached its budget: the run went on
 * for ever at the frame rate.  The budget is a limit on the work the runner
 * asks for, so it has to count frames attempted.
 */
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public class PaintThrowDemo extends MIDlet
{
    static class Bad extends Canvas
    {
        protected void paint(Graphics g)
        {
            throw new RuntimeException("paint always fails");
        }
    }

    protected void startApp()
    {
        Display.getDisplay(this).setCurrent(new Bad());
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }
}
