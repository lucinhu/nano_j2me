/*
 * A GameCanvas whose own thread draws whole frames and flushes them.
 *
 * This is how a MIDP 2.0 game is supposed to work: the application owns the
 * off-screen buffer, draws a complete frame into it, and calls
 * flushGraphics() to put that frame on the display.  The buffer is NOT double
 * buffered - the title starts drawing the next frame into the same pixels as
 * soon as flushGraphics() returns.
 *
 * That is what makes a second painter dangerous.  If the runtime also paints
 * the GameCanvas once per frame (paint() renders the buffer), that paint lands
 * wherever the title happens to be in its own loop: if the title is halfway
 * through the next frame, the screen gets that half drawn buffer, and the
 * blocks it has not redrawn yet show the previous frame - scenery appearing and
 * disappearing behind the character, which is exactly the flicker a player
 * reported for 鲁宾逊漂流记汉化版_702 (its game canvas flushes once per pass of
 * its own loop).
 *
 * The worker alternates the whole screen between blue and red, so a torn frame
 * is unmistakable: it holds BOTH colours in large amounts.  A complete frame is
 * one colour everywhere.
 *
 *   FLUSH loop=<n>          how many frames the worker flushed
 *   every presented frame is a single colour
 *
 * tools/check_flushdemo.sh holds the expectations; this file describes the game
 * side only.
 */
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.midlet.MIDlet;

public class FlushDemo extends MIDlet {

    static FlushDemo inst;
    static C canvas;

    public void startApp() {
        inst = this;
        canvas = new C();
        Display.getDisplay(this).setCurrent(canvas);
        new Worker().start();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    static class C extends GameCanvas {
        C() {
            super(false);
        }

        /*
         * The system is allowed to call this; the worker owns the buffer, and
         * whatever is in it is what should be shown.  Drawing here as well
         * would make the fixture test two things at once.
         */
        public void paint(Graphics g) {
        }

        /*
         * flushGraphics() is protected in GameCanvas, so the worker asks the
         * canvas to flush - which is how a real title's helper class reaches it
         * too.
         */
        void present() {
            flushGraphics();
        }
    }

    static class Worker extends Thread {
        public void run() {
            Graphics g = canvas.getGraphics();
            int w = canvas.getWidth();
            int h = canvas.getHeight();
            /*
             * Longer than the run lasts: the last frame of the run is still a
             * frame the worker drew, so the checker always has one to look at.
             */
            for (int i = 0; i < 300; i++) {
                /*
                 * The frame is deliberately longer than the frame interval: the
                 * top quarter is painted one pixel at a time and then the worker
                 * sleeps 45 ms, which straddles a frame boundary at 30 fps.  A
                 * runtime that paints this GameCanvas by itself copies the
                 * buffer during that sleep - a quarter of the new colour over
                 * three quarters of the previous frame - and the presented frame
                 * is torn.  A runtime that lets the application's own
                 * flushGraphics() be the presentation shows the frame whole.
                 */
                g.setColor((i & 1) == 0 ? 0x0000FF : 0xFF0000);
                int quarter = h / 4;
                for (int y = 0; y < quarter; y++) {
                    for (int x = 0; x < w; x++) {
                        g.fillRect(x, y, 1, 1);
                    }
                }
                try {
                    Thread.sleep(45);
                } catch (Exception e) {
                    /* a MIDlet has nowhere to report this */
                }
                g.fillRect(0, quarter, w, h - quarter);
                canvas.present();
                System.out.println("FLUSH loop=" + (i + 1));
                try {
                    Thread.sleep(5);
                } catch (Exception e) {
                    /* a MIDlet has nowhere to report this */
                }
            }
            inst.notifyDestroyed();
        }
    }
}
