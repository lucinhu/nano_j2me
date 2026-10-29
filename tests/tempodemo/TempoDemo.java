import java.io.InputStream;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.media.Controllable;
import javax.microedition.media.Control;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.media.control.RateControl;
import javax.microedition.media.control.TempoControl;
import javax.microedition.midlet.MIDlet;

/**
 * JSR 135's TempoControl and RateControl on a MIDI Player, as one line a shell
 * script can read.
 *
 * 速度与激情6_9 gets `getControl("TempoControl")` on its music Player, null-checks
 * it and catches the Exception a missing class would throw, so an absent
 * control degraded silently: the music played at the file's own tempo and
 * setTempo() changed nothing.  A control that answers has to really re-time the
 * song, so this fixture asks for the default tempo, halves and doubles it
 * through both interfaces, and checks the length the Player reports follows.
 *
 * Prints exactly one line:
 *
 *   TEMPO control=1 isTempo=true isRate=true t0=120000 set=240000 rate=2000
 *         rate_set=500 tempo_back=60000 dur_up=true nulls=0 tempos=1
 *
 * or `TEMPO ERROR <class>: <message>`.
 */
public class TempoDemo extends MIDlet {

    protected void startApp() {
        String line;
        Display.getDisplay(this).setCurrent(new Backdrop());
        try {
            line = run();
        } catch (Throwable t) {
            line = "TEMPO ERROR " + t.getClass().getName() + ": " + t.getMessage();
        }
        System.out.println(line);
        notifyDestroyed();
    }

    private static String run() throws Exception {
        InputStream is = TempoDemo.class.getResourceAsStream("/res/tempo.mid");
        Player p = Manager.createPlayer(is, "audio/midi");
        p.realize();
        Controllable c = (Controllable) p;
        Control raw = c.getControl("TempoControl");
        boolean isTempo = raw instanceof TempoControl;
        boolean isRate = raw instanceof RateControl;

        int t0 = -1, set = -1, rateAfterSet = -1;
        int rateSet = -1, tempoBack = -1;
        long dur0 = p.getDuration();
        long durFast = dur0, durSlow = dur0;

        if (raw != null) {
            TempoControl tc = (TempoControl) raw;
            t0 = tc.getTempo();
            set = tc.setTempo(240000);
            rateAfterSet = tc.getRate();
            durFast = p.getDuration();
            rateSet = tc.setRate(500);
            tempoBack = tc.getTempo();
            durSlow = p.getDuration();
        }

        /* getControls() has to hold the same controls, with no NULLs: a game
           walking the array would dereference one. */
        Control[] all = c.getControls();
        int nulls = 0, tempos = 0;
        for (int i = 0; i < all.length; i++) {
            if (all[i] == null) {
                nulls++;
            }
            if (all[i] instanceof TempoControl) {
                tempos++;
            }
        }
        p.close();

        return "TEMPO control=" + (raw != null ? 1 : 0)
                + " isTempo=" + isTempo
                + " isRate=" + isRate
                + " t0=" + t0
                + " set=" + set
                + " rate=" + rateAfterSet
                + " rate_set=" + rateSet
                + " tempo_back=" + tempoBack
                + " dur_fast=" + (durFast < dur0)
                + " dur_slow=" + (durSlow > dur0)
                + " nulls=" + nulls
                + " tempos=" + tempos;
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
