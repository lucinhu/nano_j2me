import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * A MIDlet that draws, and whose drawing never changes the picture.
 *
 * This is the fixture for the corpus verdict itself rather than for the
 * runtime.  The corpus used to describe every flat frame as "the game is not
 * drawing", which is a claim it had not measured: 合金弹头4(完美汉化版)_4053
 * called into Graphics 60 times over 20 frames and still presented one colour,
 * because the game's own paint state machine draws nothing in the state it was
 * in.  The verdict now has to report the numbers it actually saw.
 *
 * paint() makes two Graphics calls every frame and fills the screen with one
 * colour, so a run of it is unambiguously "flat, with drawing happening".
 */
public class FlatDemo extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new V());
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

class V extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0x103050);
        g.fillRect(0, 0, getWidth(), getHeight());
    }
}
