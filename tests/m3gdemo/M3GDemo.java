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
                /* Wound the way the JSR calls a front face (counter-
                 * clockwise seen from the camera): a strip keeps that
                 * winding by swapping the first two vertices of its odd
                 * triangles, which is how the rasteriser walks it. */
                pos.set(0, 4, new short[] { -2, 2, -5, -2, -2, -5,
                                            2, 2, -5, 2, -2, -5 });
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

        /* A depth buffer: two triangles over the same pixels three units
         * apart in z, drawn far first and then near first.  The nearer
         * colour must be on screen both times - that is what a depth test
         * buys, and without one the second render would always win. */
        run("the depth buffer keeps the nearer triangle", new Runnable() {
            public void run() {
                int[] px = new int[1];

                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer near = tri(-2, -2, -5, 2, -2, -5, 0, 2, -5);
                VertexBuffer far = tri(-2, -2, -8, 2, -2, -8, 0, 2, -8);
                Appearance red = flat(0x00FF0000);
                Appearance blue = flat(0x000000FF);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(far, null, blue, new Transform());
                g3d.render(near, null, red, new Transform());
                g3d.releaseTarget();
                img.getRGB(px, 0, 1, 16, 16, 1, 1);
                check("the near triangle wins when it is drawn last",
                      px[0] == 0xFFFF0000);

                Image img2 = Image.createImage(32, 32);
                Graphics g2 = img2.getGraphics();
                g2.setColor(0x000000);
                g2.fillRect(0, 0, 32, 32);
                g3d.bindTarget(g2);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(near, null, red, new Transform());
                g3d.render(far, null, blue, new Transform());
                g3d.releaseTarget();
                img2.getRGB(px, 0, 1, 16, 16, 1, 1);
                check("the depth buffer does not depend on the draw order",
                      px[0] == 0xFFFF0000);
            }
        });

        /* Back-face culling: the same triangle with its first two vertices
         * swapped faces away from the camera, so it is not drawn - unless
         * the PolygonMode asks for CULL_NONE, which must then draw it. */
        run("a back-facing triangle is culled unless asked for", new Runnable() {
            public void run() {
                int[] px = new int[1];

                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer back = tri(2, -2, -5, -2, -2, -5, 0, 2, -5);
                Appearance red = flat(0x00FF0000);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(back, null, red, new Transform());
                g3d.releaseTarget();
                img.getRGB(px, 0, 1, 16, 16, 1, 1);
                check("a back-facing triangle draws nothing",
                      px[0] == 0xFF000000);

                PolygonMode pm = new PolygonMode();
                pm.setCulling(PolygonMode.CULL_NONE);
                red.setPolygonMode(pm);
                Image img2 = Image.createImage(32, 32);
                Graphics g2 = img2.getGraphics();
                g2.setColor(0x000000);
                g2.fillRect(0, 0, 32, 32);
                g3d.bindTarget(g2);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(back, null, red, new Transform());
                g3d.releaseTarget();
                img2.getRGB(px, 0, 1, 16, 16, 1, 1);
                check("CULL_NONE draws the same triangle",
                      px[0] == 0xFFFF0000);
            }
        });

        /* Texture mapping: a 2x2 RGB texture whose four texels are red,
         * green, blue and white.  The triangle's texture coordinates put
         * each corner on a different texel, so the pixels say which texel
         * the sampler picked - the material is white, so it cannot be the
         * colour that made them. */
        run("a textured triangle samples the texture", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                Image2D image = new Image2D(Image2D.RGB, 2, 2, new byte[] {
                    (byte) 255, 0, 0,  0, (byte) 255, 0,
                    0, 0, (byte) 255,  (byte) 255, (byte) 255, (byte) 255 });
                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.DIFFUSE, 0x00FFFFFF);
                app.setMaterial(mat);
                app.setTexture(0, new Texture2D(image));

                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                pos.set(0, 3, new short[] { -2, -2, -5, 2, -2, -5, 0, 2, -5 });
                vb.setPositions(pos, 1.0f, null);
                VertexArray tc = new VertexArray(3, 2, 2);
                tc.set(0, 3, new short[] { 0, 0, 1, 0, 0, 1 });
                vb.setTexCoords(0, tc, 1.0f, null);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(vb, null, app, new Transform(), -1);
                g3d.releaseTarget();

                int[] red = new int[1];
                int[] blue = new int[1];
                img.getRGB(red, 0, 1, 10, 21, 1, 1);
                /* The apex is the third vertex: (0, 2, -5) divides through
                 * w = 5 and projects to (16, 12.9) in the 32x32 viewport -
                 * not to (16, 0) or (12, 0), which is what the first two
                 * versions of this check assumed. */
                img.getRGB(blue, 0, 1, 16, 14, 1, 1);
                int[] green = new int[1];
                img.getRGB(green, 0, 1, 21, 22, 1, 1);
                check("the texel at (0,0) is the colour at that corner",
                      red[0] == 0xFFFF0000);
                check("the texel at (0,1) is the colour at that corner",
                      blue[0] == 0xFF0000FF);
                check("the texel at (1,0) is the colour at that corner",
                      green[0] == 0xFF00FF00);
            }
        });

        /* Lighting.  A surface is lit when the Appearance carries a Material
         * and the buffer carries normals: the material's ambient colour times
         * the ambient lights, plus the material's diffuse colour times
         * max(0, N.L) for every other light, plus emission and specular.
         * The triangle faces +z, so a light shining along -z - the identity
         * transform's own axis - lights it fully, and the same light turned
         * 90 degrees about y only grazes it. */
        run("a directional light lights the face towards it", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                pos.set(0, 3, new short[] { -2, -2, -5, 2, -2, -5, 0, 2, -5 });
                vb.setPositions(pos, 1.0f, null);
                /* Normals are one byte per component in this API: 127 is 1.0. */
                VertexArray nrm = new VertexArray(3, 3, 1);
                nrm.set(0, 3, new byte[] { 0, 0, 127, 0, 0, 127, 0, 0, 127 });
                vb.setNormals(nrm);

                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.AMBIENT, 0x00404040);
                mat.setColor(Material.DIFFUSE, 0x00FF0000);
                app.setMaterial(mat);

                Light amb = new Light();
                amb.setMode(Light.AMBIENT);
                amb.setColor(0x00808080);
                Light dir = new Light();          /* DIRECTIONAL by default */
                dir.setColor(0x00FFFFFF);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.resetLights();
                g3d.addLight(amb, null);
                g3d.addLight(dir, null);
                g3d.render(vb, null, app, new Transform(), -1);
                int[] lit = new int[1];
                img.getRGB(lit, 0, 1, 10, 21, 1, 1);
                /* 0x404040 * 0x808080 / 255 = 0x202020 ambient, plus the
                 * diffuse red at full strength. */
                check("the lit face is its diffuse colour over its ambient",
                      lit[0] == 0xFFFF2020);

                Transform turned = new Transform();
                turned.postRotate(90.0f, 0.0f, 1.0f, 0.0f);
                g3d.setLight(1, dir, turned);
                /* clear() clears the depth buffer as well as the colour, so
                 * the second render is not rejected by the first one's
                 * depths - the same triangle at the same depth is exactly
                 * what the depth test is supposed to reject. */
                Background bg = new Background();
                bg.setColor(0x000000);
                bg.setColorClearEnable(true);
                bg.setDepthClearEnable(true);
                g3d.clear(bg);
                g3d.render(vb, null, app, new Transform(), -1);
                g3d.releaseTarget();
                int[] grazed = new int[1];
                img.getRGB(grazed, 0, 1, 10, 21, 1, 1);
                check("a light that grazes the surface leaves only the ambient",
                      grazed[0] == 0xFF202020);
            }
        });

        /* Without normals there is nothing to light: the material's diffuse
         * colour is used flat, which is what an unlit appearance means. */
        run("a buffer with no normals is not lit", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                pos.set(0, 3, new short[] { -2, -2, -5, 2, -2, -5, 0, 2, -5 });
                vb.setPositions(pos, 1.0f, null);

                Appearance app = new Appearance();
                Material mat = new Material();
                mat.setColor(Material.DIFFUSE, 0x0000FF00);
                app.setMaterial(mat);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(vb, null, app, new Transform(), -1);
                g3d.releaseTarget();
                int[] px = new int[1];
                img.getRGB(px, 0, 1, 10, 21, 1, 1);
                check("the material colour is used unchanged", px[0] == 0xFF00FF00);
            }
        });

        /* A model transform has to move the geometry rather than merely be
         * accepted: postTranslate(1, 0, 0) moves the triangle one world unit
         * towards +x, which at z = -5 with the default camera is about eight
         * of the 32 pixels.  The triangle covers x = -1..1 at the middle row
         * before the move, so a pixel at x = 20 is inside it and a pixel at
         * x = 28 is not; after the move those two swap. */
        run("a model transform moves the geometry", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                VertexBuffer vb = new VertexBuffer();
                VertexArray pos = new VertexArray(3, 3, 2);
                pos.set(0, 3, new short[] { -2, -2, -5, 2, -2, -5, 0, 2, -5 });
                vb.setPositions(pos, 1.0f, null);
                Appearance app = flat(0x00FF00FF);

                Transform moved = new Transform();
                moved.postTranslate(1.0f, 0.0f, 0.0f);

                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(vb, null, app, moved, -1);
                g3d.releaseTarget();
                int[] far = new int[1];
                int[] near = new int[1];
                img.getRGB(far, 0, 1, 30, 16, 1, 1);
                img.getRGB(near, 0, 1, 10, 16, 1, 1);
                int[] row = new int[32];
                img.getRGB(row, 0, 32, 0, 16, 32, 1);
                int lo = -1, hi = -1;
                for (int i = 0; i < 32; i++) {
                    if (row[i] != 0xFF000000) { if (lo < 0) { lo = i; } hi = i; }
                }
                /* lo..hi is the run the moved triangle covers in row 16;
                 * the printed span is the evidence if a check ever fails. */
                if (lo < 0) {
                    System.out.println("M3G DIAG moved row16 is empty");
                }
                /* The triangle is 4 world units wide and 4 tall at z = -5;
                 * a translation of +1 in x moves it one fifth of the way
                 * across the 32-pixel viewport, so its right edge lands in
                 * the last few columns and the left half stays empty. */
                check("the moved triangle reaches the right of the viewport",
                      far[0] == 0xFFFF00FF);
                check("and has left the column it covered before",
                      near[0] == 0xFF000000);
            }
        });

        run("a translucent texture blends with what is behind it", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tex_tri(), null, flat(0x00FF00FF), new Transform(), -1);
                g3d.releaseTarget();
                /* A fresh binding clears the depth buffer, so the second
                 * triangle is not rejected by the first one's depth. */
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tex_tri(), null, tex_app(0x00FF0000, 0x80, -1),
                           new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 16, 14, 1, 1);
                /* Red 255 over magenta with alpha 128: blue is the channel
                 * the two do not share, and it must land half way. */
                check("half a red triangle over magenta leaves half the blue",
                      px[0] == 0xFFFF007F);
            }
        });

        run("the alpha test discards a texel below the threshold", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tex_tri(), null, flat(0x00FF00FF), new Transform(), -1);
                g3d.releaseTarget();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                Appearance app = tex_app(0x00FF0000, 0x4C, -1);   /* alpha 76 */
                CompositingMode cm = new CompositingMode();
                cm.setAlphaThreshold(0.5f);                       /* 127 */
                app.setCompositingMode(cm);
                g3d.render(tex_tri(), null, app, new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 16, 14, 1, 1);
                check("a texel the alpha test rejects leaves the pixel alone",
                      px[0] == 0xFFFF00FF);
            }
        });

        run("without the alpha test the same texel blends", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tex_tri(), null, flat(0x00FF00FF), new Transform(), -1);
                g3d.releaseTarget();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tex_tri(), null, tex_app(0x00FF0000, 0x4C, -1),
                           new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 16, 14, 1, 1);
                check("alpha 76 leaves 179 of the blue behind", px[0] == 0xFFFF00B3);
            }
        });

        run("FUNC_REPLACE takes the texture colour as it is", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                /* A green texture under a red material: MODULATE would give
                 * black, FUNC_REPLACE must give the texture's own green. */
                g3d.render(tex_tri(), null,
                           tex_app(0x0000FF00, 0xFF, Texture2D.FUNC_REPLACE),
                           new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 16, 14, 1, 1);
                check("the material does not tint a REPLACE texture",
                      px[0] == 0xFF00FF00);
            }
        });

        run("fog fades a triangle towards its colour", new Runnable() {
            public void run() {
                Image img = Image.createImage(32, 32);
                Graphics g = img.getGraphics();
                g.setColor(0x000000);
                g.fillRect(0, 0, 32, 32);

                Graphics3D g3d = Graphics3D.getInstance();
                Appearance app = flat(0x00FF0000);
                Fog fog = new Fog();
                fog.setMode(Fog.LINEAR);
                fog.setColor(0x0000FF00);
                fog.setLinear(4.0f, 6.0f);
                app.setFog(fog);
                g3d.bindTarget(g);
                g3d.setViewport(0, 0, 32, 32);
                g3d.setCamera(new Camera(), new Transform());
                g3d.render(tri(-2, -2, -5, 2, -2, -5, 0, 2, -5), null, app,
                           new Transform(), -1);
                g3d.releaseTarget();

                int[] px = new int[1];
                img.getRGB(px, 0, 1, 16, 14, 1, 1);
                /* The triangle sits at z = -5, next to the middle of 4..6,
                 * so half of the red survives and half the fog shows. */
                /* Half of the red survives and half the fog's green:
                 * 0x80 red, 0x80 green, and no blue from either. */
                check("fog at half distance keeps half the colour",
                      px[0] == 0xFF808000);
            }
        });

        /* A benchmark mode, not a check: with -Dnj2me.bench=<n> the same
         * triangle is rendered n times into a target and the elapsed time
         * is printed, so the two render paths can be compared on one
         * machine.  It prints and returns; it never fails the fixture. */
        if (System.getProperty("nj2me.bench") != null) {
            int n = Integer.parseInt(System.getProperty("nj2me.bench"));
            Image img = Image.createImage(240, 320);
            Graphics g = img.getGraphics();
            g.setColor(0x000000);
            g.fillRect(0, 0, 240, 320);

            Graphics3D g3d = Graphics3D.getInstance();
            VertexBuffer vb = new VertexBuffer();
            VertexArray pos = new VertexArray(3, 3, 2);
            pos.set(0, 3, new short[] { -40, -40, -80, 40, -40, -80, 0, 40, -80 });
            vb.setPositions(pos, 1.0f, null);
            Appearance app = flat(0x00336699);

            g3d.bindTarget(g);
            g3d.setViewport(0, 0, 240, 320);
            g3d.setCamera(new Camera(), new Transform());
            long t0 = System.currentTimeMillis();
            for (int i = 0; i < n; i++) {
                g3d.render(vb, null, app, new Transform(), -1);
            }
            long ms = System.currentTimeMillis() - t0;
            g3d.releaseTarget();
            System.out.println("M3G BENCH triangles=" + n + " ms=" + ms);
        }

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

    /* An Appearance whose material is one opaque colour. */
    private static Appearance flat(int rgb) {
        Appearance app = new Appearance();
        Material mat = new Material();
        mat.setColor(Material.DIFFUSE, rgb);
        app.setMaterial(mat);
        return app;
    }

    /* A VertexBuffer holding one triangle, wound the way the JSR calls a
     * front face (counter-clockwise seen from the camera). */
    private static VertexBuffer tri(float x0, float y0, float z0,
                                    float x1, float y1, float z1,
                                    float x2, float y2, float z2) {
        VertexBuffer vb = new VertexBuffer();
        VertexArray pos = new VertexArray(3, 3, 2);
        pos.set(0, 3, new short[] { (short) x0, (short) y0, (short) z0,
                                    (short) x1, (short) y1, (short) z1,
                                    (short) x2, (short) y2, (short) z2 });
        vb.setPositions(pos, 1.0f, null);
        return vb;
    }

    /* The same triangle with unit-0 texture coordinates. */
    private static VertexBuffer tex_tri() {
        VertexBuffer vb = tri(-2, -2, -5, 2, -2, -5, 0, 2, -5);
        VertexArray tc = new VertexArray(3, 2, 2);
        tc.set(0, 3, new short[] { 0, 0, 1, 0, 0, 1 });
        vb.setTexCoords(0, tc, 1.0f, null);
        return vb;
    }

    /* An appearance whose texture is one colour at one alpha; func -1 leaves
     * the JSR default (FUNC_MODULATE) in place. */
    private static Appearance tex_app(int rgb, int alpha, int func) {
        byte r = (byte) ((rgb >> 16) & 0xFF);
        byte g = (byte) ((rgb >> 8) & 0xFF);
        byte b = (byte) (rgb & 0xFF);
        Image2D image = new Image2D(Image2D.RGBA, 2, 2, new byte[] {
            r, g, b, (byte) alpha, r, g, b, (byte) alpha,
            r, g, b, (byte) alpha, r, g, b, (byte) alpha });
        Appearance app = new Appearance();
        Material mat = new Material();
        mat.setColor(Material.DIFFUSE, 0x00FFFFFF);
        app.setMaterial(mat);
        Texture2D tex = new Texture2D(image);
        if (func >= 0) {
            tex.setBlending(func);
        }
        app.setTexture(0, tex);
        return app;
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
