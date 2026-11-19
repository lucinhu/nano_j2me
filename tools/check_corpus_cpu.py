#!/usr/bin/env python3
"""Scan the corpus for CPU-heavy titles.

Runs every title the compat sweep recorded as ``ok`` for a fixed number of
frames with the frame cap disabled and measures the child's user+system CPU
through ``os.wait4``.  The report is sorted by total CPU so outliers can be
inspected with a profiler.

This does not judge compatibility (see ``tools/sweep_compat.py``); it answers
"where does the runtime actually spend its time" without trusting wall clock:
titles that pace themselves on ``System.currentTimeMillis``/``Thread.yield``
show up as high CPU at 30 fps even though their frame time looks normal.

Usage:
    NJ_BIN=build/nj2me python3 tools/check_corpus_cpu.py ROWS_DIR \
        [--count N] [--frames 300] [--heap 32] [--jobs 6] [--out cpu.csv]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = os.path.join(ROOT, "tests", "games")
PAT = {
    "frames": re.compile(r"frames=(\d+)"),
    "paints": re.compile(r"paints=(\d+)"),
    "gfx": re.compile(r"gfx=(\d+)"),
    "bytecodes": re.compile(r"bytecodes=(\d+)"),
    "tris": re.compile(r"tris=(\d+)"),
    "m3g": re.compile(r"m3g=(\d+)"),
}


def stats_from(text):
    line = ""
    for candidate in text.splitlines():
        if "stats:" in candidate:
            line = candidate
    return {name: int(m.group(1)) if (m := regex.search(line)) else 0
            for name, regex in PAT.items()}


def run_one(binary, row, frames, heap, timeout):
    jar = os.path.join(GAMES, row["file"])
    if not os.path.exists(jar):
        return None
    rms = tempfile.mkdtemp(prefix="nj-cpu-rms-")
    log = tempfile.TemporaryFile(mode="w+")
    try:
        proc = subprocess.Popen(
            [binary, "--port", "headless", "--size", "240x320",
             "--heap", str(heap), "--fps", "0", "--run-frames", str(frames),
             "--stats", "--rms-dir", rms, jar],
            stdout=log, stderr=subprocess.STDOUT)
    except OSError as error:
        print(f"cannot start {binary}: {error}", file=sys.stderr)
        return None
    started = time.time()
    cpu = 0.0
    status = 0
    while True:
        pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
        cpu = usage.ru_utime + usage.ru_stime
        if pid:
            break
        if time.time() - started > timeout:
            proc.kill()
            os.wait4(proc.pid, 0)
            status = -9
            break
        time.sleep(0.02)
    log.seek(0)
    stats = stats_from(log.read())
    log.close()
    return dict(file=row["file"], cpu=cpu, status=status, **stats)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rows_dir")
    parser.add_argument("--count", type=int, default=0, help="sample size (0 = all ok rows)")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--heap", type=int, default=32)
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    binary = os.environ.get("NJ_BIN") or os.path.join(ROOT, "build", "nj2me")
    if not os.path.exists(binary):
        sys.exit(f"NJ_BIN not found: {binary}")
    rows = [json.loads(line) for line in open(os.path.join(args.rows_dir, "rows.jsonl"))]
    rows = [r for r in rows if r.get("verdict") == "ok"]
    if args.count:
        rows = rows[:args.count]

    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(lambda r: run_one(binary, r, args.frames, args.heap,
                                                 args.timeout), rows):
            if result:
                results.append(result)
    results.sort(key=lambda r: -r["cpu"])

    if args.out:
        with open(args.out, "w") as out:
            out.write("file,cpu,frames,paints,gfx,bytecodes,tris,m3g,status\n")
            for result in results:
                out.write(",".join([result["file"], f'{result["cpu"]:.3f}',
                                    *(str(result[name]) for name in
                                      ("frames", "paints", "gfx", "bytecodes",
                                       "tris", "m3g", "status"))]) + "\n")

    print(f"{len(results)} titles scanned ({args.frames} frames, heap {args.heap} MB)")
    for result in results[:15]:
        per_frame = result["cpu"] / max(result["frames"], 1) * 1000.0
        print(f'  {result["cpu"]:7.3f} s cpu  {per_frame:6.2f} ms/frame  '
              f'paints={result["paints"]:4d} gfx={result["gfx"]:8d} '
              f'm3g={result["m3g"]:7d}  {result["file"]}')
    over = [r for r in results if r["cpu"] > 1.0]
    print(f"{len(over)} titles used more than 1 s of CPU")


if __name__ == "__main__":
    main()
