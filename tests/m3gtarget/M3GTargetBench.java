import javax.microedition.lcdui.*;
import javax.microedition.midlet.MIDlet;
import javax.microedition.m3g.*;

/** Same 160 textured, lit triangles at 240x320, direct or through an Image. */
public final class M3GTargetBench extends MIDlet implements Runnable {
    private Scene scene;
    protected void startApp() {
        scene = new Scene(getAppProperty("Render-Path"));
        Display.getDisplay(this).setCurrent(scene);
        new Thread(this).start();
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean unconditional) {}
    public void run() {
        while (!scene.done) {
            scene.repaint(); scene.serviceRepaints();
            try { Thread.sleep(1); } catch(Exception ignored) {}
        }
        try { Thread.sleep(100); } catch(Exception ignored) {}
        notifyDestroyed();
    }
    private static final class Scene extends Canvas {
        boolean direct;
        boolean selected;
        Image buffer;
        Graphics bufferGraphics;
        final Graphics3D renderer=Graphics3D.getInstance();
        final VertexBuffer vertices=new VertexBuffer();
        final TriangleStripArray indices;
        final Appearance appearance=new Appearance();
        final Camera camera=new Camera();
        final Transform model=new Transform();
        final Background background=new Background();
        final Light light=new Light();
        int frame;
        long start;
        volatile boolean done;
        Scene(String mode) {
            if (!"select".equals(mode)) select("direct".equals(mode));
            int cells=80,n=cells*4;
            short[] positions=new short[n*3];
            short[] normals=new short[n*3];
            short[] coordinates=new short[n*2];
            int[] strips=new int[cells];
            for(int row=0;row<10;row++)for(int col=0;col<8;col++) {
                int cell=row*8+col;
                strips[cell]=4;
                for(int corner=0;corner<4;corner++) {
                    int at=cell*4+corner;
                    positions[at*3]=(short)(-72+col*18+(corner&1)*16);
                    positions[at*3+1]=(short)(-96+row*19+(corner>>1)*17);
                    positions[at*3+2]=(short)(-200+(row%3)*3);
                    normals[at*3+2]=64;
                    coordinates[at*2]=(short)((corner&1)*64);
                    coordinates[at*2+1]=(short)((corner>>1)*64);
                }
            }
            VertexArray p=new VertexArray(n,3,2);p.set(0,n,positions);
            VertexArray t=new VertexArray(n,2,2);t.set(0,n,coordinates);
            VertexArray normal=new VertexArray(n,3,2);normal.set(0,n,normals);
            vertices.setPositions(p,.02f,null);
            vertices.setNormals(normal);
            vertices.setTexCoords(0,t,1f/64,null);
            indices=new TriangleStripArray(0,strips);
            byte[] rgba={(byte)255,32,16,(byte)255,16,(byte)224,48,(byte)255,
                32,64,(byte)255,(byte)255,(byte)240,(byte)224,16,(byte)255};
            Texture2D texture=new Texture2D(new Image2D(Image2D.RGBA,2,2,rgba));
            texture.setWrapping(Texture2D.WRAP_CLAMP,Texture2D.WRAP_CLAMP);
            texture.setFiltering(Texture2D.FILTER_BASE_LEVEL,Texture2D.FILTER_NEAREST);
            texture.setBlending(Texture2D.FUNC_MODULATE);
            appearance.setTexture(0,texture);
            PolygonMode polygon=new PolygonMode();polygon.setCulling(PolygonMode.CULL_NONE);
            appearance.setPolygonMode(polygon);
            Material material=new Material();material.setColor(Material.AMBIENT,0xffffffff);
            material.setColor(Material.DIFFUSE,0xffffffff);appearance.setMaterial(material);
            light.setMode(Light.AMBIENT);light.setIntensity(1f);
            camera.setPerspective(50f,.75f,1f,20f);
            background.setColor(0x081018);
        }
        private void select(boolean value) {
            direct=value;
            if(!direct) {buffer=Image.createImage(240,320);bufferGraphics=buffer.getGraphics();}
            selected=true;
        }
        protected void keyPressed(int key) {
            if(!selected && (key==53 || key==54)) select(key==53);
        }
        protected void paint(Graphics output) {
            if (done || !selected) return;
            if (frame == 1) start=System.currentTimeMillis();
            Graphics destination=direct?output:bufferGraphics;
            renderer.bindTarget(destination);
            try {
                renderer.setViewport(0,0,240,320);
                renderer.setCamera(camera,null);
                renderer.resetLights();renderer.addLight(light,null);
                renderer.clear(background);
                model.setIdentity();model.postRotate((frame%60)*.4f,0,1,0);
                renderer.render(vertices,indices,appearance,model);
            } finally {renderer.releaseTarget();}
            if(!direct)output.drawImage(buffer,0,0,Graphics.TOP|Graphics.LEFT);
            if (++frame == 61) {
                long elapsed=System.currentTimeMillis()-start;
                System.out.println("M3G_TARGET_DONE path="+(direct?"direct":"buffered")+
                    " frames=60 warmup=1 submitted_triangles_per_frame=160 elapsed_ms="+elapsed);
                done=true;
            }
        }
    }
}
