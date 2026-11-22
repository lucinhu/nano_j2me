#!/usr/bin/env python3
"""Exercise libretro manual savestates on a sample of real corpus titles.

The mock frontend already performs one serialize/unserialize round trip during
every run when the core reports savestate support.  This tool turns that into a
corpus-level probe: it samples `ok` rows from a sweep, loads each JAR through
the real core, and requires the serialize assertions to pass:

    serialize_size_stable, serialize_writes_state, unserialize_restores_state

It also reports the reported serialize size so the cost of the VM-heap-backed
snapshot is visible per title.  The harness is built from
tests/test_libretro_frontend.c; `--harness` or NJ_LIBRETRO_HARNESS points at it,
and `--core` at a libretro core built from this tree.

Frames are paced at 60 Hz by default (`--pace-us 16666`) because that is how a
frontend drives the core.  An unpaced probe can deliver the whole frame budget
before an asynchronous MIDlet construction finishes, and a state requested
from that half-loaded VM is not a state a real frontend would ever take.

Usage:
    tools/check_libretro_states.py <rows dir> [--count N] [--frames N] [--seed N]
                                   [--pace-us N] [--harness PATH] [--core PATH]
                                   [--jobs N]

Rows come from tools/sweep_compat.py; the sample is deterministic for a seed.
Only rows classified as ok with a painted frame are sampled.  The tool writes
to a temporary directory per title and prints one line per title plus a
summary; it exits non-zero if any title fails.
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed


ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
SEARCH = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms",
          "/tmp/vault2", "/tmp/vault3"]

REQUIRED = (
    "serialize_size_stable",
    "serialize_writes_state",
    "unserialize_restores_state",
)
SIZE_RE = re.compile(r"serialize_size=(\d+)")


def find(basename):
    for base in SEARCH:
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            if basename in files:
                return os.path.join(dirpath, basename)
    return None


def jar_for(row):
    """Prefer the path the sweep recorded; fall back to the legacy search."""
    path = row.get("jar_path")
    if path and os.path.isfile(path):
        return path
    return find(os.path.basename(row["file"]))


def one(harness, core, row, frames, pace_us):
    """Run one title's serialize round trip; returns (name, ok, line)."""
    path = jar_for(row)
    if path is None:
        return None
    with tempfile.TemporaryDirectory() as save_dir:
        cmd = [harness, "--core", core, "--jar", path,
               "--frames", str(frames), "--save-dir", save_dir]
        if pace_us > 0:
            cmd += ["--pace-us", str(pace_us)]
        screen = row.get("screen")
        if screen:
            # The core option sets the canvas; --expect-screen makes the
            # harness's base-geometry check expect the same size (a 320x240
            # build measured on a 240x320 extent would otherwise fail it).
            cmd += ["--set", "nano_j2me_screen=" + screen,
                    "--expect-screen", screen]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=300)
        except subprocess.TimeoutExpired:
            return (row["file"], False,
                    "%-52s FAIL timeout" % row["file"][:52])
        out = p.stdout + p.stderr
        missing = [k for k in REQUIRED if ("PASS " + k) not in out]
        size = SIZE_RE.search(out)
        if p.returncode != 0 or missing:
            reason = "rc=%d missing=%s" % (p.returncode,
                                           ",".join(missing) or "none")
            return (row["file"], False,
                    "%-52s FAIL %s" % (row["file"][:52], reason))
        return (row["file"], True,
                "%-52s PASS serialize_size=%s"
                % (row["file"][:52], size.group(1) if size else "?"))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--pace-us", type=int, default=16666,
                    help="frame pacing in microseconds (0 disables pacing)")
    ap.add_argument("--harness", default=os.environ.get("NJ_LIBRETRO_HARNESS"))
    ap.add_argument("--core", default=os.environ.get("NJ_LIBRETRO_CORE"))
    ap.add_argument("--jobs", type=int, default=1,
                    help="parallel titles (default 1; each runs one process)")
    args = ap.parse_args(argv)

    harness = args.harness or os.path.join(ROOT, "build",
                                           "test_libretro_frontend")
    if not os.path.isfile(harness):
        print("harness not found: %s" % harness, file=sys.stderr)
        return 2
    core = args.core or os.path.join(ROOT, "build", "libretro-dist",
                                     "linux-x86_64",
                                     "nano_j2me_libretro.so")
    if not os.path.isfile(core):
        print("core not found: %s" % core, file=sys.stderr)
        return 2

    # mmap hazard: copy the core first, so a concurrent rebuild of the shared
    # dist directory cannot fault a running probe.
    iso_dir = tempfile.mkdtemp(prefix="lj-core-")
    iso_core = os.path.join(iso_dir, os.path.basename(core))
    shutil.copy2(core, iso_core)
    os.chmod(iso_core, 0o500)
    core = iso_core

    rows_path = os.path.join(args.rows, "rows.jsonl")
    rows = []
    with open(rows_path) as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    ok = [r for r in rows
          if r.get("verdict") == "ok" and r.get("paints", 0) > 0
          and r.get("frames", 0) >= args.frames]
    random.Random(args.seed).shuffle(ok)

    seen = passed = 0
    fail = []
    todo = ok[:args.count]

    def handle(result):
        nonlocal seen, passed
        if result is None:
            return
        row_file, good, line = result
        seen += 1
        print(line, flush=True)
        if good:
            passed += 1
        else:
            fail.append((row_file, line.split("FAIL ", 1)[-1]))

    if args.jobs > 1:
        with ProcessPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(one, harness, core, row, args.frames,
                              args.pace_us) for row in todo]
            for f in as_completed(futs):
                handle(f.result())
    else:
        for row in todo:
            handle(one(harness, core, row, args.frames, args.pace_us))

    print("\n%d titles: %d state round trips, %d failed"
          % (seen, passed, len(fail)))
    return 0 if not fail else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
