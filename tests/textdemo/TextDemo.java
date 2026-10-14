import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

/**
 * Draws the characters the font round is about, and prints how wide it thinks
 * they are.
 *
 * <p>Two claims have to hold together, and they are measured differently: that
 * the ink on screen is the phone's own font, and that the runtime MEASURES the
 * same glyphs it draws.  The second one is where the bug was - the glyph
 * provider was consulted for ink, but anything inside the built-in face's
 * first..last character range (which is exactly ASCII) was measured with the
 * bundled bitmap's fixed cell width, so a proportional face came out with
 * every Latin advance wrong.
 *
 * <p>So each sample is drawn at x and a red hairline is drawn at
 * x + stringWidth(sample): the ink has to stop at the line, which is what a
 * screenshot can check.  The lines printed below say the same thing
 * numerically - "iii" and "WWW" are three characters each, so a fixed cell
 * makes them the same width and a proportional face does not - and they can be
 * read out of logcat without a screenshot at all.
 */
public final class TextDemo extends MIDlet {

    private static final String[] SAMPLES = {
        "iii", "WWW", "WAVE 0123", "Il1|", "你好，世界", "中文English混排",
    };

    private static final class Screen extends Canvas {
        private final Font plain;
        private final Font bold;

        Screen(Font plain, Font bold) {
            this.plain = plain;
            this.bold = bold;
            setFullScreenMode(true);
        }

        protected void paint(Graphics g) {
            g.setColor(0xFFFFFFFF);
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setFont(plain);
            int y = 8;
            int h = plain.getHeight();
            for (int i = 0; i < SAMPLES.length; i++) {
                String s = SAMPLES[i];
                g.setColor(0xFF000000);
                g.drawString(s, 6, y, Graphics.TOP | Graphics.LEFT);
                /* The ruler: the ink of every glyph has to stop at this line. */
                int w = plain.stringWidth(s);
                g.setColor(0xFFD00000);
                g.drawLine(6 + w, y, 6 + w, y + h - 1);
                y += h + 6;
            }
            g.setFont(bold);
            g.setColor(0xFF000000);
            g.drawString("BOLD WAVE 0123", 6, y, Graphics.TOP | Graphics.LEFT);
            y += bold.getHeight() + 6;
            g.drawString("粗体中文", 6, y, Graphics.TOP | Graphics.LEFT);
            /* One em in a box, so a screenshot carries its own ruler. */
            int em = plain.getHeight();
            g.setColor(0xFF008000);
            g.drawRect(6, y + em + 6, em, em);
            g.setColor(0xFF000000);
            g.drawString("em", 6 + em + 4, y + em + 6, Graphics.TOP | Graphics.LEFT);
        }
    }

    public void startApp() {
        Font plain = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_MEDIUM);
        Font bold = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_MEDIUM);
        for (int i = 0; i < SAMPLES.length; i++) {
            System.out.println("TEXT \"" + SAMPLES[i] + "\" width="
                    + plain.stringWidth(SAMPLES[i]) + " chars=" + SAMPLES[i].length());
        }
        /* One character at a time: a proportional face gives these different
         * numbers, a fixed cell gives the same one to every ASCII character. */
        String probes = "iWmM .|1x";
        for (int i = 0; i < probes.length(); i++) {
            System.out.println("TEXT probe '" + probes.charAt(i) + "' width="
                    + plain.stringWidth(probes.substring(i, i + 1)));
        }
        System.out.println("TEXT probe hanzi width=" + plain.stringWidth("中"));
        System.out.println("TEXT probe space width=" + plain.stringWidth(" "));
        System.out.println("TEXT bold \"WWW\" width=" + bold.stringWidth("WWW")
                + " plain=" + plain.stringWidth("WWW"));
        System.out.println("TEXT height plain=" + plain.getHeight()
                + " bold=" + bold.getHeight());
        final Screen screen = new Screen(plain, bold);
        Display.getDisplay(this).setCurrent(screen);
        /* Redraw once a second so a font or anti-aliasing change made in the
         * host UI shows up on the screen: a Canvas paints when something asks
         * it to, and this fixture has no other reason to ask. */
        Thread tick = new Thread(new Runnable() {
            public void run() {
                while (true) {
                    screen.repaint();
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException e) {
                        return;
                    }
                }
            }
        });
        /* No setDaemon(): the runtime does not implement it, and the MIDlet
         * never exits on its own, so a plain thread is enough. */
        tick.start();
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) { }
}
