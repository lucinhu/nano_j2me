import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

import com.sprintpcs.media.Clip;
import com.sprintpcs.media.Player;
import com.sprintpcs.media.PlayerListener;
import com.sprintpcs.media.Vibrator;
import com.sprintpcs.util.Muglet;

/**
 * Round-175 fixture: exercises the Sprint PCS carrier wrappers.
 *
 * A plain MIDlet (no Nokia/Siemens classes) that builds both Clip flavours,
 * registers a PlayerListener, plays and stops a clip, vibrates, asks for a
 * Muglet and sets an exit URI.  The check asserts the markers and that the
 * canvas reached the framebuffer, which proves the classes link and dispatch
 * from ordinary game code.
 */
public class SprintDemo extends MIDlet implements PlayerListener {
    private DemoCanvas canvas;

    protected void startApp() {
        canvas = new DemoCanvas();
        Display.getDisplay(this).setCurrent(canvas);
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }

    public void playerUpdate(int event, Object data) { }

    private class DemoCanvas extends Canvas implements Runnable {
        private int paints;

        DemoCanvas() {
            new Thread(this).start();
        }

        public void run() {
            try {
                Clip uriClip = new Clip("http://example.invalid/tone.mid",
                                        "audio/midi", 100, 1);
                System.out.println("SPRINT clip_uri=" + uriClip.getContentType());

                byte[] data = new byte[] { 1, 2, 3, 4 };
                Clip dataClip = new Clip(data, "audio/x-tone-seq", 50, 0);
                System.out.println("SPRINT clip_data=" + dataClip.getContentType());

                Player.addPlayerListener(SprintDemo.this);
                System.out.println("SPRINT player_listener=ok");

                Player.play(uriClip, 0);
                Player.stop();
                System.out.println("SPRINT play=ok stop=ok");

                Vibrator.vibrate(100);
                System.out.println("SPRINT vibrator=ok");

                Muglet muglet = Muglet.getMuglet();
                System.out.println("SPRINT muglet_uri=[" + muglet.getURI() + "]"
                    + " media=" + muglet.getMediaType()
                    + " referring=" + muglet.getReferringURI());

                com.sprintpcs.util.System.setExitURI("http://example.invalid/exit");
                System.out.println("SPRINT system_exit=ok");

                try {
                    Player.play(dataClip, -2);
                    System.out.println("SPRINT repeat_bad=accepted");
                } catch (IllegalArgumentException e) {
                    System.out.println("SPRINT repeat_rejected=" + e);
                }
            } catch (Throwable t) {
                System.out.println("SPRINT failed=" + t);
            }

            while (paints < 6) {
                repaint();
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    /* ignored */
                }
            }
            notifyDestroyed();
        }

        protected void paint(Graphics g) {
            g.setColor(0x0000cc);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xffffff);
            g.drawString("SPRINT", 8, 8, Graphics.TOP | Graphics.LEFT);
            paints++;
        }
    }
}
