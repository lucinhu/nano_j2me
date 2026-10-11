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

    print("wrote %s (%dx%d) and %s" % (out, WIDTH, HEIGHT, alpha_out))


if __name__ == "__main__":
    main()
