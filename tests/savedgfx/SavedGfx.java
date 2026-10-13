/*
 * Drawing through the Graphics that paint() handed you, after paint() returned.
 *
 * A Graphics is valid only for the duration of paint(), so keeping it in a
 * field and drawing through it afterwards is out of spec - and it is what real
 * titles do.  合金弹头2013BT版_678 paints its HUD inside paint() and then draws
 * the scenery behind the character through the same object; 仙魔飞行棋_417
 * keeps one in a static field and draws its map from its own thread.
 *
 * The runtime has to cope: whatever the application has drawn into its canvas
 * buffer by the time the frame is over must be what the screen shows.  This
 * fixture pins that down.  Each iteration of the worker thread is one frame:
 *
 *   canvas.repaint(); canvas.serviceRepaints();   runs paint(), which clears
 *                                                 the canvas to black and
 *                                                 paints a small blue square,
 *                                                 saving the Graphics
 *   saved.fillRect(red)                           drawn AFTER paint() returned
 *   Thread.sleep(20)                              the frame boundary lands here
 *
 * The presented frame must hold BOTH the blue square and the red block.  A
 * runtime that copies the canvas buffer onto the screen at the END OF paint()
 * shows the red block never: the copy has already happened, and the next
 * paint() clears the buffer before the next copy - every presented frame is
 * black with a blue square.  (That is the flicker the user reported: the layer
 * drawn after paint() appeared on every other frame.)
 *
 * tools/check_savedgfx.sh holds the expectations; this file only describes the
 * game side.
 */
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public class SavedGfx extends MIDlet {

    static SavedGfx inst;
    static Canvas canvas;
    static Graphics saved;
    static int paints;
    static int loop;

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

    static class C extends Canvas {
        protected void paint(Graphics g) {
            saved = g;
            paints++;
            g.setColor(0x000000);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0x0000FF);
            g.fillRect(0, 0, 8, 8);
        }
    }

    static class Worker extends Thread {
        public void run() {
            /*
             * Keep going for longer than the run lasts: the point of the
             * fixture is that the block drawn AFTER paint() returned is on the
             * presented frame, so the last frame of the run still holds it.
             */
            for (int i = 0; i < 300; i++) {
                canvas.repaint();
                canvas.serviceRepaints();
                Graphics g = saved;
                if (g != null) {
                    g.setColor(0xFF0000);
                    g.fillRect(40, 40, 40, 40);
                }
                loop = i + 1;
                System.out.println("SAVED loop=" + loop + " paints=" + paints);
                try {
                    Thread.sleep(20);
                } catch (Exception e) {
                    /* a MIDlet has nowhere to report this */
                }
            }
            inst.notifyDestroyed();
        }
    }
}
