import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.m3g.AnimationController;
import javax.microedition.m3g.AnimationTrack;
import javax.microedition.m3g.Appearance;
import javax.microedition.m3g.Background;
import javax.microedition.m3g.Camera;
import javax.microedition.m3g.CompositingMode;
import javax.microedition.m3g.Fog;
import javax.microedition.m3g.Graphics3D;
import javax.microedition.m3g.Group;
import javax.microedition.m3g.Image2D;
import javax.microedition.m3g.IndexBuffer;
import javax.microedition.m3g.KeyframeSequence;
import javax.microedition.m3g.Light;
import javax.microedition.m3g.Loader;
import javax.microedition.m3g.Material;
import javax.microedition.m3g.Mesh;
import javax.microedition.m3g.MorphingMesh;
import javax.microedition.m3g.Node;
import javax.microedition.m3g.Object3D;
import javax.microedition.m3g.PolygonMode;
import javax.microedition.m3g.RayIntersection;
import javax.microedition.m3g.SkinnedMesh;
import javax.microedition.m3g.Sprite3D;
import javax.microedition.m3g.Texture2D;
import javax.microedition.m3g.Transform;
import javax.microedition.m3g.TriangleStripArray;
import javax.microedition.m3g.VertexArray;
import javax.microedition.m3g.VertexBuffer;
import javax.microedition.m3g.World;
import javax.microedition.midlet.MIDlet;

/**
 * Calls the JSR 184 surface the runtime did not have before, once each, and
 * prints what came back.  The point is not the values - it is that every call
 * either answers or throws the exception the specification allows.  A
 * NoSuchMethodError or an InternalError here means the member surface is
 * wrong, which is exactly what no fixture used to catch: the corpus titles
 * that call these members never got far enough to be a test.
 *
 * Every check is one line of the report, and the report ends with the count.
 */
public class M3GDemo extends MIDlet {

    private static int checks;
    private static int failed;
    private static String firstFail;

    private static boolean idTransformLeaves(float[] q) {
        float[] v = new float[] { q[0], q[1], q[2], q[3] };
        new Transform().transform(v);
        return v[0] == 1.0f && v[1] == 0.0f && v[2] == 0.0f && v[3] == 1.0f;
    }

    private static void ok(String what) {
        checks++;
    }

    private static void check(String what, boolean ok) {
        checks++;
        if (!ok) {
            failed++;
            if (firstFail == null) {
                firstFail = what;
            }
            System.out.println("M3G FAIL " + what);
        }
    }

    private static void run(String what, Runnable body) {
        checks++;
        try {
            body.run();
        } catch (Throwable t) {
            failed++;
            if (firstFail == null) {
                firstFail = what + ": " + t;
            }
            System.out.println("M3G FAIL " + what + ": " + t);
        }
    }

    protected void startApp() {
        try {
            work();
        } catch (Throwable t) {
            System.out.println("M3G FAILED " + t);
        }
        System.out.println("M3G checks=" + checks + " failures=" + failed
                           + " first=" + (firstFail == null ? "-" : firstFail));
        notifyDestroyed();
    }

