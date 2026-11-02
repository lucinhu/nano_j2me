import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.InputStream;
import javax.microedition.io.Connector;
import javax.microedition.io.file.FileConnection;
import javax.microedition.m3g.Camera;
import javax.microedition.m3g.Graphics3D;
import javax.microedition.m3g.Group;
import javax.microedition.m3g.Image2D;
import javax.microedition.m3g.Loader;
import javax.microedition.m3g.Mesh;
import javax.microedition.m3g.Node;
import javax.microedition.m3g.Object3D;
import javax.microedition.m3g.Sprite3D;
import javax.microedition.m3g.Texture2D;
import javax.microedition.m3g.Transform;
import javax.microedition.m3g.VertexBuffer;
import javax.microedition.m3g.World;
import javax.microedition.midlet.MIDlet;

/**
 * Loader/mutation harness for task-31.
 *
 * Reads "file:///root/mut/manifest.txt" from the runtime's fs dir and for each
 * listed file: reads the bytes, calls Loader.load(byte[], 0), then exercises
 * the returned objects - Object3D.duplicate(), the main getters, and (when
 * file:///root/mut/render.on exists) rendering loaded Worlds / Nodes through
 * Graphics3D into a 32x32 Image2D target.  This reaches construction, copy and
 * raster paths that plain Loader.load never touches.
 *
 * Expected bad data must surface as a Java exception.  An ASan/UBSan report, a
 * SIGSEGV or a hang is the finding; the "MUT i name" line printed before each
 * file maps it to the mutant.
 *
 * Kept Java 1.3 compatible (no generics, no varargs) like the other fixtures.
 */
public class M3gMutDemo extends MIDlet
{
    private static Graphics3D g3d;
    private static Image2D target;
    private static Transform identity;
    private static boolean renderOn;

    private static int dupOk, dupFail, getOk, getFail, renderOk, renderFail;
    private static int dupPrinted, getPrinted, renderPrinted;

    private static String readText(String url) throws Exception
    {
        FileConnection fc = (FileConnection) Connector.open(url, Connector.READ);
        try
        {
            InputStream in = fc.openInputStream();
            try
            {
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                byte[] b = new byte[512];
                int n;
                while ((n = in.read(b)) > 0)
                {
                    bo.write(b, 0, n);
                }
                return bo.toString();
            }
            finally
            {
                in.close();
            }
        }
        finally
        {
            fc.close();
        }
    }

    private static byte[] readAll(String url) throws Exception
    {
        FileConnection fc = (FileConnection) Connector.open(url, Connector.READ);
        try
        {
            int size = (int) fc.fileSize();
            if (size < 0)
            {
                size = 0;
            }
            byte[] out = new byte[size];
            DataInputStream in = fc.openDataInputStream();
            try
            {
                in.readFully(out);
            }
            finally
            {
                in.close();
            }
            return out;
        }
        finally
        {
            fc.close();
        }
    }

    private static void note(String tag, int idx, Throwable t)
    {
        if (tag.equals("DUPFAIL"))
        {
            if (dupPrinted++ < 20)
            {
                System.out.println("DUPFAIL " + idx + " "
                        + t.getClass().getName() + ": " + t.getMessage());
            }
        }
        else if (tag.equals("GETFAIL"))
        {
            if (getPrinted++ < 20)
            {
                System.out.println("GETFAIL " + idx + " "
                        + t.getClass().getName() + ": " + t.getMessage());
            }
        }
        else
        {
            if (renderPrinted++ < 20)
            {
                System.out.println("RENDERFAIL " + idx + " "
                        + t.getClass().getName() + ": " + t.getMessage());
            }
        }
    }

    private static void getters(Object3D o, int idx)
    {
        if (o instanceof Mesh)
        {
            Mesh m = (Mesh) o;
            int n = m.getSubmeshCount();
            if (n < 0)
            {
                n = 0;
            }
            if (n > 64)
            {
                n = 64;
            }
            for (int j = 0; j < n; j++)
            {
                m.getIndexBuffer(j);
                m.getAppearance(j);
            }
            VertexBuffer vb = m.getVertexBuffer();
            if (vb != null)
            {
                vb.getVertexCount();
            }
        }
        else if (o instanceof VertexBuffer)
        {
            ((VertexBuffer) o).getVertexCount();
        }
        else if (o instanceof Texture2D)
        {
            Image2D img = ((Texture2D) o).getImage();
            if (img != null)
            {
                img.getWidth();
                img.getHeight();
                img.getFormat();
            }
        }
        else if (o instanceof Sprite3D)
        {
            ((Sprite3D) o).getImage();
        }
        else if (o instanceof Image2D)
        {
            Image2D img = (Image2D) o;
            img.getWidth();
            img.getHeight();
            img.getFormat();
        }
        else if (o instanceof Group)
        {
            Group g = (Group) o;
            int n = g.getChildCount();
            if (n < 0)
            {
                n = 0;
            }
            if (n > 64)
            {
                n = 64;
            }
            for (int j = 0; j < n; j++)
            {
                g.getChild(j);
            }
        }
    }

