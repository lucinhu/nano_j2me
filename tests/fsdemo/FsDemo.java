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

    private String readFile(FileConnection fc) throws java.io.IOException {
        java.io.InputStream in = fc.openInputStream();
        StringBuffer text = new StringBuffer();
        int ch;
        while ((ch = in.read()) != -1) { text.append((char) ch); }
        in.close();
        return text.toString();
    }

    private void tryTruncate(String key, FileConnection fc, long length) {
        try {
            fc.truncate(length);
            say(key, "accepted");
        } catch (Exception e) {
            say(key, e.getClass().getName());
        }
    }

    private void testTruncate() throws java.io.IOException {
        String url = "file:///root/sub/truncate.txt";
        FileConnection f = (FileConnection) Connector.open(url);
        f.truncate(4L);
        say("truncate_prefix", "size=" + f.fileSize() + " text=" + readFile(f));
        f.truncate(4L);                         // equal size: no change
        f.truncate(100L);                       // never extend
        f.truncate(0x100000000L);               // must not narrow long to int
        f.truncate(Long.MAX_VALUE);
        say("truncate_noextend", "size=" + f.fileSize() + " text=" + readFile(f));
        tryTruncate("truncate_negative", f, -1L);
        tryTruncate("truncate_minlong", f, Long.MIN_VALUE);
        say("truncate_negative_data", readFile(f));

        FileConnection ro = (FileConnection) Connector.open(url, Connector.READ);
        tryTruncate("truncate_readonly", ro, 0L);
        ro.close();
        FileConnection timed = (FileConnection) Connector.open(url, Connector.READ, true);
        tryTruncate("truncate_readonly_timeout", timed, 0L);
        timed.close();
        tryTruncate("truncate_closed", ro, 0L);
        say("truncate_refused_data", readFile(f));

        FileConnection dir = (FileConnection) Connector.open("file:///root/sub/");
        tryTruncate("truncate_directory", dir, 0L);
        dir.close();
        FileConnection missing = (FileConnection) Connector.open("file:///root/sub/missing.txt");
        tryTruncate("truncate_missing", missing, 0L);
        tryTruncate("truncate_missing_large", missing, Long.MAX_VALUE);
        say("truncate_missing_exists", "" + missing.exists());
        missing.close();

        f.truncate(0L);                         // Minecraft's save sequence
        say("truncate_zero", "size=" + f.fileSize() + " text=[" + readFile(f) + "]");
        java.io.OutputStream out = f.openOutputStream();
        out.write(new byte[] { 'n', 'e', 'w' });
        out.close();
        f.close();
        FileConnection saved = (FileConnection) Connector.open(url, Connector.READ);
        say("truncate_saved", "size=" + saved.fileSize() + " text=" + readFile(saved));
        saved.close();
        FileConnection write = (FileConnection) Connector.open(url, Connector.WRITE);
        write.truncate(2L);
        write.close();
        FileConnection reopened = (FileConnection) Connector.open(url, Connector.READ);
        say("truncate_writeonly", "size=" + reopened.fileSize() + " text=" + readFile(reopened));
        reopened.close();
    }

    private void testRenameOffset() throws java.io.IOException {
        String old = "file:///root/sub/rename-source.txt";
        String renamed = "file:///root/sub/rename-saved.txt";
        FileConnection target = (FileConnection) Connector.open(renamed);
        if (target.exists()) { target.delete(); }
        target.close();
        FileConnection f = (FileConnection) Connector.open(old);
        if (!f.exists()) { f.create(); }
        f.truncate(0L);
        java.io.OutputStream out = f.openOutputStream();
        out.write(new byte[] { 'a', 'b', 'c', 'd', 'e', 'f' });
        out.close();
        out = f.openOutputStream(2L);
        out.write(new byte[] { 'X', 'Y' });
        out.close();
        say("offset_replace", readFile(f));
        out = f.openOutputStream(Long.MAX_VALUE);
        out.write('!');
        out.close();
        say("offset_eof", "size=" + f.fileSize() + " text=" + readFile(f));
        try { f.openOutputStream(-1L); say("offset_negative", "accepted"); }
        catch (Exception e) { say("offset_negative", e.getClass().getName()); }
        try { f.rename("../escaped"); say("rename_path", "accepted"); }
        catch (Exception e) { say("rename_path", e.getClass().getName()); }
        f.rename("rename-saved.txt");
        say("rename_saved", f.getURL() + " text=" + readFile(f));
        f.close();
        FileConnection oldName = (FileConnection) Connector.open(old);
        say("rename_old_exists", "" + oldName.exists());
        oldName.close();
        target = (FileConnection) Connector.open(renamed, Connector.READ);
        say("rename_reopen", readFile(target));
        try { target.rename("readonly.txt"); say("rename_readonly", "accepted"); }
        catch (Exception e) { say("rename_readonly", e.getClass().getName()); }
        target.close();
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

            /* Not file:// at all, and not something a port can serve either:
             * http:// used to be the example here, but the runtime now serves
             * it through the port's http_request hook, so the example is a
             * scheme that stays refused. */
            try {
                Connector.open("socket://example.com:80/x");
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

            testTruncate();
            testRenameOffset();

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
