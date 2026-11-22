#!/usr/bin/env python3
"""Linkage-gap scanner over a corpus sweep directory.

The sweep records thrown exceptions per title, but a title that catches a
`NoSuchMethodError`/`NoClassDefFoundError` can still end up `ok` (the game
swallows the error and draws a degraded screen).  This tool re-runs each title
briefly and reports every linkage-class error it can see, so the remaining API
surface gaps show up as a list instead of hiding behind an `ok` verdict.

Usage:
  tools/check_corpus_linkage.py <rows dir> [--frames 120] [--jobs 18]
                                [--timeout 60] [--out results.jsonl]

Environment: NJ_ROOT, NJ_BIN, NJ_KEYS_DIR.  The rows directory must contain
`rows.jsonl`; `--keys` uses tools/corpus_keys/ per title and falls back to the
generic script shipped next to a previous sweep output.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(os.environ.get("NJ_ROOT", Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(ROOT / "tools"))
import corpus_keymap  # noqa: E402

BIN = os.environ.get("NJ_BIN", str(ROOT / "build" / "nj2me"))
PATTERNS = (
    "NoClassDefFoundError", "NoSuchMethodError", "NoSuchFieldError",
    "AbstractMethodError", "IllegalAccessError", "IncompatibleClassChangeError",
    "UnsatisfiedLinkError", "ClassFormatError", "ExceptionInInitializerError",
)
HIT = re.compile(r"thrown:\s*java/lang/(%s)" % "|".join(PATTERNS))
GENERIC = ROOT / "tools" / "corpus_keys" / "generic.txt"


def keys_for(name, rows_dir):
    found = corpus_keymap.resolve(name)
    if found:
        return found
    if GENERIC.exists():
        return str(GENERIC)
    fallback = Path(rows_dir) / "keys.txt"
    return str(fallback) if fallback.exists() else None


def resolve_jar(row, rows_dir):
    path = row.get("jar_path")
    if path and os.path.exists(path):
        return path
    base = row.get("file")
    for directory in (ROOT / "tests" / "games",
                      Path.home() / ".cache" / "nano_j2me" / "corpus"):
        candidate = directory / base
        if candidate.exists():
            return str(candidate)
    return None


def one(job):
    row, rows_dir = job
    path = resolve_jar(row, rows_dir)
    if not path:
        return {"file": row.get("file"), "missing": True}
    size = row.get("screen") or corpus_keymap.screen(row.get("file"), "240x320")
    heap = row.get("heap") or 16
    cmd = [BIN, "--port", "headless", "--size", size, "--heap", str(heap),
           "--fps", "30", "--run-frames", "120", "--rms-dir",
           os.path.join("/tmp", "linkage-rms",
                        row.get("file", "x").replace(".jar", "") + "-" + str(heap)),
           "--stats"]
    keys = keys_for(row.get("file", ""), rows_dir)
    if keys:
        cmd[cmd.index("--stats"):cmd.index("--stats")] = ["--keys", "@" + keys]
    device = row.get("device")
    if device:
        cmd[cmd.index("--stats"):cmd.index("--stats")] = ["--device", device]
    cmd.append(path)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        err = p.stderr
        rc = p.returncode
    except subprocess.TimeoutExpired:
        return {"file": row.get("file"), "timeout": True, "hits": []}
    hits = []
    for line in err.splitlines():
        m = HIT.search(line)
        if m:
            hits.append(m.group(1) + " | " + line.strip()[:180])
    if not hits:
        # A method missing from a class the title bundles itself is a title
        # bug; the sweep classifies that, so only report runtime-side hints.
        if "cannot find" in err or "no such" in err.lower():
            for line in err.splitlines():
                if "cannot find" in line or "no such" in line.lower():
                    hits.append("hint | " + line.strip()[:180])
    return {"file": row.get("file"), "jar": row.get("jar_path"), "rc": rc,
            "hits": sorted(set(hits))}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rows_dir")
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=18)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    rows_dir = args.rows_dir
    rows = []
    with open(os.path.join(rows_dir, "rows.jsonl"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    rows = [r for r in rows if r.get("verdict") != "bad-jar"]
    out = open(args.out, "w", encoding="utf-8") if args.out else None
    gap_rows = []
    missing = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(one, (r, rows_dir)) for r in rows]
        for i, fut in enumerate(as_completed(futures), 1):
            res = fut.result()
            if res.get("missing"):
                missing += 1
            elif res.get("hits"):
                gap_rows.append(res)
                print("GAP %s" % res["file"])
                for hit in res["hits"]:
                    print("    " + hit[:200])
            if out:
                out.write(json.dumps(res, ensure_ascii=False) + "\n")
            if i % 100 == 0:
                print("done %d/%d" % (i, len(rows)))
    if out:
        out.close()
    print("scanned %d titles, %d with linkage hits, %d jars unavailable"
          % (len(rows), len(gap_rows), missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