    private static Camera synthCam;

    private static Camera findCamera(Object3D[] objs)
    {
        for (int i = 0; i < objs.length; i++)
        {
            if (objs[i] instanceof Camera)
            {
                return (Camera) objs[i];
            }
        }
        return null;
    }

    private static void render(Object3D[] objs, int idx)
    {
        if (!renderOn)
        {
            return;
        }
        Camera cam = findCamera(objs);
        if (cam == null)
        {
            cam = synthCam;
        }
        if (cam != null)
        {
            try
            {
                g3d.setCamera(cam, identity);
            }
            catch (Throwable t)
            {
                note("RENDERFAIL", idx, t);
            }
        }
        for (int i = 0; i < objs.length && i < 64; i++)
        {
            Object3D o = objs[i];
            if (o instanceof World)
            {
                World w = (World) o;
                try
                {
                    if (w.getActiveCamera() == null && synthCam != null)
                    {
                        w.addChild(synthCam);
                        w.setActiveCamera(synthCam);
                    }
                }
                catch (Throwable ignored)
                {
                }
                try
                {
                    g3d.render(w);
                    renderOk++;
                }
                catch (Throwable t)
                {
                    renderFail++;
                    note("RENDERFAIL", idx, t);
                }
            }
            else if (o instanceof Node && cam != null)
            {
                try
                {
                    g3d.render((Node) o, identity);
                    renderOk++;
                }
                catch (Throwable t)
                {
                    renderFail++;
                    note("RENDERFAIL", idx, t);
                }
            }
        }
    }

    private static void exercise(Object3D[] objs, int idx)
    {
        int max = objs.length;
        if (max > 128)
        {
            max = 128;
        }
        for (int i = 0; i < max; i++)
        {
            Object3D o = objs[i];
            if (o == null)
            {
                continue;
            }
            try
            {
                Object3D d = o.duplicate();
                if (d == null)
                {
                    dupFail++;
                }
                else
                {
                    dupOk++;
                }
            }
            catch (Throwable t)
            {
                dupFail++;
                note("DUPFAIL", idx, t);
            }
            try
            {
                getters(o, idx);
                getOk++;
            }
            catch (Throwable t)
            {
                getFail++;
                note("GETFAIL", idx, t);
            }
        }
        render(objs, idx);
    }

    protected void startApp()
    {
        int loaded = 0;
        int failed = 0;
        int total = 0;
        try
        {
            try
            {
                readText("file:///root/mut/render.on");
                renderOn = true;
            }
            catch (Throwable ignored)
            {
                renderOn = false;
            }
            if (renderOn)
            {
                try
                {
                    target = new Image2D(Image2D.RGB, 32, 32);
                    g3d = Graphics3D.getInstance();
                    g3d.bindTarget(target);
                    identity = new Transform();
                    synthCam = new Camera();
                    synthCam.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
                }
                catch (Throwable t)
                {
                    System.out.println("RENDER-SETUP-FAIL " + t.getClass().getName()
                            + ": " + t.getMessage());
                    renderOn = false;
                }
            }
            String man = readText("file:///root/mut/manifest.txt");
            int pos = 0;
            int len = man.length();
            while (pos < len)
            {
                int e = man.indexOf('\n', pos);
                if (e < 0)
                {
                    e = len;
                }
                String name = man.substring(pos, e).trim();
                pos = e + 1;
                if (name.length() == 0)
                {
                    continue;
                }
                total++;
                System.out.println("MUT " + total + " " + name);
                try
                {
                    byte[] data = readAll("file:///root/mut/" + name);
                    Object3D[] objs = Loader.load(data, 0);
                    if (objs == null)
                    {
                        System.out.println("E " + total + " null-array");
                        failed++;
                    }
                    else
                    {
                        System.out.println("OK " + total + " bytes=" + data.length
                                + " objs=" + objs.length);
                        loaded++;
                        exercise(objs, total);
                    }
                }
                catch (Throwable t)
                {
                    System.out.println("E " + total + " "
                            + t.getClass().getName() + ": " + t.getMessage());
                    failed++;
                }
                if ((total % 100) == 0)
                {
                    System.out.println("PROGRESS " + total + " loaded=" + loaded
                            + " failed=" + failed);
                }
            }
        }
        catch (Throwable t)
        {
            System.out.println("HARNESS-ERROR " + t.getClass().getName() + ": "
                    + t.getMessage());
        }
        System.out.println("MUTDONE total=" + total + " loaded=" + loaded
                + " failed=" + failed + " dupOk=" + dupOk + " dupFail=" + dupFail
                + " getOk=" + getOk + " getFail=" + getFail
                + " render=" + (renderOn ? 1 : 0) + " renderOk=" + renderOk
                + " renderFail=" + renderFail);
        notifyDestroyed();
    }

    protected void pauseApp()
    {
    }

    protected void destroyApp(boolean unconditional)
    {
    }
}
