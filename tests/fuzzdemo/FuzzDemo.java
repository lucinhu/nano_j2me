import javax.microedition.io.Connector;
import javax.microedition.io.file.FileConnection;
import javax.microedition.lcdui.Image;
import javax.microedition.m3g.AnimationController;
import javax.microedition.m3g.AnimationTrack;
import javax.microedition.m3g.KeyframeSequence;
import javax.microedition.m3g.MorphingMesh;
import javax.microedition.m3g.SkinnedMesh;
import javax.microedition.m3g.Appearance;
import javax.microedition.m3g.Background;
import javax.microedition.m3g.Camera;
import javax.microedition.m3g.CompositingMode;
import javax.microedition.m3g.Fog;
import javax.microedition.m3g.Graphics3D;
import javax.microedition.m3g.Group;
import javax.microedition.m3g.Image2D;
import javax.microedition.m3g.IndexBuffer;
import javax.microedition.m3g.Light;
import javax.microedition.m3g.Loader;
import javax.microedition.m3g.Material;
import javax.microedition.m3g.Mesh;
import javax.microedition.m3g.Node;
import javax.microedition.m3g.Object3D;
import javax.microedition.m3g.PolygonMode;
import javax.microedition.m3g.Sprite3D;
import javax.microedition.m3g.Texture2D;
import javax.microedition.m3g.Transform;
import javax.microedition.m3g.Transformable;
import javax.microedition.m3g.TriangleStripArray;
import javax.microedition.m3g.VertexArray;
import javax.microedition.m3g.VertexBuffer;
import javax.microedition.m3g.World;
import javax.microedition.rms.RecordStore;
import javax.microedition.midlet.MIDlet;

/**
 * Seeded edge-case fuzz driver for the JSR 184 (M3G) and JSR 75 APIs.
 *
 * The driver is deliberately dumb about the API contract: it feeds random
 * combinations of legal and illegal arguments to every entry point that has a
 * plausible crash path, prints "F <op> <label>" before the call and
 * "E <op> <exception>" when the runtime answers with a Throwable, and keeps
 * going.  A JVM-level crash (SEGV/ASan/UBSan report), a hang, or a corrupted
 * subsequent state is the finding; a Java exception is just a data point.
 *
 * The sequence is a deterministic function of seed+ops, so the last "F" line
 * printed before a sanitizer abort identifies the minimised case.
 */
public class FuzzDemo extends MIDlet {

    static final int SEEDS[] = { 0x5eed0001, 0x5eed0002, 0x5eed0003, 0x5eed0011, 0x5eed0012, 0x5eed0021, 0x5eed0022, 0x5eed0023 };
    static final int OPS_PER_SEED = 400;

    static long rng;
    static int op;
    static int excs;
    static int oom;

    static String[] excName = new String[48];
    static int[] excCount = new int[48];
    static int excKinds;

    static float lastFloat = 1.0f;

    static void seed(long s) {
        rng = s;
    }

    static long nextLong() {
        rng = rng * 6364136223846793005L + 1442695040888963407L;
        return rng;
    }

    static int nextInt(int n) {
        if (n <= 0) {
            return 0;
        }
        long v = nextLong() >>> 33;
        return (int) (v % n);
    }

    static int mix(int i) {
        long x = (long) i * 0x9E3779B97F4A7C15L + rng;
        x ^= x >>> 29;
        return (int) x;
    }

    static int pickInt(int[] a) {
        return a[nextInt(a.length)];
    }

    static float pickFloat() {
        int k = nextInt(12);
        switch (k) {
            case 0: return 0.0f;
            case 1: return -0.0f;
            case 2: return 1.0f;
            case 3: return -1.0f;
            case 4: return 2.5f;
            case 5: return 1.0e30f;
            case 6: return -1.0e30f;
            case 7: return Float.NaN;
            case 8: return Float.POSITIVE_INFINITY;
            case 9: return Float.NEGATIVE_INFINITY;
            case 10: return 0.5f;
            default: return (float) (nextLong() % 2000000L) / 128.0f;
        }
    }

    static byte[] bytes(int n) {
        byte[] a = new byte[n < 0 ? 0 : n];
        for (int i = 0; i < a.length; i++) {
            a[i] = (byte) nextInt(256);
        }
        return a;
    }

    static short[] shorts(int n) {
        short[] a = new short[n < 0 ? 0 : n];
        for (int i = 0; i < a.length; i++) {
            a[i] = (short) nextInt(65536);
        }
        return a;
    }

    static int[] ints(int n) {
        int[] a = new int[n < 0 ? 0 : n];
        for (int i = 0; i < a.length; i++) {
            a[i] = mix(i) % 17;
        }
        return a;
    }

    static float[] floats(int n) {
        float[] a = new float[n < 0 ? 0 : n];
        for (int i = 0; i < a.length; i++) {
            a[i] = pickFloat();
        }
        return a;
    }

    static void begin(int i, String label) {
        op++;
        System.out.println("F " + i + " " + label);
    }

    static void record(Throwable t) {
        excs++;
        String n = t.getClass().getName();
        for (int i = 0; i < excKinds; i++) {
            if (excName[i].equals(n)) {
                excCount[i]++;
                return;
            }
        }
        if (excKinds < excName.length) {
            excName[excKinds] = n;
            excCount[excKinds] = 1;
            excKinds++;
        }
    }

    static void fail(int i, Throwable t) {
        record(t);
        if (t instanceof OutOfMemoryError) {
            oom++;
        }
        System.out.println("E " + i + " " + t.getClass().getName() + ": " + t.getMessage());
    }

    static void failA(int i, Throwable t) {
        record(t);
        if (t instanceof OutOfMemoryError) {
            oom++;
        }
        System.out.println("E " + i + " " + t.getClass().getName());
    }

    // ---------------------------------------------------------------- objects

    static Image target;
    static Image smallTarget;
    static Graphics3D g3d;

    static final int COMPONENTS[] = { 0, 1, 2, 3, 4, 5, 8, 16, -1, 255 };
    static final int COUNTS[] = { 0, 1, 2, 3, 4, 5, 16, 64, 300, -1 };
    static final int SIZES[] = { 0, 1, 2, 3, 4, 5, 8, -1, 16 };
    static final int FIRST[] = { -100, -1, 0, 1, 2, 3, 62, 64, 0x3FFFFFFF, 0x7FFFFFFF };
    static final int NVERTS[] = { -100, -1, 0, 1, 2, 3, 7, 64, 0x3FFFFFFF };
    static final int FORMATS[] = { 96, 97, 98, 99, 100, 0, 1, 101, 200, -1 };
    static final int DIMS[] = { -1, 0, 1, 2, 3, 4, 5, 16, 31, 256, 4096 };
    static final int UNITS[] = { -1, 0, 1, 2, 7, 8, 15, 16, 255, 0x7FFFFFFF };
    static final int WRAP[] = { -1, 0, 239, 240, 241, 242, 0x7FFFFFFF };
    static final int FILTER[] = { -1, 0, 207, 208, 209, 210, 211, 0x7FFFFFFF };
    static final int FUNCS[] = { -1, 0, 223, 224, 225, 226, 227, 228, 229, 0x7FFFFFFF };
    static final int MODES[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                 16, 17, 18, 19, 20, 21, 32, 64, 0x7FFFFFFF };
    static final int PRIMS[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                 16, 17, 18, 19, 20, 21, 22, 23, 0x7FFFFFFF };
    static final int LIGHT_MODES[] = { 128, 129, 130, 131, 0, -1, 132, 0x7FFFFFFF };

    static VertexArray vaAny() {
        int c = pickInt(COMPONENTS);
        int n = pickInt(COUNTS);
        int s = pickInt(SIZES);
        return new VertexArray(c, n, s);
    }

    static VertexArray vaValid(int comp, int count) {
        VertexArray a = new VertexArray(comp, count, comp == 1 ? 1 : 2);
        if (comp == 1) {
            a.set(0, count, bytes(count));
        } else {
            a.set(0, count, shorts(count * comp));
        }
        return a;
    }

    static VertexBuffer vbAny() {
        VertexBuffer vb = new VertexBuffer();
        int which = nextInt(8);
        try {
            switch (which) {
                case 0: vb.setPositions(vaAny(), pickFloat(), floats(nextInt(8) - 2)); break;
                case 1: vb.setColors(vaAny()); break;
                case 2: vb.setNormals(vaAny()); break;
                case 3: vb.setTexCoords(pickInt(UNITS), vaAny(), pickFloat(), floats(nextInt(8) - 2)); break;
                default: {
                    VertexArray p = vaValid(3, 3);
                    vb.setPositions(p, 1.0f, null);
                    int k = nextInt(4);
                    if (k == 0) vb.setColors(vaValid(3, 3));
                    if (k == 1) vb.setColors(vaValid(4, 3));
                    if (k == 2) vb.setNormals(vaValid(3, 3));
                    if (k == 3) vb.setTexCoords(nextInt(8), vaValid(2, 3), 1.0f, null);
                }
            }
        } catch (Throwable t) {
            /* leave the buffer as-is; the caller is allowed to see it half-set */
        }
        return vb;
    }

    static IndexBuffer ibAny() {
        int k = nextInt(6);
        switch (k) {
            case 0: return new TriangleStripArray(pickInt(FIRST), ints(nextInt(5)));
            case 1: {
                int[] ind = ints(nextInt(10));
                return new TriangleStripArray(ind, ints(nextInt(5)));
            }
            case 2: {
                int[] lens = new int[nextInt(4)];
                for (int i = 0; i < lens.length; i++) {
                    lens[i] = pickInt(NVERTS);
                }
                return new TriangleStripArray(ints(9), lens);
            }
            case 3: return new TriangleStripArray(0, new int[] { 3, 3 });
            case 4: return new TriangleStripArray(new int[] { 0, 1, 2, 0, 2, 3 }, new int[] { 3, 3 });
            default: return new TriangleStripArray(new int[] { 0, 1, 2, 0, 1, 3, 0, 2, 3 }, null);
        }
    }

    static Appearance appearanceAny() {
        Appearance a = new Appearance();
        try {
            int k = nextInt(6);
            if (k == 0) a.setTexture(pickInt(UNITS), texAny());
            if (k == 1) a.setMaterial(new Material());
            if (k == 2) a.setFog(new Fog());
            if (k == 3) a.setPolygonMode(new PolygonMode());
            if (k == 4) a.setCompositingMode(new CompositingMode());
            if (k == 5) a.setLayer(pickInt(NVERTS));
        } catch (Throwable t) {
            /* ignore */
        }
        return a;
    }

