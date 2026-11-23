#!/usr/bin/env python3
"""Report how many distinct frames a sample of corpus titles paints.

The sweep only records that a title painted *something*; a title can paint the
same pixels 900 times and still look frozen.  This tool dumps three frames per
title through the same recipe as tools/sweep_compat.py and counts how many of
them are byte-different, which separates "the frame is static because the title
is on its title screen" from "the emulator keeps painting a stale frame".

Usage:
    tools/check_corpus_motion.py <rows dir> [--count N] [--frames N] [--seed N]
                                [--jobs N]

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
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from corpus_keymap import screen as resolve_screen  # noqa: E402
# Same override as tools/sweep_compat.py: the corpus may run a binary built
# outside the tree (e.g. /var/tmp/nj2me-goal13-build/nj2me).
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms",
          "/tmp/vault2", "/tmp/vault3"]
# Same extra corpus directories as tools/sweep_compat.py (NJ_SWEEP_DIRS).
SEARCH += [d for d in os.environ.get("NJ_SWEEP_DIRS", "").split(os.pathsep)
           if d]


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


def jar_for(row):
    """Resolve a row's JAR, preferring the absolute path the sweep recorded.

    Fetched titles live in the persistent cache (or an ad-hoc directory), so a
    rows directory produced from them cannot be replayed by bare file name.
    """
    path = row.get("jar_path")
    if path and os.path.exists(path):
        return path
    return find(os.path.basename(row["file"]))


def _dump_every(frames):
    """Spacing between dumped frames.

    The dump schedule has to include the EARLY frames, not just the middle and
    the end: a title that animates its intro for two seconds and then settles
    on a static menu looks "frozen" if the only dumps are at 300/600/900
    (round 139: 33 of 250 fetched titles were flagged static at 900 frames
    even though 24 of them had animated at 300).  Keep dumps at 100-frame
    steps so the first-second behaviour is always sampled, and thin them out
    only when the run is long enough to produce more than a dozen files.
    """
    every = 100 if frames >= 300 else max(1, frames // 3)
    if frames // every > 12:
        every = max(1, frames // 12)
    return every


def probe(path, frames, keys, heap=16, device=None, timeout=180):
    """Run one title and return (unique_dumps, non_black_ratio).

    A timeout returns ``(-1, 0.0)`` so one hung title cannot abort the whole
    probe (round 224: TheMummy_K700F hung for 180 s and raised out of the
    ThreadPoolExecutor).
    """
    with tempfile.TemporaryDirectory() as out:
        size = resolve_screen(os.path.basename(str(path)), "240x320")
        cmd = [BIN, "--port", "headless",
               "--size", size, "--heap", str(heap), "--fps", "30",
               "--run-frames", str(frames), "--dump-frames", out,
               "--dump-every", str(_dump_every(frames)),
               "--keys", "@" + keys,
               "--stats", path]
        if device:
            cmd[cmd.index("--stats"):cmd.index("--stats")] = ["--device", device]
        try:
            subprocess.run(cmd, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return -1, 0.0
        digests = set()
        coverage = 0.0
        for name in sorted(os.listdir(out)):
            if name.endswith(".ppm"):
                raw = open(os.path.join(out, name), "rb").read()
                digests.add(hashlib.md5(raw).hexdigest())
                coverage = max(coverage, non_black_ratio(raw))
    return len(digests), coverage


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
    ap.add_argument("--jobs", type=int, default=1,
                    help="run this many titles in parallel (default 1)")
    ap.add_argument("--timeout", type=int, default=180,
                    help="seconds per title before it counts as a timeout "
                         "(default 180)")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows
          if r.get("verdict") == "ok" and r.get("paints", 0) > 0
          and r.get("frames", 0) >= args.frames * 2 // 3
          and (r.get("m3g") or 0) >= args.min_m3g]
    random.Random(args.seed).shuffle(ok)

    seen, moving, static = 0, 0, 0
    jobs = [r for r in ok if jar_for(r) is not None]
    jobs = jobs[:max(args.count, 0)]
    generic_keys = args.keys or os.path.join(args.rows, "keys.txt")
    # Honor the recipe the sweep had to use for a title: a device retry also
    # bumped the heap, and replaying with the generic profile would report a
    # false freeze (1945空战英雄 never paints a frame without --device).  The
    # same applies to input: a title the sweep had to run without keys (the
    # generic script made it exit) must be replayed without keys, and a
    # per-title script must win over the generic one.
    none_keys = os.path.join(args.rows, "keys-none.txt")

    def spec_for(row):
        keys = generic_keys
        if row.get("keys") == "none":
            if not os.path.exists(none_keys):
                with open(none_keys, "w", encoding="utf-8") as fh:
                    fh.write("# deliberately empty: run without input\n")
            keys = none_keys
        elif row.get("keys"):
            keys = os.path.join(ROOT, "tools", "corpus_keys", row["keys"])
        return (jar_for(row),
                int(row.get("heap") or 16), row.get("device"), keys)

    specs = [spec_for(r) for r in jobs]
    paths = [s[0] for s in specs]
    results = []
    if args.jobs <= 1:
        for spec in specs:
            results.append(probe(spec[0], args.frames, spec[3], spec[1],
                                 spec[2], args.timeout))
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(
                lambda spec: probe(spec[0], args.frames, spec[3], spec[1],
                                   spec[2], args.timeout),
                specs))
    timeouts = 0
    for path, (unique, coverage) in zip(paths, results):
        seen += 1
        if unique < 0:
            timeouts += 1
            print("%-52s timeout" % os.path.basename(path)[:52])
            continue
        if unique > 1:
            moving += 1
        else:
            static += 1
        black = "black" if coverage < 0.01 else ""
        print("%-52s unique_frames=%d non_black=%3.0f%% %s"
              % (os.path.basename(path)[:52], unique, coverage * 100.0, black))

    print("\n%d titles: %d change frames, %d paint one frame, %d timeout"
          % (seen, moving, static, timeouts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
