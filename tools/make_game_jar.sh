#!/usr/bin/env bash
#
# Build the full-game test fixture: a MIDlet that uses the MIDP 2.0 game API
# the way a real title does.
#
# Why a second fixture when hello.jar already exists: hello.jar proves the VM
# (interpreter, GC, exceptions, strings).  This one proves the *platform* -
# GameCanvas, Sprite with animation and transforms, TiledLayer scrolling,
# LayerManager, key polling, a background game-loop thread, RecordStore, and
# PNG assets loaded out of the JAR.  Those are the parts a real game touches on
# every frame, and they are exactly the parts that cannot be tested from C.
#
# The program is deliberately deterministic: it runs a fixed number of frames,
# then prints a checksum of the framebuffer and exits.  That makes "the game
# actually rendered something" an assertion rather than an eyeball.
#
# Usage: tools/make_game_jar.sh [output.jar]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/gamedemo.jar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for tool in javac jar; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool not found; cannot build $OUT" >&2; exit 1; }
done

mkdir -p "$WORK/src" "$WORK/classes/res" "$(dirname "$OUT")"

# ---------------------------------------------------------------------
# Assets: a 4x2 sprite sheet of 16x16 frames, and a 64x16 tile strip of
# four 16x16 tiles.  Generated rather than committed so the fixture stays
# self-contained and reproducible.
# ---------------------------------------------------------------------
python3 - "$WORK" <<'PY'
import sys, os
try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required to generate the fixture assets")

work = sys.argv[1]
res = os.path.join(work, "classes", "res")

# --- sprite sheet: 4 columns x 2 rows of 16x16 frames ----------------
# Every frame has a distinct solid body colour and a 1px transparent border,
# so pixel-level collision has something meaningful to test and a wrong
# transform is immediately visible as the wrong colour in the wrong place.
body = [(230, 60, 60), (60, 200, 90), (70, 120, 240), (240, 210, 60),
        (200, 90, 220), (80, 220, 220), (250, 150, 60), (160, 160, 160)]
