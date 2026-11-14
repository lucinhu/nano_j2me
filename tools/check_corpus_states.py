#!/usr/bin/env python3
"""Probe corpus titles for manual save-state round trips.

The compatibility sweep only records whether a title starts and paints.  A
title that runs is not automatically a title whose snapshot can be restored:
the VM has to find every saved class, rebuild the threads inside their current
Java frames and keep going from exactly the saved instruction.  This tool
samples titles classified `ok` and, for each one:

  1. runs it headlessly and writes a manual state at a fixed frame
     (`--save-state <file> --save-state-frame N`);
  2. starts a fresh process that only loads that state (`--load-state <file>`)
     and runs it on for a few more frames.

A title passes when the first process exits cleanly, the state file is
non-empty, and the second process logs `resumed <class> from <file>` (not
`cannot resume from ...; starting the MIDlet instead`), exits cleanly and
still produces frames.  Liquid-RMS or files outside the snapshot are not
expected to be rewound; only the VM state is checked.

Usage:
    tools/check_corpus_states.py <rows dir> [--count N] [--seed N]
                                 [--frames N] [--state-frame N]

`rows dir` is a tools/sweep_compat.py output directory; only titles classified
`ok` are sampled.  The sample is deterministic for a given seed.  Temporary
state files go to a scratch directory and are removed; nothing else is
written.  The binary is taken from NJ_BIN, like the sweep and motion tools.
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games")]

FRAMES_RE = re.compile(r"frames=(\d+)")
RESUMED_RE = re.compile(r"resumed \S+ from ")


def find(name):
    for root in SEARCH + [d for d in
                          os.environ.get("NJ_SWEEP_DIRS", "").split(":") if d]:
        p = os.path.join(root, name)
        if os.path.exists(p):
            return p
    return None


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return 124, ""


def phases(binary, jar, frames, state_frame, state, keys, scratch):
    """Run one save pass and one restore pass; return (ok, detail)."""
    common = [binary, "--port", "headless", "--size", "240x320",
              "--heap", "16", "--fps", "30"]
    save = common + ["--run-frames", str(frames),
                     "--save-state", state,
                     "--save-state-frame", str(state_frame),
                     "--stats", jar]
    resume = common + ["--run-frames", str(frames - state_frame + 30),
                       "--load-state", state,
                       "--stats", jar]
    if keys:
        save[7:7] = ["--keys", "@" + keys]
        resume[7:7] = ["--keys", "@" + keys]

    rc1, out1 = run(save, 300)
    if rc1 != 0:
        return False, "save pass rc=%d" % rc1
    if not os.path.exists(state) or os.path.getsize(state) < 64:
        return False, "no state file"
    size = os.path.getsize(state)

    rc2, out2 = run(resume, 300)
    if rc2 != 0:
        return False, "restore rc=%d" % rc2
    if "cannot resume from " in out2:
        return False, "state refused"
    if not RESUMED_RE.search(out2):
        return False, "no resume log"
    m = FRAMES_RE.search(out2)
    if not m or int(m.group(1)) == 0:
        return False, "restored but no frames"
    return True, "state=%dB frames=%s" % (size, m.group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--seed", type=int, default=4)
    ap.add_argument("--frames", type=int, default=240,
                    help="frames before the state is written")
    ap.add_argument("--state-frame", type=int, default=200,
                    help="frame at which the state is requested")
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    args = ap.parse_args()
    if args.state_frame < 1 or args.state_frame >= args.frames:
        print("--state-frame must be >= 1 and < --frames", file=sys.stderr)
        return 2

    rows = [json.loads(line) for line in
            open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows
          if r.get("verdict") == "ok"
          and r.get("frames", 0) >= args.frames // 2
          and r.get("paints", 0) > 0]
    random.Random(args.seed).shuffle(ok)

    keys = args.keys
    if keys is None:
        keys = os.path.join(args.rows, "keys.txt")
    if not os.path.exists(keys):
        keys = None

    seen = passed = 0
    failures = []
    with tempfile.TemporaryDirectory(prefix="nj-states-") as scratch:
        for row in ok:
            if seen >= args.count:
                break
            path = find(os.path.basename(row["file"]))
            if path is None:
                continue
            seen += 1
            state = os.path.join(scratch, "state-%02d.bin" % seen)
            ok, detail = phases(BIN, path, args.frames, args.state_frame,
                                state, keys, scratch)
            mark = "pass" if ok else "FAIL"
            if ok:
                passed += 1
            else:
                failures.append((os.path.basename(path), detail))
            print("%-52s %-4s %s" % (os.path.basename(path)[:52], mark, detail))

    print("\n%d titles: %d state round trips, %d failed"
          % (seen, passed, len(failures)))
    for name, detail in failures:
        print("  failed: %s (%s)" % (name, detail))
    return 0 if passed == seen else 1


if __name__ == "__main__":
    sys.exit(main())