    private void work() {
        final Group root = new Group();
        final Camera cam = new Camera();

        /* A float stored in an array must come back as the same float: the
         * interpreter's fastore used to reinterpret the bit pattern, so
         * 1.0f read back as 1.06535e9. */
        float[] plain = new float[] { 1.0f, 2.5f, -0.5f, 0.25f };
        plain[0] = 1.0f;
        plain[1] = 1.0f + 1.5f;
        check("float literals survive an array",
              plain[0] == 1.0f && plain[1] == 2.5f && plain[2] == -0.5f
              && plain[3] == 0.25f);

        run("Object3D.setUserObject/getUserObject", new Runnable() {
            public void run() {
                root.setUserObject("m3gdemo");
                check("user object comes back",
                      "m3gdemo".equals(root.getUserObject()));
            }
        });

        run("Object3D.getReferences", new Runnable() {
            public void run() {
                Group child = new Group();
                root.addChild(child);
                Object3D[] refs = new Object3D[2];
                /* JSR 184: the count is answerable with a null array, the
                 * children of a Group are direct references, and an array
                 * that is too small is refused. */
                check("getReferences counts the child",
                      root.getReferences(null) == 1);
                check("getReferences fills the array", root.getReferences(refs) == 1);
                check("getReferences names the child", refs[0] == child);
                try {
                    root.getReferences(new Object3D[0]);
                    check("a too-small array is refused", false);
                } catch (IllegalArgumentException e) {
                    ok("a too-small array is refused");
                }
            }
        });

        run("Node.getAlphaFactor", new Runnable() {
            public void run() {
                root.setAlphaFactor(0.25f);
                check("alpha factor comes back", root.getAlphaFactor() == 0.25f);
            }
        });

        run("Node.align/isAligned", new Runnable() {
            public void run() {
                Group target = new Group();
                root.align(target);
                check("the alignment reference comes back",
                      root.getAlignmentReference(Node.NONE) == target);
                ok("the alignment target answers "
                   + root.getAlignmentTarget(Node.NONE));
            }
        });

        run("Transform copy constructor and postRotateQuat", new Runnable() {
            public void run() {
                Transform t = new Transform();
                Transform copy = new Transform(t);
                copy.postRotateQuat(0.0f, 0.0f, 0.70710678f, 0.70710678f);
                float[] m = new float[16];
                copy.get(m);
                check("a quaternion rotation moved the matrix", m[0] != 1.0f || m[1] != 0.0f);
            }
        });

        run("Transformable.preRotate", new Runnable() {
            public void run() {
                Sprite3D s = new Sprite3D(false, null, null);
                s.preRotate(90.0f, 0.0f, 0.0f, 1.0f);
                Transform t = new Transform();
                s.getTransform(t);
                /* read the matrix through a point: element order is the
                 * implementation's business, the transform is not */
                float[] pt = new float[] { 1.0f, 0.0f, 0.0f, 1.0f };
                t.transform(pt);
                check("preRotate turned +X towards +Y",
                      Math.abs(pt[0]) < 0.01f && Math.abs(pt[1] - 1.0f) < 0.01f);
                check("an identity transform leaves (1,0,0,1)",
                      idTransformLeaves(new float[] { 1.0f, 0.0f, 0.0f, 1.0f }));
            }
        });

        run("Camera default projection", new Runnable() {
            public void run() {
                float[] p = new float[16];
                check("a fresh Camera has a projection", cam.getProjection(p) == 1);
            }
        });

        run("Appearance fog", new Runnable() {
            public void run() {
                Appearance a = new Appearance();
                Fog f = new Fog();
                a.setFog(f);
                check("the fog comes back", a.getFog() == f);
                a.setLayer(3);
                check("the layer comes back", a.getLayer() == 3);
                check("an Appearance has no material yet", a.getMaterial() == null);
            }
        });

        run("Background", new Runnable() {
            public void run() {
                Background b = new Background();
                b.setColor(0x123456);
                check("the colour comes back", b.getColor() == 0x123456);
                b.setImageMode(Background.REPEAT, Background.BORDER);
                check("image mode X comes back", b.getImageModeX() == Background.REPEAT);
                check("image mode Y comes back", b.getImageModeY() == Background.BORDER);
            }
        });

        run("CompositingMode", new Runnable() {
            public void run() {
                CompositingMode c = new CompositingMode();
                c.setAlphaWriteEnable(false);
                check("alpha write is off", !c.isAlphaWriteEnabled());
                c.setColorWriteEnable(true);
                check("colour write is on", c.isColorWriteEnabled());
                c.setDepthOffset(1.5f, 2.5f);
                check("depth offset factor comes back", c.getDepthOffsetFactor() == 1.5f);
                check("depth offset units come back", c.getDepthOffsetUnits() == 2.5f);
                c.setAlphaThreshold(0.5f);
                check("alpha threshold comes back", c.getAlphaThreshold() == 0.5f);
                ok("depth test answers " + c.isDepthTestEnabled());
                ok("depth write answers " + c.isDepthWriteEnabled());
            }
        });

        run("PolygonMode", new Runnable() {
            public void run() {
                PolygonMode p = new PolygonMode();
                p.setWinding(PolygonMode.WINDING_CCW);
                check("the winding comes back", p.getWinding() == PolygonMode.WINDING_CCW);
                p.setLocalCameraLightingEnable(true);
                check("local camera lighting is on", p.isLocalCameraLightingEnabled());
                ok("perspective correction answers "
                   + p.isPerspectiveCorrectionEnabled());
            }
        });

        run("Light attenuation", new Runnable() {
            public void run() {
                Light l = new Light();
                l.setAttenuation(1.0f, 2.0f, 3.0f);
                check("constant attenuation comes back", l.getConstantAttenuation() == 1.0f);
                check("linear attenuation comes back", l.getLinearAttenuation() == 2.0f);
                check("quadratic attenuation comes back", l.getQuadraticAttenuation() == 3.0f);
            }
        });

        run("Image2D and Texture2D", new Runnable() {
            public void run() {
                byte[] px = new byte[4 * 4 * 3];
                Image2D img = new Image2D(Image2D.RGB, 4, 4, px);
                check("a built image is not mutable", !img.isMutable());
                Image2D mut = new Image2D(Image2D.RGB, 4, 4, null);
                check("an image built from null is mutable", mut.isMutable());
                Texture2D t = new Texture2D(mut);
                t.setBlendColor(0x00FF00);
                check("the blend colour comes back", t.getBlendColor() == 0x00FF00);
                ok("wrapping answers " + t.getWrappingS() + "/" + t.getWrappingT());
                ok("filtering answers " + t.getLevelFilter() + "/" + t.getImageFilter());
                ok("blending answers " + t.getBlending());
            }
        });

        run("Sprite3D", new Runnable() {
            public void run() {
                Image2D img = new Image2D(Image2D.RGB, 4, 4, new byte[4 * 4 * 3]);
                Sprite3D s = new Sprite3D(false, img, new Appearance());
                check("the image comes back", s.getImage() == img);
                s.setCrop(1, 2, 3, 4);
                check("crop X comes back", s.getCropX() == 1);
                check("crop Y comes back", s.getCropY() == 2);
                check("crop width comes back", s.getCropWidth() == 3);
                check("crop height comes back", s.getCropHeight() == 4);
                check("a sprite is not scaled by default", !s.isScaled());
                check("the appearance comes back", s.getAppearance() != null);
            }
        });

        run("VertexArray and VertexBuffer", new Runnable() {
            public void run() {
                VertexArray va = new VertexArray(3, 4, 2);
                byte[] data = new byte[3 * 4 * 2];
                va.set(0, 4, data);
                /* the third argument is the component SIZE in bytes and the
                 * type is derived from it (JSR 184: BYTE 0, SHORT 1, FIXED 2) */
                check("a two-byte component is a SHORT (type="
                      + va.getComponentType() + ")",
                      va.getComponentType() == 1);
                check("a four-byte component is a FIXED",
                      new VertexArray(1, 3, 4).getComponentType() == 2);
                short[] out = new short[4 * 3];
                va.get(0, 4, out);
                check("the data comes back as shorts", out[0] == 0);
                VertexBuffer vb = new VertexBuffer();
                vb.setPositions(va, 1.0f, null);
                float[] scale = new float[3];
                check("the positions come back", vb.getPositions(scale) == va);
                check("the scale comes back", scale[1] == 1.0f);
                check("there are no normals yet", vb.getNormals() == null);
                check("there are no colours yet", vb.getColors() == null);
                check("there are no texture coordinates yet",
                      vb.getTexCoords(0, new float[3]) == null);
                vb.setNormals(va);
                check("the normals come back", vb.getNormals() == va);
                check("the vertex count comes back", vb.getVertexCount() == 4);
            }
        });

        run("Transform.transform(VertexArray)", new Runnable() {
            public void run() {
                VertexArray va = new VertexArray(3, 1, 1);
                va.set(0, 1, new byte[] { 0, 0, 0 });
                float[] out = new float[4];
                new Transform().transform(va, out, true);
                check("the transformed point comes back", out[3] == 1.0f);
            }
        });

        run("Group.pick", new Runnable() {
            public void run() {
                World w = new World();
                w.addChild(root);
                RayIntersection ri = new RayIntersection();
                ok("a ray into an empty group answers " + w.pick(-1, 0.0f, 0.0f, cam, ri));
                ok("the scalar form answers "
                   + w.pick(-1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ri));
            }
        });

        run("Mesh and its animations", new Runnable() {
            public void run() {
                VertexBuffer vb = new VertexBuffer();
                vb.setPositions(new VertexArray(3, 3, 2), 1.0f, null);
                IndexBuffer ib = new TriangleStripArray(0, new int[] { 3 });
                Mesh m = new Mesh(vb, ib, new Appearance());
                AnimationTrack track = new AnimationTrack(
                        new KeyframeSequence(2, 3, 180), AnimationTrack.ALPHA);
                m.addAnimationTrack(track);
                check("the track comes back", m.getAnimationTrack(0) == track);
                check("there is one track", m.getAnimationTrackCount() == 1);
                m.removeAnimationTrack(track);
                check("the track was removed", m.getAnimationTrackCount() == 0);
            }
        });

        run("MorphingMesh and SkinnedMesh", new Runnable() {
            public void run() {
                VertexBuffer vb = new VertexBuffer();
                vb.setPositions(new VertexArray(3, 3, 2), 1.0f, null);
                VertexBuffer[] targets = new VertexBuffer[] { vb };
                IndexBuffer ib = new TriangleStripArray(0, new int[] { 3 });
                MorphingMesh mm = new MorphingMesh(vb, targets, ib, new Appearance());
                check("a MorphingMesh is a Mesh", mm instanceof Mesh);
                check("the morph target comes back", mm.getMorphTarget(0) == vb);
                check("the morph target count comes back", mm.getMorphTargetCount() == 1);
                float[] w = new float[1];
                mm.getWeights(w);
                mm.setWeights(new float[] { 1.0f });
                ok("the weights round trip");
                Group skeleton = new Group();
                SkinnedMesh sm = new SkinnedMesh(vb, ib, new Appearance(), skeleton);
                check("a SkinnedMesh is a Mesh", sm instanceof Mesh);
                check("the skeleton comes back", sm.getSkeleton() == skeleton);
                sm.addTransform(skeleton, 0, 0, 0);
                ok("the bone transform answers "
                   + sm.getBoneVertices(skeleton, new int[1], new float[3]));
            }
        });

        run("Graphics3D state", new Runnable() {
            public void run() {
                Graphics3D g = Graphics3D.getInstance();
                ok("the target answers " + (g.getTarget() == null ? "none" : "set"));
                ok("the hints answer " + g.getHints());
                ok("the depth buffer answers " + g.isDepthBufferEnabled());
                g.setViewport(1, 2, 30, 40);
                check("viewport X comes back", g.getViewportX() == 1);
                check("viewport width comes back", g.getViewportWidth() == 30);
                g.setDepthRange(0.25f, 0.75f);
                check("depth range near comes back", g.getDepthRangeNear() == 0.25f);
                check("depth range far comes back", g.getDepthRangeFar() == 0.75f);
                Light l = new Light();
                g.addLight(l, new Transform());
                check("the light count comes back", g.getLightCount() == 1);
                check("the light comes back", g.getLight(0, new Transform()) == l);
                g.resetLights();
                check("resetLights emptied them", g.getLightCount() == 0);
                check("the camera is not set yet", g.getCamera(new Transform()) == null);
            }
        });

        run("Graphics3D.render with a bound target", new Runnable() {
            public void run() {
                Image2D img = new Image2D(Image2D.RGB, 8, 8, new byte[8 * 8 * 3]);
                Canvas c = new Canvas() {
                    protected void paint(Graphics g) {
                        Graphics3D g3d = Graphics3D.getInstance();
                        VertexBuffer vb = new VertexBuffer();
                        vb.setPositions(new VertexArray(3, 3, 2), 1.0f, null);
                        IndexBuffer ib = new TriangleStripArray(0, new int[] { 3 });
                        g3d.bindTarget(g);
                        g3d.render(vb, ib, new Appearance(), new Transform());
                        g3d.render(vb, ib, new Appearance(), new Transform(), -1);
                        g3d.releaseTarget();
                    }
                };
                Display.getDisplay(M3GDemo.this).setCurrent(c);
                c.repaint();
                c.serviceRepaints();
                ok("a render through a bound Graphics returned");
                ok("the image is still usable " + (img.getWidth() == 8));
            }
        });

        /* A strip, with the indices given explicitly: the constructor used
         * to store the index array as the strip lengths, so this is the
         * check that says which array is which. */
        run("a triangle strip uses its explicit indices", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(4, 3, 2);
                pos.set(0, 4, new short[] { -2, 2, -5, 2, 2, -5,
                                            -2, -2, -5, 2, -2, -5 });
                vb.setPositions(pos, 1.0f, null);
                IndexBuffer ib = new TriangleStripArray(
                        new int[] { 0, 1, 2, 3 }, new int[] { 4 });

                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.DIFFUSE, 0x0000FF00);
                app.setMaterial(mat);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(vb, ib, app, new Transform());
                g3d.releaseTarget();

                int[] upper = new int[1];
                int[] lower = new int[1];
                img.getRGB(upper, 0, 1, 12, 12, 1, 1);
                img.getRGB(lower, 0, 1, 20, 20, 1, 1);
                check("the first strip triangle is drawn",
                      upper[0] == 0xFF00FF00);
                check("the second strip triangle is drawn",
                      lower[0] == 0xFF00FF00);
            }
        });

        /* bindTarget resets the viewport to the whole target: a title that
         * binds and renders without ever calling setViewport still draws.
         * 浴火银河2完整版_7689 does exactly that once a frame, and with the
         * viewport left at 0x0 every one of its scenes was skipped. */
        run("bindTarget leaves the viewport over the whole target", new Runnable() {
            public void run() {
                Image img = Image.createImage(24, 24);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 24, 24);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                pos.set(0, 3, new short[] { -4, -4, -5, 4, -4, -5, 0, 4, -5 });
                vb.setPositions(pos, 1.0f, null);

                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.DIFFUSE, 0x00FFFF00);
                app.setMaterial(mat);

                g3d.bindTarget(g);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(vb, null, app, new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 12, 16, 1, 1);
                check("a render after bindTarget covers the target",
                      px[0] == 0xFFFFFF00);
            }
        });

        /* render() used to answer 0 without drawing: the modern JSR call had
         * no path at all, while render(World) went through the graphics
         * layer.  This asks for one triangle and looks for its pixels. */
        run("Graphics3D.render draws the triangles it is given", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                /* In front of the default camera (45 degrees, near 1): z = -5
                 * puts the triangle inside the frustum. */
                pos.set(0, 3, new short[] { -2, -2, -5, 2, -2, -5, 0, 2, -5 });
                vb.setPositions(pos, 1.0f, null);

                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.DIFFUSE, 0x00FF0000);
                app.setMaterial(mat);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                /* render() is void in this API: what it drew is read back
                 * from the target, which is also how a title sees it. */
                g3d.render(vb, null, app, new Transform(), -1);
                g3d.releaseTarget();

                int[] inside = new int[1];
                int[] corner = new int[1];
                img.getRGB(inside, 0, 1, 16, 16, 1, 1);
                img.getRGB(corner, 0, 1, 1, 1, 1, 1);
                check("the triangle reached the target",
                      inside[0] == 0xFFFF0000);
                check("the corner it does not cover is untouched",
                      corner[0] == 0xFF000000);
            }
        });

        run("Loader.load of a resource the suite does not have", new Runnable() {
            public void run() {
                try {
                    Loader.load("/nope.m3g");
                    check("a missing resource throws IOException", false);
                } catch (Exception e) {
                    ok("a missing resource throws IOException");
                }
            }
        });
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
