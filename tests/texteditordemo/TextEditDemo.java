/*
 * Fixture for the Nokia UI TextEditor (CanvasItem) family.
 *
 * The canvas paints solid yellow.  A TextEditor at (10,10) 60x20 paints its
 * own background (blue) and content; the runtime must composite it after the
 * canvas paint.  The checks exercise the content/selection/caret API and the
 * TextEditorListener callbacks.
 */
import com.nokia.mid.ui.TextEditor;
import com.nokia.mid.ui.TextEditorListener;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public class TextEditDemo extends MIDlet implements Runnable,
        TextEditorListener {
    static final int BG = 0xFFFF00;
    static final int EDITOR_BG = 0x0000FF;
    static final int EDITOR_FG = 0xFFFFFF;
    static final int X = 10;
    static final int Y = 10;
    static final int W = 60;
    static final int H = 20;

    private TextEditor editor;
    private int paints;

    static void log(String s) {
        System.out.println("TE " + s);
    }

    static final class DemoCanvas extends Canvas {
        protected void paint(Graphics g) {
            g.setColor(BG);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    private void checks() {
        TextEditor te = editor;
        log("content=" + te.getContent());
        te.insert("X", 0);
        log("inserted=" + te.getContent());
        te.delete(1, 2);
        log("deleted=" + te.getContent());
        te.setContent("hello");
        te.setMaxSize(4);
        log("truncated=" + te.getContent());
        te.setMaxSize(20);
        te.setContent("abc");
        te.setCaret(2);
        log("caret=" + te.getCaretPosition());
        te.setSelection(1, 2);
        log("selection=" + te.getSelection());
        te._contentChanged("changed");
        log("after_callback=" + te.getContent());
        te._inputAction(TextEditorListener.ACTION_CARET_MOVE);
        int caught = 0;
        try {
            te.setContent(null);
        } catch (IllegalArgumentException e) {
            caught++;
        }
        try {
            te.setMaxSize(0);
        } catch (IllegalArgumentException e) {
            caught++;
        }
        log("iae=" + caught);
        te.setFocus(false);
        log("unfocused_caret=" + te.getCaretPosition());
        te.setFocus(true);
        te.setContent("Blue");
        log("max=" + te.getMaxSize() + " size=" + te.size());
        log("painted");
    }

    public void startApp() {
        DemoCanvas canvas = new DemoCanvas();
        editor = TextEditor.createTextEditor("abc", 20, 0, W, H);
        editor.setBackgroundColor(EDITOR_BG);
        editor.setForegroundColor(EDITOR_FG);
        editor.setParent(canvas);
        editor.setPosition(X, Y);
        editor.setVisible(true);
        editor.setFocus(true);
        editor.setTextEditorListener(this);
        log("attached=" + (editor.getParent() == canvas));
        checks();
        Display.getDisplay(this).setCurrent(canvas);
        new Thread(this).start();
    }

    public void run() {
        try {
            Thread.sleep(900);
        } catch (InterruptedException e) {
        }
        notifyDestroyed();
    }

    public void inputAction(TextEditor te, int action) {
        log("listener action=" + action
                + (action == TextEditorListener.ACTION_CONTENT_CHANGE
                   ? " content=" + te.getContent() : ""));
    }

    public void destroyApp(boolean unconditional) {
    }

    public void pauseApp() {
    }
}
