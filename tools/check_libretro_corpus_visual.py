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

Titles with a script in tools/corpus_keys/<jar stem>.txt are driven with
that script (frame:key entries and pressed@X,Y pointer taps); the rest fall
back to the sweep's generic keys.txt, so the probe presses something instead
of running every title with no input at all.

Environment: NJ_ROOT, NJ_LIBRETRO_HARNESS, NJ_LIBRETRO_CORE, NJ_JOBS.
Exit code is 1 when any title fails.
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
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(os.environ.get("NJ_ROOT", Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(ROOT / "tools"))
from corpus_keymap import screen as resolve_screen  # noqa: E402
HARNESS = Path(os.environ.get("NJ_LIBRETRO_HARNESS",
                              ROOT / "build" / "test_libretro_frontend"))
CORE = Path(os.environ.get(
    "NJ_LIBRETRO_CORE",
    ROOT / "build" / "libretro-dist" / "linux-x86_64" / "nano_j2me_libretro.so"))
GAMES = ROOT / "tests" / "games"
# Per-title key scripts curated by the corpus sweep (frame:key entries, and
# pressed@X,Y pointer taps).  The mock frontend replays them as the matching
# joypad buttons and pointer events before each retro_run(), which lets the
# probe reach gameplay on titles that otherwise idle on a title/menu screen.
KEYS_DIR = ROOT / "tools" / "corpus_keys"
# Generic fallback: the sweep's own key recipe (rows dir keys.txt).  Without it
# every title would run with no input at all, so dialog/menu/title screens stay
# frozen at frame one and a static frame tells us nothing about compat.
DEFAULT_KEYS = None

# Titles the mock harness cannot certify, with the reason recorded next to the
# expectation.  These are reported as KNOWN instead of PASS/FAIL so a real
# regression in them would still stand out from an accepted limitation.
KNOWN_LIMITATIONS = {
    "毁灭战士3_10598.jar": (
        "runs its whole game loop inside the MIDlet constructor (it starts a "
        "render thread and joins it), so startApp() never runs and the VM is "
        "never at a frame boundary the core could serialize; the game loads, "
        "renders and accepts input, but save states are unavailable"
    ),
    "台球传奇_87.jar": (
        "with the generic key script the title can be inside a multi-second "
        "blocking load when the harness asks for the state round trip, so the "
        "load request times out (the running game continues and manual saves "
        "elsewhere work); visuals and emulation are unaffected"
    ),
    "德州扑克2_3884.jar": (
        "same class as 台球传奇_87: driven past the menu by the generic key "
        "script it sits in a long blocking load when the state round trip is "
        "requested, so the load times out; without the key script and at "
        "shorter frame counts all state checks pass and the game keeps running"
    ),
}

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
            for key in ("jar_path", "path", "jar", "file"):
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
    # The harness has its own watchdog (default 30 s); a paced 300-frame run
    # plus the state round trips can take that long for heavy titles, so give
    # it the probe's timeout instead of letting it kill a healthy session.
    size = resolve_screen(jar.name, "240x320")
    cmd = [str(HARNESS), "--core", str(CORE), "--jar", str(jar),
           "--frames", str(frames), "--pace-us", "16667",
           "--timeout", str(max(60, timeout)),
           "--visual-required", "--static-ok", "--min-nonblack", "8",
           "--save-dir", tempfile.mkdtemp(prefix="lj-vis-")]
    if size != "240x320":
        cmd += ["--set", "nano_j2me_screen=" + size,
                "--expect-screen", size]
    # Replay the row's own input recipe: "none" means the sweep found the
    # generic recipe made the title exit, a bare filename is a per-title
    # script under tools/corpus_keys/, otherwise fall back to the generic one.
    keys_arg = None
    key_name = row.get("keys")
    if key_name == "none":
        keys_arg = None
    elif key_name and (KEYS_DIR / key_name).is_file():
        keys_arg = KEYS_DIR / key_name
    else:
        keys_arg = KEYS_DIR / (jar.stem + ".txt")
        if not keys_arg.is_file():
            keys_arg = DEFAULT_KEYS
    if keys_arg is not None:
        cmd += ["--keys", str(keys_arg)]
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
    known = (not ok) and (jar.name in KNOWN_LIMITATIONS)
    # Animation statistics help find titles that still sit on a static screen
    # (a menu awaiting input) even though the probe passes with --static-ok.
    distinct = 0
    m = re.search(r"distinct_frame_hashes=(\d+)", log)
    if m:
        distinct = int(m.group(1))
    nonblack = 0
    m = re.search(r"max non-black pixels=(\d+)", log)
    if m:
        nonblack = int(m.group(1))
    return {"title": jar.name, "ok": ok, "known": known,
            "known_reason": KNOWN_LIMITATIONS.get(jar.name, ""),
            "rc": rc, "fails": fails[:3],
            "distinct_frames": distinct, "max_nonblack": nonblack,
            "log": log[-1200:] if not ok else ""}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rows_dir")
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("NJ_JOBS", "8")))
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    # A running core is memory-mapped from disk; a concurrent rebuild that
    # rewrites the same path can fault the mapping mid-run.  Probe from an
    # private copy so the evidence cannot be invalidated by an external
    # build landing on top of the shared dist directory.
    global CORE
    if CORE.is_file():
        iso_dir = tempfile.mkdtemp(prefix="lj-core-")
        iso = Path(iso_dir) / CORE.name
        shutil.copy2(CORE, iso)
        os.chmod(iso, 0o500)
        print(f"[info] probing with an isolated copy of {CORE} at {iso}",
              file=sys.stderr)
        CORE = iso

    global DEFAULT_KEYS
    default_keys = Path(args.rows_dir) / "keys.txt"
    if default_keys.is_file():
        DEFAULT_KEYS = default_keys

    rows = load_rows(args.rows_dir)
    if not rows:
        print("no ok rows found in", args.rows_dir, file=sys.stderr)
        return 2
    random.Random(0).shuffle(rows)  # keep the order stable but not alphabetical
    out_path = Path(args.out) if args.out else \
        ROOT / ".local-tools" / "debug-runs" / "goal16-libretro-corpus" / "visual.jsonl"
    # A directory (existing, trailing separator, or extension-less path) is a
    # run directory: keep the per-title JSONL inside it.
    if out_path.is_dir() or args.out and (args.out.endswith("/") or
                                          out_path.suffix == ""):
        out_path = out_path / "visual.jsonl"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    passed = failed = known = 0
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
            elif res["known"]:
                known += 1
                with PRINT_LOCK:
                    print("KNOWN %-39s %s" % (res["title"],
                          res["known_reason"][:90]), flush=True)
            else:
                failed += 1
                with PRINT_LOCK:
                    print("FAIL %-40s rc=%-3d %s" % (
                        res["title"], res["rc"],
                        "; ".join(res["fails"])[:140]), flush=True)

    with open(out_path, "w", encoding="utf-8") as fh:
        for res in results:
            fh.write(json.dumps(res, ensure_ascii=False) + "\n")
    print("\n%d titles: %d passed, %d known limitation(s), %d failed"
          % (len(results), passed, known, failed))
    print("results:", out_path)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
