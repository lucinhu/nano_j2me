package fsfix;

import javax.microedition.midlet.MIDlet;
import javax.microedition.io.Connector;
import javax.microedition.io.file.FileConnection;
import java.util.Enumeration;

/*
 * Exercises javax.microedition.io.file and PRINTS WHAT IT SAW, so the test can
 * assert on it.  The escape attempts are the point: a file API that can be
 * talked out of its root is worse than one that is missing.
 */
public class FsDemo extends MIDlet {

    private void say(String k, String v) {
        System.out.println("FS " + k + "=" + v);
    }

    private void tryOpen(String url) {
        try {
            FileConnection fc = (FileConnection) Connector.open(url,
                    Connector.READ);
            say("opened", url + " exists=" + fc.exists()
                + " dir=" + fc.isDirectory() + " name=" + fc.getName()
                + " path=" + fc.getPath());
            if (fc.exists() && !fc.isDirectory()) {
                say("size", "" + fc.fileSize());
            }
            fc.close();
        } catch (Exception e) {
            say("refused", url + " -> " + e.getClass().getName());
        }
    }

    public void startApp() {
        try {
            tryOpen("file:///root/a.txt");
            tryOpen("file:///root/sub");
            tryOpen("file:///root/nope.txt");

            /* The sandbox.  Each of these must be REFUSED, and refused by the
             * runtime rather than by the host's permissions accidentally. */
            tryOpen("file:///root/../etc/passwd");
            tryOpen("file:///root/sub/../../etc/passwd");
            tryOpen("file:///root/a/../../etc/passwd");

            /* Not file:// at all. */
            try {
                Connector.open("http://example.com/x");
                say("scheme", "accepted");
            } catch (Exception e) {
                say("scheme", e.getClass().getName());
            }

            FileConnection d = (FileConnection) Connector.open("file:///root/");
            say("rootname", "[" + d.getName() + "]");
            say("rootdir", "" + d.isDirectory());
            /*
             * Reader/Writer over a real file: the five methods CLDC defines as
             * concrete on top of the abstract ones.  Each is called and its
             * result printed, because "it did not throw" is not evidence that
             * it read or wrote the right thing.
             *
             * Written under sub/ so the listing counts asserted below keep
             * the values they had.
             */
            FileConnection rw =
                (FileConnection) Connector.open("file:///root/sub/rw.txt");
            if (!rw.exists()) { rw.create(); }
            java.io.OutputStreamWriter w =
                new java.io.OutputStreamWriter(rw.openOutputStream());
            w.write('A');                       /* Writer.write(int) */
            w.write(new char[] { 'B', 'C' });   /* Writer.write(char[]) */
            w.write("DEF");                     /* Writer.write(String) */
            w.close();
            rw.close();

            FileConnection rr =
                (FileConnection) Connector.open("file:///root/sub/rw.txt");
            java.io.InputStreamReader rd =
                new java.io.InputStreamReader(rr.openInputStream());
            long sk = rd.skip(2);               /* Reader.skip(long) */
            char[] buf = new char[4];
            int n = rd.read(buf);               /* Reader.read(char[]) */
            String seen = "";
            for (int i = 0; i < n; i++) { seen = seen + buf[i]; }
            say("rw", "skip=" + sk + " n=" + n + " text=" + seen);
            rd.close();
            rr.close();

            Enumeration en = d.list();
            String acc = "";
            int ndir = 0, nfile = 0;
            while (en.hasMoreElements()) {
                String s = (String) en.nextElement();
                acc = acc + s + "|";
                /* JSR 75 marks directories with a trailing slash. */
                if (s.endsWith("/")) { ndir++; } else { nfile++; }
            }
            say("entries", acc);
            say("dirs", "" + ndir);
            say("files", "" + nfile);

            Enumeration nes = d.list("*.nes", false);
            String nesacc = "";
            while (nes.hasMoreElements()) { nesacc = nesacc + nes.nextElement() + "|"; }
            say("nes", nesacc);
            d.close();

            /* Reading a file's bytes. */
            FileConnection f = (FileConnection) Connector.open("file:///root/a.txt");
            java.io.InputStream in = f.openInputStream();
            StringBuffer sb = new StringBuffer();
            int ch;
            while ((ch = in.read()) != -1) { sb.append((char) ch); }
            in.close();
            f.close();
            say("content", sb.toString().trim());

            /* A closed connection refuses further work. */
            FileConnection c = (FileConnection) Connector.open("file:///root/a.txt");
            c.close();
            try {
                c.exists();
                say("closed", "still usable");
            } catch (Exception e) {
                say("closed", e.getClass().getName());
            }
        } catch (Throwable t) {
            say("failed", "" + t);
        }
        notifyDestroyed();
    }

    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
