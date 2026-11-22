#!/usr/bin/env python3
"""Find corpus titles whose behaviour depends on the configured heap size.

The sweep runs every title once at a fixed heap (16 MB) and only retries at
32 MB when the *runtime* reports an uncaught OutOfMemoryError.  A title that
catches the OutOfMemoryError itself and paints an error screen instead keeps
verdict "ok" - the sweep sees frames and paints - so it never gets the bigger
heap.  Those titles look broken to a user on the default 16 MB CLI heap while
they work fine with a larger `--heap`.

This probe samples `ok` rows and runs each twice (low and high heap) with the
same frame/key recipe, then compares the two runs.  A title is flagged
`heap-sensitive` when the low-heap run burns at least `--ratio` times more
bytecodes than the high-heap run, which is what a game-side "out of memory"
screen plus a busy wait looks like from the outside.  The two stats lines are
printed so a manual check can confirm.

Usage:
    tools/check_corpus_heap.py <rows dir> [--count N] [--frames N] [--seed N]
        [--heap-low 16] [--heap-high 32] [--ratio 8] [--min-frames 240]

Set NJ_BIN to point at a binary outside the tree (default build/nj2me).
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from corpus_keymap import screen as resolve_screen  # noqa: E402
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms",
          "/tmp/vault2", "/tmp/vault3"]

STATS_RE = re.compile(
    r"paints=(\d+).*?bytecodes=(\d+)")


def find(name):
    for root in SEARCH:
        p = os.path.join(root, name)
        if os.path.exists(p):
            return p
    return None


def run(path, heap, frames, keys):
    size = resolve_screen(os.path.basename(str(path)), "240x320")
    cmd = [BIN, "--port", "headless", "--size", size,
           "--heap", str(heap), "--fps", "30",
           "--run-frames", str(frames), "--stats",
           "--keys", "@" + keys, path]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=300)
    except subprocess.TimeoutExpired:
        return {"rc": 124, "bytecodes": 0, "paints": 0, "line": "timeout"}
    out = (p.stdout + p.stderr).decode("utf-8", "replace")
    line = ""
    for ln in out.splitlines():
        if "bytecodes=" in ln:
            line = ln.strip()
    m = STATS_RE.search(line)
    return {"rc": p.returncode,
            "bytecodes": int(m.group(2)) if m else 0,
            "paints": int(m.group(1)) if m else 0,
            "line": line}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=12)
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--heap-low", type=int, default=16)
    ap.add_argument("--heap-high", type=int, default=32)
    ap.add_argument("--ratio", type=float, default=8.0)
    ap.add_argument("--min-frames", type=int, default=240)
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows
          if r.get("verdict") == "ok" and r.get("paints", 0) > 0
          and r.get("frames", 0) >= args.min_frames]
    random.Random(args.seed).shuffle(ok)
    keys = args.keys or os.path.join(args.rows, "keys.txt")

    seen = flagged = 0
    for row in ok:
        if seen >= args.count:
            break
        path = row.get("jar_path")
        if not path or not os.path.exists(path):
            path = find(os.path.basename(row["file"]))
        if path is None:
            continue
        seen += 1
        lo = run(path, args.heap_low, args.frames, keys)
        hi = run(path, args.heap_high, args.frames, keys)
        ratio = (lo["bytecodes"] / float(hi["bytecodes"])
                 if hi["bytecodes"] else 0.0)
        tag = ""
        if lo["rc"] == 124 or hi["rc"] == 124:
            tag = "timeout"
        elif ratio >= args.ratio:
            tag = "heap-sensitive"
            flagged += 1
        print("%-46s low_bc=%-12d high_bc=%-12d ratio=%8.1f %s"
              % (os.path.basename(path)[:46], lo["bytecodes"],
                 hi["bytecodes"], ratio, tag))
        if tag == "heap-sensitive":
            print("    low : %s" % lo["line"][:150])
            print("    high: %s" % hi["line"][:150])

    print("\n%d titles: %d heap-sensitive" % (seen, flagged))
    return 0


if __name__ == "__main__":
    sys.exit(main())
