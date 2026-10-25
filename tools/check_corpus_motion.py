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
the sample is deterministic for a given seed.  Output is one line per title plus
a summary, and nothing is written anywhere.
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
SEARCH = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms",
          "/tmp/vault2", "/tmp/vault3"]


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
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows
          if r.get("verdict") == "ok" and r.get("paints", 0) > 0
          and r.get("frames", 0) >= args.frames * 2 // 3]
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
                [os.path.join(ROOT, "build", "nj2me"), "--port", "headless",
                 "--size", "240x320", "--heap", "16", "--fps", "30",
                 "--run-frames", str(args.frames), "--dump-frames", out,
                 "--dump-every", str(args.frames // 3),
                 "--keys", "@" + (args.keys or os.path.join(args.rows, "keys.txt")),
                 "--stats", path],
                capture_output=True, timeout=180)
            digests = set()
            for name in sorted(os.listdir(out)):
                if name.endswith(".ppm"):
                    digests.add(hashlib.md5(
                        open(os.path.join(out, name), "rb").read()).hexdigest())
        unique = len(digests)
        if unique > 1:
            moving += 1
        else:
            static += 1
        print("%-52s unique_frames=%d" % (os.path.basename(path)[:52], unique))

    print("\n%d titles: %d change frames, %d paint one frame" % (seen, moving, static))
    return 0


if __name__ == "__main__":
    sys.exit(main())
