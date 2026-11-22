#!/usr/bin/env python3
"""Deep-run corpus probe: run sweep-ok titles far beyond the sweep window and
report late crashes, hangs, early exits and new uncaught exceptions.

The sweep (`tools/sweep_compat.py`) runs every title for a fixed frame budget,
usually uncapped. That answers "does it start and draw", but a title can still
crash or hang minutes into gameplay. This probe samples titles that the sweep
already marked `ok`, runs each one paced at a real-time cadence for a much
longer frame count, and compares the result with the sweep row.

It reports deltas, it does not decide that a title is broken:

- FAIL: non-zero exit, timeout, or fewer than half the requested frames without
  the MIDlet requesting destruction.
- EARLY: the MIDlet called `notifyDestroyed()` before the frame budget; that is
  usually a key-script-driven menu exit and is reported separately.
- PASS: reached the requested frames.

Usage:
  tools/check_corpus_deep.py <rows dir> [--count 24] [--seed 31]
                             [--frames 1800] [--fps 30] [--heap 16]
                             [--timeout 240] [--jobs 8] [--keys PATH]
                             [--out results.json]

Environment: NJ_ROOT, NJ_BIN. The rows directory is the output directory of a
`tools/sweep_compat.py` run (it must contain `rows.jsonl`).
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(os.environ.get("NJ_ROOT", Path(__file__).resolve().parent.parent))
BIN = Path(os.environ.get("NJ_BIN", ROOT / "build" / "nj2me"))
UNCAUGHT = re.compile(r"uncaught exception in thread \d+:\s*([\w.$]+)")
THROWN = re.compile(r"thrown: ([\w.$]+)")
STATS = re.compile(r"^stats: frames=(\d+) paints=(\d+)")

ROW_PATH_KEYS = ("jar_path", "path", "jar", "file")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus_keymap import resolve as resolve_keys  # noqa: E402
from corpus_keymap import screen as resolve_screen  # noqa: E402


def corpus_dirs():
    """Search path for JARs: the repo fixtures plus NJ_SWEEP_DIRS entries."""
    dirs = [ROOT / "tests" / "games"]
    for d in os.environ.get("NJ_SWEEP_DIRS", "").split(os.pathsep):
        if d:
            dirs.append(Path(d))
    return dirs


def resolve_jar(row):
    """Absolute JAR path for a row, tolerating fetched/vault corpus layouts."""
    for key in ROW_PATH_KEYS:
        value = row.get(key)
        if not value:
            continue
        path = Path(value)
        if path.is_absolute() and path.exists():
            return path
        for directory in corpus_dirs():
            cand = directory / path.name
            if cand.exists():
                return cand
        if not path.is_absolute():
            cand = ROOT / path
            if cand.exists():
                return cand
    return None


def run_one(row, args, keys, work):
    jar = resolve_jar(row)
    if jar is None:
        return {
            "name": row.get("name") or row.get("file", "?"),
            "jar": "",
            "rc": -1,
            "timed_out": False,
            "wall_s": 0.0,
            "frames": 0,
            "paints": 0,
            "early_exit": False,
            "uncaught": [],
            "new_uncaught": 0,
            "thrown_kinds": [],
            "base_uncaught": 0,
            "fail": True,
            "note": "JAR not found (set NJ_SWEEP_DIRS)",
        }
    name = row.get("name") or jar.name
    size = resolve_screen(jar.name, "240x320")
    rms = work / ("rms-" + re.sub(r"[^A-Za-z0-9_.-]", "_", name))
    cmd = [
        str(BIN), "--port", "headless", "--size", size,
        "--heap", str(row.get("heap") or args.heap), "--fps", str(args.fps),
        "--run-frames", str(args.frames), "--stats",
        "--rms-dir", str(rms),
    ]
    # Replay the sweep's device profile too: a title that only starts with the
    # Nokia property set (1945空战英雄) fails the constructor at load without it.
    if row.get("device"):
        cmd[cmd.index("--stats"):cmd.index("--stats")] = [
            "--device", str(row["device"])]
    if row.get("keys") == "none":
        row_keys = None
    elif row.get("keys"):
        row_keys = ROOT / "tools" / "corpus_keys" / row["keys"]
    else:
        row_keys = resolve_keys(jar.name, keys)
    if row_keys:
        cmd += ["--keys", "@" + str(row_keys)]
    cmd.append(str(jar))
    t0 = time.time()
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=args.timeout, cwd=str(ROOT))
        out, err, rc = cp.stdout, cp.stderr, cp.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        def decode(blob):
            if isinstance(blob, bytes):
                return blob.decode("utf-8", "replace")
            return blob or ""
        out = decode(exc.stdout)
        err = decode(exc.stderr)
        rc, timed_out = -1, True
    dt = time.time() - t0
    log = out + "\n" + err
    frames = paints = 0
    for line in log.splitlines():
        m = STATS.match(line)
        if m:
            frames, paints = int(m.group(1)), int(m.group(2))
    uncaught = UNCAUGHT.findall(log)
    thrown = THROWN.findall(log)
    early = "the MIDlet called notifyDestroyed()" in log
    base_uncaught = int(row.get("uncaught") or 0)
    hard = (timed_out or rc != 0 or frames < args.frames // 2) and not early
    return {
        "name": name,
        "jar": str(jar),
        "rc": rc,
        "timed_out": timed_out,
        "wall_s": round(dt, 1),
        "frames": frames,
        "paints": paints,
        "early_exit": early,
        "uncaught": uncaught,
        "new_uncaught": max(0, len(uncaught) - base_uncaught),
        "thrown_kinds": sorted(set(thrown)),
        "base_uncaught": base_uncaught,
        "fail": bool(hard),
    }


def load_rows(rows_dir):
    rows = []
    with open(rows_dir / "rows.jsonl", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("verdict") != "ok":
                continue
            if int(row.get("paints") or 0) <= 0:
                continue
            if int(row.get("frames") or 0) < 100:
                continue
            for key in ROW_PATH_KEYS:
                if row.get(key):
                    row["path"] = row[key]
                    break
            if "path" not in row:
                continue
            row.setdefault("name", Path(row["path"]).name)
            rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rows_dir", help="sweep output directory containing rows.jsonl")
    ap.add_argument("--count", type=int, default=24)
    ap.add_argument("--seed", type=int, default=31)
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--heap", type=int, default=16)
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--keys", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    rows_dir = Path(args.rows_dir)
    rows = load_rows(rows_dir)
    random.Random(args.seed).shuffle(rows)
    rows = rows[: args.count]
    keys = Path(args.keys) if args.keys else (rows_dir / "keys.txt")
    if not keys.exists():
        keys = None

    print("running %d titles: %d frames at %d fps, heap %d MB, %d jobs%s"
          % (len(rows), args.frames, args.fps, args.heap, args.jobs,
             "" if keys else " (no keys file)"), flush=True)
    work = Path(tempfile.mkdtemp(prefix="njdeep-"))
    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(run_one, r, args, keys, work) for r in rows]
        for fut in as_completed(futs):
            res = fut.result()
            results.append(res)
            status = "FAIL" if res["fail"] else ("EARLY" if res["early_exit"] else "PASS")
            print("%-58s %s rc=%-3d frames=%-5d paints=%-5d uncaught=%-2d%s wall=%ss"
                  % (res["name"][:58], status, res["rc"], res["frames"], res["paints"],
                     len(res["uncaught"]),
                     (" new=%d" % res["new_uncaught"]) if res["new_uncaught"] else "",
                     res["wall_s"]), flush=True)
    results.sort(key=lambda r: r["name"])
    early = [r for r in results if r["early_exit"]]
    fails = [r for r in results if r["fail"]]
    print("\n%d titles: %d passed, %d early exit, %d failed"
          % (len(results), len(results) - len(early) - len(fails), len(early), len(fails)), flush=True)
    for r in fails:
        print("FAIL %s rc=%s timeout=%s frames=%s uncaught=%s"
              % (r["name"], r["rc"], r["timed_out"], r["frames"], r["uncaught"][:3]))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            json.dump(results, fh, ensure_ascii=False, indent=1)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
