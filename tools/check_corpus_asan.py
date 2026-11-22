#!/usr/bin/env python3
"""Run every corpus title under an ASan+UBSan build and report real findings.

The runtime has fuzzers for malformed containers, but ordinary titles exercise
code paths no mutation reaches: a truncated archive that still loads, a sprite
sheet with odd dimensions, a handset API used once in a startup branch.  This
campaign executes the same rows the compatibility sweep measured, with the
sanitizer build of the CLI, and flags only genuine sanitizer reports
(AddressSanitizer, UndefinedBehaviorSanitizer `runtime error:`).

Usage: tools/check_corpus_asan.py [rows directory] [options]

The rows directory is a sweep output (rows.jsonl); the default is the newest
`.local-tools/debug-runs/compat-*` directory.  `--bin` is required and should
point at a binary built with -DNJ_SANITIZE=ON (CMake option).  Exit status is 1
when a title reports a sanitizer finding, so the campaign can gate a release
branch even though it is too slow for the default test suite.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_KEYS = os.path.join(ROOT, "tools", "corpus_keys", "generic.txt")


def default_rows():
    dirs = sorted(glob.glob(os.path.join(ROOT, ".local-tools", "debug-runs",
                                         "compat-*")))
    for d in reversed(dirs):
        if os.path.isfile(os.path.join(d, "rows.jsonl")):
            return d
    return None


def one(args, idx, row, out_dir):
    jar = row.get("jar_path") or os.path.join(ROOT, "tests", "games",
                                              row["file"])
    if not os.path.exists(jar):
        return None
    log = os.path.join(out_dir, "%04d.log" % idx)
    cmd = [args.bin, "--port", "headless",
           "--size", row.get("screen") or "240x320",
           "--heap", str(row.get("heap") or 16),
           "--fps", str(args.fps), "--run-frames", str(args.frames),
           "--rms-dir", os.path.join(out_dir, "rms", str(idx))]
    if row.get("device"):
        cmd += ["--device", row["device"]]
    if args.keys and os.path.exists(args.keys):
        cmd += ["--keys", "@" + args.keys]
    cmd.append(jar)
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = (
        "detect_leaks=0:abort_on_error=0:handle_segv=1:symbolize=1:"
        "log_path=" + os.path.join(out_dir, "asan"))
    env["UBSAN_OPTIONS"] = (
        "print_stacktrace=1:halt_on_error=0:symbolize=1:"
        "log_path=" + os.path.join(out_dir, "ubsan"))
    try:
        p = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                           stderr=open(log, "w"), timeout=args.timeout,
                           env=env)
    except subprocess.TimeoutExpired:
        open(log, "a").write("TIMEOUT\n")
        return None
    txt = open(log, errors="replace").read()
    bad = []
    if "AddressSanitizer" in txt:
        bad.append("asan")
    if "runtime error:" in txt:
        bad.append("ubsan")
    if not bad:
        return None
    return {"file": row["file"], "rc": p.returncode, "bad": bad, "log": log}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rows", nargs="?", default=None,
                    help="sweep output directory holding rows.jsonl")
    ap.add_argument("--bin", required=True,
                    help="CLI built with -DNJ_SANITIZE=ON")
    ap.add_argument("--out", default=os.path.join(ROOT, ".local-tools",
                                                  "debug-runs", "asan"),
                    help="directory for per-title logs (default: %(default)s)")
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--keys", default=DEFAULT_KEYS,
                    help="input replay script (empty string disables)")
    args = ap.parse_args()

    rows_dir = args.rows or default_rows()
    if not rows_dir:
        print("check_corpus_asan: no sweep rows directory found")
        return 2
    rows = [json.loads(l) for l
            in open(os.path.join(rows_dir, "rows.jsonl"), encoding="utf-8")]
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(os.path.join(args.out, "rms"), exist_ok=True)
    print("check_corpus_asan: %d titles from %s" % (len(rows), rows_dir),
          flush=True)
    hits = []
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(one, args, i, r, args.out)
                for i, r in enumerate(rows)]
        for n, f in enumerate(as_completed(futs), 1):
            v = f.result()
            if v:
                hits.append(v)
                print("HIT %s %s" % (v["file"], ",".join(v["bad"])),
                      flush=True)
            if n % 100 == 0:
                print("done %d/%d" % (n, len(rows)), flush=True)
    if hits:
        print("check_corpus_asan: %d title(s) with sanitizer findings"
              % len(hits))
        return 1
    print("check_corpus_asan: clean (%d titles)" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
