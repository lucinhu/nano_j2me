#!/usr/bin/env python3
"""Compare a runtime MPEG-4 Part 2 decode against the reference decode.

Two dumps are compared file by file, frame by frame:

  /tmp/refdecode_m4v clip.3gp ref 8     # system FFmpeg, planar YUV
  /tmp/m4v_dump clip.3gp run 8          # the runtime's own decoder

Both write <prefix>-NNN.y/.u/.v, so the comparison is just a byte diff; the
reference's manifest line supplies the geometry.  Prints, per frame and
plane, the maximum absolute difference and the percentage of equal samples,
then a verdict.  Exit status: 0 when every plane is within --max-diff
(default 0), 1 otherwise, 2 on a missing file.

Usage:
  python3 tools/compare_m4v.py REF_PREFIX RUN_PREFIX [--frames N]
                               [--width W --height H] [--max-diff D]
                               [--verbose]

For YUV 4:2:0, --width/--height are the luma size; the chroma planes are
half of each.  With --manifest FILE the geometry comes from refdecode_m4v's
manifest instead, which is what the evidence runs use.
"""
import argparse
import os
import sys


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def compare_plane(a, b):
    if len(a) != len(b):
        return None
    max_diff = 0
    equal = 0
    for x, y in zip(a, b):
        d = x - y
        if d < 0:
            d = -d
        if d == 0:
            equal += 1
        elif d > max_diff:
            max_diff = d
    return max_diff, equal, len(a)


def manifest_geometry(path):
    geometry = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 7 and parts[0] == "frame":
                    geometry[int(parts[1])] = (
                        int(parts[2]), int(parts[3]), int(parts[4]), int(parts[5]))
    except OSError:
        return {}
    return geometry


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Compare runtime and reference MPEG-4 Part 2 YUV dumps.")
    ap.add_argument("ref_prefix")
    ap.add_argument("run_prefix")
    ap.add_argument("--frames", type=int, default=5)
    ap.add_argument("--width", type=int, default=0)
    ap.add_argument("--height", type=int, default=0)
    ap.add_argument("--manifest", default=None,
                    help="refdecode_m4v manifest to take the geometry from")
    ap.add_argument("--max-diff", type=int, default=0,
                    help="largest absolute difference treated as a pass")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    geometry = manifest_geometry(args.manifest) if args.manifest else {}
    if args.width > 0 and args.height > 0:
        w, h = args.width, args.height
        cw, ch = (w + 1) // 2, (h + 1) // 2
    elif geometry:
        w, h, cw, ch = geometry[0]
    else:
        print("error: give --width/--height or --manifest", file=sys.stderr)
        return 2

    failed = False
    missing = False
    for frame in range(args.frames):
        for plane, pw, ph in (("y", w, h), ("u", cw, ch), ("v", cw, ch)):
            rp = "%s-%03d.%s" % (args.ref_prefix, frame, plane)
            tp = "%s-%03d.%s" % (args.run_prefix, frame, plane)
            ref = read(rp)
            run = read(tp)
            if ref is None or run is None:
                missing = True
                if args.verbose:
                    print("frame %3d %s: missing (%s)" %
                          (frame, plane, rp if ref is None else tp))
                continue
            result = compare_plane(ref, run)
            if result is None:
                print("frame %3d %s: size mismatch reference=%d runtime=%d" %
                      (frame, plane, len(ref), len(run)))
                failed = True
                continue
            max_diff, equal, total = result
            pct = 100.0 * equal / total
            ok = max_diff <= args.max_diff
            if not ok:
                failed = True
            print("frame %3d %s: max-abs-diff %3d  equal %8d/%d (%6.2f%%)  %s" %
                  (frame, plane, max_diff, equal, total, pct,
                   "ok" if ok else "FAIL"))
    if missing:
        print("error: one of the dumps is incomplete", file=sys.stderr)
        return 2
    if failed:
        print("result: FAIL (some plane exceeds max-abs-diff %d)" % args.max_diff)
        return 1
    print("result: PASS (all %d frames, every plane within max-abs-diff %d)" %
          (args.frames, args.max_diff))
    return 0


if __name__ == "__main__":
    sys.exit(main())