    static Image2D imgAny() {
        int k = nextInt(6);
        switch (k) {
            case 0: return new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS), bytes(nextInt(20)));
            case 1: return new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS));
            case 2: return new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS), bytes(nextInt(20)), bytes(nextInt(20)));
            default: return new Image2D(99, 2, 2, bytes(12));
        }
    }

    static Texture2D texAny() {
        Texture2D t = null;
        try {
            t = new Texture2D(imgAny());
        } catch (Throwable x) {
            return null;
        }
        try {
            t.setWrapping(pickInt(WRAP), pickInt(WRAP));
            t.setFiltering(pickInt(FILTER), pickInt(FILTER));
            t.setBlending(pickInt(FUNCS));
            t.setBlendColor(mix(7));
            t.setImage(imgAny());
        } catch (Throwable x) {
            /* ignore */
        }
        return t;
    }

    static Transform xfAny() {
        Transform t = new Transform();
        try {
            int k = nextInt(5);
            if (k == 0) t.set(floats(nextInt(20)));
            if (k == 1) t.postRotate(pickFloat(), pickFloat(), pickFloat(), pickFloat());
            if (k == 2) t.postScale(pickFloat(), pickFloat(), pickFloat());
            if (k == 3) t.postTranslate(pickFloat(), pickFloat(), pickFloat());
            if (k == 4) t.invert();
        } catch (Throwable x) {
            /* ignore */
        }
        return t;
    }

    static Node nodeAny() {
        int k = nextInt(6);
        Node n;
        switch (k) {
            case 0: n = new Group(); break;
            case 1: n = new Mesh(vbAny(), ibAny(), appearanceAny()); break;
            case 2: n = new Sprite3D(true, null, appearanceAny()); break;
            case 3: {
                VertexBuffer vb = vbAny();
                IndexBuffer ib0 = ibAny();
                Node m;
                try {
                    m = new Mesh(vb, new IndexBuffer[] { ib0, ibAny() },
                                 new Appearance[] { appearanceAny(), appearanceAny() });
                } catch (Throwable t) {
                    m = new Group();
                }
                n = m;
                break;
            }
            case 4: n = new Camera(); break;
            default: n = new Light(); break;
        }
        return n;
    }

    // ---------------------------------------------------------------- M3G ops

    static void opVertexArray(int i) {
        begin(i, "VertexArray ctor/set/get");
        try {
            VertexArray a = vaAny();
            int first = pickInt(FIRST);
            int n = pickInt(NVERTS);
            if (nextInt(2) == 0) {
                short[] dst = shorts(nextInt(20));
                a.get(first, n, dst);
            } else {
                byte[] dst = bytes(nextInt(20));
                a.get(first, n, dst);
            }
            if (nextInt(2) == 0) {
                a.set(pickInt(FIRST), pickInt(NVERTS), shorts(nextInt(24)));
            } else {
                a.set(pickInt(FIRST), pickInt(NVERTS), bytes(nextInt(24)));
            }
            a.getVertexCount();
            a.getComponentCount();
            a.getComponentType();
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opVertexArrayExtreme(int i) {
        begin(i, "VertexArray extreme");
        try {
            VertexArray a = new VertexArray(4, 0x40000000, 2);
            a.set(0, 0x3FFFFFFF, shorts(4));
        } catch (Throwable t) {
            fail(i, t);
        }
        try {
            new VertexArray(-1, -1, -1);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opVertexBuffer(int i) {
        begin(i, "VertexBuffer set/get");
        try {
            VertexBuffer vb = new VertexBuffer();
            switch (nextInt(8)) {
                case 0: vb.setPositions(vaAny(), pickFloat(), floats(nextInt(6))); break;
                case 1: vb.setPositions(vaValid(2, 3), pickFloat(), floats(4)); break;
                case 2: vb.setColors(vaAny()); break;
                case 3: vb.setNormals(vaAny()); break;
                case 4: vb.setTexCoords(pickInt(UNITS), vaAny(), pickFloat(), floats(nextInt(6))); break;
                case 5: vb.setTexCoords(0, vaValid(1, 3), 1.0f, null); break;
                case 6: vb.setPositions(null, 1.0f, null); break;
                default: vb.setPositions(vaValid(4, 3), 1.0f, floats(4)); break;
            }
            vb.getVertexCount();
            vb.getPositions(floats(nextInt(8)));
            vb.getNormals();
            vb.getColors();
            vb.getTexCoords(pickInt(UNITS), floats(nextInt(8)));
            vb.setDefaultColor(mix(9));
            vb.getDefaultColor();
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opIndexBuffer(int i) {
        begin(i, "TriangleStripArray malformed");
        try {
            IndexBuffer ib = ibAny();
            ib.getIndexCount();
            int n = ib.getIndexCount();
            int[] dst = ints(nextInt(12));
            ((TriangleStripArray) ib).getIndices(dst);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opImage2D(int i) {
        begin(i, "Image2D odd format/dims/pixels");
        try {
            Image2D img = imgAny();
            img.isMutable();
            img.getFormat();
            img.getWidth();
            img.getHeight();
            if (img.isMutable()) {
                img.set(pickInt(FIRST), pickInt(FIRST), nextInt(8) - 2, nextInt(8) - 2, bytes(nextInt(32)));
            }
            new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS), null, bytes(nextInt(12)));
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opTexture2D(int i) {
        begin(i, "Texture2D odd values");
        try {
            Texture2D t = texAny();
            if (t != null) {
                t.getWrappingS();
                t.getWrappingT();
                t.getLevelFilter();
                t.getImageFilter();
                t.getBlending();
                t.getBlendColor();
                t.getImage();
                t.setImage(null);
            }
            new Texture2D(null);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opAppearance(int i) {
        begin(i, "Appearance/Material/Fog/PolygonMode/CompositingMode");
        try {
            Appearance a = new Appearance();
            a.setTexture(pickInt(UNITS), texAny());
            a.getTexture(pickInt(UNITS));
            a.setTexture(-1, null);
            Material m = new Material();
            m.setColor(mix(11), mix(12));
            m.getColor(pickInt(NVERTS));
            m.setShininess(pickFloat());
            m.setVertexColorTrackingEnable(nextInt(2) == 0);
            m.isVertexColorTrackingEnabled();
            Fog f = new Fog();
            f.setMode(pickInt(LIGHT_MODES));
            f.setColor(mix(13));
            f.setDensity(pickFloat());
            f.setLinear(pickFloat(), pickFloat());
            PolygonMode pm = new PolygonMode();
            pm.setCulling(pickInt(NVERTS));
            pm.setShading(pickInt(NVERTS));
            pm.setWinding(pickInt(NVERTS));
            pm.setTwoSidedLightingEnable(nextInt(2) == 0);
            CompositingMode cm = new CompositingMode();
            cm.setBlending(pickInt(FUNCS));
            cm.setAlphaThreshold(pickFloat());
            cm.setAlphaWriteEnable(nextInt(2) == 0);
            cm.setColorWriteEnable(nextInt(2) == 0);
            cm.setDepthWriteEnable(nextInt(2) == 0);
            cm.setDepthTestEnable(nextInt(2) == 0);
            cm.setDepthOffset(pickFloat(), pickFloat());
            a.setMaterial(m);
            a.setFog(f);
            a.setPolygonMode(pm);
            a.setCompositingMode(cm);
            a.setLayer(pickInt(NVERTS));
            a.getLayer();
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opTransformable(int i) {
        begin(i, "Transformable odd values");
        try {
            Transformable t = new Group();
            t.setOrientation(pickFloat(), pickFloat(), pickFloat(), pickFloat());
            t.preRotate(pickFloat(), pickFloat(), pickFloat(), pickFloat());
            t.postRotate(pickFloat(), pickFloat(), pickFloat(), pickFloat());
            t.getOrientation(floats(nextInt(8) - 2));
            t.setScale(pickFloat(), pickFloat(), pickFloat());
            t.scale(pickFloat(), pickFloat(), pickFloat());
            t.getScale(floats(nextInt(8) - 2));
            t.setTranslation(pickFloat(), pickFloat(), pickFloat());
            t.translate(pickFloat(), pickFloat(), pickFloat());
            t.getTranslation(floats(nextInt(8) - 2));
            t.setTransform(xfAny());
            t.getTransform(xfAny());
            t.getCompositeTransform(xfAny());
            t.setTransform(null);
            t.getTransform(null);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opTransform(int i) {
        begin(i, "Transform matrix odd arrays");
        try {
            Transform t = new Transform();
            t.set(floats(nextInt(20)));
            t.get(floats(nextInt(20) - 2));
            t.get(null);
            t.set((Transform) null);
            t.setIdentity();
            t.postMultiply(xfAny());
            t.postMultiply(null);
            t.postRotateQuat(pickFloat(), pickFloat(), pickFloat(), pickFloat());
            t.transpose();
            t.invert();
            t.transform(floats(nextInt(20) - 2));
            t.transform(null);
            VertexArray va = vaAny();
            t.transform(va, floats(nextInt(12) - 2), nextInt(2) == 0);
            t.transform(va, null, false);
            t.set(floats(16));
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opNodeGraph(int i) {
        begin(i, "Group/Node graph edges");
        try {
            Group g = new Group();
            Group a = new Group();
            Group b = new Group();
            g.addChild(a);
            a.addChild(b);
            try {
                g.addChild(g);
            } catch (Throwable t) {
                record(t);
            }
            try {
                b.addChild(g);
            } catch (Throwable t) {
                record(t);
            }
            try {
                g.addChild(null);
            } catch (Throwable t) {
                record(t);
            }
            try {
                g.removeChild(null);
            } catch (Throwable t) {
                record(t);
            }
            g.removeChild(new Group());
            g.getChildCount();
            g.getChild(pickInt(NVERTS));
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opNodeQueries(int i) {
        begin(i, "Node transform queries");
        try {
            Group root = new Group();
            Group g1 = new Group();
            Group g2 = new Group();
            root.addChild(g1);
            g1.addChild(g2);
            g2.getParent();
            g2.getTransformTo(null, xfAny());
            g2.getTransformTo(g2, xfAny());
            g2.getTransformTo(root, xfAny());
            new Group().getTransformTo(g2, xfAny());
            g2.getTransformTo(root, new Transform());
            Node n = nodeAny();
            n.setRenderingEnable(nextInt(2) == 0);
            n.setPickingEnable(nextInt(2) == 0);
            n.setScope(pickInt(NVERTS));
            n.setAlphaFactor(pickFloat());
            n.getScope();
            n.getAlphaFactor();
            n.getParent();
            n.getTransformTo(null, null);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opMeshRender(int i) {
        begin(i, "Mesh/render with odd buffers");
        try {
            World w = new World();
            Camera cam = new Camera();
            cam.setPerspective(60.0f, 1.5f, 0.1f, 100.0f);
            w.addChild(cam);
            w.setActiveCamera(cam);
            Mesh m = new Mesh(vbAny(), ibAny(), appearanceAny());
            w.addChild(m);
            g3d.bindTarget(smallTarget);
            try {
                g3d.render(w);
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opMeshCtor(int i) {
        begin(i, "Mesh/Submesh odd constructors");
        try {
            VertexBuffer vb = vbAny();
            IndexBuffer ib0 = ibAny();
            new Mesh(vb, ib0, null);
            new Mesh((VertexBuffer) null, (IndexBuffer) null, (Appearance) null);
            new Mesh(vb, null, appearanceAny());
            Mesh mm = new Mesh(vb, new IndexBuffer[] { ib0, ibAny() },
                               new Appearance[] { appearanceAny() });
            mm.setAppearance(0, appearanceAny());
            mm.setAppearance(pickInt(NVERTS), null);
            mm.getAppearance(pickInt(NVERTS));
            mm.getIndexBuffer(pickInt(NVERTS));
            mm.getSubmeshCount();
            mm.getVertexBuffer();
            Sprite3D s = new Sprite3D(true, null, null);
            s.setCrop(pickInt(NVERTS), pickInt(NVERTS), pickInt(NVERTS), pickInt(NVERTS));
            s.getCropX();
            s.getCropWidth();
            s.isScaled();
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opCamera(int i) {
        begin(i, "Camera projection odd values");
        try {
            Camera c = new Camera();
            switch (nextInt(4)) {
                case 0: c.setPerspective(pickFloat(), pickFloat(), pickFloat(), pickFloat()); break;
                case 1: c.setParallel(pickFloat(), pickFloat(), pickFloat(), pickFloat()); break;
                case 2: c.setGeneric(xfAny()); break;
                default: c.setGeneric(null); break;
            }
            c.getProjection(xfAny());
            float[] f = floats(nextInt(18) - 2);
            c.getProjection(f);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opLight(int i) {
        begin(i, "Light odd values");
        try {
            Light l = new Light();
            l.setMode(pickInt(LIGHT_MODES));
            l.setIntensity(pickFloat());
            l.setColor(mix(15));
            l.setSpotAngle(pickFloat());
            l.setSpotExponent(pickFloat());
            l.setAttenuation(pickFloat(), pickFloat(), pickFloat());
            l.getMode();
            l.getIntensity();
            l.getColor();
            l.getSpotAngle();
            l.getSpotExponent();
            l.getConstantAttenuation();
            l.getLinearAttenuation();
            l.getQuadraticAttenuation();
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opGraphicsState(int i) {
        begin(i, "Graphics3D state odd values");
        try {
            g3d.getHints();
            g3d.setViewport(pickInt(NVERTS), pickInt(NVERTS), pickInt(NVERTS), pickInt(NVERTS));
            g3d.getViewportX();
            g3d.getViewportY();
            g3d.getViewportWidth();
            g3d.getViewportHeight();
            g3d.setDepthRange(pickFloat(), pickFloat());
            g3d.getDepthRangeNear();
            g3d.getDepthRangeFar();
            g3d.isDepthBufferEnabled();
            g3d.getTarget();
            g3d.releaseTarget();
            g3d.releaseTarget();
            g3d.clear(null);
            g3d.clear(new Background());
            g3d.resetLights();
            g3d.getLightCount();
            g3d.getLight(pickInt(NVERTS), xfAny());
            g3d.addLight(pickInt(NVERTS) > 0 ? new Light() : null, xfAny());
            g3d.setLight(pickInt(NVERTS), new Light(), xfAny());
            g3d.setCamera(null, xfAny());
            g3d.getCamera(xfAny());
            g3d.setViewport(0, 0, 1, 1);
            g3d.setDepthRange(0.0f, 0.0f);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opBindTarget(int i) {
        begin(i, "bindTarget odd objects");
        try {
            switch (nextInt(5)) {
                case 0: g3d.bindTarget(null); break;
                case 1: g3d.bindTarget(new Object()); break;
                case 2: g3d.bindTarget(new Transform()); break;
                case 3: g3d.bindTarget(target); break;
                default: g3d.bindTarget(target, nextInt(2) == 0, mix(16)); break;
            }
            g3d.getTarget();
            g3d.releaseTarget();
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opImmediate(int i) {
        begin(i, "immediate render odd formats");
        try {
            g3d.bindTarget(smallTarget);
            try {
                VertexBuffer vb = vbAny();
                IndexBuffer ib = ibAny();
                Appearance app = appearanceAny();
                int mode = pickInt(MODES);
                switch (nextInt(5)) {
                    case 0:
                        g3d.render(vb, ib, app, xfAny(), mode);
                        break;
                    case 1:
                        g3d.render(vb, null, app, xfAny(), mode);
                        break;
                    case 2:
                        g3d.render(null, ib, app, xfAny(), mode);
                        break;
                    case 3:
                        g3d.render(vb, ib, app, null, mode);
                        break;
                    default:
                        g3d.render(vb, ib, null, null);
                        break;
                }
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opRenderWorld(int i) {
        begin(i, "render(World) null camera/wrong target");
        try {
            World w = new World();
            Group g = new Group();
            g.addChild(new Light());
            w.addChild(g);
            if (nextInt(2) == 0) {
                Camera c = new Camera();
                w.addChild(c);
                w.setActiveCamera(c);
            } else {
                w.setActiveCamera(null);
            }
            g3d.bindTarget(smallTarget);
            try {
                g3d.render(w);
            } finally {
                g3d.releaseTarget();
            }
            try {
                g3d.render((World) null);
            } catch (Throwable t) {
                record(t);
            }
            Group noCam = new Group();
            g3d.bindTarget(smallTarget);
            try {
                g3d.render(noCam, xfAny());
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opWorld(int i) {
        begin(i, "World/Background/Fog odd edits");
        try {
            World w = new World();
            Background b = new Background();
            b.setColor(mix(17));
            b.setColorClearEnable(nextInt(2) == 0);
            b.setDepthClearEnable(nextInt(2) == 0);
            b.setCrop(pickInt(NVERTS), nextInt(8) - 2, nextInt(8) - 2, nextInt(8) - 2);
            b.setImage(imgAny());
            b.setImageMode(pickInt(NVERTS), pickInt(NVERTS));
            w.setBackground(b);
            w.getBackground();
            w.setActiveCamera(null);
            w.getActiveCamera();
            w.addChild(null);
            w.addChild(new Group());
            w.getChild(pickInt(NVERTS));
            w.removeChild(new Group());
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    static void opPick(int i) {
        begin(i, "Group.pick odd rays");
        try {
            Group g = new Group();
            g.addChild(new Mesh(vbAny(), ibAny(), appearanceAny()));
            javax.microedition.m3g.RayIntersection ri = new javax.microedition.m3g.RayIntersection();
            g.pick(pickInt(NVERTS), pickFloat(), pickFloat(), pickFloat(), pickFloat(), pickFloat(), pickFloat(), ri);
            javax.microedition.m3g.RayIntersection ri2 = new javax.microedition.m3g.RayIntersection();
            g.pick(pickInt(MODES), pickFloat(), pickFloat(), new Camera(), ri2);
        } catch (Throwable t) {
            fail(i, t);
        }
    }

    // ------------------------------------------------------------ JSR 75 ops

    static void opFileConnection(int i) {
        begin(i, "FileConnection closed/mode paths");
        FileConnection fc = null;
        try {
            int mode = 1 + nextInt(3);
            fc = (FileConnection) Connector.open("file:///root/fuzz" + (i % 3) + ".dat", mode);
            fc.exists();
            fc.isDirectory();
            fc.fileSize();
            fc.lastModified();
            fc.canRead();
            fc.canWrite();
            fc.isHidden();
            fc.getName();
            fc.getPath();
            fc.getURL();
            try {
                fc.create();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.mkdir();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.list();
            } catch (Throwable t) {
                record(t);
            }
            fc.close();
            try {
                fc.exists();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.create();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.mkdir();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.delete();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.list();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.openInputStream();
            } catch (Throwable t) {
                record(t);
            }
            try {
                fc.close();
            } catch (Throwable t) {
                record(t);
            }
        } catch (Throwable t) {
            fail(i, t);
        } finally {
            if (fc != null) {
                try {
                    fc.close();
                } catch (Throwable t) {
                    /* ignore */
                }
            }
        }
    }

    static void opFileUrls(int i) {
        begin(i, "FileConnection odd URLs/names");
        String[] urls = {
            "file:///root/", "file:///root", "file:///root/../etc/passwd",
            "file:///root/sub/../fuzz.txt", "file:///root//fuzz.txt", "file:///root/fuzz dir/x",
            "file:///root/sub", "file:///root/sub/", "file:///root/nul\0byte",
            "file:///root/a/b/c/d/e/f/g", "file:///other/x", "http://x/"
        };
        String u = urls[nextInt(urls.length)];
        try {
            FileConnection fc = (FileConnection) Connector.open(u, 1 + nextInt(3));
            try {
                fc.exists();
                fc.isDirectory();
                fc.getName();
                fc.getPath();
                try {
                    fc.list(nextInt(3) == 0 ? null : "*.*", nextInt(2) == 0);
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.list("", false);
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.rename("renamed" + (i % 3));
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.truncate(pickInt(FIRST));
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.openInputStream();
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.openOutputStream();
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.openOutputStream(pickInt(FIRST));
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.openDataInputStream();
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.openDataOutputStream();
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    fc.setFileConnection((String) null);
                } catch (Throwable t) {
                    record(t);
                }
            } finally {
                fc.close();
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opRecordStore(int i) {
        begin(i, "RecordStore odd values");
        RecordStore rs = null;
        try {
            String[] names = { "fuzz", "", "a b", "\u00e9\u00e9", "root/" + (i % 3) };
            rs = RecordStore.openRecordStore(names[nextInt(names.length)], true);
            rs.addRecord(rs.getNumRecords() >= 0 ? bytes(nextInt(32)) : null, 0, nextInt(32));
            rs.addRecord(null, 0, 0);
            int id = pickInt(FIRST);
            try {
                rs.getRecord(id);
            } catch (Throwable t) {
                record(t);
            }
            try {
                rs.getRecordSize(id);
            } catch (Throwable t) {
                record(t);
            }
            try {
                rs.setRecord(id, bytes(nextInt(8)), 0, nextInt(8));
            } catch (Throwable t) {
                record(t);
            }
            try {
                rs.deleteRecord(id);
            } catch (Throwable t) {
                record(t);
            }
            rs.getNumRecords();
            rs.getSize();
            rs.getSizeAvailable();
            rs.getName();
            rs.listRecordStores();
        } catch (Throwable t) {
            failA(i, t);
        } finally {
            if (rs != null) {
                try {
                    rs.closeRecordStore();
                } catch (Throwable t) {
                    /* ignore */
                }
            }
        }
    }


    // ------------------------------------------------------- wave 2 probes

    static void opRenderStress(int i) {
        begin(i, "valid scene render stress");
        try {
            World w = new World();
            Camera cam = new Camera();
            cam.setPerspective(45.0f, 1.333f, 0.5f, 100.0f);
            w.addChild(cam);
            w.setActiveCamera(cam);
            Light lt = new Light();
            lt.setMode(Light.AMBIENT);
            w.addChild(lt);
            VertexBuffer vb = new VertexBuffer();
            vb.setPositions(vaValid(3, 3), 1.0f, null);
            vb.setColors(vaValid(3, 3));
            vb.setNormals(vaValid(3, 3));
            vb.setTexCoords(0, vaValid(2, 3), 1.0f, null);
            IndexBuffer ib = new TriangleStripArray(new int[] { 0, 1, 2 }, new int[] { 3 });
            Appearance ap = new Appearance();
            ap.setMaterial(new Material());
            Texture2D tex = new Texture2D(new Image2D(99, 8, 8));
            tex.setWrapping(Texture2D.WRAP_REPEAT, Texture2D.WRAP_REPEAT);
            tex.setFiltering(Texture2D.FILTER_LINEAR, Texture2D.FILTER_NEAREST);
            ap.setTexture(0, tex);
            ap.setCompositingMode(new CompositingMode());
            Mesh m = new Mesh(vb, ib, ap);
            w.addChild(m);
            Background bg = new Background();
            bg.setColor(0x102030);
            w.setBackground(bg);
            for (int k = 0; k < 36; k++) {
                g3d.bindTarget(target);
                try {
                    switch (k % 7) {
                        case 0: g3d.setViewport(0, 0, 32, 32); break;
                        case 1: g3d.setViewport(-16, -16, 64, 64); break;
                        case 2: g3d.setViewport(0, 0, 0x7FFFFFFF, 0x7FFFFFFF); break;
                        case 3: g3d.setViewport(0, 0, 7, 5); break;
                        case 4: g3d.setViewport(31, 31, 2, 2); break;
                        case 5: g3d.setViewport(-0x40000000, -0x40000000, 0x40000000, 0x40000000); break;
                        default: g3d.setViewport(0, 0, 1, 1); break;
                    }
                    g3d.setDepthRange(k % 2 == 0 ? 0.0f : 1.0f, k % 3 == 0 ? 0.0f : 1.0f);
                    g3d.clear(bg);
                    g3d.render(w);
                    Transform tr = new Transform();
                    tr.postRotate(k * 17.0f, 1.0f, 0.0f, 0.0f);
                    g3d.render(m, tr);
                } finally {
                    g3d.releaseTarget();
                }
            }
            g3d.bindTarget(target);
            g3d.bindTarget(smallTarget);
            g3d.releaseTarget();
            g3d.releaseTarget();
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opRasterExtreme(int i) {
        begin(i, "NaN/Inf raster");
        try {
            g3d.bindTarget(smallTarget);
            try {
                for (int k = 0; k < 6; k++) {
                    float v = pickFloat();
                    VertexArray p = new VertexArray(3, 3, 2);
                    p.set(0, 3, new short[] { 0, 0, 0, 1, 0, 0, 0, 1, 0 });
                    VertexBuffer vb = new VertexBuffer();
                    vb.setPositions(p, v, null);
                    IndexBuffer ib = new TriangleStripArray(new int[] { 0, 1, 2 }, new int[] { 3 });
                    g3d.render(vb, ib, null, null, 4);
                    Camera cam = new Camera();
                    cam.setPerspective(v, 1.0f, 1.0f, 10.0f);
                    g3d.setCamera(cam, null);
                    Group g = new Group();
                    Mesh m = new Mesh(vb, ib, null);
                    Transform t = new Transform();
                    float[] nm = new float[16];
                    for (int q = 0; q < 16; q++) {
                        nm[q] = (q % 5 == 0) ? v : 1.0f;
                    }
                    t.set(nm);
                    m.setTransform(t);
                    g.addChild(m);
                    g3d.render(g, null);
                    g3d.bindTarget(target);
                    g3d.render(worldMesh(), null);
                    g3d.releaseTarget();
                }
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static World worldMesh() {
        World w = new World();
        Camera cam = new Camera();
        w.addChild(cam);
        w.setActiveCamera(cam);
        VertexArray p = new VertexArray(3, 3, 2);
        p.set(0, 3, new short[] { 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        VertexBuffer vb = new VertexBuffer();
        vb.setPositions(p, 1.0f, null);
        IndexBuffer ib = new TriangleStripArray(new int[] { 0, 1, 2 }, new int[] { 3 });
        w.addChild(new Mesh(vb, ib, null));
        return w;
    }

    static void opLightIndex(int i) {
        begin(i, "extreme light index");
        try {
            Light l = new Light();
            Transform t = new Transform();
            int[] idx = { 0, 1, 15, 16, 0x7FFFFFFF, -0x80000000, 0x40000000, 0x7FFFFFFE };
            for (int k = 0; k < idx.length; k++) {
                try {
                    g3d.setLight(idx[k], l, t);
                } catch (Throwable x) {
                    record(x);
                    System.out.println("E " + i + " setLight " + idx[k] + " " + x.getClass().getName());
                }
                try {
                    g3d.getLight(idx[k], t);
                } catch (Throwable x) {
                    record(x);
                }
            }
            g3d.addLight(l, t);
            g3d.resetLights();
            g3d.getLightCount();
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opTransformArray(int i) {
        begin(i, "transform(VertexArray) array writes");
        try {
            int comp = 2 + nextInt(3);
            int n = 1 + nextInt(6);
            VertexArray va = new VertexArray(n, comp, 2);
            va.set(0, n, shorts(n * comp));
            Transform t = new Transform();
            if (nextInt(3) == 0) {
                float[] m = new float[16];
                for (int k = 0; k < 16; k++) {
                    m[k] = pickFloat();
                }
                t.set(m);
            } else {
                t.postRotate(pickFloat(), 1.0f, 0.0f, 0.0f);
                t.postTranslate(pickFloat(), pickFloat(), pickFloat());
            }
            int dn = nextInt(24) - 2;
            float[] dst = new float[dn < 0 ? 0 : dn];
            t.transform(va, dst, true);
            t.transform(va, dst, false);
            float[] dst2 = floatArray(64);
            t.transform(va, dst2, true);
            float[] m2 = floatArray(nextInt(20) - 1);
            t.transform(m2);
            t.get(dst2);
            float[] m3 = floatArray(16);
            t.set(m3);
            t.get(m3);
            t.invert();
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static float[] floatArray(int n) {
        float[] a = new float[n < 0 ? 0 : n];
        for (int i = 0; i < a.length; i++) {
            a[i] = pickFloat();
        }
        return a;
    }

    static void opKeyframes(int i) {
        begin(i, "keyframe/animation odd values");
        try {
            int comp = 1 + nextInt(4);
            int[] nks = { 1, 2, 3, 16, 4096 };
            int nkeys = nks[nextInt(nks.length)];
            int[] interps = { 176, 177, 178, 179, 180, -1 };
            KeyframeSequence ks = new KeyframeSequence(comp, nkeys, interps[nextInt(interps.length)]);
            ks.setKeyframe(nextInt(20) - 2, nextInt(1000), floats(nextInt(10) - 2));
            ks.getKeyframe(nextInt(20) - 2, floatArray(nextInt(10) - 2));
            ks.setValidRange(nextInt(4), nextInt(4));
            ks.getRepeatMode();
            ks.getDuration();
            int[] props = { AnimationTrack.ALPHA, AnimationTrack.CROP, AnimationTrack.ORIENTATION,
                            AnimationTrack.MORPH_WEIGHTS, -1, 0x7FFFFFFF };
            AnimationTrack at = new AnimationTrack(ks, props[nextInt(props.length)]);
            at.getKeyframeSequence();
            AnimationController ac = new AnimationController();
            ac.setActiveInterval(nextInt(1000) - 10, nextInt(1000) - 10);
            ac.setSpeed(pickFloat(), nextInt(4) - 1);
            ac.setPosition(pickFloat(), nextInt(4) - 1);
            ac.setWeight(pickFloat());
            at.setController(ac);
            at.getController();
            ks.setDuration(1 + nextInt(5000));
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opSkinMorph(int i) {
        begin(i, "skinned/morphing mesh");
        try {
            VertexArray p = vaValid(3, 3);
            VertexBuffer vb = new VertexBuffer();
            vb.setPositions(p, 1.0f, null);
            IndexBuffer ib = new TriangleStripArray(new int[] { 0, 1, 2 }, new int[] { 3 });
            VertexBuffer tgt = new VertexBuffer();
            tgt.setPositions(vaValid(3, 3), 1.0f, null);
            try {
                MorphingMesh mm = new MorphingMesh(vb, new VertexBuffer[] { tgt },
                                                   new IndexBuffer[] { ib }, new Appearance[] { new Appearance() });
                mm.getMorphTargetCount();
                mm.getMorphTarget(nextInt(400) - 20);
                mm.setWeights(floatArray(nextInt(8) - 2));
                mm.getWeights(floatArray(nextInt(8) - 2));
                mm.getVertexBuffer();
            } catch (Throwable x) {
                record(x);
            }
            Group skel = new Group();
            Node bone = new Group();
            skel.addChild(bone);
            try {
                SkinnedMesh sm = new SkinnedMesh(vb, ib, new Appearance(), skel);
                sm.addTransform(bone, 1, 0, 3);
                sm.getBoneTransform(bone, new Transform());
                sm.getBoneVertices(bone, new int[nextInt(6)], floatArray(nextInt(6)));
                sm.getSkeleton();
            } catch (Throwable x) {
                record(x);
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opFileWrite(int i) {
        begin(i, "FileConnection write/truncate/rename");
        try {
            FileConnection fc = (FileConnection) Connector.open("file:///root/fuzz-w.txt", Connector.READ_WRITE);
            if (fc.exists()) {
                fc.delete();
            }
            fc.create();
            java.io.OutputStream os = fc.openOutputStream();
            os.write(bytes(24));
            os.flush();
            os.close();
            fc.fileSize();
            fc.truncate(nextInt(40) - 5);
            fc.rename("fuzz-w2.txt");
            if (fc.exists()) {
                java.io.InputStream is = fc.openInputStream();
                byte[] buf = new byte[8];
                int total = 0;
                int n;
                while ((n = is.read(buf)) > 0) {
                    total += n;
                }
                is.close();
                fc.fileSize();
            }
            fc.close();
        } catch (Throwable t) {
            failA(i, t);
        }
        try {
            FileConnection d = (FileConnection) Connector.open("file:///root/fuzz-dir/", Connector.READ_WRITE);
            if (d.exists()) {
                d.delete();
            }
            d.mkdir();
            d.exists();
            d.isDirectory();
            d.list("*", false);
            d.list();
            d.close();
            try {
                d.mkdir();
            } catch (Throwable x) {
                record(x);
            }
            FileConnection left = (FileConnection) Connector.open("file:///root/fuzz-left.txt", Connector.READ_WRITE);
            if (left.exists()) {
                left.delete();
            }
            left.create();
            left.close();
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    // ------------------------------------------------------- wave-3 ops
    //
    // Loader fuzzing needs real container bytes, and the format is small
    // enough to write by hand: a 12-byte identifier, then sections of
    // [scheme][totalLen][ulLen][body][adler], and a section body is a chain of
    // [type][len][payload] objects.  Payloads below follow the field offsets
    // the loader decodes so that some containers reach the object builders and
    // link passes instead of dying in the decoder.

    static final int[] OBJECT_TYPES = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 255 };
    /* The decoder rejects an object it cannot read, and one bad object fails
       the whole container, so structured files use the shapes below. */
    static final int[] STRUCT_TYPES = { 1, 2, 3, 8, 9, 10, 11, 14, 16, 19, 20,
                                        21, 22, 255 };

    static byte[] cat(byte[] a, byte[] b) {
        if (a == null) {
            return b;
        }
        if (b == null) {
            return a;
        }
        byte[] r = new byte[a.length + b.length];
        System.arraycopy(a, 0, r, 0, a.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }

    static byte[] pad(int n) {
        return new byte[n < 0 ? 0 : n];
    }

    static byte[] u8(int v) {
        byte[] b = new byte[1];
        b[0] = (byte)v;
        return b;
    }

    static byte[] u32(int v) {
        byte[] b = new byte[4];
        b[0] = (byte)v;
        b[1] = (byte)(v >>> 8);
        b[2] = (byte)(v >>> 16);
        b[3] = (byte)(v >>> 24);
        return b;
    }

    static int anyRef(int nobj) {
        int k = nextInt(7);
        if (k == 0) {
            return -1;
        }
        if (k == 1) {
            return 0;
        }
        if (k == 2) {
            return 0x7FFFFFFF;
        }
        if (k == 3) {
            return nobj + 1;
        }
        if (k == 4) {
            return 0x40000000;
        }
        return nextInt(nobj + 1);
    }

    static byte[] meshPayload(int nobj, boolean skinned) {
        int nsub = 1 + nextInt(3);
        byte[] b = cat(pad(12), u8(0));
        b = cat(b, pad(3));
        b = cat(b, u32(-1));
        b = cat(b, pad(2));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(nsub));
        for (int k = 0; k < nsub; k++) {
            b = cat(b, u32(anyRef(nobj)));
            b = cat(b, u32(anyRef(nobj)));
        }
        if (skinned) {
            int ntr = 1 + nextInt(3);
            b = cat(b, u32(anyRef(nobj)));
            b = cat(b, u32(ntr));
            b = cat(b, bytes(16 * ntr));
        }
        return b;
    }

    static byte[] worldPayload(int nobj) {
        int n = 1 + nextInt(4);
        byte[] b = cat(pad(22), u32(n));
        for (int k = 0; k < n; k++) {
            b = cat(b, u32(anyRef(nobj)));
        }
        b = cat(b, u32(anyRef(nobj)));
        return cat(b, u32(anyRef(nobj)));
    }

    static byte[] groupPayload(int nobj) {
        int n = 1 + nextInt(4);
        byte[] b = cat(pad(12), u32(n));
        for (int k = 0; k < n; k++) {
            b = cat(b, u32(anyRef(nobj)));
        }
        return b;
    }

    static byte[] vaPayload() {
        int size = pickInt(new int[] { 0, 1, 2, 4 });
        int comp = nextInt(4);
        int type = nextInt(3);
        int vc = nextInt(24);
        byte[] b = cat(pad(12), u8(size));
        b = cat(b, u8(comp));
        b = cat(b, u8(type));
        b = cat(b, u8(vc & 0xFF));
        b = cat(b, u8((vc >>> 8) & 0xFF));
        return cat(b, bytes(vc * size * comp));
    }

    static byte[] tsaPayload() {
        int enc = nextInt(3) == 0 ? 128 : 0;
        if (enc == 0) {
            int nstrip = 1 + nextInt(4);
            int first = pickInt(FIRST);
            byte[] b = cat(pad(12), u8(0));
            b = cat(b, u32(first));
            b = cat(b, u32(nstrip));
            for (int k = 0; k < nstrip; k++) {
                b = cat(b, u32(1 + nextInt(6)));
            }
            return b;
        }
        int n = nextInt(10);
        int nstrip = 1 + nextInt(3);
        byte[] b = cat(pad(12), u8(128));
        b = cat(b, u32(n));
        for (int k = 0; k < n; k++) {
            b = cat(b, u32(pickInt(NVERTS)));
        }
        b = cat(b, u32(nstrip));
        for (int k = 0; k < nstrip; k++) {
            b = cat(b, u32(1 + nextInt(6)));
        }
        return b;
    }

    static byte[] appearancePayload(int nobj) {
        int ntex = nextInt(3);
        byte[] b = cat(pad(12), u8(pickInt(NVERTS)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(ntex));
        for (int k = 0; k < ntex; k++) {
            b = cat(b, u32(anyRef(nobj)));
        }
        return b;
    }

    static byte[] vbPayload(int nobj) {
        int texn = nextInt(3);
        byte[] b = cat(pad(12), u32(mix(7)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(mix(5)));
        b = cat(b, u32(mix(6)));
        b = cat(b, u32(mix(7)));
        b = cat(b, u32(mix(8)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(anyRef(nobj)));
        b = cat(b, u32(texn));
        for (int k = 0; k < texn; k++) {
            b = cat(b, u32(anyRef(nobj)));
            b = cat(b, u32(mix(9)));
            b = cat(b, u32(mix(10)));
            b = cat(b, u32(mix(11)));
            b = cat(b, u32(mix(12)));
        }
        return b;
    }

    static byte[] imagePayload() {
        int fmt = pickInt(FORMATS);
        int bpp;
        if (fmt == 96) {
            bpp = 1;
        } else if (fmt == 97) {
            bpp = 1;
        } else if (fmt == 98) {
            bpp = 2;
        } else if (fmt == 99) {
            bpp = 3;
        } else if (fmt == 100) {
            bpp = 4;
        } else {
            bpp = 0;
        }
        int w = 1 + nextInt(8);
        int h = 1 + nextInt(8);
        byte[] b = cat(pad(12), u8(fmt));
        b = cat(b, u8(nextInt(2)));
        b = cat(b, u32(w));
        b = cat(b, u32(h));
        if (bpp == 0 || nextInt(3) == 0) {
            int pal = 4 * (1 + nextInt(4));
            b = cat(b, u32(pal));
            return cat(cat(b, bytes(pal)), bytes(w * h));
        }
        b = cat(b, u32(0));
        b = cat(b, u32(w * h * bpp));
        return cat(b, bytes(w * h * bpp));
    }

    static byte[] kfsPayload() {
        int interp = pickInt(new int[] { 176, 177, 178, 179, 180 });
        int comp = (interp == 177 || interp == 179) ? 4 : 1 + nextInt(4);
        int nkeys = 1 + nextInt(12);
        int enc = nextInt(3);
        byte[] b = cat(pad(12), u8(interp));
        b = cat(b, u8(nextInt(3)));
        b = cat(b, u8(enc));
        b = cat(b, u32(1 + nextInt(1000)));
        b = cat(b, u32(nextInt(nkeys + 1)));
        b = cat(b, u32(nkeys - 1));
        b = cat(b, u32(comp));
        b = cat(b, u32(nkeys));
        int need = nkeys * (4 + comp * (enc == 0 ? 4 : enc == 1 ? 1 : 2));
        if (enc != 0) {
            need += comp * 8;
        }
        return cat(b, bytes(need));
    }

    static byte[] realPayload(int type, int nobj) {
        switch (type) {
            case 20: return vaPayload();
            case 11: return tsaPayload();
            case 21: return vbPayload(nobj);
            case 3: return appearancePayload(nobj);
            case 10: return imagePayload();
            case 14: return meshPayload(nobj, false);
            case 16: return meshPayload(nobj, true);
            case 22: return worldPayload(nobj);
            case 9: return groupPayload(nobj);
            case 19: return kfsPayload();
            case 2: {
                byte[] b = cat(pad(12), u32(anyRef(nobj)));
                b = cat(b, u32(anyRef(nobj)));
                return cat(b, u32(pickInt(new int[] { 256, 257, 258, 259, 260, 261,
                    262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
                    275, 276, 0, -1, 255, 999, 0x7FFFFFFF })));
            }
            case 1: {
                byte[] b = pad(12);
                for (int k = 0; k < 6; k++) {
                    b = cat(b, u32(mix(20 + k)));
                }
                return b;
            }
            case 8: {
                byte[] b = cat(pad(12), u8(pickInt(new int[] { 0, 160, 161, 162, 163 })));
                b = cat(b, u8(pickInt(new int[] { 0, 164, 165, 166 })));
                b = cat(b, u8(pickInt(new int[] { 0, 168, 169, 170 })));
                return cat(b, u8(nextInt(2)));
            }
            default: return bytes(nextInt(70));
        }
    }

    static byte[] objStream(int type, byte[] payload) {
        return cat(cat(u8(type), u32(payload.length)), payload);
    }

    static byte[] m3gSection(int scheme, byte[] body, int ulen) {
        byte[] b = cat(u8(scheme), u32(13 + body.length));
        b = cat(b, u32(ulen));
        b = cat(b, body);
        return cat(b, u32(0));
    }

    static byte[] m3gIdent() {
        byte[] b = new byte[12];
        b[0] = (byte)0xAB;
        b[1] = 0x4A; b[2] = 0x53; b[3] = 0x52;
        b[4] = 0x31; b[5] = 0x38; b[6] = 0x34;
        b[7] = (byte)0xBB; b[8] = 0x0D; b[9] = 0x0A; b[10] = 0x1A; b[11] = 0x0A;
        return b;
    }

    static byte[] m3gFile(int nobj, boolean structured) {
        byte[] body = new byte[0];
        for (int k = 0; k < nobj; k++) {
            int type = pickInt(structured ? STRUCT_TYPES : OBJECT_TYPES);
            byte[] pay = (structured && nextInt(5) != 0) ? realPayload(type, nobj)
                                                          : bytes(nextInt(80));
            if (type == 255) {
                pay = cat(pay, u8(0));
            }
            body = cat(body, objStream(type, pay));
        }
        byte[] data = cat(m3gIdent(), m3gSection(0, pad(20), 0));
        if (nobj > 0) {
            data = cat(data, m3gSection(0, body, body.length));
        }
        return data;
    }

    static byte[] truncated(byte[] data, int n) {
        if (n <= 0) {
            return new byte[0];
        }
        if (n >= data.length) {
            return data;
        }
        byte[] r = new byte[n];
        System.arraycopy(data, 0, r, 0, n);
        return r;
    }

    static void opLoaderBytes(int i) {
        begin(i, "Loader.load synthetic container");
        byte[] data = new byte[0];
        try {
            int kind = nextInt(8);
            if (kind == 0) {
                data = m3gFile(0, false);
            } else if (kind == 1) {
                data = m3gFile(1 + nextInt(4), true);
            } else if (kind == 2) {
                data = m3gFile(nextInt(8), false);
            } else if (kind == 3) {
                data = truncated(m3gFile(nextInt(3), true), nextInt(64));
            } else if (kind == 4) {
                data = m3gFile(nextInt(3), false);
                if (data.length > 0) {
                    data[nextInt(data.length)] = (byte)nextInt(256);
                }
            } else if (kind == 5) {
                byte[] body = new byte[0];
                for (int k = 0; k < 4097; k++) {
                    body = cat(body, objStream(1, new byte[0]));
                }
                data = cat(cat(m3gIdent(), m3gSection(0, pad(20), 0)),
                           m3gSection(0, body, body.length));
            } else if (kind == 6) {
                data = cat(cat(m3gIdent(), m3gSection(0, pad(20), 0)),
                           m3gSection(1, bytes(24), 0x7FFFFFFF));
            } else {
                data = m3gFile(1 + nextInt(3), true);
                if (data.length > 33) {
                    data[29] = (byte)nextInt(256);
                    data[30] = (byte)nextInt(256);
                    data[31] = (byte)nextInt(256);
                    data[32] = (byte)nextInt(256);
                }
            }
        } catch (Throwable t) {
            record(t);
        }
        try {
            Object3D[] objs = Loader.load(data, pickInt(new int[] { 0, 0, 0, 0, 0, 1, -1,
                                                                   data.length - 1, data.length,
                                                                   0x7FFFFFFF }));
            if (objs != null) {
                for (int k = 0; k < objs.length && k < 3; k++) {
                    if (objs[k] == null) {
                        continue;
                    }
                    try {
                        objs[k].duplicate();
                    } catch (Throwable t) {
                        record(t);
                    }
                    if (objs[k] instanceof Node) {
                        renderOne(i, (Node)objs[k]);
                    }
                }
            }
        } catch (Throwable t) {
            record(t);
        }
        try {
            Loader.load((byte[])null, 0);
        } catch (Throwable t) {
            record(t);
        }
        try {
            Loader.load(new byte[] { 1, 2, 3 }, 0);
        } catch (Throwable t) {
            record(t);
        }
        try {
            Loader.load("fuzz-does-not-exist.m3g");
        } catch (Throwable t) {
            record(t);
        }
    }

    static void renderOne(int i, Node n) {
        try {
            g3d.bindTarget(target);
            try {
                g3d.render(n, xfAny());
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            record(t);
        }
    }

    static void opAnimationDeep(int i) {
        begin(i, "animation: keyframes, tracks, animate");
        try {
            int comp = 1 + nextInt(4);
            int interp = pickInt(new int[] { 176, 177, 178, 179, 180, 175, 181, -1 });
            int nkeys = pickInt(new int[] { 1, 2, 3, 16, 64, 4096 });
            KeyframeSequence ks = new KeyframeSequence(comp, nkeys, interp);
            ks.setDuration(pickInt(new int[] { -1, 0, 1, 1000, 0x7FFFFFFF }));
            ks.setRepeatMode(pickInt(new int[] { 192, 193, 0, -1 }));
            ks.setValidRange(pickInt(new int[] { -1, 0, 1, 3, nkeys, nkeys - 1, 0x7FFFFFFF }),
                             pickInt(new int[] { -1, 0, 1, nkeys - 1, nkeys, 0x7FFFFFFF }));
            for (int k = 0; k < nextInt(4); k++) {
                int idx = pickInt(new int[] { -1, 0, 1, nkeys / 2, nkeys - 1, nkeys, 0x7FFFFFFF });
                int time = pickInt(new int[] { 0, 1, 100, 0x7FFFFFFF, -0x80000000 });
                float[] val = new float[comp];
                for (int c = 0; c < comp; c++) {
                    val[c] = pickFloat();
                }
                try {
                    ks.setKeyframe(idx, time, val);
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    ks.getKeyframe(idx, floats(nextInt(comp + 2)));
                } catch (Throwable t) {
                    record(t);
                }
            }
            AnimationController c = new AnimationController();
            c.setActiveInterval(pickInt(new int[] { -1, 0, 1, 6, 0x7FFFFFFF }),
                                pickInt(new int[] { 0, 5, 10, 0x7FFFFFFF }));
            c.setSpeed(pickFloat(), pickInt(new int[] { -1, 0, 1, 1000 }));
            c.setPosition(pickFloat(), pickInt(new int[] { 0, 1, 2, -1, 0x7FFFFFFF }));
            c.setWeight(pickFloat());
            c.getPosition(pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF }));
            c.getRefWorldTime();
            int prop = pickInt(new int[] { 256, 257, 258, 259, 260, 261, 262, 263, 264,
                265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 0, -1, 255,
                999, 0x7FFFFFFF });
            AnimationTrack at = new AnimationTrack(ks, prop);
            try {
                at.setController(nextInt(2) == 0 ? c : null);
            } catch (Throwable t) {
                record(t);
            }
            at.getController();
            at.getKeyframeSequence();
            at.getTargetProperty();
            Node holder;
            int which = nextInt(4);
            if (which == 0) {
                holder = new Camera();
            } else if (which == 1) {
                holder = new Light();
            } else if (which == 2) {
                holder = new Group();
            } else {
                holder = new Mesh(vbAny(), ibAny(), appearanceAny());
            }
            try {
                holder.addAnimationTrack(at);
                holder.addAnimationTrack(at);
            } catch (Throwable t) {
                record(t);
            }
            holder.getAnimationTrackCount();
            holder.getAnimationTrack(pickInt(new int[] { -1, 0, 1, 0x7FFFFFFF }));
            try {
                holder.animate(pickInt(new int[] { -0x80000000, -1, 0, 1, 0x7FFFFFFF }));
            } catch (Throwable t) {
                record(t);
            }
            holder.getReferences(new Object3D[pickInt(new int[] { 0, 1, 4 })]);
            holder.duplicate();
            holder.removeAnimationTrack(at);
            renderOne(i, holder);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opAnimateGraph(int i) {
        begin(i, "animation graph: remove/re-add, animate");
        try {
            World w = new World();
            Group g = new Group();
            Mesh m = new Mesh(vbAny(), ibAny(), appearanceAny());
            g.addChild(m);
            w.addChild(g);
            AnimationController c = new AnimationController();
            c.setSpeed(1.0f, 0);
            KeyframeSequence ks = new KeyframeSequence(3, 2, 176);
            float[] k1 = { 0.0f, 0.0f, 0.0f };
            float[] k2 = { 1.0f, 2.0f, 3.0f };
            try {
                ks.setKeyframe(0, 0, k1);
                ks.setKeyframe(1, 1000, k2);
            } catch (Throwable t) {
                record(t);
            }
            AnimationTrack at = new AnimationTrack(ks, 275);
            at.setController(c);
            m.addAnimationTrack(at);
            try {
                w.animate(pickInt(new int[] { -1, 0, 500, 0x7FFFFFFF }));
            } catch (Throwable t) {
                record(t);
            }
            g.removeChild(m);
            try {
                w.animate(pickInt(new int[] { 0, 500, 0x7FFFFFFF }));
            } catch (Throwable t) {
                record(t);
            }
            try {
                m.animate(1000);
            } catch (Throwable t) {
                record(t);
            }
            World other = new World();
            other.addChild(g);
            try {
                other.animate(1000);
            } catch (Throwable t) {
                record(t);
            }
            g.addChild(m);
            try {
                w.addChild(g);
            } catch (Throwable t) {
                record(t);
            }
            w.find(pickInt(new int[] { 0, -1, 5 }));
            w.getReferences(new Object3D[3]);
            w.duplicate();
            renderOne(i, w);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opSkinnedDeep(int i) {
        begin(i, "SkinnedMesh skeleton and bones");
        try {
            VertexBuffer vb = vbAny();
            Group skel = new Group();
            Node b1 = new Group();
            Node b2 = new Group();
            skel.addChild(b1);
            skel.addChild(b2);
            SkinnedMesh sm;
            try {
                if (nextInt(2) == 0) {
                    sm = new SkinnedMesh(vb, ibAny(), appearanceAny(), skel);
                } else {
                    sm = new SkinnedMesh(vb,
                                         new IndexBuffer[] { ibAny(), ibAny() },
                                         new Appearance[] { appearanceAny(), appearanceAny() },
                                         skel);
                }
            } catch (Throwable t) {
                record(t);
                return;
            }
            int[] weights = { -1, 0, 1, 2, 0x7FFFFFFF };
            int[] firsts = { -1, 0, 1, 3, 100, 0x7FFFFFFF };
            try {
                sm.addTransform(nextInt(5) == 0 ? null : b1,
                                pickInt(weights), pickInt(firsts),
                                pickInt(new int[] { -1, 0, 1, 2, 3, 1000 }));
            } catch (Throwable t) {
                record(t);
            }
            try {
                sm.getBoneTransform(b1, xfAny());
            } catch (Throwable t) {
                record(t);
            }
            try {
                sm.getBoneVertices(b2, ints(nextInt(5)), floats(nextInt(9)));
            } catch (Throwable t) {
                record(t);
            }
            sm.getSkeleton();
            renderOne(i, sm);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opMorphDeep(int i) {
        begin(i, "MorphingMesh targets and weights");
        try {
            int ntargets = pickInt(new int[] { 0, 1, 2, 3, 8 });
            VertexBuffer[] targets = new VertexBuffer[ntargets];
            for (int k = 0; k < ntargets; k++) {
                targets[k] = vbAny();
            }
            MorphingMesh mm;
            try {
                if (nextInt(2) == 0) {
                    mm = new MorphingMesh(vbAny(), targets, ibAny(), appearanceAny());
                } else {
                    int nsub = pickInt(new int[] { 0, 1, 2, 3 });
                    IndexBuffer[] ibs = new IndexBuffer[nsub];
                    Appearance[] apps = new Appearance[nsub];
                    for (int k = 0; k < nsub; k++) {
                        ibs[k] = ibAny();
                        apps[k] = appearanceAny();
                    }
                    mm = new MorphingMesh(vbAny(), targets, ibs, apps);
                }
            } catch (Throwable t) {
                record(t);
                return;
            }
            mm.getMorphTargetCount();
            mm.getMorphTarget(pickInt(new int[] { -1, 0, 1, ntargets, 0x7FFFFFFF }));
            try {
                float[] w = floats(pickInt(new int[] { 0, 1, 2, 8 }));
                if (w.length > 0 && nextInt(2) == 0) {
                    w[0] = pickFloat();
                }
                mm.setWeights(w);
            } catch (Throwable t) {
                record(t);
            }
            try {
                mm.getWeights(floats(pickInt(new int[] { 0, 1, 2, 8 })));
            } catch (Throwable t) {
                record(t);
            }
            renderOne(i, mm);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opMipTexture(int i) {
        begin(i, "Texture2D/Image2D levels and enums");
        try {
            Image2D img = null;
            int kind = nextInt(4);
            try {
                if (kind == 0) {
                    img = new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS),
                                      bytes(nextInt(32)));
                } else if (kind == 1) {
                    img = new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS),
                                      bytes(nextInt(32)), bytes(nextInt(16)));
                } else if (kind == 2) {
                    img = new Image2D(pickInt(FORMATS), pickInt(DIMS), pickInt(DIMS));
                    img.set(pickInt(new int[] { -1, 0, 1, 4 }),
                            pickInt(new int[] { -1, 0, 1, 4 }),
                            pickInt(new int[] { -1, 0, 1, 8, 0x7FFFFFFF }),
                            pickInt(new int[] { -1, 0, 1, 8, 0x7FFFFFFF }),
                            bytes(nextInt(24)));
                } else {
                    img = new Image2D(99, 2, 2, bytes(12));
                }
            } catch (Throwable t) {
                record(t);
            }
            Texture2D tex = null;
            try {
                tex = new Texture2D(nextInt(3) == 0 ? null : img);
                tex.setImage(img);
                tex.setFiltering(pickInt(new int[] { 0, 208, 209, 210, 1, -1, 0x7FFFFFFF }),
                                 pickInt(new int[] { 0, 208, 209, 210, 1, -1, 0x7FFFFFFF }));
                tex.setWrapping(pickInt(new int[] { 0, 240, 241, 242, -1, 0x7FFFFFFF }),
                                pickInt(new int[] { 0, 240, 241, 242, -1, 0x7FFFFFFF }));
                tex.setBlending(pickInt(new int[] { 0, 224, 225, 226, 227, 228, 1, -1, 0x7FFFFFFF }));
                tex.setBlendColor(mix(13));
                tex.getLevelFilter();
                tex.getImageFilter();
                tex.getWrappingS();
                tex.getWrappingT();
                tex.getBlending();
                tex.getBlendColor();
                tex.getImage();
            } catch (Throwable t) {
                record(t);
            }
            Appearance a = new Appearance();
            try {
                a.setTexture(pickInt(new int[] { 0, 1, 2, -1 }), tex);
            } catch (Throwable t) {
                record(t);
            }
            Mesh m = new Mesh(vbAny(), ibAny(), a);
            renderOne(i, m);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opImmediateDeep(int i) {
        begin(i, "immediate-mode render edges");
        try {
            g3d.bindTarget(target);
            try {
                g3d.setViewport(pickInt(new int[] { 0, -1, 32, 0x7FFFFFFF, -0x40000000 }),
                                pickInt(new int[] { 0, -1, 32, 0x7FFFFFFF, -0x40000000 }),
                                pickInt(new int[] { 0, 1, 32, 0x7FFFFFFF }),
                                pickInt(new int[] { 0, 1, 32, 0x7FFFFFFF }));
                g3d.setDepthRange(pickFloat(), pickFloat());
                Background bg = new Background();
                bg.setImageMode(pickInt(new int[] { 32, 33, 0, -1 }), pickInt(new int[] { 32, 33, 0, -1 }));
                bg.setCrop(pickInt(new int[] { -1, 0, 1, 0x7FFFFFFF }),
                           pickInt(new int[] { -1, 0, 1, 0x7FFFFFFF }),
                           pickInt(new int[] { -1, 0, 1, 0x7FFFFFFF }),
                           pickInt(new int[] { -1, 0, 1, 0x7FFFFFFF }));
                bg.setImage(imgAny());
                try {
                    g3d.clear(bg);
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    g3d.render(vbAny(), ibAny(), appearanceAny(), xfAny(),
                               pickInt(new int[] { 0, 4, 5, 6, 7, -1, 0x7FFFFFFF }));
                } catch (Throwable t) {
                    record(t);
                }
                try {
                    g3d.render((VertexBuffer)null, (IndexBuffer)null, (Appearance)null,
                               (Transform)null);
                } catch (Throwable t) {
                    record(t);
                }
                g3d.getCamera(xfAny());
                g3d.setCamera(nextInt(2) == 0 ? null : new Camera(), xfAny());
            } finally {
                g3d.releaseTarget();
            }
        } catch (Throwable t) {
            failA(i, t);
        }
        try {
            g3d.releaseTarget();
        } catch (Throwable t) {
            record(t);
        }
    }

    static void opFileEdge(int i) {
        begin(i, "FileConnection edge semantics");
        try {
            FileConnection a = (FileConnection)Connector.open("file:///root/fuzz-e.txt",
                                                              Connector.READ_WRITE);
            FileConnection dm = (FileConnection)Connector.open("file:///root/", Connector.READ);
            try {
                dm.list(null, false);
            } catch (Throwable t) {
                record(t);
            }
            try {
                dm.list("", false);
            } catch (Throwable t) {
                record(t);
            }
            try {
                dm.list("..", false);
            } catch (Throwable t) {
                record(t);
            }
            try {
                dm.list("*.*", false);
            } catch (Throwable t) {
                record(t);
            }
            try {
                dm.setFileConnection("fuzz-e.txt");
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.create();
                a.create();
            } catch (Throwable t) {
                record(t);
            }
            try {
                dm.mkdir();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.mkdir();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.rename("fuzz-e2.txt");
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.truncate(-1);
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.openInputStream();
            } catch (Throwable t) {
                record(t);
            }
            a.close();
            try {
                a.exists();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.list();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.getName();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.fileSize();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.lastModified();
            } catch (Throwable t) {
                record(t);
            }
            try {
                a.delete();
            } catch (Throwable t) {
                record(t);
            }
            dm.close();
        } catch (Throwable t) {
            failA(i, t);
        }
    }


    static byte[] rawFile(byte[] body) {
        byte[] data = cat(m3gIdent(), m3gSection(0, pad(20), 0));
        if (body.length > 0) {
            data = cat(data, m3gSection(0, body, body.length));
        }
        return data;
    }

    static byte[] groupRefs(int[] refs) {
        byte[] b = cat(pad(12), u32(refs.length));
        for (int k = 0; k < refs.length; k++) {
            b = cat(b, u32(refs[k]));
        }
        return b;
    }

    static byte[] worldRefs(int[] kids, int cam, int bg) {
        byte[] b = cat(pad(22), u32(kids.length));
        for (int k = 0; k < kids.length; k++) {
            b = cat(b, u32(kids[k]));
        }
        b = cat(b, u32(cam));
        return cat(b, u32(bg));
    }

    static byte[] meshRefs(int vb, int[] pairs) {
        byte[] b = cat(pad(16), u32(-1));
        b = cat(b, pad(2));
        b = cat(b, u32(vb));
        b = cat(b, u32(pairs.length / 2));
        for (int k = 0; k < pairs.length; k++) {
            b = cat(b, u32(pairs[k]));
        }
        return b;
    }

    static byte[] chainGroups(int n) {
        byte[] body = new byte[0];
        for (int k = 1; k <= n; k++) {
            int ref = (k < n) ? (k + 1) : 0;
            body = cat(body, objStream(9, groupRefs(new int[] { ref })));
        }
        return body;
    }

    static void renderQuietly(Node n) {
        boolean bound = false;
        try {
            g3d.bindTarget(target);
            bound = true;
            g3d.render(n, xfAny());
        } catch (Throwable t) {
            record(t);
        } finally {
            if (bound) {
                try { g3d.releaseTarget(); } catch (Throwable t) { record(t); }
            }
        }
    }

    static void opLoaderEvil(int i) {
        begin(i, "Loader.load evil container");
        try {
            byte[] body;
            int variant = nextInt(11);
            switch (variant) {
                case 0:
                    body = cat(objStream(9, groupRefs(new int[] { 2 })),
                               objStream(9, groupRefs(new int[] { 1 })));
                    break;
                case 1:
                    body = objStream(9, groupRefs(new int[] { 1 }));
                    break;
                case 2:
                    body = cat(objStream(9, groupRefs(new int[] { 2 })),
                               objStream(22, worldRefs(new int[] { 1 }, 1, 1)));
                    break;
                case 3:
                    body = cat(cat(objStream(14, meshRefs(2, new int[] { 3, 4 })),
                                   objStream(5, realPayload(5, 5))),
                               cat(objStream(11, tsaPayload()),
                                   objStream(3, appearancePayload(5))));
                    break;
                case 4:
                    body = cat(cat(objStream(14, meshRefs(999, new int[] { 0, -1 })),
                                   objStream(11, tsaPayload())),
                               objStream(3, appearancePayload(3)));
                    break;
                case 5:
                    body = cat(cat(objStream(22, worldRefs(new int[] { 2, 3 }, 2, 3)),
                                   objStream(14, meshRefs(1, new int[] { 4, 4 }))),
                               cat(objStream(5, realPayload(5, 3)),
                                   objStream(4, realPayload(4, 3))));
                    break;
                case 6:
                    body = cat(cat(objStream(17, bytes(20)),
                                   objStream(5, realPayload(5, 2))),
                               objStream(3, appearancePayload(2)));
                    break;
                case 7:
                    body = cat(objStream(2, realPayload(2, 12)),
                               objStream(9, groupRefs(new int[] { 0 })));
                    break;
                case 8:
                    body = chainGroups(12);
                    break;
                case 9:
                    body = cat(objStream(16, meshRefs(2, new int[] { 3, 4 })),
                               cat(objStream(5, realPayload(5, 5)),
                                   cat(objStream(11, tsaPayload()),
                                       objStream(3, appearancePayload(5)))));
                    break;
                default: {
                    byte[] full = m3gFile(6, true);
                    Loader.load(truncated(full, nextInt(full.length)), 0);
                    return;
                }
            }
            byte[] data = rawFile(body);
            int off = pickInt(new int[] { 0, 0, 0, 0, 0, 1, -1, data.length - 1,
                                          data.length, 0x7FFFFFFF });
            Object3D[] objs = Loader.load(data, off);
            if (objs == null) {
                return;
            }
            Object3D firstNode = null;
            for (int j = 0; j < objs.length; j++) {
                Object3D o = objs[j];
                if (o == null) {
                    continue;
                }
                if (firstNode == null && o instanceof Node) {
                    firstNode = o;
                }
                try { o.setUserID(j + 1); } catch (Throwable t) { record(t); }
                if (o instanceof Node) {
                    renderQuietly((Node) o);
                    try {
                        ((Node) o).animate(pickInt(new int[] { 0, 1, 1000 }));
                    } catch (Throwable t) { record(t); }
                }
                try {
                    Object3D d = o.duplicate();
                    if (d instanceof Node) { renderQuietly((Node) d); }
                } catch (Throwable t) { record(t); }
                try { o.find(0); } catch (Throwable t) { record(t); }
            }
            if (firstNode != null) {
                for (int j = 0; j < objs.length; j++) {
                    if (objs[j] instanceof AnimationTrack) {
                        try {
                            ((Node) firstNode).addAnimationTrack((AnimationTrack) objs[j]);
                            ((Node) firstNode).animate(pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF }));
                        } catch (Throwable t) { record(t); }
                    }
                }
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opAnimControllerEdge(int i) {
        begin(i, "animation controller edge");
        try {
            int interp = pickInt(new int[] { 176, 177, 178, 179, 180 });
            int comp = (interp == 177 || interp == 179) ? 4 : 1 + nextInt(4);
            int nkeys = pickInt(new int[] { 1, 2, 3, 16 });
            KeyframeSequence ks = new KeyframeSequence(comp, nkeys, interp);
            for (int k = 0; k < nkeys; k++) {
                float[] key = new float[comp + 1];
                key[0] = pickFloat();
                for (int c = 0; c < comp; c++) {
                    key[c + 1] = pickFloat();
                }
                try {
                    ks.setKeyframe(k, (int) pickFloat(), key);
                } catch (Throwable t) { record(t); }
            }
            try {
                ks.setValidRange(pickInt(new int[] { 0, 1, -1, nkeys, 0x7FFFFFFF, (int) 0x80000000 }),
                                 pickInt(new int[] { 0, 1, -1, nkeys, 0x7FFFFFFF }));
            } catch (Throwable t) { record(t); }
            try {
                ks.setDuration(pickInt(new int[] { -1, 0, 1, 1000, 0x7FFFFFFF, (int) 0x80000000 }));
            } catch (Throwable t) { record(t); }
            try {
                ks.setRepeatMode(pickInt(new int[] { 192, 193, 0, -1 }));
            } catch (Throwable t) { record(t); }
            int prop = pickInt(new int[] { 256, 257, 258, 259, 260, 261, 262, 263, 264,
                                           265, 266, 267, 268, 269, 270, 271, 272, 273,
                                           274, 275, 276, 0, -1, 999, 0x7FFFFFFF });
            AnimationTrack tr = new AnimationTrack(ks, prop);
            AnimationController c = new AnimationController();
            try { tr.setController(c); } catch (Throwable t) { record(t); }
            int refTime = pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 });
            try { c.setSpeed(pickFloat(), refTime); } catch (Throwable t) { record(t); }
            try { c.setPosition(pickFloat(), refTime); } catch (Throwable t) { record(t); }
            try { c.setWeight(pickFloat()); } catch (Throwable t) { record(t); }
            try {
                c.setActiveInterval(pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 }),
                                    pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF }));
            } catch (Throwable t) { record(t); }
            Node nd = (Node) new Group();
            try { nd.addAnimationTrack(tr); } catch (Throwable t) { record(t); }
            for (int k = 0; k < 4; k++) {
                try {
                    nd.animate(pickInt(new int[] { 0, 1, -1, 1000, 0x7FFFFFFF, (int) 0x80000000 }));
                } catch (Throwable t) { record(t); }
            }
            try { c.setActiveInterval(0, 0); } catch (Throwable t) { record(t); }
            try { nd.animate(0); } catch (Throwable t) { record(t); }
            if (nextInt(3) == 0) {
                try { nd.removeAnimationTrack(tr); } catch (Throwable t) { record(t); }
                try { nd.animate(0); } catch (Throwable t) { record(t); }
            }
            try {
                Object3D d = nd.duplicate();
                if (d instanceof Node) { renderQuietly((Node) d); }
            } catch (Throwable t) { record(t); }
            renderQuietly(nd);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opSpriteTexture(int i) {
        begin(i, "sprite and texture churn");
        try {
            int w = 1 + nextInt(8), h = 1 + nextInt(8);
            Image2D img = new Image2D(100, w, h, bytes(w * h * 4));
            Image2D one = new Image2D(100, 1, 1, bytes(4));
            Texture2D tex = new Texture2D(img);
            int[] filter = new int[] { 208, 209, 210, 0, -1, 211 };
            int[] wrap = new int[] { 240, 241, 0, -1, 242 };
            try { tex.setFiltering(pickInt(filter), pickInt(filter)); } catch (Throwable t) { record(t); }
            try { tex.setWrapping(pickInt(wrap), pickInt(wrap)); } catch (Throwable t) { record(t); }
            try {
                tex.setBlending(pickInt(new int[] { 224, 225, 226, 227, 228, 0, -1, 229 }));
            } catch (Throwable t) { record(t); }
            try {
                tex.setBlendColor(pickInt(new int[] { 0, -1, 0x7FFFFFFF, (int) 0x80000000 }));
            } catch (Throwable t) { record(t); }
            Appearance ap = new Appearance();
            try { ap.setTexture(0, tex); } catch (Throwable t) { record(t); }
            Sprite3D s = new Sprite3D(nextInt(2) == 0, img, ap);
            try {
                s.setCrop(pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 }),
                          pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 }),
                          pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 }),
                          pickInt(new int[] { 0, 1, -1, 0x7FFFFFFF, (int) 0x80000000 }));
            } catch (Throwable t) { record(t); }
            renderQuietly(s);
            try { s.setImage(nextInt(2) == 0 ? one : img); } catch (Throwable t) { record(t); }
            try { tex.setImage(nextInt(3) == 0 ? one : img); } catch (Throwable t) { record(t); }
            try { ap.setTexture(0, nextInt(4) == 0 ? null : tex); } catch (Throwable t) { record(t); }
            renderQuietly(s);
            try { s.setAppearance(nextInt(3) == 0 ? null : ap); } catch (Throwable t) { record(t); }
            renderQuietly(s);
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    static void opLifeChurn(int i) {
        begin(i, "object lifetime churn");
        try {
            g3d.bindTarget(target);
            g3d.releaseTarget();
            try { g3d.bindTarget(target, true, 0); } catch (Throwable t) { record(t); }
            try { g3d.releaseTarget(); } catch (Throwable t) { record(t); }
            try { g3d.releaseTarget(); } catch (Throwable t) { record(t); }
            for (int k = 0; k < 4; k++) {
                Object3D[] objs = Loader.load(m3gFile(5, true), 0);
                if (objs != null) {
                    for (int j = 0; j < objs.length; j++) {
                        if (objs[j] instanceof Node) { renderQuietly((Node) objs[j]); }
                    }
                }
                World w = new World();
                Camera cam = new Camera();
                cam.setPerspective(60.0f, 1.0f, 0.1f, 100.0f);
                w.setActiveCamera(cam);
                Group grp = new Group();
                w.addChild(grp);
                g3d.bindTarget(target);
                try { g3d.render(w); } catch (Throwable t) { record(t); }
                g3d.releaseTarget();
            }
            Node nd = (Node) new Group();
            for (int k = 0; k < 16; k++) {
                Object3D d = nd.duplicate();
                if (d instanceof Node) { renderQuietly((Node) d); }
            }
        } catch (Throwable t) {
            failA(i, t);
        }
    }

    // ---------------------------------------------------------------- driver

    static void runSeed(int s) {
        seed(s);
        System.out.println("FUZZ SEED " + s);
        for (int i = 0; i < OPS_PER_SEED; i++) {
            int i0 = i;
            int k = nextInt(46);
            switch (k) {
                case 0: case 1: case 2: opVertexArray(i0); break;
                case 3: opVertexArrayExtreme(i0); break;
                case 4: case 5: opVertexBuffer(i0); break;
                case 6: case 7: opIndexBuffer(i0); break;
                case 8: case 9: opImage2D(i0); break;
                case 10: opTexture2D(i0); break;
                case 11: opAppearance(i0); break;
                case 12: case 13: opTransformable(i0); break;
                case 14: case 15: opTransform(i0); break;
                case 16: opNodeGraph(i0); break;
                case 17: opNodeQueries(i0); break;
                case 18: opMeshRender(i0); break;
                case 19: opMeshCtor(i0); break;
                case 20: opCamera(i0); break;
                case 21: case 22: opLight(i0); break;
                case 23: opGraphicsState(i0); break;
                case 24: opBindTarget(i0); break;
                case 25: opImmediate(i0); break;
                case 26: opRenderWorld(i0); break;
                case 27: opRenderStress(i0); break;
                case 28: opRasterExtreme(i0); break;
                case 29: opLightIndex(i0); break;
                case 30: opTransformArray(i0); break;
                case 31: opKeyframes(i0); break;
                case 32: opSkinMorph(i0); break;
                case 33: opFileWrite(i0); break;
                case 34: opAnimationDeep(i0); break;
                case 35: opAnimateGraph(i0); break;
                case 36: opSkinnedDeep(i0); break;
                case 37: opMorphDeep(i0); break;
                case 38: opMipTexture(i0); break;
                case 39: opLoaderBytes(i0); break;
                case 40: opImmediateDeep(i0); break;
                case 41: opFileEdge(i0); break;
                case 42: opLoaderEvil(i0); break;
                case 43: opAnimControllerEdge(i0); break;
                case 44: opSpriteTexture(i0); break;
                case 45: opLifeChurn(i0); break;
                default: opWorld(i0); break;
            }
        }
        for (int i = 0; i < 12; i++) {
            opFileConnection(100000 + i);
            opFileUrls(200000 + i);
            opRecordStore(300000 + i);
        }
        for (int i = 0; i < 4; i++) {
            opPick(400000 + i);
            opNodeQueries(500000 + i);
        }
    }

    protected void startApp() {
        try {
            target = Image.createImage(32, 32);
            smallTarget = Image.createImage(1, 1);
            g3d = Graphics3D.getInstance();
        } catch (Throwable t) {
            System.out.println("FUZZ SETUP FAILED " + t);
            notifyDestroyed();
            return;
        }
        for (int i = 0; i < SEEDS.length; i++) {
            try {
                runSeed(SEEDS[i]);
            } catch (Throwable t) {
                fail(-1, t);
            }
        }
        System.out.println("FUZZ DONE ops=" + op + " exc=" + excs + " oom=" + oom + " kinds=" + excKinds);
        for (int i = 0; i < excKinds; i++) {
            System.out.println("EXC " + excName[i] + " " + excCount[i]);
        }
        notifyDestroyed();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
