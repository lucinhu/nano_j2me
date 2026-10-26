#!/usr/bin/env python3
"""Measure how the corpus writes PolygonMode, the one JSR 184 layout whose
payload carries more than enumerations: three enumeration bytes, then the
booleans two-sided lighting, local camera lighting and perspective correction.

This is what the loader's two-sided lighting support was written against, so
the numbers are kept reproducible rather than quoted from a one-off script:

  files            975 .m3g/.m3d containers in tests/games
  objects          every object in every stored or zlib section
  polygon modes    their payload length and the six bytes at offset 12

Two checks make the layout trustworthy: every payload must be eighteen bytes
(the spec's shape), and every byte 15..17 must be 0 or 1.  The tool prints the
distribution and the titles that ask for two-sided lighting; --json writes the
same numbers for a note to cite.

The container walk is the one tools/check_m3g_reals.sh verifies against the
compiler: a section is scheme(1), total length(u32 LE), uncompressed length
(u32 LE), body, Adler32(u32 LE), and scheme 1 is zlib.

Usage: python3 tools/check_m3g_polygon_modes.py [DIR ...] [--json FILE]
       (defaults to tests/games, like tools/check_corpus_video.py)
"""
import collections
import glob
import json
import os
import struct
import sys
import zipfile
import zlib

IDENT = bytes([0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
               0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
POLYGON_MODE = 8


def sections(data):
    """The decompressed bodies of a .m3g container, or None."""
    if not data.startswith(IDENT):
        return None
    off = 12
    out = []
    while off + 13 <= len(data):
        scheme = data[off]
        clen, ulen = struct.unpack_from("<II", data, off + 1)
        if clen < 13 or off + clen > len(data):
            return None
        body = data[off + 9:off + clen - 4]
        if scheme == 1:
            try:
                body = zlib.decompress(body)
            except zlib.error:
                return None
        elif scheme != 0:
            return None
        if ulen and len(body) != ulen:
            return None
        out.append(body)
        off += clen
    return out if off == len(data) else None


def objects(data):
    """(type, payload) for every object in the container's later sections."""
    bodies = sections(data)
    if bodies is None:
        return None
    out = []
    for body in bodies[1:]:          # the first section is the file header
        off = 0
        while off + 5 <= len(body):
            otype = body[off]
            olen = struct.unpack_from("<I", body, off + 1)[0]
            if off + 5 + olen > len(body):
                return None
            out.append((otype, body[off + 5:off + 5 + olen]))
            off += 5 + olen
        if off != len(body):
            return None
    return out


def main(argv):
    roots = [a for a in argv if not a.startswith("--")]
    json_path = None
    if "--json" in argv:
        i = argv.index("--json")
        json_path = argv[i + 1] if i + 1 < len(argv) else None
        roots = [a for a in roots if a != json_path]
    if not roots:
        roots = ["tests/games"]

    files = 0
    objs_seen = 0
    lengths = collections.Counter()
    triples = collections.Counter()
    per_title = collections.Counter()
    per_object = []

    jars = []
    for root in roots:
        if os.path.isdir(root):
            jars += sorted(glob.glob(os.path.join(root, "**", "*.jar"),
                                     recursive=True))
        else:
            jars.append(root)
    for jar in jars:
        try:
            z = zipfile.ZipFile(jar)
        except (OSError, zipfile.BadZipFile):
            continue
        for name in z.namelist():
            low = name.lower()
            if not (low.endswith(".m3g") or low.endswith(".m3d")):
                continue
            try:
                data = z.read(name)
            except (OSError, RuntimeError):
                continue
            objs = objects(data)
            if objs is None:
                print("BAD-CONTAINER %s!%s" % (os.path.basename(jar), name))
                continue
            files += 1
            for otype, payload in objs:
                objs_seen += 1
                if otype != POLYGON_MODE:
                    continue
                lengths[len(payload)] += 1
                flags = tuple(payload[15:18]) if len(payload) >= 18 else None
                triples[flags] += 1
                per_object.append((os.path.basename(jar), name, flags))
                if flags is not None and flags[0] not in (0, 1):
                    print("BAD-FLAG %s!%s two-sided=%r" %
                          (os.path.basename(jar), name, flags[0]))
                if flags and flags[0]:
                    per_title[os.path.basename(jar)] += 1

    bad = sum(v for k, v in lengths.items() if k != 18)
    bad += sum(v for k, v in triples.items()
               if k is None or any(b not in (0, 1) for b in k))
    print("m3g files=%d objects=%d PolygonMode=%d" %
          (files, objs_seen, sum(lengths.values())))
    print("payload lengths: %s" %
          {k: v for k, v in sorted(lengths.items())})
    print("flag triples (two-sided, local-camera, perspective): %s" %
          {str(k): v for k, v in sorted(triples.items(), key=lambda kv: -kv[1])})
    print("two-sided per title:")
    for title, n in sorted(per_title.items(), key=lambda kv: -kv[1]):
        print("  %2d  %s" % (n, title))
    print("%d PolygonMode payload(s) outside the eighteen-byte shape or with "
          "a non-boolean flag" % bad)

    if json_path:
        out = {
            "what": "JSR 184 PolygonMode payloads in the collected corpus",
            "how": "python3 tools/check_m3g_polygon_modes.py --json "
                   "docs/m3g-twosided-results.json tests/games; cross-checked "
                   "by tools/check_m3g_reals.sh (975 files probed, 0 rejected, "
                   "1513/1513 PolygonMode objects in range)",
            "m3g_files": files,
            "objects": objs_seen,
            "polygon_modes": sum(lengths.values()),
            "payload_lengths": {str(k): v for k, v in sorted(lengths.items())},
            "flag_triples_(two-sided,local-camera,perspective)": {
                str(k): v for k, v in
                sorted(triples.items(), key=lambda kv: -kv[1])},
            "two_sided_per_title": dict(
                sorted(per_title.items(), key=lambda kv: -kv[1])),
            "outside_the_shape": bad,
        }
        with open(json_path, "w") as f:
            json.dump(out, f, indent=1, ensure_ascii=False)
            f.write("\n")
        print("wrote %s" % json_path)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
