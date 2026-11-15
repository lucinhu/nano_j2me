// MIT. Movie fixture: decode an MPEG-4 Part 2 video track from a 3gp resource
// and show it through VideoControl's direct-video overlay on a Canvas.
//
// The point of the fixture is that it exercises the whole MMAPI video path
// from Java: Manager.createPlayer(InputStream, "video/3gpp"), realize,
// prefetch, VideoControl.initDisplayMode(USE_DIRECT_VIDEO, canvas),
// setDisplayLocation/setVisible, start, and the frame-boundary overlay the
// port presents.  It prints what happened so the shell check and the fuzzers
// can tell "decoded and shown" from "refused".

import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.media.control.VideoControl;

public class MovieDemo extends MIDlet implements Runnable {
    private Display display;
    private Canvas canvas;
    private volatile boolean done;

    protected void startApp() {
        display = Display.getDisplay(this);
        canvas = new Canvas() {
            protected void paint(Graphics g) {
                // Dark backdrop so the video overlay is the only bright thing.
                g.setColor(0x101010);
                g.fillRect(0, 0, getWidth(), getHeight());
                g.setColor(0x00ff00);
                g.fillRect(0, 0, 6, 6);
            }
        };
        display.setCurrent(canvas);
        new Thread(this).start();
    }

    public void run() {
        Player p = null;
        try {
            p = Manager.createPlayer(getClass().getResourceAsStream("/res/movie.3gp"),
                                     "video/3gpp");
            p.realize();
            p.prefetch();
            VideoControl vc = (VideoControl) p.getControl("VideoControl");
            if (vc == null) {
                System.out.println("VIDEO nocontrol");
                return;
            }
            vc.initDisplayMode(VideoControl.USE_DIRECT_VIDEO, canvas);
            vc.setDisplayLocation(4, 4);
            vc.setVisible(true);
            p.start();
            System.out.println("VIDEO started duration=" + p.getDuration());
            /* The headless port runs frames as fast as --fps allows; sleeping
             * here lets the overlay present a few video samples before we
             * stop.  The shell check only needs the start plus the core's
             * media line. */
            for (int i = 0; i < 40 && !done; i++) {
                Thread.sleep(20);
            }
            vc.setVisible(false);
            p.stop();
            System.out.println("VIDEO state=" + p.getState());
        } catch (Throwable t) {
            System.out.println("VIDEO failed=" + t);
        } finally {
            if (p != null) {
                try { p.deallocate(); } catch (Throwable t) { }
                try { p.close(); } catch (Throwable t) { }
            }
            notifyDestroyed();
        }
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { done = true; }
}
