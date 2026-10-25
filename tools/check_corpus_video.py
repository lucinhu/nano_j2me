#!/usr/bin/env python3
"""Survey the corpus for video containers and the codecs inside them.

The video decoder work needs to know what real assets look like before
choosing what to implement: the corpora under tests/games and the out-of-tree
vaults are the only evidence there is.  A 3GP file names its codec in the
sample description ('h263', 'mp4v', ...), so a byte search answers the question
without a full ISO-BMFF parser.

Usage: python3 tools/check_corpus_video.py [DIR ...]
       (defaults to tests/games; extra dirs may be passed or set in
        NJ_SWEEP_DIRS, colon separated, like tools/sweep_compat.py)
"""
import collections
import glob
import os
import sys
import zipfile

FOURCCS = ((b"h263", "h263"), (b"s263", "h263"), (b"mp4v", "mpeg4"),
           (b"avc1", "h264"), (b"H264", "h264"))
EXTS = (".3gp", ".3g2", ".mp4", ".m4v", ".mjpg")


def dirs():
    out = list(sys.argv[1:])
    if not out:
        out = ["tests/games"]
    extra = os.environ.get("NJ_SWEEP_DIRS", "")
    out += [d for d in extra.split(":") if d]
    return out


def main():
    seen = collections.Counter()
    rows = []
    for root in dirs():
        for jar in sorted(glob.glob(os.path.join(root, "*.jar"))):
            try:
                z = zipfile.ZipFile(jar)
            except Exception:
                continue
            for name in z.namelist():
                if not name.lower().endswith(EXTS):
                    continue
                try:
                    data = z.read(name)
                except Exception:
                    continue
                codecs = [label for fourcc, label in FOURCCS if fourcc in data]
                label = ",".join(sorted(set(codecs))) or "unknown"
                seen[label] += 1
                rows.append((os.path.basename(jar), name, label, len(data)))
    for r in rows:
        print("%-44s %-24s %-8s %8d" % r)
    print("%d video container(s): %s" % (len(rows), dict(seen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
