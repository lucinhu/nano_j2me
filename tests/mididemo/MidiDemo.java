import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.media.*;
import java.io.*;

/**
 * The smallest MIDlet that plays a MIDI file: it puts a Canvas on the display
 * (so the runner has frames to pump audio in), opens "/song.mid" through
 * Manager.createPlayer() and starts it.
 *
 * The point of the fixture is that it says what happened: the Player's
 * duration and state go to stdout, so a run either reports a real duration
 * (the file was rendered to PCM) or -1 (the silent Player).  It is the only
 * fixture that exercises the path from the Java audio API down to
 * nj_port.pcm_open(), which is where "the corpus has no sound" is decided.
 */
public class MidiDemo extends MIDlet {

    static class Backdrop extends Canvas {
        public void paint(Graphics g) {
            g.setColor(0x102030);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    public void startApp() {
        try {
            Display.getDisplay(this).setCurrent(new Backdrop());
            InputStream is = getClass().getResourceAsStream("/song.mid");
            Player p = Manager.createPlayer(is, "audio/midi");
            p.realize();
            p.prefetch();
            p.start();
            System.out.println("MIDIDEMO started duration=" + p.getDuration()
                               + " state=" + p.getState());
        } catch (Throwable e) {
            System.out.println("MIDIDEMO failed " + e);
        }
    }

    public void pauseApp() {}

    public void destroyApp(boolean u) {}
}
