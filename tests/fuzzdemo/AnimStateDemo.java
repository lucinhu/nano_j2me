import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.m3g.Group;
import javax.microedition.m3g.KeyframeSequence;
import javax.microedition.m3g.AnimationTrack;

/*
 * Minimal title that actually holds one AnimationTrack instance, so a saved
 * state written by a pre-fix runtime really contains an instance of the class.
 * tools/m3g_fuzz2_state_evidence.sh uses it to prove the bounded legacy-shape
 * migration in src/core/nj_save.c refuses an old-layout instance instead of
 * rebasing it with the wrong field offsets.
 *
 * AnimationTrack extends Object3D in JSR 184; a runtime that registered it as
 * extending java/lang/Object therefore has a smaller instance_size for it, and
 * a state saved there cannot be restored into the corrected runtime when the
 * state holds a track.
 */
public class AnimStateDemo extends MIDlet {
    private DemoCanvas canvas;

    public void startApp() {
        KeyframeSequence kfs = new KeyframeSequence(2, 1, KeyframeSequence.STEP);
        AnimationTrack track = new AnimationTrack(kfs, AnimationTrack.ALPHA);
        Group root = new Group();
        root.addAnimationTrack(track);
        /* keep a strong ref so the GC cannot drop the track (or the class) */
        this.canvas = new DemoCanvas(track);
        Display.getDisplay(this).setCurrent(canvas);
    }

    public void pauseApp() { }
    public void destroyApp(boolean unconditional) { }

    static class DemoCanvas extends Canvas {
        private final Object held;
        private int frames;

        DemoCanvas(Object held) { this.held = held; }

        protected void paint(Graphics g) {
            g.setColor(0);
            g.fillRect(0, 0, getWidth(), getHeight());
            frames++;
            if (frames < 1000000) {
                repaint();
            }
        }

        protected void keyPressed(int key) { }
    }
}
