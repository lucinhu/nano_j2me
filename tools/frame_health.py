#!/usr/bin/env python3
"""Measure how a run's frames move: colours, change, and two-frame flicker.

    tools/frame_health.py <dump-dir> [frames]

Reads the PPMs a run wrote with --dump-frames/--dump-every 1 and prints one
line:

    frames=900 colours=211 step=812 two=7164px worst=308 verdict=flicker

`step` is the median number of pixels that differ from the frame before, and
`two` counts pixels that flip back and forth over the whole run - equal two
frames apart, different from the frame between.  That is what a title looks
like when the runtime presents two different pictures on alternate frames,
which the corpus sweep cannot see: it samples every fortieth present.

The total is what matters, not the per-frame share: 鲁宾逊漂流记汉化版_702 tore
three block-sized holes in 900 frames (7164 pixels in total) against the
runtime before 08c7c26, and the same run against the fixed one tore none.
"""

import glob
import os
import sys

import numpy as np
from PIL import Image

FLICKER_PIXELS = 1000   # total alternating pixels in a run before it is a flicker
FLICKER_BLOCK = 200     # ... or one frame where this many pixels flip at once


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    paths = sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm")))
    want = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    if len(paths) < 3:
        print("frames=%d (too few to judge)" % len(paths))
        return 0

    prev2 = prev = None
    two = worst = 0
    steps = []
    for path in paths:
        cur = np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)
        if prev is not None:
            steps.append(int(np.count_nonzero(np.any(cur != prev, axis=2))))
        if prev is not None and prev2 is not None:
            n = int(np.count_nonzero(np.all(cur == prev2, axis=2) & np.any(cur != prev, axis=2)))
            two += n
            worst = max(worst, n)
        prev2, prev = prev, cur

    seen = set()
    for path in paths[:: max(1, len(paths) // 60)]:
        for row in np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8).reshape(-1, 3):
            seen.add(row.tobytes())
    step = int(np.median(steps)) if steps else 0
    verdict = "ok"
    if two >= FLICKER_PIXELS or worst >= FLICKER_BLOCK:
        verdict = "flicker"
    elif step == 0:
        verdict = "static"
    elif len(seen) <= 1:
        verdict = "flat"
    line = "frames=%d colours=%d step=%d two=%dpx worst=%d verdict=%s" % (
        len(paths), len(seen), step, two, worst, verdict)
    if want:
        line += " wanted=%d" % want
    print(line)
    return 1 if verdict == "flicker" else 0


if __name__ == "__main__":
    sys.exit(main())
