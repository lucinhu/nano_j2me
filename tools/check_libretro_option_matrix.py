#!/usr/bin/env python3
"""Run real corpus titles through the mock libretro frontend under non-default
core options, to catch crashes and blank frames that only appear when a user
changes a core option.

Each title is loaded and run under every profile below. A profile passes when
the mock frontend exits 0 and all of its conformance checks pass (the harness
always checks save-state round trips, video callbacks and input polling; this
script adds `--visual-required --static-ok` so a blank screen is visible).

Profiles that legitimately change the reported geometry supply the matching
harness expectation: `nano_j2me_screen` changes the base size directly, and
`nano_j2me_rotation=90` presents a rotated 320x240 picture through SET_ROTATION.

Usage:
  tools/check_libretro_option_matrix.py <rows dir> [--count 8] [--seed 5]
                                        [--frames 240] [--jobs 6] [--core PATH]
                                        [--harness PATH]

Environment: NJ_ROOT, NJ_LIBRETRO_HARNESS, NJ_LIBRETRO_CORE, NJ_JOBS.
The harness is `tests/test_libretro_frontend.c` built as
`test_libretro_frontend` (cmake target of the same name).
"""
import argparse
import json
import os
import random
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(os.environ.get("NJ_ROOT", Path(__file__).resolve().parent.parent))
HARNESS = Path(os.environ.get("NJ_LIBRETRO_HARNESS", ROOT / "build" / "test_libretro_frontend"))
CORE = Path(os.environ.get(
    "NJ_LIBRETRO_CORE",
    ROOT / "build" / "libretro-dist" / "linux-x86_64" / "nano_j2me_libretro.so"))

PROFILES = [
    ("default", [], []),
    ("screen176", ["nano_j2me_screen=176x208"], ["--expect-screen", "176x208"]),
    ("screen320", ["nano_j2me_screen=320x240"], ["--expect-screen", "320x240"]),
    ("rot90", ["nano_j2me_rotation=90"], ["--rotation", "90"]),
    ("rot180", ["nano_j2me_rotation=180"], []),
    ("heap32", ["nano_j2me_heap=32"], []),
    ("font-builtin", ["nano_j2me_font=builtin"], []),
    ("sound-off", ["nano_j2me_sound=disabled"], []),
    ("pointer-off", ["nano_j2me_pointer=disabled"], []),
    ("device-nokia6230", ["nano_j2me_device=nokia6230"], []),
    ("locale-zh", ["nano_j2me_locale=zh-CN"], []),
]


def load_rows(rows_dir):
    rows = []
    with open(rows_dir / "rows.jsonl", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("verdict") != "ok" or int(row.get("paints") or 0) <= 0:
                continue
            for key in ("path", "jar", "file"):
                if row.get(key):
                    row["path"] = row[key]
                    break
            if "path" not in row:
                continue
            rows.append(row)
    return rows


def find_jar(row):
    jar = Path(row["path"])
    if jar.is_absolute() or jar.exists():
        return jar if jar.exists() else None
    cand = ROOT / "tests" / "games" / jar.name
    return cand if cand.exists() else None


def run(jar, sets, extra, frames, pace, timeout):
    cmd = [str(HARNESS), "--core", str(CORE), "--jar", str(jar),
           "--frames", str(frames), "--pace-us", str(pace),
           "--visual-required", "--static-ok", "--min-nonblack", "8",
           "--save-dir", tempfile.mkdtemp(prefix="lj-opt-")]
    cmd += extra
    for item in sets:
        cmd += ["--set", item]
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=timeout, cwd=str(ROOT))
    except subprocess.TimeoutExpired:
        return -1, ["TIMEOUT"], ""
    log = cp.stdout + cp.stderr
    fails = [line.strip() for line in log.splitlines()
             if line.strip().startswith("FAIL")]
    return cp.returncode, fails, log


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rows_dir")
    ap.add_argument("--count", type=int, default=8)
    ap.add_argument("--seed", type=int, default=5)
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--pace-us", type=int, default=16667)
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("NJ_JOBS", "6")))
    args = ap.parse_args()

    rows = load_rows(Path(args.rows_dir))
    random.Random(args.seed).shuffle(rows)
    rows = rows[: args.count]
    tasks = []
    for row in rows:
        jar = find_jar(row)
        if jar is None:
            print("SKIP missing jar for", row.get("name"), flush=True)
            continue
        for name, sets, extra in PROFILES:
            tasks.append((row, jar, name, sets, extra))

    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {}
        for row, jar, name, sets, extra in tasks:
            futs[pool.submit(run, jar, sets, extra, args.frames, args.pace_us,
                             args.timeout)] = (row, jar, name)
        for fut in futs:
            row, jar, name = futs[fut]
            rc, fails, _log = fut.result()
            ok = rc == 0 and not fails
            results.append((row, name, ok, rc, fails))
            print("%-30s %-20s %s rc=%-3d %s"
                  % ((row.get("name") or jar.name)[:30], name,
                     "PASS" if ok else "FAIL", rc,
                     "; ".join(fails[:2])[:120]), flush=True)

    bad = [r for r in results if not r[2]]
    print("\n%d runs: %d passed, %d failed"
          % (len(results), len(results) - len(bad), len(bad)), flush=True)
    for row, name, _ok, rc, fails in bad:
        print("FAIL", row.get("name"), name, "rc=%s" % rc, fails[:3])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
