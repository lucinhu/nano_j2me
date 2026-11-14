#!/usr/bin/env python3
"""Probe corpus titles for audio output.

The compatibility sweep only records frames and exceptions: a title can run
fine and never hand a single PCM frame to the platform.  This tool picks the
titles that actually carry audio assets (.mid/.mp3/.amr/.wav) and reports how
many tones and PCM frames the headless port observed, which separates "the
title is silent by design / waits for user input" from "music assets present
but the mixer never receives them".

Usage:
    tools/check_corpus_audio.py <rows dir> [--count N] [--frames N] [--seed N]

`rows dir` is a tools/sweep_compat.py output directory; only titles classified
`ok` are sampled, so the probe measures audio rather than startup problems.
The sample is deterministic for a given seed.  Nothing is written anywhere.
The binary is taken from NJ_BIN, like the sweep and motion tools.
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
AUDIO_EXTS = (".mid", ".mp3", ".amr", ".wav", ".imy", ".mld")


def find(name):
    for root in SEARCH + [d for d in
                          os.environ.get("NJ_SWEEP_DIRS", "").split(":") if d]:
        p = os.path.join(root, name)
        if os.path.exists(p):
            return p
    return None


def audio_assets(path):
    try:
        with zipfile.ZipFile(path) as zf:
            return [n for n in zf.namelist()
                    if n.lower().endswith(AUDIO_EXTS)]
    except (OSError, zipfile.BadZipFile):
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--seed", type=int, default=5)
    ap.add_argument("--keys", default=None,
                    help="key script (default: <rows>/keys.txt)")
    ap.add_argument("--require", action="append", default=None,
                    help="only titles whose assets include this extension "
                         "(repeatable, e.g. --require .mid)")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(os.path.join(args.rows, "rows.jsonl"))]
    ok = [r for r in rows if r.get("verdict") == "ok" and r.get("paints", 0) > 0]
    random.Random(args.seed).shuffle(ok)
    keys = args.keys or os.path.join(args.rows, "keys.txt")

    heard = silent = seen = 0
    for row in ok:
        if seen >= args.count:
            break
        path = find(os.path.basename(row["file"]))
        if path is None:
            continue
        assets = audio_assets(path)
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
                 "--keys", "@" + keys, "--rms-dir", rms, "--stats", path],
                capture_output=True, text=True, timeout=120)
        out = (cp.stderr or "") + (cp.stdout or "")
        observed = re.search(r"observed:.*", out)
        observed = observed.group(0) if observed else ""
        pcm = int((re.search(r"pcm_frames=(\d+)", observed) or [0, "0"])[1])
        tones = int((re.search(r"tones=(\d+)", observed) or [0, "0"])[1])
        frames = int((re.search(r"frames=(\d+)", out) or [0, "0"])[1])
        kind = "sound" if (pcm or tones) else "silent"
        if pcm or tones:
            heard += 1
        else:
            silent += 1
        print("%-50s %-8s assets=%-3d frames=%-4d tones=%-3d pcm=%-7d rc=%d"
              % (os.path.basename(path)[:50], kind, len(assets), frames,
                 tones, pcm, cp.returncode))

    print("\n%d titles: %d delivered audio, %d silent" % (seen, heard, silent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