sheet = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
for i, col in enumerate(body):
    fx = (i % 4) * 16
    fy = (i // 4) * 16
    for y in range(1, 15):
        for x in range(1, 15):
            # A darker centre pixel grid makes rotation direction visible.
            if (x + y) % 7 == 0:
                sheet.putpixel((fx + x, fy + y), (col[0] // 2, col[1] // 2, col[2] // 2, 255))
            else:
                sheet.putpixel((fx + x, fy + y), (col[0], col[1], col[2], 255))
sheet.save(os.path.join(res, "player.png"))

# --- tile strip: 4 tiles of 16x16 -----------------------------------
tiles = [(40, 60, 40), (60, 90, 55), (90, 120, 70), (150, 130, 90)]
strip = Image.new("RGBA", (64, 16), (0, 0, 0, 0))
for i, col in enumerate(tiles):
    for y in range(16):
        for x in range(16):
            shade = 1.0 - (y / 40.0)
            strip.putpixel((i * 16 + x, y),
                           (int(col[0] * shade), int(col[1] * shade),
                            int(col[2] * shade), 255))
strip.save(os.path.join(res, "tiles.png"))
print("assets written to", res)
PY

# ---------------------------------------------------------------------
# Source
# ---------------------------------------------------------------------
cat > "$WORK/src/DemoGame.java" <<'JAVA'
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.lcdui.game.LayerManager;
import javax.microedition.lcdui.game.Sprite;
import javax.microedition.lcdui.game.TiledLayer;
import javax.microedition.rms.RecordStore;

/**
 * A MIDlet that exercises the whole MIDP 2.0 game API on every frame.
 *
 * Deterministic on purpose: FRAMES frames, then a checksum of the framebuffer
 * and exit.  A test can therefore assert that the platform rendered the right
 * thing rather than merely that it did not crash.
 */
public class DemoGame extends MIDlet implements CommandListener {

    static final int FRAMES = 90;

    private DemoCanvas canvas;
    private Command exitCommand;

    static DemoGame instance;

    public void startApp() {
        instance = this;
        exitCommand = new Command("Exit", Command.EXIT, 1);
        canvas = new DemoCanvas();
        canvas.addCommand(exitCommand);
        canvas.setCommandListener(this);
        Display.getDisplay(this).setCurrent(canvas);
    }

    public void pauseApp() { }

    public void destroyApp(boolean unconditional) {
        if (canvas != null) {
            canvas.stop();
        }
    }

    public void commandAction(Command c, Displayable d) {
        if (c == exitCommand) {
            destroyApp(true);
            notifyDestroyed();
        }
    }
}

class DemoCanvas extends GameCanvas implements Runnable {

    private static final int TILE = 16;
    private static final int COLS = 8;
    private static final int ROWS = 12;

    private final LayerManager manager;
    private final TiledLayer background;
    private final Sprite player;
    private final Image playerSheet;
    private final Image tileStrip;

    /*
     * MIDP cannot read pixels back out of a Canvas, so the frame is composed
     * into an offscreen Image and blitted - the standard J2ME double-buffer
     * pattern, and the only way the fixture can checksum what it drew.
     */
    private Image offscreen;
    private Graphics offg;

    private volatile boolean running = true;
    private int frame;
    private int vx = 1;
    private int vy = 1;
    private int saved = -1;
    private String status = "";

    DemoCanvas() {
        super(true);   /* suppressKeyEvents: we poll with getKeyStates() */

        /* ---- assets out of the JAR ---- */
        Image sheet = null;
        Image tiles = null;
        try {
            sheet = Image.createImage("/res/player.png");
            tiles = Image.createImage("/res/tiles.png");
        } catch (Exception e) {
            status = "asset error: " + e;
        }
        playerSheet = sheet;
        tileStrip = tiles;

        offscreen = Image.createImage(getWidth(), getHeight());
        offg = offscreen.getGraphics();

        /* ---- the world ---- */
        manager = new LayerManager();
        manager.setViewWindow(0, 0, getWidth(), getHeight());

        if (tileStrip != null) {
            background = new TiledLayer(COLS, ROWS, tileStrip, TILE, TILE);
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    background.setCell(c, r, 1 + ((r + c) % 3));
                }
            }
            background.setPosition(0, 0);
            manager.append(background);
        } else {
            background = null;
        }

        if (playerSheet != null) {
            player = new Sprite(playerSheet, TILE, TILE);
            player.defineReferencePixel(TILE / 2, TILE / 2);
            player.setRefPixelPosition(getWidth() / 2, getHeight() / 2);
            player.setFrame(0);
            manager.append(player);
        } else {
            player = null;
        }

        /* ---- persistence ---- */
        try {
            RecordStore rs = RecordStore.openRecordStore("demo", true);
            if (rs.getNumRecords() > 0) {
                byte[] b = rs.getRecord(1);
                saved = b[0] & 0xFF;
            }
            byte[] nb = new byte[] { (byte) DemoGame.FRAMES };
            if (rs.getNumRecords() == 0) {
                rs.addRecord(nb, 0, 1);
            } else {
                rs.setRecord(1, nb, 0, 1);
            }
            rs.closeRecordStore();
        } catch (Exception e) {
            status = "rms error: " + e;
        }

        Thread t = new Thread(this);
        t.start();
    }

    void stop() {
        running = false;
    }

    public void run() {
        while (running && frame < DemoGame.FRAMES) {
            tick();
            draw();
            flushGraphics();
            try {
                Thread.sleep(10);
            } catch (InterruptedException e) {
                return;
            }
        }
        report();
    }

    /* ---- update ---- */
    private void tick() {
        int keys = getKeyStates();

        if (player == null) {
            return;
        }
        int x = player.getX();
        int y = player.getY();

        if ((keys & GameCanvas.LEFT_PRESSED) != 0) {
            vx = -2;
        } else if ((keys & GameCanvas.RIGHT_PRESSED) != 0) {
            vx = 2;
        }
        if ((keys & GameCanvas.UP_PRESSED) != 0) {
            vy = -2;
        } else if ((keys & GameCanvas.DOWN_PRESSED) != 0) {
            vy = 2;
        }

        x += vx;
        y += vy;
        if (x < 0 || x > getWidth() - TILE) {
            vx = -vx;
            x += vx;
        }
        if (y < 0 || y > getHeight() - TILE) {
            vy = -vy;
            y += vy;
        }
        player.setPosition(x, y);

        /* Cycle the frame and, every 8th frame, the transform, so the
         * renderer's animation and transform paths both get used. */
        player.nextFrame();
        if ((frame & 7) == 0) {
            player.setTransform(TRANSFORMS[(frame >> 3) % TRANSFORMS.length]);
        }

        /* Scroll the tile map: the LayerManager view window is what makes
         * TiledLayer's clipping matter. */
        if (background != null) {
            manager.setViewWindow(0, frame % (ROWS * TILE - getHeight() + 1),
                                  getWidth(), getHeight());
        }
        frame++;
    }

    private static final int[] TRANSFORMS = {
        Sprite.TRANS_NONE,
        Sprite.TRANS_ROT90,
        Sprite.TRANS_ROT180,
        Sprite.TRANS_ROT270,
        Sprite.TRANS_MIRROR,
        Sprite.TRANS_MIRROR_ROT90
    };

    /* ---- render ---- */
    private void draw() {
        offg.setColor(0x102030);
        offg.fillRect(0, 0, getWidth(), getHeight());

        manager.paint(offg, 0, 0);

        offg.setColor(0xFFFFFF);
        offg.drawString("f" + frame, 2, 2, Graphics.TOP | Graphics.LEFT);
        offg.drawString(status, 2, getHeight() - 12,
                        Graphics.BOTTOM | Graphics.LEFT);

        /* Blit the composed frame to the GameCanvas back buffer. */
        Graphics g = getGraphics();
        g.drawImage(offscreen, 0, 0, Graphics.TOP | Graphics.LEFT);
    }

    /**
     * A checksum of everything that was drawn, plus the facts the test cares
     * about.  Printed as a single line so the harness can assert on it.
     */
    private void report() {
        int[] rgb = new int[getWidth() * getHeight()];
        offscreen.getRGB(rgb, 0, getWidth(), 0, 0, getWidth(), getHeight());
        int sum = 0;
        int nonBg = 0;
        for (int i = 0; i < rgb.length; i++) {
            sum = sum * 31 + (rgb[i] & 0xFFFFFF);
            if ((rgb[i] & 0xFFFFFF) != 0x102030) {
                nonBg++;
            }
        }
        int dist = 0;
        int frm = -1;
        int trn = -1;
        if (player != null) {
            dist = player.getRefPixelX() + player.getRefPixelY();
            frm = player.getFrame();
            trn = player.getTransform();
        }
        System.out.println("GAMEDEMO frames=" + frame
                           + " checksum=" + Integer.toHexString(sum)
                           + " drawn=" + nonBg
                           + " refsum=" + dist
                           + " frame=" + frm
                           + " transform=" + trn
                           + " saved=" + saved
                           + " status=[" + status + "]");
        if (DemoGame.instance != null) {
            DemoGame.instance.notifyDestroyed();
        }
    }

    /* GameCanvas.paint is final and empty; the loop draws explicitly. */
    protected void paint(Graphics g) { }
}
JAVA

# MIDP does not exist in Java SE, so javac needs signatures to compile
# against.  tools/midp_stubs.py generates them; they are a compile-time
# classpath only and are never packaged into the JAR.
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
      "$WORK/src/DemoGame.java"

mkdir -p "$WORK/classes/META-INF"
cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MANIFEST'
Manifest-Version: 1.0
MIDlet-Name: nano_j2me game API fixture
MIDlet-Version: 1.0.0
MIDlet-Vendor: nano_j2me
MIDlet-1: DemoGame, /res/player.png, DemoGame
MicroEdition-Profile: MIDP-2.0
MicroEdition-Configuration: CLDC-1.1
MANIFEST

( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF \
      DemoGame.class DemoCanvas.class res/player.png res/tiles.png )

echo "wrote $OUT"
unzip -l "$OUT"
