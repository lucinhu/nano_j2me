#!/usr/bin/env python3
"""Render every corpus title the compat sweep marked `ok` through the mock
libretro frontend and require a non-black, animated picture.

This is the libretro-side twin of the CLI stall sweep: it proves that a title
that runs under the headless CLI also loads through the actual core, paints,
presents a non-black frame and survives the harness's save-state round trips.

A title that legitimately paints one static screen passes because of
`--static-ok`; only a completely black frame (or a failed check) fails.

Some titles finish loading their first Displayable only well after two
seconds (the sweep's default 300 frames at 60 Hz covers them; six corpus
titles were falsely reported black at 120 frames), so default to 300.

Usage:
  tools/check_libretro_corpus_visual.py <rows dir> [--frames 300] [--jobs 8]
                                        [--timeout 180] [--core PATH]
                                        [--harness PATH] [--out results.jsonl]

Environment: NJ_ROOT, NJ_LIBRETRO_HARNESS, NJ_LIBRETRO_CORE, NJ_JOBS.
Exit code is 1 when any title fails.
"""
import argparse
import json
import os
import random
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(os.environ.get("NJ_ROOT", Path(__file__).resolve().parent.parent))
HARNESS = Path(os.environ.get("NJ_LIBRETRO_HARNESS",
                              ROOT / "build" / "test_libretro_frontend"))
CORE = Path(os.environ.get(
    "NJ_LIBRETRO_CORE",
    ROOT / "build" / "libretro-dist" / "linux-x86_64" / "nano_j2me_libretro.so"))
GAMES = ROOT / "tests" / "games"

PRINT_LOCK = threading.Lock()


def load_rows(rows_dir):
    rows = []
    with open(Path(rows_dir) / "rows.jsonl", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("verdict") != "ok" or int(row.get("paints") or 0) <= 0:
                continue
            jar = None
            for key in ("path", "jar", "file"):
                value = row.get(key)
                if not value:
                    continue
                p = Path(value)
                if p.is_absolute() and p.exists():
                    jar = p
                    break
                cand = GAMES / p.name
                if cand.exists():
                    jar = cand
                    break
            if jar is None:
                continue
            row["jar"] = str(jar)
            rows.append(row)
    return rows


def run_one(row, frames, timeout):
    jar = Path(row["jar"])
    cmd = [str(HARNESS), "--core", str(CORE), "--jar", str(jar),
           "--frames", str(frames), "--pace-us", "16667",
           "--visual-required", "--static-ok", "--min-nonblack", "8",
           "--save-dir", tempfile.mkdtemp(prefix="lj-vis-")]
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=timeout, cwd=str(ROOT))
        rc = cp.returncode
        log = cp.stdout + cp.stderr
    except subprocess.TimeoutExpired:
        rc = -1
        log = "TIMEOUT"
    fails = [line.strip() for line in log.splitlines()
             if line.strip().startswith("FAIL")]
    ok = rc == 0 and not fails
    return {"title": jar.name, "ok": ok, "rc": rc, "fails": fails[:3],
            "log": log[-1200:] if not ok else ""}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rows_dir")
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("NJ_JOBS", "8")))
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    rows = load_rows(args.rows_dir)
    if not rows:
        print("no ok rows found in", args.rows_dir, file=sys.stderr)
        return 2
    random.Random(0).shuffle(rows)  # keep the order stable but not alphabetical
    out_path = Path(args.out) if args.out else \
        ROOT / ".local-tools" / "debug-runs" / "goal16-libretro-corpus" / "visual.jsonl"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    passed = failed = 0
    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, row, args.frames, args.timeout): row
                for row in rows}
        for fut in futs:
            row = futs[fut]
            res = fut.result()
            results.append(res)
            if res["ok"]:
                passed += 1
                with PRINT_LOCK:
                    print("PASS %-40s" % res["title"], flush=True)
            else:
                failed += 1
                with PRINT_LOCK:
                    print("FAIL %-40s rc=%-3d %s" % (
                        res["title"], res["rc"],
                        "; ".join(res["fails"])[:140]), flush=True)

    with open(out_path, "w", encoding="utf-8") as fh:
        for res in results:
            fh.write(json.dumps(res, ensure_ascii=False) + "\n")
    print("\n%d titles: %d passed, %d failed"
          % (len(results), passed, failed))
    print("results:", out_path)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
