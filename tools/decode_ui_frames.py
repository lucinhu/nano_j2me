#!/usr/bin/env python3
"""Decode the ESP-emulator UI demo's RLE framebuffer dumps into images.

The demo firmware (tests/embedded/idf/main/ui_demo.c) cannot show a panel -
esp-emu models no LCD controller - so after it fills its real 240x320 RGB565
framebuffer it prints it over UART0 as

    FRAME:START:<width>:<height>
    R:<run-length-in-hex>:<rgb565-in-hex>
    ...
    FRAME:END

one line per run.  This script reads an esp-emu UART log, expands the runs,
writes each frame as PNG (or PPM when Pillow is not installed) and prints the
statistics the verification script asserts on.

Usage: tools/decode_ui_frames.py <uart.log> <output-prefix>

Exit status: 0 on success, 1 when the log holds no usable frames.
"""
import hashlib
import pathlib
import re
import sys

FRAME_START = re.compile(r"FRAME:START:(\d+):(\d+)$")
RUN = re.compile(r"R:([0-9a-fA-F]+):([0-9a-fA-F]{4})$")


def rgb565(value):
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def parse(log_path):
    frames = []
    width = height = 0
    runs = None
    for raw in pathlib.Path(log_path).read_text(errors="replace").splitlines():
        line = raw.strip()
        start = FRAME_START.match(line)
        if start:
            width, height = int(start.group(1)), int(start.group(2))
            runs = []
            continue
        if runs is None:
            continue
        if line == "FRAME:END":
            frames.append((width, height, runs))
            runs = None
            continue
        match = RUN.match(line)
        if match:
            runs.append((int(match.group(1), 16), int(match.group(2), 16)))
    return frames


def write_image(prefix, index, width, height, pixels):
    try:
        from PIL import Image  # type: ignore
    except ImportError:
        out = pathlib.Path(prefix + "%d.ppm" % index)
        with open(out, "wb") as handle:
            handle.write(b"P6\n%d %d\n255\n" % (width, height))
            for r, g, b in pixels:
                handle.write(bytes((r, g, b)))
        return out
    image = Image.new("RGB", (width, height))
    image.putdata(pixels)
    out = pathlib.Path(prefix + "%d.png" % index)
    image.save(out)
    return out


def main(argv):
    if len(argv) != 3:
        print("usage: decode_ui_frames.py <uart.log> <output-prefix>",
              file=sys.stderr)
        return 2
    log_path, prefix = argv[1], argv[2]
    frames = parse(log_path)
    print("frames decoded: %d" % len(frames))
    for index, (width, height, runs) in enumerate(frames):
        pixels = []
        colors = {}
        for count, value in runs:
            colors[value] = colors.get(value, 0) + count
            pixels.extend([rgb565(value)] * count)
        nonblack = sum(count for value, count in colors.items() if value != 0)
        digest = hashlib.sha256(str(runs).encode()).hexdigest()[:16]
        top = sorted(colors.items(), key=lambda item: -item[1])[:4]
        print("  frame %d: %dx%d runs=%d distinct_colors=%d nonblack=%d/%d "
              "hash=%s" % (index, width, height, len(runs), len(colors),
                           nonblack, width * height, digest))
        print("    top colors: " +
              ", ".join("0x%04xx%d" % (value, count) for value, count in top))
        if len(pixels) != width * height:
            print("  frame %d: BAD pixel count %d != %d"
                  % (index, len(pixels), width * height))
            return 1
        out = write_image(prefix, index, width, height, pixels)
        print("    wrote %s" % out)

    if len(frames) == 2:
        same = frames[0][2] == frames[1][2]
        print("frames differ: %s" % ("NO" if same else "YES"))
    return 0 if frames else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
