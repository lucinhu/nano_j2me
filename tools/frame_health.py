#!/usr/bin/env python3
"""Measure how a run's frames move: colours, change, and two-frame flicker.

    tools/frame_health.py <dump-dir> [frames]

Reads the PPMs a run wrote with --dump-frames/--dump-every 1 and prints one
line:

    frames=900 colours=211 step=812 two=7164px worst=4632 blob=4632 verdict=flicker

`step` is the median number of pixels that differ from the frame before, and
`two` counts pixels that flip back and forth over the whole run - equal two
frames apart, different from the frame between.  That is what a title looks
like when the runtime presents two different pictures on alternate frames,
which the corpus sweep cannot see: it samples every fortieth present.

Size alone is not enough: an animated logo produces thousands of such pixels
too, in thin streaks (`波斯王子之武者之心_8424`).  A tear is ONE connected
piece, so the verdict needs `blob`, the largest connected group in the worst
frame.  `鲁宾逊漂流记汉化版_702` against the runtime before 08c7c26 tore 4632
pixels in a single piece and the same run against the fixed runtime tore none.
"""

import glob
import os
import sys

import numpy as np
from PIL import Image

FLICKER_PIXELS = 1000   # total alternating pixels in a run before it is worth a look
FLICKER_BLOB = 500      # ... and this many in ONE connected piece to call it a tear


def largest_blob(mask):
    """The biggest 4-connected group of set pixels."""
    seen = np.zeros_like(mask, dtype=bool)
    h, w = mask.shape
    best = 0
    for y in range(h):
        for x in np.nonzero(mask[y] & ~seen[y])[0]:
            if seen[y, x]:
                continue
            stack = [(y, int(x))]
            seen[y, x] = True
            n = 0
            while stack:
                cy, cx = stack.pop()
                n += 1
                for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            best = max(best, n)
    return best


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
    worst_mask = None
    steps = []
    for path in paths:
        cur = np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)
        if prev is not None:
            steps.append(int(np.count_nonzero(np.any(cur != prev, axis=2))))
        if prev is not None and prev2 is not None:
            mask = np.all(cur == prev2, axis=2) & np.any(cur != prev, axis=2)
            n = int(np.count_nonzero(mask))
            two += n
            if n > worst:
                worst, worst_mask = n, mask
        prev2, prev = prev, cur

    seen = set()
    for path in paths[:: max(1, len(paths) // 60)]:
        for row in np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8).reshape(-1, 3):
            seen.add(row.tobytes())
    step = int(np.median(steps)) if steps else 0
    blob = largest_blob(worst_mask) if worst_mask is not None else 0
    verdict = "ok"
    if blob >= FLICKER_BLOB:
        verdict = "flicker"
    elif two >= FLICKER_PIXELS:
        verdict = "suspect"
    elif step == 0:
        verdict = "static"
    elif len(seen) <= 1:
        verdict = "flat"
    line = "frames=%d colours=%d step=%d two=%dpx worst=%d blob=%d verdict=%s" % (
        len(paths), len(seen), step, two, worst, blob, verdict)
    if want:
        line += " wanted=%d" % want
    print(line)
    return 1 if verdict in ("flicker", "suspect") else 0


if __name__ == "__main__":
    sys.exit(main())
