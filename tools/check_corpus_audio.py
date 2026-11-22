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

The probe runs the CLI at `--verbose 3`, so the runtime's level-3 media lines
are reported too: a title that never started a Player ("silent, no started
Player") is a menu/phase artefact, while one that started a Player but handed
no PCM to the device ("silent, started") is worth investigating.

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
sys.path.insert(0, os.path.join(ROOT, "tools"))
from corpus_keymap import screen as resolve_screen  # noqa: E402
BIN = os.environ.get("NJ_BIN", os.path.join(ROOT, "build", "nj2me"))
SEARCH = [os.path.join(ROOT, "tests", "games")]
# Same extra corpus directories as tools/sweep_compat.py (NJ_SWEEP_DIRS).
SEARCH += [d for d in os.environ.get("NJ_SWEEP_DIRS", "").split(os.pathsep)
           if d]
AUDIO_EXTS = (".mid", ".mp3", ".amr", ".wav", ".imy", ".mld")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus_keymap import resolve as resolve_keys  # noqa: E402


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
    ap.add_argument("--timeout", type=int, default=240,
                    help="per-title wall clock budget in seconds")
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
        path = row.get("jar_path")
        if not path or not os.path.exists(path):
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
        if row.get("keys") == "none":
            row_keys = None
        elif row.get("keys"):
            row_keys = (os.path.dirname(os.path.dirname(
                os.path.abspath(__file__))) + "/tools/corpus_keys/" + row["keys"])
        else:
            row_keys = resolve_keys(os.path.basename(path), keys)
        with tempfile.TemporaryDirectory() as rms:
            size = resolve_screen(os.path.basename(path), "240x320")
            cmd = [BIN, "--port", "headless", "--size", size, "--heap", "16",
                   "--fps", "30", "--run-frames", str(args.frames),
                   "--rms-dir", rms, "--stats", "--verbose", "3"]
            if row_keys:
                cmd += ["--keys", "@" + str(row_keys)]
            cmd.append(path)
            try:
                cp = subprocess.run(cmd, capture_output=True, text=True,
                                    timeout=args.timeout)
            except subprocess.TimeoutExpired:
                print("%-46s %-15s assets=%-3d (run exceeded %d s)"
                      % (os.path.basename(path)[:46], "timeout",
                         len(assets), args.timeout))
                continue
        out = (cp.stderr or "") + (cp.stdout or "")
        observed = re.search(r"observed:.*", out)
        observed = observed.group(0) if observed else ""
        pcm = int((re.search(r"pcm_frames=(\d+)", observed) or [0, "0"])[1])
        tones = int((re.search(r"tones=(\d+)", observed) or [0, "0"])[1])
        frames = int((re.search(r"frames=(\d+)", out) or [0, "0"])[1])
        started = len(re.findall(r"media: started Player", out))
        if pcm or tones:
            kind = "sound"
            heard += 1
        elif started:
            kind = "silent-started"
            silent += 1
        else:
            kind = "silent-nostart"
            silent += 1
        print("%-46s %-15s assets=%-3d frames=%-4d tones=%-2d pcm=%-7d "
              "started=%-2d rc=%d"
              % (os.path.basename(path)[:46], kind, len(assets), frames,
                 tones, pcm, started, cp.returncode))

    print("\n%d titles: %d delivered audio, %d silent" % (seen, heard, silent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
