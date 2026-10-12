#!/usr/bin/env python3
"""
nano_j2me - tests/gen_gif_fixtures.py

Builds the GIF fixtures tests/test_gif.c decodes.

There was no GIF test because there was no GIF decoder: the runtime shipped
PNG and JPEG only, and MIDP 1.0 made GIF mandatory.  src/gfx/nj_gif.c now
decodes 87a/89a, and this is what proves it - the decoder is driven directly,
without a VM, so a failure here is the decoder and not the class library.

The reference is written by RE-OPENING the file Pillow just saved, never from
the image that went in.  Pillow remaps the palette on save, so an expectations
file built from the source pixels would disagree with any decoder that read
the file correctly - the fixture would be wrong, not the decoder.  The same
mistake is why gen_jpeg_fixtures.py re-opens its own output.

Usage: python3 tests/gen_gif_fixtures.py
"""
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures", "gif")

# A palette that survives GIF's 256-entry table unchanged, four flat colours.
COLOURS = [
    (0, 0, 0),
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
    (255, 255, 255),
]


def _checker(w, h, n):
    return Image.new("P", (w, h), 0)


def _paint(im, w, h, n, interlace=False):
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x // max(1, w // 4)) + (y // max(1, h // 4))) % n
    return im


def _palette(im, n):
    pal = []
    for i in range(256):
        c = COLOURS[i % len(COLOURS)] if i < n else (0, 0, 0)
        pal.extend(c)
    im.putpalette(pal)


def write_reference(path, gif_path):
    """
    The expectations file: one RRGGBB per pixel, row-major, read back from the
    GIF Pillow wrote.  Transparency becomes FF000000-style alpha, which the C
    side compares as "is this pixel opaque".
    """
    with Image.open(gif_path) as im:
        rgba = im.convert("RGBA")
        w, h = rgba.size
        with open(path, "w", encoding="ascii") as f:
            f.write("# %dx%d\n" % (w, h))
            for y in range(h):
                for x in range(w):
                    r, g, b, a = rgba.getpixel((x, y))
                    f.write("%02X%02X%02X %d\n" % (r, g, b, 255 if a >= 128 else 0))


def save(im, name, **kw):
    path = os.path.join(OUT, name + ".gif")
    im.save(path, format="GIF", **kw)
    write_reference(os.path.join(OUT, name + ".txt"), path)
    return path


def hand_built_interlaced(path):
    """
    An interlaced GIF written byte by byte.

    Pillow's `interlace=True` does NOT set the interlace bit for GIF - the
    first version of this fixture relied on it, and the file that came out had
    packed=0x00, so the "interlaced" case was testing nothing at all.  The
    teeth check is what caught it: making the decoder ignore interlacing
    changed no result.

    Interlacing is two things: the bit, and the order the scanlines are stored
    in.  For height 4 the four passes store real rows 0, 2, 1, 3.  The rows
    here are chosen so a decoder that reads them in file order produces a
    different picture, which is the only way this fixture can fail.

    The LZW is emitted as CLEAR, two literals, repeated.  Two literals add one
    table entry, so next_code never reaches 8 and the 3-bit code size is
    constant - a tiny encoder that is obviously correct rather than a real one
    that would need a table to be.
    """
    w = h = 4
    real_rows = [[0, 0, 0, 0],
                 [1, 1, 1, 1],
                 [0, 1, 0, 1],
                 [1, 0, 1, 0]]
    order = [0, 2, 1, 3]          # the four interlace passes for height 4

    out = bytearray(b"GIF89a")
    out += struct.pack("<HH", w, h)
    out += bytes([0x80])          # global colour table, 2 entries
    out += bytes([0x00, 0x00])    # background index, pixel aspect ratio
    out += bytes([0, 0, 0, 255, 255, 255])
    out += b"\x2c"                 # image descriptor
    out += struct.pack("<HHHH", 0, 0, w, h)
    out += bytes([0x40])          # <-- interlaced
    out += bytes([2])             # LZW minimum code size

    codes = []
    for r in order:
        for pair in (real_rows[r][0:2], real_rows[r][2:4]):
            codes.append(4)       # CLEAR, so the code size never grows
            codes.extend(pair)
    codes.append(5)               # EOI

    bits = []
    for c in codes:
        for k in range(3):
            bits.append((c >> k) & 1)
    data = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for k, bit in enumerate(bits[i:i + 8]):
            byte |= bit << k
        data.append(byte)

    out += bytes([len(data)]) + bytes(data) + b"\x00"
    out += b"\x3b"
    with open(path, "wb") as f:
        f.write(bytes(out))


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))

    made = []

    # 1. Plain GIF89a, global colour table, no transparency.
    im = _paint(_checker(4, 4, 4), 4, 4, 4)
    _palette(im, 4)
    made.append(save(im, "basic"))

    # 2. Interlaced.  GIF's four-pass row order is easy to get wrong by
    #    reading rows in file order, and a decoder that does writes a
    #    recognisably striped image, so this one fixture is worth its cost.
    hand_built_interlaced(os.path.join(OUT, "interlaced.gif"))
    write_reference(os.path.join(OUT, "interlaced.txt"),
                    os.path.join(OUT, "interlaced.gif"))
    made.append(os.path.join(OUT, "interlaced.gif"))

    # 3. One transparent index.  MIDP's Image.createImage has to end up with a
    #    pixel that is not opaque, or a sprite drawn over a background shows a
    #    box around itself.
    im = _paint(_checker(4, 4, 4), 4, 4, 4)
    _palette(im, 4)
    px = im.load()
    for y in range(4):
        px[0, y] = 1
    made.append(save(im, "transparent", transparency=1))

    # 4. The smallest legal GIF.
    im = _checker(1, 1, 1)
    _palette(im, 4)
    made.append(save(im, "tiny"))

    # 5. Animated.  Only the first frame is asserted on - which frame a MIDP
    #    Image should hold is a policy question, and reading the first is the
    #    one every handset did.
    frames = []
    for i in range(2):
        f = _paint(_checker(4, 4, 4), 4, 4, 4)
        _palette(f, 4)
        frames.append(f)
    frames[0].save(os.path.join(OUT, "anim.gif"), format="GIF",
                   save_all=True, append_images=frames[1:], duration=100)
    write_reference(os.path.join(OUT, "anim.txt"),
                    os.path.join(OUT, "anim.gif"))
    made.append(os.path.join(OUT, "anim.gif"))

    # 6. A GIF cut off mid-image.  The decoder must refuse it with a message;
    #    the failure mode this guards against is reading past the end and
    #    returning a half-filled image, which looks like a torn sprite.
    whole = open(os.path.join(OUT, "basic.gif"), "rb").read()
    with open(os.path.join(OUT, "truncated.gif"), "wb") as f:
        f.write(whole[:len(whole) // 2])

    # 7. Not a GIF at all: a PNG header.  A decoder that dispatches on the
    #    extension rather than the magic accepts this and then reads garbage.
    with open(os.path.join(OUT, "notagif.gif"), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 40)

    for m in made:
        print(m)
    print("%d GIF fixtures + 2 negative" % len(made))
    return 0


if __name__ == "__main__":
    sys.exit(main())
