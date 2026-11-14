#!/usr/bin/env python3
"""Report how many distinct frames a sample of corpus titles paints.

The sweep only records that a title painted *something*; a title can paint the
same pixels 900 times and still look frozen.  This tool dumps three frames per
title through the same recipe as tools/sweep_compat.py and counts how many of
them are byte-different, which separates "the frame is static because the title
is on its title screen" from "the emulator keeps painting a stale frame".

Usage:
    tools/check_corpus_motion.py <rows dir> [--count N] [--frames N] [--seed N]

Titles are drawn from the rows the sweep already classifies as ok with paints;
the sample is deterministic for a given seed.  Each frame is also checked for
non-black coverage, so "painted something" that is actually a black screen is
visible in the output.  `--min-m3g N` restricts the sample to titles the sweep
saw render N or more M3G triangles, which is how the 3D pipeline gets sampled
separately.  Output is one line per title plus a summary, and nothing is
written anywhere.
"""

import argparse
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
# Same override as tools/sweep_compat.py: the corpus may run a binary built
# outside the tree (e.g. /var/tmp/nj2me-goal13-build/nj2me).
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms",
          "/tmp/vault2", "/tmp/vault3"]


def non_black_ratio(raw):
    """Fraction of pixels in a binary PPM that are not pure black."""
    if not raw.startswith(b"P6"):
        return 0.0
    pos, vals = 2, []
    while len(vals) < 3 and pos < len(raw):
        c = raw[pos:pos + 1]
        if c.isspace():
            pos += 1
            continue
        if c == b"#":
            while pos < len(raw) and raw[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(raw) and not raw[pos:pos + 1].isspace():
            pos += 1
        vals.append(int(raw[start:pos]))
    if len(vals) < 3:
        return 0.0
    w, h, _max = vals
    data = raw[pos + 1:pos + 1 + w * h * 3]
    if len(data) < w * h * 3:
        return 0.0
    lit = sum(1 for i in range(0, len(data), 3)
              if data[i] or data[i + 1] or data[i + 2])
    return lit / float(w * h)


def find(name):
    for root in SEARCH:
        p = os.path.join(root, name)
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--min-m3g", type=int, default=0,
                    help="only titles that rendered at least N M3G triangles "
                         "(3D sample)")
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows
          if r.get("verdict") == "ok" and r.get("paints", 0) > 0
          and r.get("frames", 0) >= args.frames * 2 // 3
          and (r.get("m3g") or 0) >= args.min_m3g]
    random.Random(args.seed).shuffle(ok)

    seen, moving, static = 0, 0, 0
    for row in ok:
        if seen >= args.count:
            break
        path = find(os.path.basename(row["file"]))
        if path is None:
            continue
        seen += 1
        with tempfile.TemporaryDirectory() as out:
            subprocess.run(
                [BIN, "--port", "headless",
                 "--size", "240x320", "--heap", "16", "--fps", "30",
                 "--run-frames", str(args.frames), "--dump-frames", out,
                 "--dump-every", str(args.frames // 3),
                 "--keys", "@" + (args.keys or os.path.join(args.rows, "keys.txt")),
                 "--stats", path],
                capture_output=True, timeout=180)
            digests = set()
            coverage = 0.0
            for name in sorted(os.listdir(out)):
                if name.endswith(".ppm"):
                    raw = open(os.path.join(out, name), "rb").read()
                    digests.add(hashlib.md5(raw).hexdigest())
                    coverage = max(coverage, non_black_ratio(raw))
        unique = len(digests)
        if unique > 1:
            moving += 1
        else:
            static += 1
        black = "black" if coverage < 0.01 else ""
        print("%-52s unique_frames=%d non_black=%3.0f%% %s"
              % (os.path.basename(path)[:52], unique, coverage * 100.0, black))

    print("\n%d titles: %d change frames, %d paint one frame" % (seen, moving, static))
    return 0


if __name__ == "__main__":
    sys.exit(main())
