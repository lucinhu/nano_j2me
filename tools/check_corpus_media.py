#!/usr/bin/env python3
"""Report which decoder a corpus title actually reaches.

The sweep cannot see inside a Player: a title can carry .mp3/.mid/.amr/.3gp
assets and never create one, or create one and stop before decoding.  This
tool replays titles with `--verbose 3` and collects the runtime's `media:`
decisions, which name the decoder that was chosen (and the refusal or silent
fallback when none was).

Usage:
    tools/check_corpus_media.py <rows dir> [--count N] [--frames N] [--seed N]
                                [--require .mp3] [--require .3gp]

Rows are the ok rows of a tools/sweep_compat.py output; the sample is
deterministic for a seed.  Titles without audio/video assets are skipped.  The
binary comes from NJ_BIN like the other corpus tools.  Nothing is written
anywhere.
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games")]
MEDIA_EXTS = (".mid", ".midi", ".mp3", ".amr", ".wav", ".imy", ".mld", ".3gp",
              ".mp4")


def find(name):
    for root in SEARCH + [d for d in
                          os.environ.get("NJ_SWEEP_DIRS", "").split(":") if d]:
        p = os.path.join(root, name)
        if os.path.exists(p):
            return p
    return None


def media_assets(path):
    try:
        with zipfile.ZipFile(path) as zf:
            return [n for n in zf.namelist()
                    if n.lower().endswith(MEDIA_EXTS)]
    except (OSError, zipfile.BadZipFile):
        return []


# What the runtime says, turned into a short label.  Ordered: the first match
# wins, so a movie that also has AMR audio is reported as a movie.
KINDS = (
    (re.compile(r"media: (\S+) movie (\d+)x(\d+), (\d+) samples"),
     lambda m: "movie:%s %sx%s/%sf" % (m.group(1), m.group(2), m.group(3),
                                       m.group(4))),
    (re.compile(r"media: (\S+) decoded (\S+), (\d+) frames"),
     lambda m: "audio:%s %sf" % (m.group(1), m.group(3))),
    (re.compile(r"media: the port decoded (\S+) into (\d+) PCM bytes"),
     lambda m: "audio:port:%s" % m.group(1)),
    (re.compile(r"media: decoded (\d+) AMR frames from (\S+) into (\d+)"),
     lambda m: "audio:amr %sf" % m.group(1)),
    (re.compile(r"media: decoded IMA ADPCM into (\d+) PCM bytes"),
     lambda m: "audio:adpcm"),
    (re.compile(r"media: silent Player for (\d+) bytes of (\S+)"),
     lambda m: "silent:%s" % m.group(2)),
    (re.compile(r"media: cannot play (\S+)"), lambda m: "refused:%s" % m.group(1)),
    (re.compile(r"media: no AMR narrowband track"), lambda m: "movie:no-amr"),
    (re.compile(r"media: system port decoded (\d+) AMR blocks"),
     lambda m: "audio:system-amr %sblk" % m.group(1)),
    (re.compile(r"media: built-in decoded (\d+) AMR blocks"),
     lambda m: "audio:amr %sblk" % m.group(1)),
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    ap.add_argument("--require", action="append", default=None,
                    help="only titles whose assets include this extension")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows if r.get("verdict") == "ok"]
    random.Random(args.seed).shuffle(ok)
    keys = args.keys or os.path.join(args.rows, "keys.txt")

    seen = 0
    for row in ok:
        if seen >= args.count:
            break
        path = find(os.path.basename(row["file"]))
        if path is None:
            continue
        assets = media_assets(path)
        if not assets:
            continue
        if args.require and not all(
                any(a.lower().endswith(x) for a in assets)
                for x in args.require):
            continue
        seen += 1
        with tempfile.TemporaryDirectory() as rms:
            cp = subprocess.run(
                [BIN, "--port", "headless", "--size", "240x320", "--heap", "16",
                 "--fps", "30", "--run-frames", str(args.frames),
                 "--keys", "@" + keys, "--rms-dir", rms, "--verbose", "3",
                 "--stats", path],
                capture_output=True, text=True, timeout=180)
        out = (cp.stderr or "") + (cp.stdout or "")
        lines = [ln for ln in out.splitlines() if "media:" in ln]
        kinds = []
        for ln in lines:
            for rx, make in KINDS:
                m = rx.search(ln)
                if m:
                    k = make(m)
                    if k not in kinds:
                        kinds.append(k)
                    break
        if not kinds:
            kinds = ["none"]
        print("%-46s assets=%-3d %s"
              % (os.path.basename(path)[:46], len(assets), " ".join(kinds)))
    print("\n%d titles sampled" % seen)
    return 0


if __name__ == "__main__":
    sys.exit(main())
