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

    /*
     * The closed-state and mode rules from JSR 75.  A game opens a read-only
     * connection to show a save file and a write-only one to overwrite it; if
     * the runtime ignores the requested mode, the first call can delete the
     * save.  Every one of these has a defined exception, so each is called and
     * printed rather than only "did not throw".
     */
    private void testClosedAndModes() {
        try {
            FileConnection closed =
                (FileConnection) Connector.open("file:///root/a.txt");
            closed.close();
            try { closed.create(); say("closed_create", "accepted"); }
            catch (Exception e) { say("closed_create", e.getClass().getName()); }
            try { closed.mkdir(); say("closed_mkdir", "accepted"); }
            catch (Exception e) { say("closed_mkdir", e.getClass().getName()); }
            try { closed.delete(); say("closed_delete", "accepted"); }
            catch (Exception e) { say("closed_delete", e.getClass().getName()); }
            FileConnection survivor =
                (FileConnection) Connector.open("file:///root/a.txt", Connector.READ);
            say("closed_preserved", "" + survivor.exists());
            survivor.close();

            /* Connector.READ: mutating calls must be refused before they touch
             * the filesystem. */
            FileConnection ro = (FileConnection) Connector.open(
                    "file:///root/sub/ro.txt", Connector.READ);
            try { ro.create(); say("ro_create", "accepted"); }
            catch (Exception e) { say("ro_create", e.getClass().getName()); }
            try { ro.mkdir(); say("ro_mkdir", "accepted"); }
            catch (Exception e) { say("ro_mkdir", e.getClass().getName()); }
            try { ro.delete(); say("ro_delete", "accepted"); }
            catch (Exception e) { say("ro_delete", e.getClass().getName()); }
            FileConnection roCheck = (FileConnection) Connector.open(
                    "file:///root/sub/ro.txt", Connector.READ);
            say("ro_exists", "" + roCheck.exists());
            roCheck.close();
            ro.close();

            /* Connector.WRITE is write-only: reads must refuse too. */
            FileConnection wo = (FileConnection) Connector.open(
                    "file:///root/a.txt", Connector.WRITE);
            try { say("wo_exists", "" + wo.exists()); }
            catch (Exception e) { say("wo_exists", e.getClass().getName()); }
            try { say("wo_size", "" + wo.fileSize()); }
            catch (Exception e) { say("wo_size", e.getClass().getName()); }
            try { wo.list(); say("wo_list", "accepted"); }
            catch (Exception e) { say("wo_list", e.getClass().getName()); }
            wo.close();
        } catch (Throwable t) {
            say("closed_modes_failed", "" + t);
        }
    }

    /*
     * Name, size and metadata semantics: a directory's name carries a trailing
     * slash, fileSize() answers -1 (not an exception) for a missing file, and
     * metadata calls on a missing target answer 0/false instead of throwing.
     * These are the answers a game's world-list screen depends on: a throw here
     * hides every world.
     */
    private void testMetadata() {
        try {
            FileConnection dir =
                (FileConnection) Connector.open("file:///root/sub/");
            say("dir_name", "[" + dir.getName() + "]");
            say("dir_path", "[" + dir.getPath() + "]");
            try { say("dir_size", "" + dir.fileSize()); }
            catch (Exception e) { say("dir_size", e.getClass().getName()); }
            dir.close();
            FileConnection dirNoSlash =
                (FileConnection) Connector.open("file:///root/sub");
            say("dir_name_noslash", "[" + dirNoSlash.getName() + "]");
            say("dir_noslash_isdir", "" + dirNoSlash.isDirectory());
            dirNoSlash.close();

            FileConnection file =
                (FileConnection) Connector.open("file:///root/a.txt");
            say("file_name", "[" + file.getName() + "]");
            say("avail", "" + (file.availableSize() != file.fileSize()));
            file.close();

            FileConnection missing = (FileConnection) Connector.open(
                    "file:///root/sub/missing-meta.txt", Connector.READ);
            say("missing_size", "" + missing.fileSize());
            say("missing_time_zero", "" + (missing.lastModified() == 0L));
            say("missing_canread", "" + missing.canRead());
            missing.close();

            FileConnection newDir =
                (FileConnection) Connector.open("file:///root/sub/newdir/");
            newDir.mkdir();
            say("mkdir_new", "" + newDir.exists());
            try { newDir.mkdir(); say("mkdir_existing", "accepted"); }
            catch (Exception e) { say("mkdir_existing", e.getClass().getName()); }
            newDir.delete();
            say("mkdir_deleted", "" + newDir.exists());
            newDir.close();

            FileConnection existing =
                (FileConnection) Connector.open("file:///root/a.txt");
            try { existing.create(); say("create_existing", "accepted"); }
            catch (Exception e) { say("create_existing", e.getClass().getName()); }
            existing.close();

            FileConnection trailing = (FileConnection) Connector.open(
                    "file:///root/sub/nope/");
            try { trailing.create(); say("create_trailing", "accepted"); }
            catch (Exception e) { say("create_trailing", e.getClass().getName()); }
            trailing.close();

            FileConnection nonEmpty =
                (FileConnection) Connector.open("file:///root/sub/");
            try { nonEmpty.delete(); say("delete_notempty", "accepted"); }
            catch (Exception e) { say("delete_notempty", e.getClass().getName()); }
            nonEmpty.close();

            FileConnection missingDir = (FileConnection) Connector.open(
                    "file:///root/sub/missing-dir/");
            say("missing_dir_isdir", "" + missingDir.isDirectory());
            missingDir.close();
        } catch (Throwable t) {
            say("metadata_failed", "" + t);
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
            /* After the listing above, so the root entry counts stay fixed. */
            testClosedAndModes();
            testMetadata();

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
