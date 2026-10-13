/*
 * A static field whose value lives in a ConstantValue attribute.
 *
 * javac inlines a compile-time constant at its use site, so a title can only
 * read one through a fieldref - and then the value has to come from the
 * attribute, because javac leaves such a field out of <clinit> entirely.  That
 * happens for real: of the 395 titles collected in tests/games, 20 static
 * fields across 11 titles are read by a getstatic and never assigned by any
 * <clinit>.
 *
 * This fixture builds that shape on purpose.  Holder is compiled twice:
 * first with plain statics (so CVRead's reads are fieldrefs, not inlined
 * constants), then again with `static final` fields (so the same reads hit a
 * ConstantValue attribute and nothing else).  tools/make_cvdemo_jar.sh does
 * the two passes; see tests/cvdemo/Holder.plain.java and Holder.final.java.
 *
 * The expected line is therefore "CV a=42 s=cv-string b=7 l=1311768467463790320
 * f=2.5": every field but `b` (which a real <clinit> assigns) would print 0 or
 * null if the runtime ignored the attribute.
 */

import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;

public class CVDemo extends MIDlet {
    private Form form;

    protected void startApp() {
        form = new Form("CVDemo");
        form.append(CVRead.line());
        Display.getDisplay(this).setCurrent(form);
        /* One frame is enough: the check reads the line, not the screen. */
        System.out.println(CVRead.line());
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }
}
