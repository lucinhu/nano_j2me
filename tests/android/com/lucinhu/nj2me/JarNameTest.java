package com.lucinhu.nj2me;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.jar.Attributes;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;

/**
 * The name a game is shown under, off the device.
 *
 * <p>Requirement: after importing a jar the launcher must show the MIDlet's own
 * name, not the file's.  Jars in the wild declare it in any of three places,
 * and a sidecar from an older install may have baked the file name in as if the
 * player had chosen it - both are decided in {@link JarName}.
 */
public final class JarNameTest {

    private JarNameTest() {
    }

    public static void main(String[] args) throws Exception {
        File tmp = Files.createTempDirectory("nj2me-jarname").toFile();

        Check.section("what a jar calls itself");
        Check.eq("MIDlet-1 names the game",
                 JarName.of(jar(tmp, "midlet1.jar",
                         "MIDlet-1", "Metal Slug 4, /icon.png, MetalSlug",
                         "MIDlet-Name", "Whatever")),
                 "Metal Slug 4");
        Check.eq("a jar that only names the suite falls back to MIDlet-Name",
                 JarName.of(jar(tmp, "suite.jar",
                         "MIDlet-1", " , , KeysDemo",
                         "MIDlet-Name", "KeysDemo")),
                 "KeysDemo");
        Check.eq("a jar whose MIDlet is listed second",
                 JarName.of(jar(tmp, "second.jar",
                         "MIDlet-2", "Second Game, , Second")),
                 "Second Game");
        Check.eq("quotes are not part of the name",
                 JarName.of(jar(tmp, "quoted.jar",
                         "MIDlet-1", "\"Quoted Name\", , Quoted")),
                 "Quoted Name");
        Check.eq("spaces around the name are trimmed",
                 JarName.of(jar(tmp, "spaces.jar",
                         "MIDlet-1", "  Padded  , , Padded")),
                 "Padded");
        Check.eq("a manifest with no name at all",
                 JarName.of(jar(tmp, "nameless.jar", "MIDlet-1", " , , NoName")),
                 null);
        Check.eq("a file that is not a jar at all",
                 JarName.of(notAJar(tmp, "broken.jar")),
                 null);
        Check.eq("a jar that is not there", JarName.of(new File(tmp, "gone.jar")), null);

        Check.section("which name a row shows");
        Check.eq("the manifest name beats the file name",
                 JarName.chooseTitle("metal_slug_4", "Metal Slug 4", null, false, false),
                 "Metal Slug 4");
        Check.eq("a jar with no name falls back to the file name",
                 JarName.chooseTitle("metal_slug_4", null, null, false, false),
                 "metal_slug_4");
        Check.eq("what the player typed wins over the manifest",
                 JarName.chooseTitle("ms4", "Metal Slug 4", "My Slug", true, true),
                 "My Slug");

        Check.section("an old sidecar's title is not a rename");
        Check.isTrue("a title equal to the file name is the file name",
                    !JarName.renamed("keysdemo", "keysdemo", false, false));
        Check.isTrue("a title that differs was a rename",
                    JarName.renamed("keysdemo", "My Keys", false, false));
        Check.isTrue("a marked title is a rename",
                    JarName.renamed("keysdemo", "keysdemo", true, true));
        Check.isTrue("an unmarked title is not",
                    !JarName.renamed("keysdemo", "My Keys", true, false));
        Check.isTrue("no saved title is not a rename",
                    !JarName.renamed("keysdemo", null, true, true));
        Check.eq("an old sidecar carrying the file name still shows the manifest name",
                 JarName.chooseTitle("keysdemo", "KeysDemo", "keysdemo", false, false),
                 "KeysDemo");

        Check.section("the import dialog and a re-import");
        File again = jar(tmp, "again.jar", "MIDlet-1", "First Name, , First");
        Check.eq("read once", JarName.of(again), "First Name");
        Check.eq("and again from the cache", JarName.of(again), "First Name");
        jar(tmp, "again.jar", "MIDlet-1",
            "A Much Longer Second Name, , Second", "MIDlet-Name", "ignored");
        Check.eq("a rewritten jar is read afresh", JarName.of(again),
                 "A Much Longer Second Name");

        Check.done();
    }

    /** Writes a jar whose manifest carries the given key/value pairs. */
    private static File jar(File dir, String name, String... kv) throws Exception {
        Manifest mf = new Manifest();
        Attributes at = mf.getMainAttributes();
        at.put(Attributes.Name.MANIFEST_VERSION, "1.0");
        for (int i = 0; i + 1 < kv.length; i += 2) {
            at.putValue(kv[i], kv[i + 1]);
        }
        File f = new File(dir, name);
        FileOutputStream fos = new FileOutputStream(f);
        JarOutputStream jos = new JarOutputStream(fos, mf);
        jos.putNextEntry(new java.util.jar.JarEntry("com/example/Midlet.class"));
        jos.write(new byte[]{(byte) 0xca, (byte) 0xfe, (byte) 0xba, (byte) 0xbe});
        jos.closeEntry();
        jos.close();
        return f;
    }

    private static File notAJar(File dir, String name) throws Exception {
        File f = new File(dir, name);
        OutputStream os = new FileOutputStream(f);
        os.write("this is not a zip file, whatever its name says".getBytes("UTF-8"));
        os.close();
        return f;
    }
}
