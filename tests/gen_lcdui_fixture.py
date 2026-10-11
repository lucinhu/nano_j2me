#!/usr/bin/env python3
"""
nano_j2me - tests/gen_lcdui_fixture.py

Generate the image fixture tests/test_lcdui.c decodes through
Image.createImage(byte[], int, int).

The image is intentionally small, non-uniform and fully opaque so that a
decoder regression (wrong stride, wrong channel order, alpha mishandled) shows
up as a pixel mismatch rather than as "something was drawn".

Usage:
    python3 tests/gen_lcdui_fixture.py [output.png]

Default output: tests/fixtures/lcdui_rgb.png
"""
import os
import sys
import zipfile

try:
    from PIL import Image
except ImportError:  # pragma: no cover - the build machine has Pillow
    sys.stderr.write("Pillow is required: python3 -m pip install pillow\n")
    raise SystemExit(2)

WIDTH = 13          # deliberately not a multiple of 4 or 8
HEIGHT = 7

# A deterministic pattern: four quadrants with distinct colours plus a diagonal.
QUADRANTS = [
    (255, 0, 0),      # top-left      red
    (0, 255, 0),      # top-right     green
    (0, 0, 255),      # bottom-left   blue
    (255, 255, 255),  # bottom-right  white
]


def pixel(x, y):
    if x == y:                      # a black diagonal
        return (0, 0, 0, 255)
    qx = 0 if x < WIDTH // 2 else 1
    qy = 0 if y < HEIGHT // 2 else 1
    r, g, b = QUADRANTS[qy * 2 + qx]
    return (r, g, b, 255)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fixtures", "lcdui_rgb.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    img = Image.new("RGBA", (WIDTH, HEIGHT))
    img.putdata([pixel(x, y) for y in range(HEIGHT) for x in range(WIDTH)])
    img.save(out, "PNG", optimize=False)

    # A second fixture with real transparency, so processAlpha can be checked.
    alpha_out = out.replace(".png", "_alpha.png")
    aimg = Image.new("RGBA", (WIDTH, HEIGHT))
    aimg.putdata([(255, 0, 0, 128) if (x + y) % 2 == 0 else (0, 0, 0, 0)
                  for y in range(HEIGHT) for x in range(WIDTH)])
    aimg.save(alpha_out, "PNG", optimize=False)

    # ------------------------------------------------------------------
    # The resource JAR the LCDUI test opens as an application.
    # ------------------------------------------------------------------
    # test_lcdui.c does nj_app_open() on this and then exercises
    # Image.createImage("/lcdui_rgb.png"), i.e. the resource-loading path.  It
    # never instantiates a MIDlet - it drives the VM directly - so the JAR needs
    # a parseable manifest and the resources, and no compiled class at all.
    #
    # It is built HERE, by the script the test's own error message names,
    # because for a while it was not built by anything: the fixtures directory
    # is generated and gitignored, so the JAR survived only as long as nobody
    # cleaned it, and the test then failed with a message telling the reader to
    # run this script - which did not produce the file.
    jar_out = os.path.join(os.path.dirname(out), "lcdui_res.jar")
    manifest = (
        "Manifest-Version: 1.0\r\n"
        "MIDlet-Name: lcdui fixture\r\n"
        "MIDlet-Version: 1.0.0\r\n"
        "MIDlet-Vendor: nano_j2me\r\n"
        "MIDlet-1: LcduiFixture, /lcdui_rgb.png, LcduiFixture\r\n"
        "\r\n"
    )
    with zipfile.ZipFile(jar_out, "w", zipfile.ZIP_DEFLATED) as z:
        # The manifest must be the first entry for a reader that looks for it
        # there rather than walking the central directory.
        z.writestr("META-INF/MANIFEST.MF", manifest)
        z.write(out, "lcdui_rgb.png")
        z.write(alpha_out, "lcdui_rgb_alpha.png")

    print("wrote %s (%dx%d) and %s" % (out, WIDTH, HEIGHT, alpha_out))
    print("wrote %s (%d bytes)" % (jar_out, os.path.getsize(jar_out)))


if __name__ == "__main__":
    main()
